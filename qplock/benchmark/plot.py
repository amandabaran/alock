from enum import Enum
from absl import flags
from absl import app
import os
import google.protobuf.text_format as text_format
import google.protobuf.descriptor as descriptor
import qplock.benchmark.experiment_pb2 as experiment
from alive_progress import alive_bar
import pandas
import matplotlib.pyplot as plt
import seaborn
from rome.rome.util.debugpy_util import debugpy_hook


flags.DEFINE_string("figdir", 'figures',
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
            data |= result
        else:
            data[path + ('.' if len(path) !=
                         0 else '') + field.name] = getattr(proto, field.name)
    return data


x1_ = 'experiment_params.cluster_size'
x2_ = 'lock_type'
# x2_ = 'experiment_params.num_readonly'
# x3_ = 'experiment_params.workload.worker_threads'
# x4_ = 'experiment_params.num_servers'
# x_ = [x1_, x2_, x3_, x4_]
x_ = [x1_, x2_]
y_ = 'results.driver.qps.summary.mean'
cols_ = [x1_, x2_,  y_]

r_ = 'results.read_only'
n_ = 'results.client.name'
c_ = 'results.client.nid'


def get_data_common(data, ycsb):
    global cols_, y_, r_, n_, c_
    data = data[data['experiment_params.workload.ycsb']
                == common.get_ycsb(ycsb)]

    y_data = data[cols_]
    y_data = y_data.reset_index(drop=True)

    y_data[y_] = y_data[y_].apply(
        lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    y_data = y_data.explode(y_)
    y_data = y_data.reset_index(drop=True)

    r_data = data[r_]
    r_data = r_data.reset_index(drop=True)
    r_data = r_data.apply(lambda s: [True if x.strip(
        ' []').lower() == 'true' else False for x in s.strip(' []').split(',')])
    r_data = r_data.explode()
    r_data = r_data.reset_index(drop=True)

    n_data = data[n_]
    n_data = n_data.reset_index(drop=True)
    n_data = n_data.apply(lambda s: [x.strip(' \'')
                          for x in s.strip(' []').split(',')])
    n_data = n_data.explode()
    n_data = n_data.reset_index(drop=True)

    c_data = data[c_]
    c_data = c_data.reset_index(drop=True)
    c_data = c_data.apply(lambda s: [int(x) for x in s.strip(' []').split(',')])
    c_data = c_data.explode()
    c_data = c_data.reset_index(drop=True)

    data = pandas.concat([y_data, r_data, n_data, c_data], axis=1)
    print(data)
    return data


def get_client_data(data, ycsb):
    data = data[data['experiment_params.mode'] == 1]
    data = data[data[x3_] <= 64]
    # data = data[data[x1_] + data[x2_] <= 64]
    return get_data_common(data, ycsb)


def get_server_data(data, ycsb):
    data = data[data['experiment_params.mode'] == 0]
    return get_data_common(data, ycsb)


def get_originals(data):
    global cols_
    return data[cols_].rename(
        columns={x1_: 'c', x2_: 'r', x3_: 'w', x4_: 's'})


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
    print(summary)
    return summary


def client_throughput(data, nreadonly, nworkers, nservers):
    global x1_, x2_, x3_, y_, r_, n_, c_

    data = data[data[r_] == False]
    if nreadonly != None:
        data = data[data[x2_] == nreadonly]
    if nworkers != None:
        data = data[data[x3_] == nworkers]
    if nservers != None:
        data = data[data[x4_] == nservers]

    originals = get_originals(data)
    summary = get_summary(data)

    name = 'client'

    return 'c', originals,  summary,  name


def plot_throughput(
        xcol, originals, summary, hue, xlabel, hue_label, name):
    global x1_, x2_, x3_, y_, r_, n_, c_
    fig, axes = plt.subplots(1, 2, figsize=(15, 3))
    seaborn.set_theme(style='ticks')
    markersize = 32

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("flare_r", num_hues)
    per = seaborn.lineplot(
        data=originals,
        x=xcol,
        y=y_,
        ax=axes[0],
        hue=hue,
        style=hue,
        markers=True,
        markersize=markersize,
        ci='sd',
        palette=palette
    )
    totals = seaborn.lineplot(
        data=summary,
        x=xcol,
        y='total',
        ax=axes[1],
        hue=hue,
        style=hue,
        markers=True,
        markersize=markersize,
        palette=palette
    )

    font = {'fontsize': 24}
    for ax in axes:
        ax.ticklabel_format(axis='y', scilimits=[-5, 3])
        ax.yaxis.get_offset_text().set_fontsize(24)
        ax.yaxis.set_major_locator(plt.MaxNLocator(5))
        ax.tick_params(labelsize=24)
        ax.set_ylim(ymin=0)

    axes[0].set_title('Per-client', font)
    axes[0].set_ylabel('Throughput (ops/s)', font, labelpad=20)
    axes[0].set_xlabel(xlabel, font)
    axes[0].set_yscale('log')
    axes[0].set_ylim(500, 1.5e5)
    axes[1].set_title('Total', font)
    axes[1].set_ylabel('', font)
    axes[1].set_xlabel(xlabel, font)
    # axes[1].set_ylim(0, 1.25e7)

    # h1, l1 = totals.get_legend_handles_labels()
    h2, l2 = axes[0].get_legend_handles_labels()
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


def plot_latency(data):
    print("Plotting latency...")
    # columns = ['experiment_params.name', 'experiment_params.cluster_size',
    #            'driver.qps.summary.mean', 'driver.latency.summary.mean',
    #            'driver.latency.summary.p99', 'driver.latency.summary.p999']
    # data = data[columns]
    # data = data[data['experiment_params.name'].str.count('.*_c.*') == 0]


def generate_csv(results_dir, datafile):
    assert(os.path.exists(results_dir))
    result_files = os.walk(results_dir)
    results_protos = []
    data_files = []
    for root, _, files in result_files:
        if len(files) == 0:
            continue
        for name in files:
            data_files.append(os.path.join(root, name))

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


configs = {
    'exp1': {
        'c': {},
        'r': {},
        'w': {'nclients': 0, 'nreadonly': 0, 'nservers': 1, 'hue': 'c', 'xlabel': '', 'hlabel': ''},
    },
    'exp2': {
        'c': {},
        'r': {'nclients': 0,
              'nworkers': None,  'nservers': 1, 'hue': 'w', 'xlabel': 'Num. read-only clients', 'hlabel': 'Num. workers'},
        'w': {'nclients': 0,
              'nreadonly': None, 'nservers': 1, 'hue': 'r', 'xlabel': 'Num. workers', 'hlabel': 'Num. read-only clients'},
    },
    'exp3': {
        'c': {'nreadonly': 20, 'nworkers': 0, 'nservers': None, 'hue': 's', 'xlabel': 'Num. clients', 'hlabel': 'Num. servers'},
        'r': {'nclients': None,
              'nworkers': 0,  'nservers': 5, 'hue': 'c', 'xlabel': 'Num. read-only', 'hlabel': 'Num. clients'},
        'w': {},
    },
    'exp4': {
        'c': {},
        'r': {'nclients': 0,
              'nworkers': None,  'nservers': 1, 'hue': 'w', 'xlabel': 'Num. read-only clients', 'hlabel': 'Num. workers'},
        'w': {'nclients': 0,
              'nreadonly': None, 'nservers': 1, 'hue': 'r', 'xlabel': 'Num. workers', 'hlabel': 'Num. read-only clients'},
    },
    'exp5': {
        'c': {},
        'r': {'nclients': 0,
              'nworkers': None,  'nservers': 1, 'hue': 'w', 'xlabel': 'Num. read-only clients', 'hlabel': 'Num. workers'},
        'w': {'nclients': 0,
              'nreadonly': None, 'nservers': 1, 'hue': 'r', 'xlabel': 'Num. workers', 'hlabel': 'Num. read-only'},
    },
    'exp6': {
        'c': {},
        'r': {'nclients': 0,
              'nworkers': None,  'nservers': 1, 'hue': 'w', 'xlabel': 'Num. read-only clients', 'hlabel': 'Num. workers'},
        'w': {'nclients': 0,
              'nreadonly': None, 'nservers': 1, 'hue': 'r', 'xlabel': 'Num. workers', 'hlabel': 'Num. read-only'},
    },
}


def plot(datafile, ycsb_list):
    data = pandas.read_csv(datafile)
    data = data[data['experiment_params.workload.think_time_ns'] == 1000]
    # data = data[data['experiment_params.num_clients'] % 4 == 0]
                # [data['experiment_params.num_clients'] >= 30]

    mcs = data[data['experiment_params.name'].str.count("lmcs.*") == 1]
    mcs['lock_type'] = 'MCS'

    spin = data[data['experiment_params.name'].str.count("lspin.*") == 1]
    spin['lock_type'] = 'Spin'

    data = pandas.concat([mcs, spin])
    data = data[['lock_type', 'experiment_params.cluster_size',
                 'results.driver.qps.summary.mean']]

    # y_data[y_] = y_data[y_].apply(
    #     lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    # y_data = y_data.explode(y_)
    # y_data = y_data.reset_index(drop=True)
    data['results.driver.qps.summary.mean'] = data['results.driver.qps.summary.mean'].apply(
        lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    data = data.explode('results.driver.qps.summary.mean')
    data = data.reset_index(drop=True)
    print(data)
    summary = get_summary(data)

    plot_throughput(x1_, data, summary, 'lock_type', 'Num. clients',
                    'Lock type', os.path.join(FLAGS.figdir, 'test'))
    print(data)
    # for ycsb in ycsb_list:
    #     client_data = get_client_data(data, ycsb)

    #     c = configs[FLAGS.exp]
    #     client_config = c['c']
    #     readonly_config = c['r']
    #     worker_config = c['w']

    #     if len(client_config.keys()) != 0:
    #         client_tput = client_throughput(
    #             client_data, client_config['nreadonly'],
    #             client_config['nworkers'],
    #             client_config['nservers'])
    #         save = os.path.join(FLAGS.figdir, ycsb, client_tput[-1])
    #         plot_throughput(
    #             *(client_tput[: -1]),
    #             client_config['hue'],
    #             client_config['xlabel'],
    #             client_config['hlabel'],
    #             save)

    #     if len(readonly_config.keys()) != 0:
    #         readonly_tput = readonly_throughput(
    #             client_data, readonly_config['nclients'],
    #             readonly_config['nworkers'],
    #             readonly_config['nservers'])
    #         save = os.path.join(FLAGS.figdir, ycsb, readonly_tput[-1])
    #         plot_throughput(
    #             *(readonly_tput[: -1]),
    #             readonly_config['hue'],
    #             readonly_config['xlabel'],
    #             readonly_config['hlabel'],
    #             save)

    #     if len(worker_config.keys()) != 0:
    #         server_data = get_server_data(data, ycsb)
    #         worker_tput = worker_throughput(
    #             server_data, worker_config['nclients'],
    #             worker_config['nreadonly'],
    #             worker_config['nservers'])
    #         save = os.path.join(FLAGS.figdir, ycsb, worker_tput[-1])
    #         plot_throughput(
    #             *(worker_tput[: -1]),
    #             worker_config['hue'],
    #             worker_config['xlabel'],
    #             worker_config['hlabel'],
    #             save)
