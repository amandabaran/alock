#!/usr/bin/env python
import configparser
import itertools
import json
import os
import subprocess
import sys
from math import log
from multiprocessing import Process
from os import abort
from time import sleep

import alock.benchmark.one_lock.experiment_pb2 as experiment_pb2
import alock.src.cluster.cluster_pb2 as cluster_pb2
import pandas
from absl import app, flags
from alive_progress import alive_bar
import alock.benchmark.one_lock.plot as plot
from rome.rome.util.debugpy_util import debugpy_hook

# `launch.py` is a helper script to run experiments remotely. It takes the same input paramters as the underlying script to execute along with additional parameters

FLAGS = flags.FLAGS


# Data collection configuration
flags.DEFINE_bool(
    'get_data', False,
    'Whether or not to print commands to retrieve data from client nodes')
flags.DEFINE_string(
    'save_root',
    '/users/adb321/results/',
    'Directory results are saved under when running')
flags.DEFINE_string('nodefile', None, 'Path to nodefile',
                    short_name='n', required=True)
flags.DEFINE_string('remote_save_dir', 'results', 'Remote save directory')
flags.DEFINE_string('local_save_dir', 'results', 'Local save directory')
flags.DEFINE_bool(
    'plot', False,
    'Generate plots (should ensure `--get_data` is called first)')
flags.DEFINE_string(
    'expfile', 'experiments.csv',
    'Name of file containing experiment configurations to run (if none exists then one is generated)'
)

# Workload definition
flags.DEFINE_multi_string('lock_type', 'spin', 'Lock type used in experiment (one of {spin, mcs, alock})')
flags.DEFINE_multi_integer('think_ns', 0, 'Think times in nanoseconds')

flags.DEFINE_integer('min_key', 1, 'Minimum key')
flags.DEFINE_multi_integer('max_key', int(1e4), 'Maximum key')
flags.DEFINE_integer('threads', 1, 'Number of Workers (threads) to launch per node')
flags.DEFINE_float('theta', 0.99, 'Theta in Zipfian distribution')
flags.DEFINE_multi_float('p_local', 0.5, 'Percentage of operations that are local to each node')
flags.DEFINE_multi_integer('local_budget', 5, 'Budget to be used for alock local cohort')
flags.DEFINE_multi_integer('remote_budget', 5, 'Budget to be used for alock remote cohort')

flags.DEFINE_integer('runtime', 10, 'Number of seconds to run experiment')

flags.DEFINE_bool('prefill', True, 'Prefill range of locks or just one')

flags.DEFINE_string('ssh_user', 'adb321', '')
flags.DEFINE_string('ssh_keyfile', '~/.ssh/id_ed25519', '')
flags.DEFINE_string('bazel_flags', '-c opt',
                    'The run command to pass to Bazel')
flags.DEFINE_string('bazel_bin', '~/go/bin/bazelisk',
                    'Location of bazel binary')
flags.DEFINE_string('bazel_prefix', 'cd alock/alock/ && ',
                    'Command to run before bazel')
flags.DEFINE_bool('gdb', False, 'Run in gdb')

# Experiment configuration
flags.DEFINE_multi_integer(
    'nodes', 1, 'Number of nodes in cluster', short_name='N')
flags.DEFINE_multi_integer(
    'num_clients', 1, 'Total number of clients in cluster', short_name='C')

flags.DEFINE_integer(
    'port', 42000, 'Port to listen for incoming connections on')
flags.DEFINE_string('log_dest', '/Users/amandabaran/Desktop/sss/async_locks/alock/logs/locks',
                    'Name of local log directory for ssh commands')
flags.DEFINE_boolean(
    'dry_run', False,
    'Prints out the commands to run without actually executing them')
flags.DEFINE_boolean(
    'debug', False, 'Whether to launch a debugpy server to debug this program')
flags.DEFINE_string('log_level', 'info', 'Rome logging level to launch client & servers with')



# Parse the experiment file and make it globally accessible
config = configparser.ConfigParser(
    interpolation=configparser.ExtendedInterpolation())
launch = None
workload = None


__lehigh__ = ['luigi']
__utah__ = ['xl170', 'c6525-100g', 'c6525-25g', 'd6515']
__clemson__ = ['r6525', 'r650']
__emulab__ = ['r320']

def get_domain(node_type):
    if node_type in __utah__:
        return 'utah.cloudlab.us'
    elif node_type in __clemson__:
        return 'clemson.cloudlab.us'
    elif node_type in __emulab__:
        return 'apt.emulab.net'
    elif node_type in __lehigh__:
        return 'cse.lehigh.edu'
    else:
        abort()


def partition_nodefile(path):
    nodes_csv = []
    assert(os.path.exists(path))
    with open(path, 'r') as __file:
        nodes_csv = [line for line in __file]
    return nodes_csv


def parse_nodes(csv, nid, num_nodes):
    csv_nodes = []
    name, public_name, node_type = csv[0].strip().split(',')
    csv_nodes.append((name, public_name))
    for line in csv[1:]:
        name, public_name, _ = line.strip().split(',')
        csv_nodes.append((name, public_name))

    proto = experiment_pb2.CloudlabClusterProto()
    proto.node_type = node_type
    proto.domain = get_domain(node_type)
    node_protos = {}
    num_clients = num_nodes * FLAGS.threads
    
    total_keys = (FLAGS.max_key[0] - FLAGS.min_key) + 1
    keys_per_client = total_keys / num_clients
    keys_per_node = total_keys / num_nodes
    i = 0
    local_low = FLAGS.min_key
    local_high = keys_per_node
    
    for r in range(0, num_clients):
        n = csv_nodes[i]
        
        # Make client ids start at 1 so that 0 can represent unlocked
        cid = nid + 1
       
        min_key = int(r * keys_per_client) + 1
        max_key = int((r + 1) * keys_per_client) if (r < num_clients - 1) else FLAGS.max_key[0]
        print("client: ", cid, " range: ", min_key, " - ", max_key)
        
        c = cluster_pb2.NodeProto(
            nid=cid, name=n[0], public_name=n[1],
            port=FLAGS.port + nid,
            range=cluster_pb2.KeyRangeProto(
                low=min_key,
                high=max_key),
            local_range=cluster_pb2.KeyRangeProto(
                low=int(local_low),
                high=int(local_high)
            ))
        proto.cluster.nodes.append(c)
        if node_protos.get(n[0]) is None:
            node_protos[n[0]] = []
        node_protos[n[0]].append(c)
        nid += 1
        if (nid == (i+1)*FLAGS.threads):
            i += 1
            local_low = local_high + 1
            local_high = (i + 1)*keys_per_node
            
    return proto, node_protos


def build_hostname(name, domain):
    return '.'.join([name, domain])


def build_ssh_command(name, domain):
    return ' '.join(
        ['ssh -i', FLAGS.ssh_keyfile, FLAGS.ssh_user + '@' +
         build_hostname(name, domain)])


def build_run_command(lock, params, cluster):
    cmd = FLAGS.bazel_prefix + (' ' if len(FLAGS.bazel_prefix) > 0 else '')
    cmd = ' '.join([cmd, FLAGS.bazel_bin])
    if not FLAGS.gdb:
        cmd = ' '.join([cmd, 'run'])
        cmd = ' '.join([cmd, FLAGS.bazel_flags])
        cmd = ' '.join([cmd, '--lock_type=' + lock])
        cmd = ' '.join([cmd, '--log_level=' + FLAGS.log_level])
        cmd = ' '.join([cmd, '//alock/benchmark/one_lock:main --'])
    else:
        cmd = ' '.join([cmd, 'build'])
        cmd = ' '.join([cmd, FLAGS.bazel_flags])
        cmd = ' '.join([cmd, '--lock_type=' + lock])
        cmd = ' '.join([cmd, '--log_level=' + FLAGS.log_level])
        cmd = ' '.join([cmd, '--copt=-g', '--strip=never',
                        '//alock/benchmark/one_lock:main'])
        cmd = ' '.join([cmd, '&& gdb -ex run -ex bt -ex q -ex y --args',
                        'bazel-bin/alock/benchmark/one_lock/main'])
    cmd = ' '.join([cmd, '--experiment_params', quote(make_one_line(params))])
    cmd = ' '.join([cmd, '--cluster', quote(make_one_line(cluster))])
    
    # print("\n\n\n\n CMD: \n", cmd, '\n\n\n\n\n')
    return cmd


def build_command(ssh_command, run_command):
    return ' '.join(
        [ssh_command, '\'' + run_command + '\''])


def make_one_line(proto):
    return ' '.join(line for line in str(proto).split('\n'))


SINGLE_QUOTE = "'\"'\"'"


def quote(line):
    return SINGLE_QUOTE + line + SINGLE_QUOTE


def build_save_dir(lock):
    return os.path.join(FLAGS.save_root, FLAGS.remote_save_dir, lock)


def build_remote_save_dir(lock):
    return os.path.join(
        FLAGS.save_root, FLAGS.remote_save_dir) + '/*'


def build_local_save_dir(lock, public_name):
    return os.path.join(FLAGS.local_save_dir, lock, public_name) + '/'

def fill_experiment_params(
        nodes, experiment_name, lock, num_nodes):
    proto = experiment_pb2.ExperimentParams()
    if nodes:
        proto.client_ids.extend(n.nid for n in nodes)
    proto.name = experiment_name
    proto.num_nodes = num_nodes
    proto.local_budget = FLAGS.local_budget[0]
    proto.remote_budget = FLAGS.remote_budget[0]
    proto.workload.runtime = FLAGS.runtime
    proto.workload.think_time_ns = FLAGS.think_ns[0]
    proto.save_dir = build_save_dir(lock)
    proto.workload.min_key = FLAGS.min_key
    proto.workload.max_key = FLAGS.max_key[0]
    proto.workload.p_local = FLAGS.p_local[0]
    # proto.workload.theta = FLAGS.theta
    proto.prefill = FLAGS.prefill
    proto.num_threads = FLAGS.threads
    proto.num_clients = FLAGS.num_clients[0]
    return proto

def build_get_data_command(lock, node, cluster):
    src = build_remote_save_dir(lock)
    dest = build_local_save_dir(lock, node.public_name)
    os.makedirs(dest, exist_ok=True)
    return 'rsync -aq ' + FLAGS.ssh_user + '@' + build_hostname(
        node.public_name, cluster.domain) + ':' + ' '.join(
        [src, dest])


def build_logfile_path(lock, experiment_name, node):
    return os.path.join(
        FLAGS.log_dest, lock, experiment_name, node.public_name +
        '_' + str(node.nid) + '.log')


def __run__(cmd, outfile, retries):
    failures = 0
    with open(outfile, 'w+') as f:
        # A little hacky, but we have a problem with bots hogging the SSH connections and causing the server to refuse the request.
        while (failures <= retries):
            try:
                subprocess.run(cmd, shell=True, check=True,
                               stderr=f, stdout=f)
                return
            except subprocess.CalledProcessError:
                failures += 1
        print(
            f'\t> Command failed. Check logs: {outfile} (trial {failures}/10)')


def execute(experiment_name, commands):
    processes = []
    for cmd in commands:
        if not FLAGS.dry_run:
            os.makedirs(
                os.path.dirname(cmd[1]),
                exist_ok=True)
            processes.append(
                Process(target=__run__, args=cmd))
            processes[-1].start()
        else:
            print(cmd[0])

    if not FLAGS.dry_run:
        for p in processes:
            p.join()


def main(args):
    debugpy_hook() 
    if FLAGS.get_data:
        nodes_csv = partition_nodefile(FLAGS.nodefile)
        cluster_proto, nodes = parse_nodes(nodes_csv, 0, len(nodes_csv))
        commands = []
        with alive_bar(len(nodes.keys()), title="Getting data...") as bar:
            for _, nodes in nodes.items():
                n = nodes[0]
                # Run servers in separate processes
                execute('get_data',
                        [(build_get_data_command(
                            '', n, cluster_proto),
                            build_logfile_path('', 'get_data', n), 0)])
                bar()
    elif FLAGS.plot:
        datafile = FLAGS.datafile
        if datafile is None:
            datafile = '__data__.csv'
        if not os.path.exists(datafile):
            plot.generate_csv(FLAGS.local_save_dir, datafile)
        plot.plot(datafile, FLAGS.lock_type)
        if FLAGS.datafile is None:
            os.remove(datafile)
    else:
        # lock type, #nodes, %local, max key, # clients
        columns = ['lock', 'n', 'm', 'p', 'lb', 'rb', 'c', 'done']
        experiments = {}
        configurations = list(itertools.product(
            set(FLAGS.lock_type),
            set(FLAGS.nodes),
            set(FLAGS.max_key),
            set(FLAGS.p_local),
            set(FLAGS.local_budget),
            set(FLAGS.remote_budget),
            set(FLAGS.num_clients),
            [False]))
        experiments = pandas.DataFrame(configurations, columns=columns)
        if not FLAGS.dry_run:
            experiments.to_csv(FLAGS.expfile, mode='w+', index_label='row')

        with alive_bar(int(experiments.shape[0]), title='Running experiments...') as bar:
            for index, row in experiments.iterrows():
                if row['done']:
                    bar()
                    continue

                lock = row['lock']
                n_count = row['n']
                max_key = row['m']
                p_local = row['p']
                local_budget = row['lb']
                remote_budget = row['rb']
                num_clients = row['c']

                nodes_csv = partition_nodefile(FLAGS.nodefile)
                if nodes_csv is None:
                    continue
                cluster_proto = experiment_pb2.CloudlabClusterProto()
                temp, nodes = parse_nodes(nodes_csv, 0, n_count)
                cluster_proto.MergeFrom(temp) 

                commands = []
                # Modify below line to change name of the log saved to accurately represent the experiment configuration
                experiment_name = lock + '_n' + str(n_count) + '_c' + str(num_clients) + '_m' + str(max_key) + '_p' + str(p_local) + '_lb' + str(local_budget) + '_rb' + str(remote_budget)
                bar.text = f'Lock type: {lock} | Current experiment: {experiment_name}'
                if not FLAGS.get_data:
                    for n in set(nodes.keys()):
                        node_list = nodes.get(n)
                        node = node_list[0]
                        ssh_command = build_ssh_command(
                            node.public_name,
                            cluster_proto.domain)
                        params  = fill_experiment_params(node_list, experiment_name, lock, num_nodes=n_count)
                        run_command = build_run_command(lock, params, cluster_proto.cluster)
                        retries = 0
                        commands.append((
                            build_command(
                                ssh_command, run_command),
                            build_logfile_path(
                                lock, experiment_name,
                                node), retries))

                    # Execute the commands.
                    execute(experiment_name, commands)
                    experiments.at[index, 'done'] = True
                    experiments.to_csv(FLAGS.expfile, index_label='row')
                    bar()

        if not FLAGS.dry_run and os.path.exists(FLAGS.expfile):
            os.remove(FLAGS.expfile)


if __name__ == '__main__':
    app.run(main)
