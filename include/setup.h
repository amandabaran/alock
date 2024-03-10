#pragma once

#include <infiniband/verbs.h>

#include <memory>
#include <random>

#include "locks/rdma_mcs_lock.h"
#include "locks/rdma_spin_lock.h"
#include "locks/a_lock.h"
#include "common.h"
#include "experiment.h"

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
// ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

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

using LockTable = LockTable<key_type, LockType>;
using root_type = rdma_ptr<LockType>;
using root_map = std::map<uint32_t, root_type>;
using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;
using Operation = key_type;

uint64_t lock_byte_size_ = sizeof(LockType);
ROME_ASSERT(lock_byte_size_ == CACHELINE_SIZE);

// struct LockOp {
//   key_type key; 
//   std::chrono::nanoseconds think;
// };

auto CalcThreadKeyRange(BenchmarkParams &params, Peer self){
  int total_keys = params.max_key - params.min_key + 1;
  int keys_per_client = total_keys / params.thread_count;
  int keys_per_node = total_keys / params.node_count;


  // return pair of key range to be populated by thread
  auto result = std::make_pair(low, high);
}

auto CalcLocalNodeRange(BenchmarkParams &params, Peer self){
  int total_keys = params.max_key - params.min_key + 1;
  int keys_per_client = total_keys / params.thread_count;
  int keys_per_node = total_keys / params.node_count;
  // return pair of node's entire local key range
  auto result = std::make_pair(low, high);

}

//Using this one since it seems to perform equally to opstream3, and is more trusted
auto CreateOpStream(BenchmarkParams &params, Peer self){
  auto num_keys = 10e6; //10M

  auto local_start, local_end = CalcLocalNodeRange(params, self);
  int local_range = local_end - local_start + 1;

  int min_key = params.min_key;
  int max_key = params.max_key;
  int full_range = max_key - min_key + 1;

  auto p_local = params.p_local * 100; //change to represent a percentage

  std::vector<key_type> keys;
  keys.reserve(num_keys); //reserve room for 5M keys

  std::mt19937 gen;
  std::uniform_int_distribution<> dist(1, max_key);

  for (auto i = 0; i < num_keys; i++){
    volatile int random = dist(gen);
    ROME_DEBUG("random % 100 is {}", random % 100 + 1);
    auto random_p = (random % 100) + 1;
    if ( p_local == 1.0 || random_p <= p_local ){
      keys.push_back(static_cast<key_type>((random % local_range) + local_start));
      ROME_DEBUG("local key");
    } else {
      //scale random float to number in full key range
      key_type key = (random % full_range) + min_key;
      ROME_DEBUG("remote key");
      //retry until key is remote (i.e. not in local)
      while (key <= local_end && key >= local_start){
        random = dist(gen);
        ROME_DEBUG("random % range is {}", random % full_range);
        key = (random % full_range) + min_key;
      }
      keys.push_back(key);
    }
  }
  ROME_ASSERT(keys.size() == num_keys, "Error generating vector for prefilled stream");

  // creates a stream such that the random numbers are already generated, and are popped from vector for each operation
  return std::make_unique<rome::PrefilledStream<key_type>>(keys, num_keys);

}

void RecordResults(BenchmarkParams &params,
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