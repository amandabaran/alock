#pragma once

#include <barrier>
#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <cmath>

#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/clocks.h"

namespace X {

inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }


template <typename K, typename V>
Node<K, V>::~Node() = default;

template <typename K, typename V>
Node<K, V>::Node(const NodeProto& self, std::vector<MemoryPool::Peer> others, const ClusterProto& cluster, const ExperimentParams& params)
    : self_(self),
      others_(others),
      cluster_(cluster),
      params_(params),
      prefill_(params.prefill()),
      lock_pool_(MemoryPool::Peer(self.nid(), self.name(), self.port()), std::make_unique<MemoryPool::cm_type>(self.nid())),
      lock_table_(self, lock_pool_) {}

template <typename K, typename V>
absl::Status Node<K,V>::Connect(){
  //calculation to determine lock pool size
  uint32_t bytesNeeded = ((64 * params_.workload().max_key()) + (64 * 5 * params_.num_threads()));
  uint32_t lockPoolSize = 1 << uint32_t(ceil(log2(bytesNeeded)));
  ROME_DEBUG("Init MemoryPool for locks size {}, bytes {}", lockPoolSize, bytesNeeded);
  ROME_ASSERT_OK(lock_pool_.Init(lockPoolSize, others_));

  ROME_ASSERT_OK(Prefill(self_.range().low(), self_.range().high()));

  ROME_ASSERT_OK(FillKeyRangeMap());

  RemoteObjectProto proto;
  proto.set_raddr(root_lock_ptr_.address());

  ROME_DEBUG("Root Lock pointer {:x}", static_cast<uint64_t>(root_lock_ptr_));

  // tell all the peers where to find the addr of the first lock on this node
  for (auto p : others_) {
    // Send all peers the root of the lock on self
    auto conn_or = lock_pool_.connection_manager()->GetConnection(p.id);
    ROME_ASSERT_OK(conn_or.status());
    auto status = conn_or.value()->channel()->Send(proto);
    ROME_ASSERT_OK(status);
    ROME_DEBUG("Node {} sent lock pointer to node {}", self_.nid(), p.id);
  }

  // Wait until roots of all other alocks on other nodes are shared
  for (auto p : others_) {
    auto conn_or = lock_pool_.connection_manager()->GetConnection(p.id);
    ROME_ASSERT_OK(conn_or.status());
    auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    while (got.status().code() == absl::StatusCode::kUnavailable) {
      got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    }
    ROME_ASSERT_OK(got.status());
    // set lock pointer to the base address of the lock on the host
    auto root = decltype(root_lock_ptr_)(p.id, got->raddr());
    ROME_DEBUG("Node {} Lock pointer {:x}", p.id, static_cast<uint64_t>(root));

    root_ptrs_.emplace(p.id, root);
  }
  std::atomic_thread_fence(std::memory_order_release);
  
  return absl::OkStatus();
}


template <typename K, typename V>
absl::Status Node<K, V>::Prefill(const key_type& min_key,
                                   const key_type& max_key) {
  if (prefill_){
    ROME_DEBUG("Prefilling lock table... [{}, {}]", min_key, max_key);
    root_lock_ptr_ = lock_table_.AllocateLocks(min_key, max_key);
  } else {
    ROME_DEBUG("Prefilling set to false, one lock per lock table");
    root_lock_ptr_ = lock_table_.AllocateLocks(min_key, min_key);
  }
  root_ptrs_.emplace(self_.nid(), root_lock_ptr_);
  return absl::OkStatus();
}

template <typename K, typename V>
absl::Status Node<K, V>::FillKeyRangeMap(){
  auto node_list = cluster_.nodes();
  for (auto node : node_list){
    auto key_range = node.range();
    key_range_map_.emplace(node.nid(), std::make_pair(key_range.low(), key_range.high()));
  }
  return absl::OkStatus();
}

} //namespace X