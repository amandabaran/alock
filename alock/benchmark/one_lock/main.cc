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

#include "node_harness.h"
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

int main(int argc, char *argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto cluster = absl::GetFlag(FLAGS_cluster);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(experiment_params));

  auto node_ids = experiment_params.node_ids();
  auto num_nodes = experiment_params.num_nodes();
  auto num_threads = experiment_params.num_threads();

  //vector of all nodes in Peer form
  std::vector<Peer> peers; 
  std::for_each(
      cluster.nodes().begin(), cluster.nodes().end(), [&](auto &c) {
        auto p =
            Peer(c.nid(), c.name(), c.port());
      });

  if (!experiment_params.workload().has_runtime() ||
      experiment_params.workload().runtime() < 0) {
    signal_handler_internal = std::function([](int signum) {
      if (done) exit(1);
      ROME_INFO("Shutting down...");
      done = true;
    });
    signal(SIGINT, signal_handler);
  }

  std::vector<std::future<absl::StatusOr<ResultProto>>> node_tasks;
  std::barrier barrier(num_nodes * num_threads);
  for (const auto& n : node_ids) {
    auto iter = cluster.nodes().begin();
    while (iter != cluster.nodes().end() && iter->nid() != n) ++iter;
    ROME_ASSERT(iter != cluster.nodes().end(), "Failed to find node: {}", n);
    
    Peer self = MemoryPool::Peer(iter->nid(), iter->name(), iter->port());
    
    auto node = std::make_shared<X::Node<key_type, LockType>>(*iter, cluster, experiment_params.prefill());
    // Create 1 NodeHarness per thread on each node
    for (int i = 0; i < num_threads; i++) {
      // Create vector of peers not including self
      std::vector<Peer> others;
      std::copy_if(peers.begin(), peers.end(), std::back_inserter(others),
                    [self](auto &p) { return p.id != self.id; });
      node_tasks.emplace_back(std::async([=, &barrier]() {
        auto harness = NodeHarness::Create(self, others, node, *iter, experiment_params, &barrier);
        return NodeHarness::Run(std::move(harness), experiment_params, &done);
      }));
    }
  }

  // number of node_tasks should equal num_nodes * num_threads
  std::for_each(node_tasks.begin(), node_tasks.end(),
                [](auto& result) { result.wait(); });

  std::vector<ResultProto> result_protos;
  for (auto& r : node_tasks) {
    auto result_or = r.get();
    if (!result_or.ok()) {
      ROME_ERROR("{}", result_or.status().message());
    } else {
      result_protos.push_back(result_or.value());
    }
  }
  RecordResults(experiment_params, result_protos);
  
  ROME_INFO("Done");
  return 0;
}