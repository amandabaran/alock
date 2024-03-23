#include <vector>

#include <remus/logging/logging.h>
#include <remus/rdma/rdma.h>
#include <remus/util/cli.h>

#include "setup.h"
#include "experiment.h"
#include "client.h"

/// An initializer list of all of the command line argument names, types, and
/// descriptions.
///
// TODO: Is this too specific to CloudLab?
auto ARGS = {
    remus::util::I64_ARG(
        "--node_id",
        "The node's id. (nodeX in CloudLab should have X in this option)"),
    remus::util::I64_ARG(
        "--runtime",
        "How long to run the experiment for. Only valid if unlimited_stream"),
    remus::util::BOOL_ARG_OPT(
        "--unlimited_stream",
        "If the stream should be endless, stopping after runtime"),
    remus::util::I64_ARG("--op_count", "The number of operations to run if unlimited stream is passed as False."),
    remus::util::I64_ARG("--region_size", "How big the region should be in 2^x bytes"),
    remus::util::I64_ARG("--thread_count",
                 "How many threads to spawn with the operations"),
    remus::util::I64_ARG("--node_count", "How many nodes are in the experiment"),
    remus::util::I64_ARG(
        "--qp_max",
        "The max number of queue pairs to allocate for the experiment."),
    remus::util::I64_ARG("--p_local", "Percentage of operations that are local"),
    remus::util::I64_ARG("--min_key", "The lower limit of the key range for operations"),
    remus::util::I64_ARG("--max_key", "The upper limit of the key range for operations"),
    remus::util::I64_ARG("--local_budget", "Initial budget for Alock Local Cohort"),
    remus::util::I64_ARG("--remote_budget", "Initial budget for Alock Remote Cohort"),
};

#define PATH_MAX 4096
#define PORT_NUM 18000

using namespace remus::rdma;

// The optimal number of memory pools is mp=min(t, MAX_QP/n) where n is the
// number of nodes and t is the number of threads To distribute mp (memory
// pools) across t threads, it is best for t/mp to be a whole number

int main(int argc, char **argv) {
  using namespace std::string_literals;

  REMUS_INIT_LOG();

  // Configure the args object, parse the command line, and turn it into a
  // useful object
  remus::util::ArgMap args;
  if (auto res = args.import_args(ARGS); res) {
    REMUS_FATAL(res.value());
  }
  if (auto res = args.parse_args(argc, argv); res) {
    args.usage();
    REMUS_FATAL(res.value());
  }
  BenchmarkParams params(args);

  // Check node count, ensure this node is part of the experiment
  if (params.node_count <= 0 || params.thread_count <= 0) {
    REMUS_FATAL("Node count and thread count cannot be zero");
  }
  if (params.node_id >= params.node_count) {
    REMUS_INFO("This node is not in the experiment. Exiting...");
    exit(0);
  }

  // Since we want a 1:1 thread to qp mapping, we currently need to use one memory pool per thread.
  int mp = params.thread_count;
  REMUS_INFO("Distributing {} MemoryPools across {} threads", mp,
            params.thread_count);


  // Initialize the vector of peers.  For ALock currently, each thread needs to have it's own unique peer (hence mp * node_count)
  // Start with peer id = 1 since we use 0 to represent an unlocked state in ALock
  std::vector<Peer> peers;
  // Also create a set of peer ids that are local to this node
  std::unordered_set<int> locals;
  for (uint16_t i = 0; i < mp * params.node_count; i++) {
    Peer next(i+1, "node"s + std::to_string((int)i / mp), PORT_NUM + i + 1);
    peers.push_back(next);
    REMUS_DEBUG("Peer list {}:{}@{}", i+1, peers.at(i).id, peers.at(i).address);
    if((int)i/mp == params.node_id){
      REMUS_DEBUG("Peer {} is local on node {}", i+1, (int)i/mp);
      locals.insert(i+1);
    }
  }
  

  // // Initialize memory pools into an array
  // std::vector<std::thread> mempool_threads;
  // std::shared_ptr<rdma_capability> pools[mp];
  // // Create one memory pool per thread
  // uint32_t block_size = 1 << params.region_size;
  // //Calculate to determine size of memory pool needed (based on number of locks per mp)
  // uint32_t bytesNeeded = ((64 * params.key_ub) + (64 * 5 * params.thread_count));
  // block_size = 1 << uint32_t(ceil(log2(bytesNeeded)));
  // for (int i = 0; i < mp; i++) {
  //   mempool_threads.emplace_back(std::thread(
  //       [&](int mp_index, int self_index) {
  //         REMUS_DEBUG("Creating pool");
  //         Peer self = peers.at(self_index);
  //         std::shared_ptr<rdma_capability> pool =
  //             std::make_shared<rdma_capability>(self);
  //         pool->init_pool(block_size, peers);
  //         pools[mp_index] = pool;
  //       },
  //       i, (params.node_id * mp) + i));
  // }
  // // Let the init finish
  // for (int i = 0; i < mp; i++) {
  //   mempool_threads[i].join();
  // }

  //  // Create and Launch each of the clients.
  // std::vector<std::thread> client_threads;
  // client_threads.reserve(params.thread_count);
  // std::barrier client_barrier(params.thread_count);
  // remus::ResultProto results[params.thread_count];
  // remus::WorkloadDriverProto workload_results[params.thread_count];
  // for (int i = 0; i < params.thread_count; i++){
  //   client_threads.emplace_back(std::thread([&clients, &params, &locals, &results, &client_barrier](int tidx){
  //     auto c = clients[tidx];
  //     Peer self = peers.at((params.node_id+1)*tidx);
  //     // Create "node" (prob want to rename)
  //     REMUS_DEBUG("Creating node for client {}:{}", self.id, self.port);

  //     std::vector<Peer> others;
  //     #ifdef REMOTE_ONLY
  //       REMUS_DEBUG("Including self in others for loopback connection");
  //       std::copy(nodes.begin(), nodes.end(), std::back_inserter(others));
  //     #else
  //       if (std::is_same<LockType, X::ALock>::value){
  //         std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(others),
  //                       [c](auto &p) { return p.id != c.id; });
  //       } else {
  //         REMUS_DEBUG("Including self in others for loopback connection");
  //         std::copy(nodes.begin(), nodes.end(), std::back_inserter(others));
  //       }   
  //     #endif

  
  //     auto node = std::make_unique<X::Node<key_type, LockType>>(*node_proto, others, cluster, experiment_params);
  //     // Create mem pools of lock tables on each node and connect with all clients
  //     OK_OR_FAIL(node->Connect());
  //     // Make sure Connect() is done before launching clients
  //     std::atomic_thread_fence(std::memory_order_release);
  //     client_barrier.arrive_and_wait();
  //     auto client = Client::Create(c, *node_proto, cluster, experiment_params, &client_barrier, 
  //                                     *(node->GetLockPool()), node->GetKeyRangeMap(), node->GetRootPtrMap(), locals);
  //     client_barrier.arrive_and_wait();                              
  //     try {
  //       auto result = Client::Run(std::move(client), experiment_params, &done);
  //       if (result.ok()){
  //         results[tidx] = result.value();
  //         REMUS_INFO("{}", results[tidx].DebugString());
  //       } else {
  //         REMUS_ERROR("Client run failed. (id={})", c.id);
  //       }
  //     } catch (std::exception &e){
  //       std::cout << "EXCEPTION: " << e.what() << std::endl;
  //     }
  //     REMUS_INFO("Client {} -- Execution Finished", c.id);
  //   }, i));
  // }

  //   // Join all client threads
  // int i = 0;
  // for (auto it = client_threads.begin(); it != client_threads.end(); it++){
  //     REMUS_DEBUG("Joining thread {}", ++i);
  //     auto t = it;
  //     t->join();
  // }

  // // [mfs]  Again, odd use of ProtoBufs for relatively straightforward combining
  // //        of results.  Or am I missing something, and each node is sending its
  // //        results, so they are all accumulated at the main node?
  // // [esl]  Each thread will create a result proto. The result struct will parse
  // //        this and save it in a csv which the launch script can scp.
  // Result result[params.thread_count];
  // for (int i = 0; i < params.thread_count; i++) {
  //   // TODO:  This process of aggregating results is error-prone.  This ought to
  //   //        be the kind of thing that is done internally by a method of the
  //   //        Result object.
  //   result[i] = Result(params);
  //   if (workload_results[i].has_ops() &&
  //       workload_results[i].ops().has_counter()) {
  //     result[i].count = workload_results[i].ops().counter().count();
  //   }
  //   if (workload_results[i].has_runtime() &&
  //       workload_results[i].runtime().has_stopwatch()) {
  //     result[i].runtime_ns =
  //         workload_results[i].runtime().stopwatch().runtime_ns();
  //   }
  //   if (workload_results[i].has_qps() &&
  //       workload_results[i].qps().has_summary()) {
  //     auto qps = workload_results[i].qps().summary();
  //     result[i].units = qps.units();
  //     result[i].mean = qps.mean();
  //     result[i].stdev = qps.stddev();
  //     result[i].min = qps.min();
  //     result[i].p50 = qps.p50();
  //     result[i].p90 = qps.p90();
  //     result[i].p95 = qps.p95();
  //     result[i].p999 = qps.p999();
  //     result[i].max = qps.max();
  //   }
  //   REMUS_DEBUG("Protobuf Result {}\n{}", i, result[i].result_as_debug_string());
  // }

  // // [mfs] Does this produce one file per node?
  // // [esl] Yes, this produces one file per node,
  // //       The launch.py script will scp this file and use the protobuf to
  // //       interpret it
  // std::ofstream file_stream("iht_result.csv");
  // file_stream << Result::result_as_string_header();
  // for (int i = 0; i < params.thread_count; i++) {
  //   file_stream << result[0].result_as_string();
  // }
  // file_stream.close();
  REMUS_INFO("[EXPERIMENT] -- End of execution; -- ");
  return 0;
}