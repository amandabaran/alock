#pragma once

#include <cstdint>
#include <string_view>

#include <remus/rdma/rdma.h>
#include "common.h"
#include "lock_table.h"
#include "experiment.h"

template <typename K, typename V>
class Node {
  using key_type = K; // some int (uint16)
  using lock_type = V; // ALock
  using root_type = rdma_ptr<lock_type>;
  using root_map = std::map<uint32_t, root_type>;
  // node, <low, high>
  using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;
  
 public:
  ~Node();
  Node(Peer self, std::vector<Peer> peers, std::shared_ptr<rdma_capability> pool, BenchmarkParams &params)
    : self_(self),
      peers_(peers),
      pool_(pool),
      params_(params),
      lock_table_(self, pool_) {}
  
  remus::util::Status Connect(){
    std::pair key_range = calcThreadKeyRange(params_, self_.id);
    REMUS_ASSERT_OK(Prefill(key_range.first, key_range.second));

    // REMUS_ASSERT_OK(FillKeyRangeMap());

    RemoteObjectProto proto;
    proto.set_raddr(root_lock_ptr_.address());

    REMUS_DEBUG("Root Lock pointer {:x}", static_cast<uint64_t>(root_lock_ptr_));

    // tell all the peers where to find the addr of the first lock on this node
    for (auto p : peers_) {
      // Send all peers the root of the lock on self
      auto status = pool_->Send<RemoteObjectProto>(p, proto);
      OK_OR_FAIL(status);
      REMUS_DEBUG("Node {} sent lock pointer to node {}", self_.id, p.id);
    }

    // Wait until roots of all other alocks on other nodes are shared
    for (auto p : peers_) {
      // Get root lock pointer from peer p
      auto got = pool_->Recv<RemoteObjectProto>(p);
        OK_OR_FAIL(got.status());
      // set lock pointer to the base address of the lock on the host
      auto root = decltype(root_lock_ptr_)(p.id, got.val().raddr());
      REMUS_DEBUG("Node {} Lock pointer {:x}", p.id, static_cast<uint64_t>(root));

      root_ptrs_.emplace(p.id, root);
    }
    std::atomic_thread_fence(std::memory_order_release);
    
    return remus::util::Status::Ok();
  }

  remus::util::Status Prefill(const key_type& min_key, const key_type& max_key) {
    REMUS_DEBUG("Prefilling lock table... [{}, {}]", min_key, max_key);
    root_lock_ptr_ = lock_table_.AllocateLocks(min_key, max_key);
    root_ptrs_.emplace(self_.id, root_lock_ptr_);
    return remus::util::Status::Ok();
  }

  // remus::util::Status FillKeyRangeMap(){
  //   for (int i = 1; i <= params_.node_count; i++)){
  //     key_range_map.at(i) = calcLocalNodeRange(params_, );
  //   }
  //   return remus::util::Status::Ok();
  // }

  LockTable<K, V>* GetLockTable() { return &lock_table_; }

  // key_map* GetKeyRangeMap() { return &key_range_map_; }

  root_map* GetRootPtrMap() { return &root_ptrs_; }

  std::shared_ptr<rdma_capability> GetLockPool(){ return pool_; }

 private:  
  Peer self_;
  std::vector<Peer> peers_;
  const BenchmarkParams &params_;

  std::shared_ptr<rdma_capability> pool_;
  LockTable<K,V> lock_table_;
  root_type root_lock_ptr_;
  // key_map key_range_map_;
  root_map root_ptrs_;
};