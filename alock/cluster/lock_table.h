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

 public:
  LockTable(const NodeProto& node, MemoryPool &lock_pool)
    : self_(node),
      lock_pool_(lock_pool) {
        min_key_ = self_.range().low();
        max_key_= self_.range().high();
        lock_byte_size_ = sizeof(lock_type);
    }

  root_type AllocateLocks(const key_type& min_key, const key_type& max_key){
    auto lock = lock_pool_.Allocate<lock_type>();
    root_lock_ptr_ = lock;
    // Allocate one lock per key
    for (auto i = min_key_ + 1; i <= max_key_; i++){
        lock_pool_.Allocate<lock_type>();
    }
    // return pointer to first lock in table
    return root_lock_ptr_;
  }

  root_type GetLock() {
    return root_lock_ptr_; 
  }

  root_type GetLock(const key_type& key) {
    if (key > max_key_ || key < min_key_){
        ROME_DEBUG("Attempting to get lock from from incorrect LockTable.");
        // TODO : return a more meaningful error
        return rome::rdma::remote_nullptr;
    } else if (key > min_key_){
        // calculate the address of the desired key
        auto diff = key - min_key_;
        auto bytes_to_jump = lock_byte_size_ * diff;
        auto temp_ptr = rome::rdma::remote_ptr<uint8_t>(root_lock_ptr_);
        temp_ptr += bytes_to_jump;
        auto lock_ptr = root_type(temp_ptr);
        return lock_ptr;
    }
    return root_lock_ptr_; 
  }

 private:
  const NodeProto& self_;
  MemoryPool &lock_pool_;
  root_type root_lock_ptr_;
  key_type max_key_;
  key_type min_key_;
  uint64_t lock_byte_size_;
};

}  // namespace X
