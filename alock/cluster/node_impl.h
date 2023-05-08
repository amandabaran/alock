#pragma once

#include <barrier>
#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/clocks.h"

namespace X {

inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }


template <typename K, typename V>
Node<K, V>::~Node(){
  //Disconnect ? shutdown?
}

template <typename K, typename V>
Node<K, V>::Node(const NodeProto& self, const ClusterProto& cluster, bool prefill)
    : self_(self),
      cluster_(cluster),
      prefill_(prefill),
      lock_pool_(MemoryPool::Peer(self.nid(), self.name(), self.port()), std::make_unique<MemoryPool::cm_type>(self.nid())),
      lock_table_(self, lock_pool_) {}

template <typename K, typename V>
absl::Status Node<K,V>::Connect(){
  std::vector<MemoryPool::Peer> peers;
  peers.reserve(cluster_.nodes_size());
  for (auto n : cluster_.nodes()){
    if (n.nid() == self_.nid()) { continue; }
    auto peer = MemoryPool::Peer(n.nid(), n.name(), n.port());
  }
  
  ROME_DEBUG("Init MemoryPool for locks");
  ROME_ASSERT_OK(lock_pool_.Init(kLockPoolSize, peers));

  ROME_ASSERT_OK(Prefill(self_.range().low(), self_.range().high()));

  RemoteObjectProto proto;
  proto.set_raddr(root_lock_ptr_.address());

  ROME_DEBUG("Root Lock pointer {:x}", static_cast<uint64_t>(root_lock_ptr_));

  // tell all the peers where to find the addr of the first lock on this node
  for (auto p : peers) {
    // Send all peers the root of the lock on self
    auto conn_or = lock_pool_.connection_manager()->GetConnection(p.id);
    ROME_ASSERT_OK(conn_or.status());
    auto status = conn_or.value()->channel()->Send(proto);
    ROME_ASSERT_OK(status);
  }

  // Wait until roots of all other alocks on other nodes are shared
  for (auto p : peers) {
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

  return absl::OkStatus();
}


template <typename K, typename V>
absl::Status Node<K, V>::Prefill(const key_type& min_key,
                                   const key_type& max_key) {
  ROME_INFO("Prefilling lock table... [{}, {})", min_key, max_key);
  if (prefill_){
    root_lock_ptr_ = lock_table_.AllocateLocks(min_key, max_key);
  } else {
    root_lock_ptr_ = lock_table_.AllocateLocks(0, 0);
  }
  ROME_INFO("Finished prefilling lock table");

  return absl::OkStatus();
}

} //namespace X