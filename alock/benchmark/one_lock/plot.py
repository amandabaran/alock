from enum import Enum
from math import ceil
from absl import flags
from absl import app
import math
import os
import google.protobuf.text_format as text_format
import google.protobuf.descriptor as descriptor
import alock.benchmark.one_lock.experiment_pb2 as experiment
from alive_progress import alive_bar
import pandas
import matplotlib.pyplot as plt
import seaborn
from rome.rome.util.debugpy_util import debugpy_hook

pandas.options.mode.chained_assignment = None  # default='warn'


flags.DEFINE_string("figdir", '/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/one_lock/plots/',
                    "Directory name to save figures under")
flags.DEFINE_string(
    'datafile', None,
    'Name of datafile for plotting (if none exists then one is generated by processing results in `save_dir`)')

flags.DEFINE_string('exp', 'exp1', 'Experiment to plot')


FLAGS = flags.FLAGS


def getData(proto, path=''):
    data = {}
    for field in proto.DESCRIPTOR.fields:
        is_repeated = field.label == descriptor.FieldDescriptor.LABEL_REPEATED
        is_empty_repeated = (is_repeated and len(
            getattr(proto, field.name)) == 0)
        is_missing = (not field.label == descriptor.FieldDescriptor.
                      LABEL_REPEATED and not proto.HasField(field.name))
        if is_empty_repeated or is_missing:
            continue
        if not is_repeated and field.type is descriptor.FieldDescriptor.TYPE_MESSAGE:
            data.update(getData(
                getattr(proto, field.name),
                path + ('.' if len(path) != 0 else '') + field.name))
        elif is_repeated and field.type is descriptor.FieldDescriptor.TYPE_MESSAGE:
            repeated = []
            for f in getattr(proto, field.name):
                repeated.append(
                    getData(
                        f, path + ('.' if len(path) != 0 else '') +
                        field.name))
            result = {}
            for r in repeated:
                for k in r.keys():
                    if not k in result.keys():
                        result[k] = [r[k]]
                    else:
                        result[k].append(r[k])
            # data |= result
            data.update(result)
        else:
            data[path + ('.' if len(path) !=
                         0 else '') + field.name] = getattr(proto, field.name)
    return data

def generate_csv(results_dir, datafile):
    assert(os.path.exists(results_dir))
    result_files = os.walk(results_dir)
    results_protos = []
    data_files = []
    # builds list of data files to read
    for root, _, files in result_files:
        if len(files) == 0:
            continue
        for name in files:
            data_files.append(os.path.join(root, name))

    # combines ResultsProto from each data file into list
    with alive_bar(len(data_files), title="Reading data...") as bar:
        for f in data_files:
            with open(f) as result_file:
                results = text_format.Parse(
                    result_file.read(), experiment.ResultsProto())
                results_protos.append(results)
                bar()

    results = []
    with alive_bar(len(results_protos), title="Preparing data...") as bar:
        for proto in results_protos:
            results.append(getData(proto))
            bar()
    data = pandas.DataFrame()
    dfs = []
    with alive_bar(len(results), title="Generating datafile: {}".format(datafile)) as bar:
        for r in results[:-1]:
            dfs.append(pandas.DataFrame([r]))
            bar()
        dfs.append(pandas.DataFrame(results[-1]))
        bar.text = "Aggregating final data..."
        data = pandas.concat(dfs, ignore_index=True)
        bar()
    data.to_csv(datafile, index_label='row')

def merge_csv(csv1, csv2):
    df1 = pandas.read_csv(csv1)
    df2 = pandas.read_csv(csv2)
    frames = [df1, df2]
    data = pandas.concat(frames)
    data.to_csv(csv1, index_label='row')
    return data
    
# Globals
x1_ = 'lock_type'
x2_ = 'experiment_params.workload.max_key'
x3_ = 'experiment_params.num_clients'
x4_ = 'experiment_params.num_nodes'
x5_ = 'experiment_params.workload.p_local'
x6_ = 'experiment_params.local_budget'
x7_ = 'experiment_params.remote_budget'
x8_ = 'experiment_params.num_threads'
x_ = [x1_, x2_, x3_, x4_, x5_, x6_, x7_, x8_]
y_ = 'results.driver.qps.summary.mean'
cols_ = [x1_, x2_, x3_, x4_, x5_, x6_, x7_, x8_, y_]

def get_summary(data):
    # Calculate totals grouped by the cluster size
    grouped = data.groupby(x_, as_index=True)[y_]
    _avg = grouped.mean()
    _stddev = grouped.std()
    _max = grouped.max()
    _min = grouped.min()
    _total = grouped.sum()
    summary = pandas.DataFrame(index=grouped.groups.keys())
    summary.index.names = x_
    summary['average'] = _avg
    summary['stddev'] = _stddev
    summary['min'] = _min
    summary['max'] = _max
    summary['total'] = _total
    print("\n\n----------------SUMMARY------------------\n")
    print(summary)
    return summary

def plot_total_throughput():
    pass

def plot_grid(nodes, keys, xcol, originals, summary, hue, xlabel, hue_label, name, plot_total):
    global y_
    # make a grid of subplots with a row for each node number and a column for each key setup
    fig, axes = plt.subplots(len(nodes), len(keys), figsize=(15, 3))
    seaborn.set_theme(style='ticks')
    markersize = 8
    
    if plot_total:
        # plot total throughput
        originals = summary

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("viridis", num_hues)
    
    plt.subplots_adjust(hspace = 1.0)
    plt.subplots_adjust(wspace = 0.25)
    
    for i, node in enumerate(nodes):
        for j, key in enumerate(keys):
            data = originals[originals['experiment_params.workload.max_key'] == key]
            data = data[data['experiment_params.num_nodes'] == node]
            seaborn.lineplot(
                    data=data,
                    x=xcol,
                    y=y_,
                    ax=axes[i][j],
                    hue=hue,
                    style=hue,
                    markers=True,
                    markersize=markersize,
                    palette=palette
            )
            # set y axis to start at 0
            axes[i][j].set_ylim(0, axes[i][j].get_ylim()[1])
            # set 3 ticks on y axis with values auto-chosen
            axes[i][j].set_yticks(axes[i][j].get_yticks()[::len(axes[i][j].get_yticks()) // 3])
            h2, l2 = axes[i][j].get_legend_handles_labels()
            axes[i][j].set_ylabel('') 
            axes[i][j].set_xlabel('')
            axes[i][j].set_title(str(key) + " Keys, " + str(node) + " Nodes")
        axes[i][0].set_ylabel('Throughput (ops/s)', labelpad=20)
    
    for j in range(len(keys)):    
     axes[len(nodes)-1][j].set_xlabel(xlabel)

    for h in h2:
        h.set_markersize(24)
        h.set_linewidth(3)
    labels_handles = {}
    labels_handles.update(dict(zip(l2, h2)))
    
    for ax in axes.flatten():
        ax.legend().remove()
    
    fig.legend(h2, l2,
        loc='upper center', fontsize=12, title_fontsize=16, title=hue_label,
        ncol=num_hues if num_hues < 6 else int(num_hues / 2),
        columnspacing=1, edgecolor='white', borderpad=0)
    
    plt.show()

    # filename = name + ".png"
    # dirname = os.path.dirname(filename)
    # os.makedirs(dirname, exist_ok=True)
    # fig.savefig(filename, dpi=300, bbox_extra_artists=(legend,)
    #             if legend is not None else None, bbox_inches='tight')


# this produces a multi-row grid, where each row is a different # of keys and hue is for different locks
def plot_locality(nodes, clients, keys, originals, summary, hue, hue_label, name, plot_total):
    global x5_, y_
    
    node_keys = []
    for i, node in enumerate(nodes):
        for j, key in enumerate(keys):
            node_keys.append((node, key))
            
    # make a grid of subplots with a row for each node number and a column for each key setup
    fig, axes = plt.subplots(len(node_keys), len(clients), figsize=(12, 3))
    seaborn.set_theme(style='ticks')
    markersize = 8

    if plot_total:
        # plot total throughput
        originals = summary

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("viridis", num_hues)
    
    plt.subplots_adjust(hspace = 1.2)
    plt.subplots_adjust(wspace = 0.25)
    
    for i, (node, key) in enumerate(node_keys):
        for j, client in enumerate(clients):
            data = originals[originals['experiment_params.workload.max_key'] == key]
            data = data[data['experiment_params.num_nodes'] == node]
            data = data[data['experiment_params.num_clients'] == client]
            seaborn.lineplot(
                    data=data,
                    x=x5_,
                    y=y_,
                    ax=axes[i][j],
                    hue=hue,
                    style=hue,
                    markers=True,
                    markersize=markersize,
                    palette=palette
            )
            # set y axis to start at 0
            axes[i][j].set_ylim(0, axes[i][j].get_ylim()[1])
            # set 3 ticks on y axis with values auto-chosen
            # axes[i][j].set_yticks(axes[i][j].get_yticks()[::len(axes[i][j].get_yticks()) // 3])
            h2, l2 = axes[i][j].get_legend_handles_labels()
            axes[i][j].invert_xaxis()
            axes[i][j].set_ylabel('') 
            axes[i][j].set_xlabel('Percent Local')
            axes[i][j].set_title(str(key) + " Keys, " + str(node) + " Nodes, Clients: " + str(client))
        axes[i][0].set_ylabel('Throughput (ops/s)', labelpad=20)
        
    for j in range(len(keys)):    
        axes[len(nodes)-1][j].set_xlabel("Percent Local")

    for h in h2:
        h.set_markersize(24)
        h.set_linewidth(3)
    labels_handles = {}
    labels_handles.update(dict(zip(l2, h2)))
    
    for ax in axes.flatten():
        ax.legend().remove()
    
    fig.legend(h2, l2,
        loc='upper center', fontsize=12, title_fontsize=16, title=hue_label,
        ncol=num_hues if num_hues < 6 else int(num_hues / 2),
        columnspacing=1, edgecolor='white', borderpad=0)
    
    fig.legend(h2, l2,
        loc='upper center', fontsize=12, title_fontsize=14, title=hue_label,
        ncol=num_hues if num_hues < 6 else int(num_hues / 2),
        columnspacing=1, edgecolor='white', borderpad=0)
    
    plt.show()
    
def plot_budget(nodes, keys, clients, plocal, originals, summary, hue, xlabel, hue_label, name, plot_total):
    global x1_, x2_, x3_, x6_, x7_, y_
    node_keys = []
    for i, node in enumerate(nodes):
        for j, key in enumerate(keys):
            for k, pl in enumerate(plocal):
                node_keys.append((node, key, pl))
            
    # make a grid of subplots with a row for each node number and a column for each key setup
    fig, axes = plt.subplots(len(node_keys), len(clients), figsize=(15, 3))
    seaborn.set_theme(style='ticks')
    markersize = 8
    
    if plot_total:
        # plot total throughput
        originals = summary

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("viridis", num_hues)
    
    plt.subplots_adjust(hspace = 1.2)
    plt.subplots_adjust(wspace = 0.25)
    
    for i, (node, key, pl) in enumerate(node_keys):
        for j, client in enumerate(clients):
            data = originals[originals['experiment_params.workload.max_key'] == key]
            data = data[data['experiment_params.num_nodes'] == node]
            data = data[data['experiment_params.workload.p_local'] == pl]
            data = data[data['experiment_params.num_clients'] == client]
            seaborn.lineplot(
                    data=data,
                    x=x6_,
                    y=y_,
                    ax=axes[i][j],
                    hue=hue,
                    style=hue,
                    markers=True,
                    markersize=markersize,
                    palette=palette
            )
            seaborn.lineplot(
                    data=data,
                    x=x7_,
                    y=y_,
                    ax=axes[i][j],
                    hue=hue,
                    style=hue,
                    markers=True,
                    markersize=markersize,
                    palette=['red']
            )
            # set y axis to start at 0
            axes[i][j].set_ylim(0, axes[i][j].get_ylim()[1])
            # set 3 ticks on y axis with values auto-chosen
            # axes[i][j].set_yticks(axes[i][j].get_yticks()[::len(axes[i][j].get_yticks()) // 3])
            h2, l2 = axes[i][j].get_legend_handles_labels()
            axes[i][j].set_ylabel('') 
            axes[i][j].set_xlabel('')
            axes[i][j].set_title(str(node) + " N, " +  str(key) + " K, " +  str(client) + " C, " + str(pl*100) + " PL ")
        axes[i][0].set_ylabel('Throughput (ops/s)', labelpad=20)
    
    for j in range(len(keys)):    
     axes[len(nodes)-1][j].set_xlabel(xlabel)

    for h in h2:
        h.set_markersize(8)
        h.set_linewidth(3)
    labels_handles = {}
    labels_handles.update(dict(zip(l2, h2)))
    
    for ax in axes.flatten():
        ax.legend().remove()
    
    fig.legend(h2, l2,
        loc='upper center', fontsize=12, title_fontsize=16, title=hue_label,
        ncol=num_hues if num_hues < 6 else int(num_hues / 2),
        columnspacing=1, edgecolor='white', borderpad=0)
    
    plt.show()

    # filename = name + ".png"
    # dirname = os.path.dirname(filename)
    # os.makedirs(dirname, exist_ok=True)
    # fig.savefig(filename, dpi=300, bbox_extra_artists=(legend,)
    #             if legend is not None else None, bbox_inches='tight')
    
            
def plot_throughput(xcol, originals, summary, hue, xlabel, hue_label, name):
    global x1_, x2_, x3_, y_
    # make a grid of subplots with r X c plots of size figsize
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8, 5))
    seaborn.set_theme(style='ticks')
    markersize = 8

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("viridis", num_hues)
    per = seaborn.lineplot(
        data=originals,
        x=xcol,
        y=y_,
        ax=ax1,
        hue=hue,
        style=hue,
        markers=True,
        markersize=markersize,
        # errorbar='sd',
        palette=palette
    )
    totals = seaborn.lineplot(
        data=summary,
        x=xcol,
        y='total',
        ax=ax2,
        hue=hue,
        style=hue,
        markers=True,
        markersize=markersize,
        palette=palette
    )

    font = {'fontsize': 24}
    for ax in ax1, ax2:
        ax.ticklabel_format(axis='y', scilimits=[-5, 3])
        ax.yaxis.get_offset_text().set_fontsize(24)
        ax.yaxis.set_major_locator(plt.MaxNLocator(5))
        ax.tick_params(labelsize=24)
        ax.set_ylim(ymin=0)

    ax1.set_title('Per-client', font)
    ax1.set_ylabel('Throughput (ops/s)', font, labelpad=20)
    ax1.set_xlabel(xlabel, font)
    # ax1.invert_xaxis()
    ax1.set_ylim(1.5e3)
    
    ax2.set_title('Total', font)
    ax2.set_ylabel('', font)
    ax2.set_xlabel(xlabel, font)
    # ax2.invert_xaxis()
    ax2.set_ylim(2e5)
    
    # h1, l1 = totals.get_legend_handles_labels()
    h2, l2 = ax1.get_legend_handles_labels()
    for h in h2:
        h.set_markersize(24)
        h.set_linewidth(3)
    labels_handles = {}
    # labels_handles.update(dict(zip(l1, h1)))
    labels_handles.update(dict(zip(l2, h2)))
    totals.legend().remove()
    per.legend().remove()
    legend = None
    if num_hues != 1:
        legend = fig.legend(
            h2, l2, bbox_to_anchor=(.5, 1.27),
            loc='center', fontsize=24, title_fontsize=24, title=hue_label,
            ncol=num_hues if num_hues < 6 else int(num_hues / 2),
            # ncol=int(num_hues / 2),
            columnspacing=1, edgecolor='white', borderpad=0)

    filename = name + ".png"
    dirname = os.path.dirname(filename)
    os.makedirs(dirname, exist_ok=True)
    fig.savefig(filename, dpi=300, bbox_extra_artists=(legend,)
                if legend is not None else None, bbox_inches='tight')


def plot_p50(data):
    print("Plotting latency...")
    
    columns = ['lock_type', 'experiment_params.num_clients', 'experiment_params.num_nodes', 
               'experiment_params.workload.max_key', 'experiment_params.workload.p_local',
               'experiment_params.remote_budget', 'experiment_params.local_budget', 
               'results.driver.latency.summary.p50']
    data = data[columns]
    
    data['results.driver.latency.summary.p50'] = data['results.driver.latency.summary.p50'].apply(lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    data = data.explode('results.driver.latency.summary.p50')
    # reset index number to label each row
    data = data.reset_index(drop=True)
    print(data)

def plot_locality_lines(nodes, keys, summary, name):
    global x8_, y_
            
    # make a grid of subplots with a row for each node number and a column for each key setup
    fig, axes = plt.subplots(len(nodes), len(keys), figsize=(12, 3))
    seaborn.set_theme(style='ticks')
    markersize = 8
    
    # if hue != None:
    #     num_hues = len(summary.reset_index()[hue].dropna().unique())
    # else:
    #     num_hues = 1
   
    
    plt.subplots_adjust(hspace = 1.2)

    # reset index in order to access all fields for hue
    summary = summary.reset_index()

    #filter data to only include desire p_local lines
    plocal = [.95, .9, .85]
    summary = summary[summary['experiment_params.workload.p_local'].isin(plocal)]
    summary = summary.reset_index(drop=True)
    
    # filter out the data with 16 threads
    summary = summary[summary[x8_] < 13]
    summary = summary.reset_index(drop=True)
    
    #filter data to only include desire remote_budget data
    budget = [10]
    summary = summary[summary['experiment_params.remote_budget'].isin(budget)]
    summary = summary.reset_index(drop=True)

    plt.subplots_adjust(wspace = 0.25)

    for i, node in enumerate(nodes):
        for j, key in enumerate(keys):
            data = summary[summary['experiment_params.workload.max_key'] == key]
            data = data[data['experiment_params.num_nodes'] == node]
            data = data.reset_index(drop=True)
            seaborn.lineplot(
                    data=data,
                    x=x8_,
                    y='total',
                    ax=axes[i][j],
                    hue='lock_type',
                    style='experiment_params.workload.p_local',
                    markers=True,
                    markersize=markersize,
                    palette="husl",
            )
            # set y axis to start at 0
            axes[i][j].set_ylim(0, axes[i][j].get_ylim()[1])
            # set 3 ticks on y axis with values auto-chosen
            # axes[i][j].set_yticks(axes[i][j].get_yticks()[::len(axes[i][j].get_yticks()) // 3])
            h2, l2 = axes[i][j].get_legend_handles_labels()
            axes[i][j].set_ylabel('') 
            axes[i][j].set_xlabel('Threads per Node')
            axes[i][j].set_title(str(key) + " Keys, " + str(node) + " Nodes")
        axes[i][0].set_ylabel('Aggregated Throughput (ops/s)', labelpad=20)

    
    for h in h2:
        h.set_markersize(24)
        h.set_linewidth(3)
    labels_handles = {}
    labels_handles.update(dict(zip(l2, h2)))

    
    for ax in axes.flatten():
        ax.legend().remove()
    
    fig.legend(h2, l2,
        loc='upper center', fontsize=8, title_fontsize=10, title='Lock', markerscale=.5,
        ncol=2, columnspacing=1, edgecolor='white', borderpad=0)
   
    
    plt.show()

def plot(datafile, lock_type):
    data = pandas.read_csv(datafile)
    # datafile2 = "/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/one_lock/plots/5node_localp.csv"
    # if os.path.exists(datafile2) :
    #     data = merge_csv(datafile, datafile2)
    # else:
    #     data = pandas.read_csv(datafile)
               
    alock = data[data['experiment_params.name'].str.count("alock.*") == 1]
    alock['lock_type'] = 'ALock'
    mcs = data[data['experiment_params.name'].str.count("mcs.*") == 1]
    mcs['lock_type'] = 'MCS'
    spin = data[data['experiment_params.name'].str.count("spin.*") == 1]
    spin['lock_type'] = 'Spin'
 
    data = pandas.concat([alock, mcs, spin])
    # plot_p50(data)
    columns = ['lock_type', 'experiment_params.num_threads', 'experiment_params.num_clients', 'experiment_params.num_nodes', 
               'experiment_params.workload.max_key', 'experiment_params.workload.p_local', 
               'experiment_params.remote_budget', 'experiment_params.local_budget', 'results.driver.qps.summary.mean']
    data = data[columns]
    print(data)
    data['results.driver.qps.summary.mean'] = data['results.driver.qps.summary.mean'].apply(
        lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    data = data.explode('results.driver.qps.summary.mean')
    data = data.reset_index(drop=True)

    summary = get_summary(data)
 
    # data = alock
    # plot_throughput(x1_, data, summary, 'lock_type', 'Clients', 'Lock type', os.path.join(FLAGS.figdir, FLAGS.exp, 'n2_m10'))
    
    # plot_grid(nodes, keys, x3_, data, summary, 'lock_type', 'Clients', 'Lock type', os.path.join(FLAGS.figdir, FLAGS.exp, 'alock_spin'), False)
    
    nodes = [5, 10, 20]
    keys = [100, 1000]
    # clients = [40, 120, 180]
    # p_local = [.3, .5, .8]
    # plot_budget(nodes, keys, clients, p_local, data, summary, 'lock_type', 'Remote Budget', 'Lock type', os.path.join(FLAGS.figdir, FLAGS.exp, 'local_budget'), False)
   
    # plot_locality(nodes, clients, keys, data, summary, 'lock_type', 'Lock type', os.path.join(FLAGS.figdir, FLAGS.exp, 'locality2'), False)
    plot_locality_lines(nodes, keys, summary, os.path.join(FLAGS.figdir, FLAGS.exp, 'locality2'))
 