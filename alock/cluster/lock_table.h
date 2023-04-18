#pragma once

#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/common.h"

namespace X {

// Class that determines which node a given key is on based on the range it falls into
template <typename K, typename V>
class LockTable<K, V> {
 using key_type = K; // some int (uint16)
 using lock_type = V; // ALock
 using MemoryPool = rome::rdma::MemoryPool;
 using root_type = remote_ptr<lock_type>;

 public:
  LockTable<K, V>::LockTable(const NodeProto& node, MemoryPool &lock_pool)
    : self_(node),
      lock_pool_(lock_pool) {
        min_key_ = n.range().low()
        max_key_= n.range().high();
        lock_byte_size_ = sizeof(lock_type);
    }

  root_type LockTable<K, V>::AllocateLocks(const key_type& min_key, const key_type& max_key){
    auto lock = lock_pool_.Allocate<lock_type>();
    root_lock_ptr_ = lock;
    auto num_locks = max_key - min_key + 1;
    // Allocate one lock per key
    for (auto i = min_key_ + 1; i <= max_key_; i++){
        auto lock = lock_pool_.Allocate<lock_type>();
    }
    // return pointer to first lock in table
    return root_lock_ptr_;
  }

  root_type LockTable<K, V>::GetLock(const key_type& key) {
    if (key > max_key_ || key < min_key_){
        ROME_DEBUG("Attempting to get lock from from incorrect LockTable.");
        return remote_nullptr;
    } else if (key > min_key_){
        // calculate the address of the desired key
        auto diff = key - min_key_;
        auto bytes_to_jump = lock_byte_size * diff;
        auto lock_ptr = root_lock_ptr + bytes_to_jump;
        return lock_ptr;
    }
    return root_lock_ptr_; 
  }

 private:
  NodeProto& self_;
  MemoryPool &lock_pool_;
  root_type root_lock_ptr_;
  key_type max_key_;
  key_type min_key_;
  uint64_t lock_byte_size_;
};

}  // namespace X
