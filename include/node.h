#pragma once

#include <cstdint>
#include <string_view>

#include <rome/rdma/rdma.h>
#include "common.h"
#include "lock_table.h"
#include "experiment.h"

template <typename K, typename V>
class Node {
  using key_type = K; // some int (uint16)
  using lock_type = V; // ALock
  using MemoryPool = rome::rdma::MemoryPool;
  using root_type = rdma_ptr<lock_type>;
  using root_map = std::map<uint32_t, root_type>;
  using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;
  
 public:
  ~Node();
  Node(Peer self, std::vector<Peer> peers, std::shared_ptr<rdma_capability> pool, const ClusterProto& cluster, BenchmarkParams &params)
    : self_(self),
      peers_(peers),
      pool_(),
      cluster_(cluster),
      params_(params),
      lock_pool_(MemoryPool::Peer(self.nid(), self.name(), self.port()), std::make_unique<MemoryPool::cm_type>(self.nid())),
      lock_table_(self, lock_pool_) {}
  
  absl::Status Connect(){
    // TODO: create key range for "node" (aka thread since we need a pool per thread rn)
    int min_key = 
    int max_key = 
    ROME_ASSERT_OK(Prefill(self_.range().low(), self_.range().high()));

    ROME_ASSERT_OK(FillKeyRangeMap());

    RemoteObjectProto proto;
    proto.set_raddr(root_lock_ptr_.address());

    ROME_DEBUG("Root Lock pointer {:x}", static_cast<uint64_t>(root_lock_ptr_));

    // tell all the peers where to find the addr of the first lock on this node
    for (auto p : peers_) {
      // Send all peers the root of the lock on self
      auto status = pool_.Send(p, proto);
      ROME_ASSERT_OK(status);
      ROME_DEBUG("Node {} sent lock pointer to node {}", self_.nid(), p.id);
    }

    // Wait until roots of all other alocks on other nodes are shared
    for (auto p : peers_) {
      // Get root lock pointer from peer p
      auto got = pool_.Recv(p);
      ROME_ASSERT_OK(got.status());
      // set lock pointer to the base address of the lock on the host
      auto root = decltype(root_lock_ptr_)(p.id, got->raddr());
      ROME_DEBUG("Node {} Lock pointer {:x}", p.id, static_cast<uint64_t>(root));

      root_ptrs_.emplace(p.id, root);
    }
    std::atomic_thread_fence(std::memory_order_release);
    
    return rome::util::Status::Ok();
  }

  absl::Status Prefill(const key_type& min_key, const key_type& max_key) {
    ROME_DEBUG("Prefilling lock table... [{}, {}]", min_key, max_key);
    root_lock_ptr_ = lock_table_.AllocateLocks(min_key, max_key);
    root_ptrs_.emplace(self_.nid(), root_lock_ptr_);
    return rome::util::Status::Ok();
  }

  absl::Status FillKeyRangeMap(){
    auto node_list = cluster_.nodes();
    for (auto node : node_list){
      auto key_range = node.range();
      key_range_map_.emplace(node.nid(), std::make_pair(key_range.low(), key_range.high()));
    }
    return rome::util::Status::Ok();
  }

  LockTable<K, V>* GetLockTable() { return &lock_table_; }

  key_map* GetKeyRangeMap() { return &key_range_map_; }

  root_map* GetRootPtrMap() { return &root_ptrs_; }

  MemoryPool* GetLockPool(){ return &pool_; }

 private:  
  const NodeProto np_self_;
  Peer self_;
  std::vector<MemoryPool::Peer> peers_;
  const ClusterProto cluster_;
  const ExperimentParams params_;

  std::shared_ptr<rdma_capability> pool_;
  LockTable<K,V> lock_table_;
  root_type root_lock_ptr_;
  key_map key_range_map_;
  root_map root_ptrs_;
};