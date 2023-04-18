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
#include "node.h"

namespace X {

inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }


template <typename K, typename V>
Node<K, V>::~Node(){
  //Disconnect ? shutdown?
}

template <typename K, typename V>
Node<K, V>::Node(const NodeProto& self, const ClusterProto& cluster, uint32_t num_threads, bool prefill)
    : self_(self),
      cluster_(cluster),
      num_threads_(num_threads_) {
  auto nid = self.node().nid();
  auto name = self.node().name();
  auto port = self.node().port();

  auto peer = MemoryPool::Peer(nid, name, port);
  auto cm = std::make_unique<MemoryPool::cm_type>(peer.id);
  lock_pool_ = std::make_unique<MemoryPool>(peer, std::move(cm));
  lock_table_ = std::make_unique<LockTable>(self, &lock_pool_);

  std::vector<MemoryPool::Peer> peers;
  peers.reserve(cluster_.nodes_size())
  for (auto n : cluster_.nodes()){
    if (n.node().nid() == nid) { continue; }
    auto peer = MemoryPool::Peer(n.node().nid(), n.node().name(), n.node().port());
  }

  // Init `MemoryPool` for locks
  ROME_ASSERT_OK(lock_pool_->Init(kPoolSize, peers));

  if (prefill){
    ROME_ASSERT_OK(Prefill(self_.range().low(), self_.range().high()));
  } else {
    auto root_lock_ptr_ = lock_pool_.Allocate<lock_type>();
  }
  RemoteObjectProto proto;
  proto.set_raddr(root_lock_ptr_.address());

  ROME_DEBUG("Root Lock pointer {:x}", static_cast<uint64_t>(root_lock_ptr_));

  // tell all the peers where to find the addr of the first lock on this node
  for (auto p : peers) {
    // Send all peers the root of the lock on self
    auto conn_or = lock_pool_.connection_manager()->GetConnection(p.id);
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    status = conn_or.value()->channel()->Send(proto);
    ROME_CHECK_OK(ROME_RETURN(status), status);
  }

  // Wait until roots of all other alocks on other nodes are shared
  for (auto p : peers) {
    auto conn_or = lock_pool_->connection_manager()->GetConnection(p.id);
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    while (got.status().code() == absl::StatusCode::kUnavailable) {
      got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    }
    ROME_CHECK_OK(ROME_RETURN(got.status()), got);
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
 
  root_lock_ptr_ = lock_table_.AllocateLocks(min_key, max_key);

  ROME_INFO("Finished prefilling lock table");

  return absl::OkStatus();
}

} //namespace X