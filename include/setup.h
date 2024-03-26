#pragma once

#include <infiniband/verbs.h>

#include <memory>
#include <random>

#include "locks/rdma_mcs_lock.h"
#include "locks/rdma_spin_lock.h"
#include "locks/a_lock.h"
#include "common.h"
#include "experiment.h"
#include "lock_table.h"

#include "remus/workload/workload_driver.h"


#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
// ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

#if defined(LOCK_TYPE) && defined(LOCK_HANDLE)
using LockType = LOCK_TYPE;
using LockHandle = LOCK_HANDLE;
#else
#error "LOCK_TYPE is undefined"
#endif

using key_type = uint64_t;
using root_type = rdma_ptr<LockType>;
using root_map = std::vector<root_type>;
// <low, high>, vector position = node
using key_map = std::vector<std::pair<key_type, key_type>>;
using Operation = key_type;

constexpr uint64_t lock_byte_size_ = sizeof(LockType);

//Using this one since it seems to perform equally to opstream3, and is more trusted
auto createOpStream(const BenchmarkParams params, Peer self){
  auto num_keys = params.op_count; 

  auto pair  = calcLocalNodeRange(params, self.id);
  auto local_start = pair.first;
  auto local_end = pair.second;
  int local_range = local_end - local_start + 1;

  int min_key = params.min_key;
  int max_key = params.max_key;
  int full_range = max_key - min_key + 1;

  auto p_local = params.p_local;

  std::vector<key_type> keys;
  keys.reserve(num_keys); //reserve room for 5M keys

  std::mt19937 gen;
  std::uniform_int_distribution<> dist(min_key, max_key);

  for (auto i = 0; i < num_keys; i++){
    volatile int random = dist(gen);
    REMUS_DEBUG("random % 100 is {}", random % 100 + 1);
    auto random_p = (random % 100) + 1;
    if ( p_local == 1.0 || random_p <= p_local ){
      keys.push_back(static_cast<key_type>((random % local_range) + local_start));
      REMUS_DEBUG("local key");
    } else {
      //scale random float to number in full key range
      key_type key = (random % full_range) + min_key;
      REMUS_DEBUG("remote key");
      //retry until key is remote (i.e. not in local)
      while (key <= local_end && key >= local_start){
        random = dist(gen);
        REMUS_DEBUG("random % range is {}", random % full_range);
        key = (random % full_range) + min_key;
      }
      keys.push_back(key);
    }
  }
  REMUS_ASSERT(keys.size() == num_keys, "Error generating vector for prefilled stream");

  // creates a stream such that the random numbers are already generated, and are popped from vector for each operation
  return std::make_unique<remus::PrefilledStream<key_type>>(keys, num_keys);

}

createRandomOpStream(const BenchmarkParams params, Peer self){
  auto num_keys = params.op_count; 
  auto pair  = calcLocalNodeRange(params, self.id);
  auto local_start = pair.first;
  auto local_end = pair.second;
  int local_range = local_end - local_start + 1;

  int min_key = params.min_key;
  int max_key = params.max_key;
  int full_range = max_key - min_key + 1;

  auto p_local = params.p_local;

  std::vector<key_type> keys;
  keys.reserve(num_keys); //reserve room for 5M keys

  std::mt19937 gen;
  std::uniform_int_distribution<> dist(min_key, max_key);

  for (auto i = 0; i < num_keys; i++){
    volatile int random = dist(gen);
    key_type key = (random % full_range) + min_key;
    keys.push_back(key);
  }
  REMUS_ASSERT(keys.size() == num_keys, "Error generating vector for prefilled stream");

  // creates a stream such that the random numbers are already generated, and are popped from vector for each operation
  return std::make_unique<remus::PrefilledStream<key_type>>(keys, num_keys);

}