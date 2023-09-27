#pragma once

#include <cstdint>
#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/common.h"
#include "rome/rdma/memory_pool/remote_ptr.h"

namespace X {

// Class that determines which node a given key is on based on the range it falls into
template <typename K, typename V>
class LockTable {
 using key_type = K; // some int (uint16)
 using lock_type = V; // ALock or MCSDescriptor or 
 using MemoryPool = rome::rdma::MemoryPool;
 using root_type = rome::rdma::remote_ptr<lock_type>;
//  using root_map = std::map<uint32_t, root_type>;
//  using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;

 public:
  LockTable(const NodeProto& node, MemoryPool &lock_pool)
    :   self_(node),
        node_id_(node.nid()),
        lock_pool_(lock_pool) {
          min_key_ = self_.range().low();
          max_key_= self_.range().high();
  }

  root_type AllocateLocks(const key_type& min_key, const key_type& max_key){
    auto lock = lock_pool_.Allocate<lock_type>();
    root_lock_ptr_ = lock;
    ROME_TRACE("Allocated root lock for key {}", static_cast<key_type>(min_key));
    // Allocate one lock per key
    for (auto i = min_key + 1; i <= max_key; i++){
        lock_pool_.Allocate<lock_type>();
        ROME_TRACE("Allocated lock for key {}", i);
    }
    ROME_DEBUG("Root Ptr on Node {} is {:x}", node_id_, static_cast<uint64_t>(root_lock_ptr_));
    // return pointer to first lock in table
    return root_lock_ptr_;
  }

 private:
  const NodeProto& self_;
  const uint32_t node_id_;
  MemoryPool &lock_pool_;
  root_type root_lock_ptr_;
  key_type max_key_;
  key_type min_key_;
};

}  // namespace X
