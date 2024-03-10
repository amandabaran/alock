#include <vector>

#include <rome/logging/logging.h>
#include <rome/rdma/rdma.h>
#include <rome/util/cli.h>


#include "setup.h"
#include "experiment.h"
#include "client.h"

/// An initializer list of all of the command line argument names, types, and
/// descriptions.
///
// TODO: Is this too specific to CloudLab?
auto ARGS = {
    rome::util::I64_ARG(
        "--node_id",
        "The node's id. (nodeX in CloudLab should have X in this option)"),
    rome::util::I64_ARG(
        "--runtime",
        "How long to run the experiment for. Only valid if unlimited_stream"),
    rome::util::BOOL_ARG_OPT(
        "--unlimited_stream",
        "If the stream should be endless, stopping after runtime"),
    rome::util::I64_ARG(
        "--op_count",
        "How many operations to run. Only valid if not unlimited_stream"),
    rome::util::I64_ARG("--region_size", "How big the region should be in 2^x bytes"),
    rome::util::I64_ARG("--thread_count",
                 "How many threads to spawn with the operations"),
    rome::util::I64_ARG("--node_count", "How many nodes are in the experiment"),
    rome::util::I64_ARG(
        "--qp_max",
        "The max number of queue pairs to allocate for the experiment."),
    rome::util::I64_ARG("--contains", "Percentage of operations are contains, "
                               "(contains + insert + remove = 100)"),
    rome::util::I64_ARG("--insert", "Percentage of operations are inserts, (contains "
                             "+ insert + remove = 100)"),
    rome::util::I64_ARG("--remove", "Percentage of operations are removes, (contains "
                             "+ insert + remove = 100)"),
    rome::util::I64_ARG("--key_lb", "The lower limit of the key range for operations"),
    rome::util::I64_ARG("--key_ub", "The upper limit of the key range for operations"),
};

#define PATH_MAX 4096
#define PORT_NUM 18000

using namespace rome::rdma;

// The optimal number of memory pools is mp=min(t, MAX_QP/n) where n is the
// number of nodes and t is the number of threads To distribute mp (memory
// pools) across t threads, it is best for t/mp to be a whole number

int main(int argc, char **argv) {
  using namespace std::string_literals;

  ROME_INIT_LOG();

  // Configure the args object, parse the command line, and turn it into a
  // useful object
  rome::util::ArgMap args;
  if (auto res = args.import_args(ARGS); res) {
    ROME_FATAL(res.value());
  }
  if (auto res = args.parse_args(argc, argv); res) {
    args.usage();
    ROME_FATAL(res.value());
  }
  BenchmarkParams params(args);

  // Check node count, ensure this node is part of the experiment
  if (params.node_count <= 0 || params.thread_count <= 0) {
    ROME_FATAL("Node count and thread count cannot be zero");
  }
  if (params.node_id >= params.node_count) {
    ROME_INFO("This node is not in the experiment. Exiting...");
    exit(0);
  }

  // Since we want a 1:1 thread to qp mapping, we currently need to use one memory pool per thread.
  int mp = params.thread_count;
  ROME_INFO("Distributing {} MemoryPools across {} threads", mp,
            params.thread_count);

  // Initialize the vector of peers.  A node can appear more than once, as long
  // as it has a different port.
  std::vector<Peer> peers;
  for (uint16_t n = 0; n < mp * params.node_count; n++) {
    Peer next(n, "node"s + std::to_string((int)n / mp), PORT_NUM + n + 1);
    peers.push_back(next);
    ROME_DEBUG("Peer list {}:{}@{}", n, peers.at(n).id, peers.at(n).address);
  }

  Peer host = peers.at(0);
  // Initialize memory pools into an array
  std::vector<std::thread> mempool_threads;
  std::shared_ptr<rdma_capability> pools[mp];
  // Create one memory pool per thread
  uint32_t block_size = 1 << params.region_size;
  //Calculate to determine size of memory pool needed (based on number of locks per mp)
  uint32_t bytesNeeded = ((64 * params.key_ub) + (64 * 5 * params.thread_count));
  block_size = 1 << uint32_t(ceil(log2(bytesNeeded)));
  for (int i = 0; i < mp; i++) {
    mempool_threads.emplace_back(std::thread(
        [&](int mp_index, int self_index) {
          ROME_DEBUG("Creating pool");
          Peer self = peers.at(self_index);
          std::shared_ptr<rdma_capability> pool =
              std::make_shared<rdma_capability>(self);
          pool->init_pool(block_size, peers);
          pools[mp_index] = pool;
        },
        i, (params.node_id * mp) + i));
  }
  // Let the init finish
  for (int i = 0; i < mp; i++) {
    mempool_threads[i].join();
  }


  // Share root pointers of memory pool to all nodes using 2-sided send/recv

  // Barrier to start all the clients at the same time
  std::barrier client_sync = std::barrier(params.thread_count);
  // [mfs]  This seems like a misuse of ProtoBufs: why would the local threads
  //        communicate via ProtoBufs?
  // [esl]  ProtoBufs were a pain to code with. I think the ClientAdaptor
  //        returns a protobuf and I never understood why it didn't just return
  //        an object.
  // TODO:  In the refactoring of the client adaptor, remove dependency on
  //        ProtoBufs for a workload object
  rome::ResultProto results[params.thread_count];
  rome::WorkloadDriverProto workload_results[params.thread_count];
  for (int i = 0; i < params.thread_count; i++) {
    threads.emplace_back(std::thread(
        [&](int thread_index) {
            //   int mempool_index = thread_index % mp;
            int mempool_index = thread_index;
            std::shared_ptr<rdma_capability> pool = pools[mempool_index];
            Peer self = peers.at((params.node_id * mp) + mempool_index);

            auto c = clients[thread_index];
            auto node_proto = cluster.nodes().begin();
            while (node_proto != cluster.nodes().end() && node_proto->nid() != c.id) ++node_proto;
            ROME_ASSERT(node_proto != cluster.nodes().end(), "Failed to find client: {}", c.id);
                

            
            ROME_DEBUG("Creating client");
            // Create and run a client in a thread
            std::unique_ptr<Client<IHT_Op<int, int>>> client =
                Client<IHT_Op<int, int>>::Create(
                    host, endpoint_managers[thread_index], params, &client_sync,
                    std::move(iht), pool);
            double populate_frac =
                0.5 / (double)(params.node_count * params.thread_count);
            rome::util::StatusVal<WorkloadDriverProto> output =
                Client<IHT_Op<int, int>>::Run(std::move(client), thread_index,
                                            populate_frac);
            // [mfs]  It would be good to document how a client can fail, because
            //        it seems like if even one client fails, on any machine, the
            //        whole experiment should be invalidated.
            // [esl]  I agree. A strange thing though: I think the output of
            //        Client::Run is always OK.  Any errors just crash the script,
            //        which lead to no results being generated?
            if (output.status.t == rome::util::StatusType::Ok &&
                output.val.has_value()) {
            workload_results[thread_index] = output.val.value();
            } else {
            ROME_ERROR("Client run failed");
            }
            ROME_INFO("[CLIENT THREAD] -- End of execution; -- ");
        },
    i));
  }

  // Join all threads
  int i = 0;
  for (auto it = threads.begin(); it != threads.end(); it++) {
    // For debug purposes, sometimes it helps to see which threads haven't
    // deadlocked
    ROME_DEBUG("Syncing {}", ++i);
    auto t = it;
    t->join();
  }
  // [mfs]  Again, odd use of ProtoBufs for relatively straightforward combining
  //        of results.  Or am I missing something, and each node is sending its
  //        results, so they are all accumulated at the main node?
  // [esl]  Each thread will create a result proto. The result struct will parse
  //        this and save it in a csv which the launch script can scp.
  Result result[params.thread_count];
  for (int i = 0; i < params.thread_count; i++) {
    // TODO:  This process of aggregating results is error-prone.  This ought to
    //        be the kind of thing that is done internally by a method of the
    //        Result object.
    result[i] = Result(params);
    if (workload_results[i].has_ops() &&
        workload_results[i].ops().has_counter()) {
      result[i].count = workload_results[i].ops().counter().count();
    }
    if (workload_results[i].has_runtime() &&
        workload_results[i].runtime().has_stopwatch()) {
      result[i].runtime_ns =
          workload_results[i].runtime().stopwatch().runtime_ns();
    }
    if (workload_results[i].has_qps() &&
        workload_results[i].qps().has_summary()) {
      auto qps = workload_results[i].qps().summary();
      result[i].units = qps.units();
      result[i].mean = qps.mean();
      result[i].stdev = qps.stddev();
      result[i].min = qps.min();
      result[i].p50 = qps.p50();
      result[i].p90 = qps.p90();
      result[i].p95 = qps.p95();
      result[i].p999 = qps.p999();
      result[i].max = qps.max();
    }
    ROME_DEBUG("Protobuf Result {}\n{}", i, result[i].result_as_debug_string());
  }

  // [mfs] Does this produce one file per node?
  // [esl] Yes, this produces one file per node,
  //       The launch.py script will scp this file and use the protobuf to
  //       interpret it
  std::ofstream file_stream("iht_result.csv");
  file_stream << Result::result_as_string_header();
  for (int i = 0; i < params.thread_count; i++) {
    file_stream << result[0].result_as_string();
  }
  file_stream.close();
  ROME_INFO("[EXPERIMENT] -- End of execution; -- ");
  return 0;
}