from enum import Enum
from absl import flags
from absl import app
import os
import google.protobuf.text_format as text_format
import google.protobuf.descriptor as descriptor
import alock.benchmark.one_lock.experiment_pb2 as experiment
from alive_progress import alive_bar
import pandas
import matplotlib.pyplot as plt
import seaborn
from rome.rome.util.debugpy_util import debugpy_hook


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


x1_ = 'cluster_size'
x2_ = 'lock_type'
# x3_ = 'experiment_params.workload.worker_threads'
# x_ = [x1_, x2_, x3_]
x_ = [x1_, x2_]
y_ = 'results.driver.qps.summary.mean'
cols_ = [x1_, x2_,  y_]

r_ = 'results.read_only'
n_ = 'results.client.name'
c_ = 'results.client.nid'


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

def plot_throughput(
        xcol, originals, summary, hue, xlabel, hue_label, name):
    global x1_, x2_, x3_, y_, r_, n_, c_
    fig, axes = plt.subplots(1, 2, figsize=(15, 3))
    seaborn.set_theme(style='ticks')
    markersize = 24

    if hue != None:
        num_hues = len(summary.reset_index()[hue].dropna().unique())
    else:
        num_hues = 1
    palette = seaborn.color_palette("viridis", num_hues)
    per = seaborn.lineplot(
        data=originals,
        x=xcol,
        y=y_,
        ax=axes[0],
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
    axes[0].set_ylim(500, 1.5e7)
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


def plot(datafile, lock_type):
    data = pandas.read_csv(datafile)
    # TODO: THIS LINE CHANGES WHICH LOCK HOLD TIME WE ARE PLOTTING
    data = data[data['experiment_params.workload.think_time_ns'] == 500] #modify what this takes to plot differernt set of
    # data = data[data['experiment_params.num_clients'] % 4 == 0]
                # [data['experiment_params.num_clients'] >= 30]
    # print("DATA1", data)    
    
    alock = data[data['experiment_params.name'].str.count("alock.*") == 1]
    # print("ALOCK", alock)
    alock['lock_type'] = 'ALock'
    # data = alock
    # mcs = data[data['experiment_params.name'].str.count("mcs.*") == 1]
    # mcs['lock_type'] = 'MCS'
    spin = data[data['experiment_params.name'].str.count("spin.*") == 1]
    spin['lock_type'] = 'Spin'
    spin = spin[spin['lock_type'] == 'Spin']

    data = pandas.concat([alock, spin])
    # data = pandas.concat([mcs, spin])
    data = data[['cluster_size', 'lock_type', 'results.driver.qps.summary.mean']]
    data['results.driver.qps.summary.mean'] = data['results.driver.qps.summary.mean'].apply(
        lambda s: [float(x.strip()) for x in s.strip(' []').split(',')])
    data = data.explode('results.driver.qps.summary.mean')
    data = data.reset_index(drop=True)
    # print(data)
    summary = get_summary(data)

    plot_throughput(x1_, data, summary, 'lock_type', 'Clients',
                    'Lock type', os.path.join(FLAGS.figdir, '1Node'))
    # print(data)