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

// THIS IS BEING LAUNCHED ON EACH NODE!!! SO, IT SHOULDN'T LOOP THROUGH ALL THE NODEIDS
int main(int argc, char *argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto cluster = absl::GetFlag(FLAGS_cluster);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(experiment_params));

  auto node_ids = experiment_params.node_ids(); 
  // Only using one nodeid per physical node:
  auto node_id = node_ids[0];
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

  auto iter = cluster.nodes().begin();
  while (iter != cluster.nodes().end() && iter->nid() != node_id) ++iter;
  ROME_ASSERT(iter != cluster.nodes().end(), "Failed to find node: {}", node_id);
  
  ROME_DEBUG("Creating node {}", node_id);
  auto node = std::make_unique<X::Node<key_type, LockType>>(*iter, cluster, experiment_params.prefill());
  ROME_ASSERT_OK(node->Connect());

  // Create a vector of "peers" in the system not including yourself
  Peer self = MemoryPool::Peer(iter->nid(), iter->name(), iter->port());
  std::vector<Peer> others;
  std::copy_if(peers.begin(), peers.end(), std::back_inserter(others),
                [self](auto &p) { return p.id != self.id; });

  auto harness = NodeHarness::Create(self, others, std::move(node), *iter, experiment_params);
  auto status = harness->Launch(&done, experiment_params);
  ROME_ASSERT_OK(status);
  
  if (num_threads > 0) {
      RecordResults(experiment_params, harness->GetResults());
  }
  
  ROME_INFO("Done");
  return 0;
}