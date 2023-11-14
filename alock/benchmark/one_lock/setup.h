#pragma once

#include <infiniband/verbs.h>

#include <memory>

#include "alock/benchmark/one_lock/experiment.pb.h"
#include "alock/locks/rdma_mcs_lock/rdma_mcs_lock.h"
#include "alock/locks/rdma_spin_lock/rdma_spin_lock.h"
#include "alock/locks/a_lock/a_lock_handle.h"

#include "absl/status/status.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/logging/logging.h"
#include "rome/metrics/counter.h"
#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/status_util.h"

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
// ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

using ::rome::ClientAdaptor;
using ::rome::Stream;
using ::rome::metrics::Counter;
using ::rome::rdma::RemoteObjectProto;
using Peer = ::rome::rdma::MemoryPool::Peer;
using cm_type = ::rome::rdma::MemoryPool::cm_type;
using conn_type = ::rome::rdma::MemoryPool::conn_type;

using key_type = uint64_t;

#ifdef LOCK_HANDLE
using LockHandle = LOCK_HANDLE;
#else
#error "LOCK_HANDLE is undefined"
#endif

#ifdef LOCK_TYPE
using LockType = LOCK_TYPE;
#else
#error "LOCK_TYPE is undefined"
#endif

using LockTable = X::LockTable<key_type, LockType>;
using root_type = X::remote_ptr<LockType>;
using root_map = std::map<uint32_t, root_type>;
using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;
using Operation = key_type;

// uint64_t lock_byte_size_ = sizeof(LockType);
uint64_t lock_byte_size_ = CACHELINE_SIZE;

// struct LockOp {
//   key_type key; 
//   std::chrono::nanoseconds think;
// };

static constexpr uint16_t kServerPort = 18000;
static constexpr uint16_t kBaseClientPort = 18001;

void PopulateDefaultValues(ExperimentParams* params) {
  if (!params->workload().has_min_key())
    params->mutable_workload()->set_min_key(0);
  if (!params->workload().has_max_key())
    params->mutable_workload()->set_max_key(1000000);
  if (!params->workload().has_theta())
    params->mutable_workload()->set_theta(0.99);
  if (!params->has_num_threads()) params->set_num_threads(1);
  if (!params->has_sampling_rate_ms())
    params->set_sampling_rate_ms(10);
}

absl::Status ValidateExperimentParams(const ExperimentParams& params) {
  if (!params.has_workload()) {
    return util::InvalidArgumentErrorBuilder()
           << "No workload: " << params.DebugString();
  }
  if (params.client_ids_size() != params.num_threads()) {
    return util::InvalidArgumentErrorBuilder()
            << "Number of threads does not match node node_ids: " << params.DebugString();
  } 
  return absl::OkStatus();
}

auto CreateOpStream(const ExperimentParams& params, const X::NodeProto& node){
  std::uniform_real_distribution<double> dist = std::uniform_real_distribution<double>(0.0, 1.0);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::bernoulli_distribution bernoulliDist(params.workload().p_local());

  int local_start = node.local_range().low();
  int local_end = node.local_range().high();
  int local_range = local_end - local_start + 1;

  int min_key = params.workload().min_key();
  int max_key = params.workload().max_key();
  int key_range = max_key - min_key + 1;

  //apply this to determine a key for each op
  std::function<key_type(void)> generator = [&](){
    // Determine whether to use the local range 
    bool useLocalRange = bernoulliDist(gen);
    key_type key;
    if (useLocalRange){
      key = dist(gen) * local_range + local_start;
    } else {
      key = dist(gen) * key_range + min_key;
      while (key <= local_end && key >= local_start){
        key = dist(gen) * key_range + min_key;
      }
    }
    return key;
  };

  return std::make_unique<rome::EndlessStream<key_type>>(generator);            
}

auto CreateOpStream(const ExperimentParams& params) {
  return std::make_unique<rome::RandomDistributionStream<
      std::uniform_int_distribution<key_type>, key_type, key_type>>(
      params.workload().min_key(), params.workload().max_key());
}

void RecordResults(const ExperimentParams &experiment_params,
                          const std::vector<ResultProto> &experiment_results) {                      
  ResultsProto results;
  results.mutable_experiment_params()->CopyFrom(experiment_params);
  results.set_cluster_size(experiment_params.num_nodes());
  for (auto &result : experiment_results) {
    auto *r = results.add_results();
    r->CopyFrom(result);
  }

  if (experiment_params.has_save_dir()) {
    auto save_dir = experiment_params.save_dir();
    ROME_ASSERT(!save_dir.empty(),
                "Trying to write results to a file, but results "
                "directory is empty string");
    if (!std::filesystem::exists(save_dir) &&
        !std::filesystem::create_directories(save_dir)) {
      ROME_FATAL("Failed to create save directory. Exiting...");
    }

    auto filestream = std::ofstream();
    std::filesystem::path outfile;
    outfile /= save_dir;
    std::string filename =
        "client."+ (experiment_params.has_name() ? experiment_params.name() : "data") + ".pbtxt";
    outfile /= filename;
    filestream.open(outfile);
    ROME_ASSERT(filestream.is_open(), "Failed to open output file: {}",
                outfile.c_str());
    ROME_INFO("Saving results to '{}'", outfile.c_str());
    filestream << results.DebugString();
    filestream.flush();
    filestream.close();
  } else {
    std::cout << results.DebugString();
  }
}