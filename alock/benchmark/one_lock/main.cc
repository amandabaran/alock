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
#include "alock/cluster/node.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/metrics/summary.h"
#include "rome/rdma/rdma_broker.h"
#include "rome/util/clocks.h"
#include "rome/util/proto_util.h"

#include "node_harness.h"
#include "setup.h"

using ::X::ClusterProto;
using ::util::SystemClock;

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

  if (!experiment_params.workload().has_runtime() ||
      experiment_params.workload().runtime() < 0) {
    signal_handler_internal = std::function([](int signum) {
      if (done) exit(1);
      ROME_INFO("Shutting down...");
      done = true;
    });
    signal(SIGINT, signal_handler);
  }

  std::vector<std::future<absl::StatusOr<ResultProto>>> nodes;
  std::vector<std::unique_ptr<NodeHarness>> harnesses;
  std::barrier<> barrier(num_nodes);
  for (const auto& n : node_ids) {
    auto iter = cluster.nodes().begin();
    // TODO: IS THE BELOW LINE NEEDED ANYMORE ?
    while (iter != cluster.nodes().end() && iter->nid() != n) ++iter;
    ROME_ASSERT(iter != cluster.nodes().end(), "Failed to find node: {}", n);
    
    auto node = std::make_unique<X::Node<X::key_type, X::value_type>>(
        *iter, cluster, experiment_params.num_threads(), experiment_params.prefill());
    auto harness = NodeHarness::Create(std::move(node), *iter,
                                         experiment_params);
    auto status = harness->Launch(&done, experiment_params);
    ROME_ASSERT_OK(status);

  }

  std::for_each(nodes.begin(), nodes.end(),
                [](auto& result) { result.wait(); });

  std::vector<ResultProto> result_protos;
  for (auto& r : nodes) {
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