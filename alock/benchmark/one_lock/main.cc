#include <infiniband/verbs.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "google/protobuf/text_format.h"
#include "alock/benchmark/one_lock/experiment.pb.h"
#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/node.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/metrics/summary.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_broker.h"
#include "rome/util/clocks.h"
#include "rome/util/proto_util.h"

#include "client.h"
#include "setup.h"

using ::X::ClusterProto;
using ::util::SystemClock;
using ::rome::rdma::MemoryPool;
namespace X {
ROME_PROTO_FLAG(ClusterProto);
}
ABSL_FLAG(X::ClusterProto, cluster, {}, "Cluster protobuf definition");

ROME_PROTO_FLAG(ExperimentParams);
ABSL_FLAG(ExperimentParams, experiment_params, {}, "Experimental parameters");  

volatile bool done = false;
std::function<void(int)> signal_handler_internal;
void signal_handler(int signum) { signal_handler_internal(signum); }

// THIS IS BEING LAUNCHED ON EACH NODE!!! SO, IT SHOULDN'T LOOP THROUGH ALL THE NODEIDS
int main(int argc, char *argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto cluster = absl::GetFlag(FLAGS_cluster);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(experiment_params));

  auto client_ids = experiment_params.client_ids(); 
  auto num_nodes = experiment_params.num_nodes();
  auto num_threads = experiment_params.num_threads();

  ROME_DEBUG("len(CLIENT_IDS): {}" , client_ids.size());
  ROME_DEBUG("num_nodes {}", num_nodes);

  //vector of all nodes in Peer form
  std::vector<Peer> nodes; 
  std::for_each(
      cluster.nodes().begin(), cluster.nodes().end(), [&](auto &c) {
        auto p =
            Peer((uint16_t)c.nid(), c.name(), (uint16_t)c.port());
        nodes.push_back(p);
  });

  ROME_DEBUG("cluster size: {}", nodes.size());

  //vector of clients on this node in Peer form
  std::vector<Peer> clients;
  for (const auto &c : client_ids){
    std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(clients),
                  [c](auto &p) { return p.id == c; });
  }

  ROME_DEBUG("num clients {}", clients.size());

  if (!experiment_params.workload().has_runtime() || experiment_params.workload().runtime() < 0) {
    signal_handler_internal = std::function([](int signum) {
      if (done) exit(1);
      ROME_INFO("Shutting down...");
      done = true;
    });
    signal(SIGINT, signal_handler);
  }

  // // Create "node" for each thread
  // for (const auto& c: client_ids){
  //   auto node_proto = cluster.nodes().begin();
  //   while (node_proto != cluster.nodes().end() && node_proto->nid() != c) ++node_proto;
  //   ROME_ASSERT(node_proto != cluster.nodes().end(), "Failed to find client: {}", c);

  //   ROME_DEBUG("Creating node for client {}", c);
  //   auto node = std::make_unique<X::Node<key_type, LockType>>(*node_proto, cluster, experiment_params.prefill(), clients);
  //   node_ptrs.insert(node);
  //   // Create mem pools of lock tables on each node and connect with all clients
  //   ROME_ASSERT_OK(node->Connect());
  // }

  // Create and Launch each of the clients.
  std::vector<std::future<absl::StatusOr<ResultProto>>> client_tasks;
  std::barrier client_barrier(experiment_params.client_ids().size());
  for (const auto &c : clients) {
    client_tasks.emplace_back(std::async([=, &client_barrier]() {
      auto node_proto = cluster.nodes().begin();
      while (node_proto != cluster.nodes().end() && node_proto->nid() != c.id) ++node_proto;
      ROME_ASSERT(node_proto != cluster.nodes().end(), "Failed to find client: {}", c.id);
      
      std::vector<Peer> others;
      std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(others),
                    [c](auto &p) { return p.id != c.id; });

      // Create "node" (prob want to rename)
      ROME_DEBUG("Creating node for client {}:{}", c.id, c.port);
      auto node = std::make_unique<X::Node<key_type, LockType>>(*node_proto, others, cluster, experiment_params.prefill());
      // Create mem pools of lock tables on each node and connect with all clients
      ROME_ASSERT_OK(node->Connect());
      // Make sure Connect() is done before launching clients
      std::atomic_thread_fence(std::memory_order_release);

      
      ROME_DEBUG("OTHERS SIZE {}", others.size());
      auto client = Client::Create(c, *node_proto, experiment_params, &client_barrier, 
                                    *(node->GetLockPool()), node->GetKeyRangeMap(), node->GetRootPtrMap());
      return Client::Run(std::move(client), experiment_params, &done);
    }));
  }

  // Join clients.
  std::vector<ResultProto> results;
  for (auto &r : client_tasks) {
    r.wait();
    ROME_ASSERT(r.valid(), "WTF");
    auto rproto = r.get();
    results.push_back(VALUE_OR_DIE(rproto));
    ROME_INFO("{}", VALUE_OR_DIE(rproto).DebugString());
  }

  RecordResults(experiment_params, results);
  
  ROME_INFO("Done");
  return 0;
}