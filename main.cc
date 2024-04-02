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
    remus::util::BOOL_ARG_OPT(
        "--topology",
        "If the stream should use the specified topology"),
};

#define PATH_MAX 4096
#define PORT_NUM 14000

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
    Peer next(i, "node"s + std::to_string((int)i / mp), PORT_NUM + i + 1);
    peers.push_back(next);
    REMUS_DEBUG("Peer list {}:{}@{}", i, peers.at(i).id, peers.at(i).address);
    if((int)i/mp == params.node_id){
      REMUS_DEBUG("Peer {} is local on node {}", i, (int)i/mp);
      locals.insert(i+1);
    }
  }
  

  // Initialize memory pools into an array
  std::vector<std::thread> mempool_threads;
  std::shared_ptr<rdma_capability> pools[mp];
  // Create one memory pool per thread
  uint32_t block_size = 1 << params.region_size;
  //Calculate to determine size of memory pool needed (based on number of locks per mp)
  // uint32_t bytesNeeded = ((64 * params.max_key) + (64 * 5 * params.thread_count));
  // block_size = 1 << uint32_t(ceil(log2(bytesNeeded)));
  for (int i = 0; i < mp; i++) {
    mempool_threads.emplace_back(std::thread(
        [&](int mp_index, int self_index) {
          REMUS_DEBUG("Creating pool");
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

  sleep(3);
  // std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // Create and Launch each of the clients.
  std::vector<std::thread> client_threads;
  client_threads.reserve(params.thread_count);
  std::barrier client_barrier(params.thread_count);
  remus::metrics::WorkloadDriverResult workload_results[params.thread_count];
  for (int i = 0; i < params.thread_count; i++){
    client_threads.emplace_back(std::thread([&mp, &peers, &pools, &params, &locals, &workload_results, &client_barrier](int tidx){
      auto self_idx = (params.node_id * mp) + tidx;
      Peer self = peers.at(self_idx);
      auto pool = pools[tidx];
    
      std::vector<Peer> others;
      #ifdef REMOTE_ONLY
        REMUS_DEBUG("Including self in others for loopback connection");
        std::copy(peers.begin(), peers.end(), std::back_inserter(others));
      #else
        if (std::is_same<LockType, ALock>::value){
          std::copy_if(peers.begin(), peers.end(), std::back_inserter(others),
                        [self](auto &p) { return p.id != self.id; });
        } else {
          REMUS_DEBUG("Including self in others for loopback connection");
          std::copy(peers.begin(), peers.end(), std::back_inserter(others));
        }   
      #endif
      // Create "node" (prob want to rename)
      REMUS_DEBUG("Creating node for client {}:{}", self.id, self.port);
      auto node = std::make_unique<Node<key_type, LockType>>(self, others, pool, params);
      // Create mem pools of lock tables on each node and connect with all clients
      OK_OR_FAIL(node->connect());
      // Make sure Connect() is done before launching clients
      client_barrier.arrive_and_wait();
      // We need a cross node barrier 
      sleep(5);

      std::unique_ptr<Client<key_type>> client = Client<key_type>::Create(self, params, &client_barrier, pool, node->getRootPtrMap(), locals);
      client_barrier.arrive_and_wait();

      remus::util::StatusVal<remus::metrics::WorkloadDriverResult> output = Client<key_type>::Run(std::move(client), params);
      if (output.status.t == remus::util::StatusType::Ok && output.val.has_value()) {
        workload_results[tidx] = output.val.value();
      } else {
        REMUS_ERROR("Client {} run failed", self.id);
      }
      REMUS_INFO("[CLIENT THREAD {}] -- End of Execution", self.id);
    }, i));
  }

    // Join all client threads
  int i = 0;
  for (auto it = client_threads.begin(); it != client_threads.end(); it++){
      REMUS_DEBUG("Joining thread {}", ++i);
      auto t = it;
      t->join();
  }

  // Puts results from all threads into result array
  Result result[params.thread_count];
  for (int i = 0; i < params.thread_count; i++) {
    // REMUS_DEBUG("Protobuf Result {}\n{}", i, result[i].result_as_debug_string());
    result[i] = Result(params);
    if (workload_results[i].ops.has_counter()) {
      result[i].count = workload_results[i].ops.try_get_counter()->counter;
    }
    if (workload_results[i].runtime.has_stopwatch()) {
      result[i].runtime_ns = workload_results[i].runtime.try_get_stopwatch()->runtime_ns;
    }
    if (workload_results[i].qps.has_summary()) {
      auto qps = workload_results[i].qps.try_get_summary();
      result[i].units = qps->units;
      result[i].mean = qps->mean;
      result[i].stdev = qps->stddev;
      result[i].min = qps->min;
      result[i].p50 = qps->p50;
      result[i].p90 = qps->p90;
      result[i].p95 = qps->p95;
      result[i].p999 = qps->p99;
      result[i].max = qps->max;
    }
    REMUS_INFO("Protobuf Result {}\n{}", i, result[i].result_as_debug_string());
  }

  std::ofstream file_stream("exp_result.csv");
  file_stream << Result::result_as_string_header();
  for (int i = 0; i < params.thread_count; i++) {
    // Add results from all threads to result file
    file_stream << result[i].result_as_string();
  }
  file_stream.close();
  REMUS_INFO("[EXPERIMENT] -- End of execution; -- ");
  return 0;
}