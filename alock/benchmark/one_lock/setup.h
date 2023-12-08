#pragma once

#include <infiniband/verbs.h>

#include <memory>

#include "alock/benchmark/one_lock/experiment.pb.h"
#include "alock/src/locks/rdma_mcs_lock/rdma_mcs_lock.h"
#include "alock/src/locks/rdma_spin_lock/rdma_spin_lock.h"
#include "alock/src/locks/a_lock/a_lock_handle.h"
#include "alock/src/locks/spin_lock/spin_lock.h"
#include "alock/util.h"

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

auto CreateOpStream3(const ExperimentParams& params, const X::NodeProto& node){
  #define NUM_KEYS 5e6
  int local_start = node.local_range().low();
  int local_end = node.local_range().high();
  int local_range = local_end - local_start + 1;
  int min_key = params.workload().min_key();
  int max_key = params.workload().max_key();
  int full_range = max_key - min_key + 1;
  auto p_local = params.workload().p_local() * 100; //change to represent a percentage

  XorShift64 rng(std::chrono::system_clock::now().time_since_epoch().count());

  std::vector<key_type> keys;
  keys.reserve(NUM_KEYS); //reserve room for 5M keys

  for (auto i = 0; i < NUM_KEYS; i++){
    XorShift64 rng(std::chrono::system_clock::now().time_since_epoch().count());
    volatile uint64_t random = rng.next();
    ROME_DEBUG("p_Local is {}", p_local);
    if ( p_local == 1.0 || (random % 100) <= p_local ){
      // scale random float to a number within local key range
      ROME_DEBUG("random % local is {}", (random % local_range) + local_start);
      keys.push_back((random % local_range) + local_start);
    } else {
      //scale random float to number in full key range
      key_type key = (random % full_range) + min_key;
      ROME_DEBUG("remote key");
      //retry until key is remote (i.e. not in local)
      while (key <= local_end && key >= local_start){
        random = rng.next();
        ROME_DEBUG("random % range is {}", random % full_range);
        key = (random % full_range) + min_key;
      }
      keys.push_back(key);
    }
  }
  ROME_ASSERT(keys.size() == NUM_KEYS, "Error generating vector for prefilled stream");

  // creates a stream such that the random numbers are already generated, and are popped from vector for each operation
  return std::make_unique<rome::PrefilledStream<key_type>>(keys, NUM_KEYS);

}

auto CreateOpStream2(const ExperimentParams& params, const X::NodeProto& node){
  int local_start = node.local_range().low();
  int local_end = node.local_range().high();
  int local_range = local_end - local_start + 1;
  int min_key = params.workload().min_key();
  int max_key = params.workload().max_key();
  int full_range = max_key - min_key + 1;
  auto p_local = params.workload().p_local() * 100; //change to represent a percentage

  //apply this to determine a key for each op
  std::function<key_type(void)> generator = [=](){
    // XorShift64 rng(std::chrono::system_clock::now().time_since_epoch().count());
    std::mt19937 gen;
    std::unfirm
    volatile uint64_t random = rng.next();
    ROME_DEBUG("random % 100 is {}", random % 100 + 1);
    if ( p_local == 1.0 || (random % 100 + 1) <= p_local ){
      return (random % local_range) + local_start;
      ROME_DEBUG("local key");
    } else {
      //scale random float to number in full key range
      key_type key = (random % full_range) + min_key;
      ROME_DEBUG("remote key");
      //retry until key is remote (i.e. not in local)
      while (key <= local_end && key >= local_start){
        random = rng.next();
        ROME_DEBUG("random % range is {}", random % full_range);
        key = (random % full_range) + min_key;
      }
      return key;
    }
  };

  return std::make_unique<rome::EndlessStream<key_type>>(generator);  
}

auto CreateOpStream(const ExperimentParams& params, const X::NodeProto& node){
  int local_start = node.local_range().low();
  int local_end = node.local_range().high();
  int min_key = params.workload().min_key();
  int max_key = params.workload().max_key();
  
  //apply this to determine a key for each op
  std::function<key_type(void)> generator = [=](){
    // Determine whether to use the local range 
    key_type key;
    std::mt19937 gen;
    std::uniform_int_distribution<key_type> local_dist(local_start, local_end);
    std::uniform_int_distribution<key_type> full_dist(min_key, max_key);
    std::bernoulli_distribution bernoulliDist(params.workload().p_local());
    if (params.workload().p_local() == 1.0){
      key = local_dist(gen);
      return key;
    } 
    bool useLocalRange = bernoulliDist(gen);
    if (useLocalRange) {
      key = local_dist(gen);
    } else {
      key = full_dist(gen);
      while (key <= local_end && key >= local_start){
        key = full_dist(gen);
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