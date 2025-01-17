#pragma once

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "rome/metrics/summary.h"
#include "../../../util.h"

// Uses MCS algorithm from page 10 of: "Algorithms for Scalable Synchronization on Shared-Memory Multiprocessor" 
// by John Mellor-Crummey and Michael L. Scott https://www.cs.rochester.edu/u/scott/papers/1991_TOCS_synch.pdf 

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

#define NEXT_PTR_OFFSET 32
#define UNLOCKED 0
#define LOCKED 1

struct alignas(64) RdmaMcsLock          {
  uint8_t locked{0};
  uint8_t pad1[NEXT_PTR_OFFSET - sizeof(locked)];
  remote_ptr<RdmaMcsLock> next{0};
  uint8_t pad2[CACHELINE_SIZE - NEXT_PTR_OFFSET - sizeof(uintptr_t)];
};
static_assert(alignof(RdmaMcsLock) == 64);
static_assert(sizeof(RdmaMcsLock) == 64);

class RdmaMcsLockHandle {
public: 
  RdmaMcsLockHandle(MemoryPool::Peer self, MemoryPool &pool, std::unordered_set<int> local_clients, int64_t local_budget, int64_t remote_budget)
      : self_(self), pool_(pool), local_clients_(local_clients), lock_count_(0) {}

  absl::Status Init() {    
    // Reserve remote memory for the local descriptor.
    desc_pointer_ = pool_.Allocate<RdmaMcsLock>();
    descriptor_ = reinterpret_cast<RdmaMcsLock *>(desc_pointer_.address());
    ROME_DEBUG("RdmaMcsLock @ {:x}", static_cast<uint64_t>(desc_pointer_));
    //Used as preallocated memory for RDMA writes
    prealloc_ = pool_.Allocate<remote_ptr<RdmaMcsLock>>();

    std::atomic_thread_fence(std::memory_order_release);
    return absl::OkStatus();
  }

  uint64_t GetReaqCount(){
    return 0;
  }

  rome::metrics::MetricProto GetLocalLatSummary() { 
    rome::metrics::Summary<double> local("local_lat", "ns", 1000);
    return local.ToProto(); 
  }
  rome::metrics::MetricProto GetRemoteLatSummary() { 
    rome::metrics::Summary<double> remote("local_lat", "ns", 1000);
    return remote.ToProto(); 
  }

  bool IsLocked() {
    if (is_host_) {
      //since we are the host, get the local addr and just interpret the value
      return std::to_address(*(std::to_address(tail_pointer_))) != 0;
    } else {
      // read in value of host's lock ptr
      auto remote = pool_.Read<remote_ptr<RdmaMcsLock>>(tail_pointer_);
      // store result of if its locked
      auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
      // deallocate the ptr used as a landing spot for reading in (which is created in Read)
      auto ptr =
          remote_ptr<remote_ptr<RdmaMcsLock>>{self_.id, std::to_address(remote)};
      pool_.Deallocate(ptr);
      return locked;
    }
  }

  void Lock(remote_ptr<RdmaMcsLock> lock) {
    lock_ = lock;
    ROME_DEBUG("lock_: {:x}", static_cast<uint64_t>(lock_));
    tail_pointer_ = decltype(tail_pointer_)(lock.id(), lock.address() + NEXT_PTR_OFFSET);
    ROME_DEBUG("tail_pointer_: {:x}", static_cast<uint64_t>(tail_pointer_));
    // Set local descriptor to initial values
    descriptor_->locked = UNLOCKED;
    descriptor_->next = remote_nullptr;
    // swap local descriptor in at the address of the hosts lock pointer
    auto prev =
        pool_.AtomicSwap(tail_pointer_, static_cast<uint64_t>(desc_pointer_));
    if (prev != remote_nullptr) { //someone else has the lock
      descriptor_->locked = LOCKED; //set descriptor to locked to indicate we are waiting for 
      auto temp_ptr = remote_ptr<uint8_t>(prev);
      temp_ptr += NEXT_PTR_OFFSET; //temp_ptr = next field of the current tail's descriptor
      // make prev point to the current tail descriptor's next pointer
      prev = remote_ptr<RdmaMcsLock>(temp_ptr);
      // set the address of the current tail's next field = to the addr of our local descriptor
      pool_.Write<remote_ptr<RdmaMcsLock>>(
          static_cast<remote_ptr<remote_ptr<RdmaMcsLock>>>(prev), desc_pointer_,
          prealloc_);
      ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
                static_cast<uint64_t>(prev.id()),
                static_cast<uint64_t>(desc_pointer_.id()));
      // spins, waits for Unlock() to unlock our desriptor and let us enter the CS
      while (descriptor_->locked == LOCKED) {
        cpu_relax();
      }
    } 
    // Once here, we can enter the critical section
    ROME_DEBUG("[Lock] Acquired: prev={:x}, locked={:x} (id={})",
              static_cast<uint64_t>(prev), descriptor_->locked,
              static_cast<uint64_t>(desc_pointer_.id()));
    //  make sure Lock operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
    // lock_count_++;
  }

  void Unlock(remote_ptr<RdmaMcsLock> lock) {
    ROME_ASSERT(lock.address() == lock_.address(), "Attempting to unlock lock that is not locked.");
    std::atomic_thread_fence(std::memory_order_release);
    // if tail_pointer_ == my desc (we are the tail), set it to 0 to unlock
    // otherwise, someone else is contending for lock and we want to give it to them
    // try to swap in a 0 to unlock the descriptor at the addr of lock_pointer, which we expect to currently be equal to our descriptor
    auto prev = pool_.CompareAndSwap(tail_pointer_,
                                    static_cast<uint64_t>(desc_pointer_), 0);
    if (prev != desc_pointer_) {  // if the lock at tail_pointer_ was not equal to our descriptor
      // attempt to hand the lock to prev
      // spin while 
      while (descriptor_->next == remote_nullptr);
      std::atomic_thread_fence(std::memory_order_acquire);
      // gets a pointer to the next descriptor object
      auto next = const_cast<remote_ptr<RdmaMcsLock> &>(descriptor_->next);
      //writes a 0 to the next descriptors locked field which lets it know it has the lock now
      pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            UNLOCKED,
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    ROME_DEBUG("[Unlock] Unlocked (id={}), locked={:x}",
                static_cast<uint64_t>(desc_pointer_.id()),
                descriptor_->locked);
  }

private: 
  uint64_t lock_count_; 
  bool is_host_;
  MemoryPool::Peer self_;
  MemoryPool &pool_; //reference to pool object, so all descriptors in same pool
  std::unordered_set<int> local_clients_;

  // Pointer to the A_Lock object, store address in constructor
  // remote_ptr<A_Lock> glock_; 

  // this is pointing to the next field of the lock on the host
  remote_ptr<RdmaMcsLock> lock_;
  remote_ptr<remote_ptr<RdmaMcsLock>> tail_pointer_; //this is supposed to be the tail on the host
  
  // Used for rdma writes to the next feld
  remote_ptr<remote_ptr<RdmaMcsLock>> prealloc_;

  //Pointer to desc to allow it to be read/write via rdma
  remote_ptr<RdmaMcsLock> desc_pointer_;
  volatile RdmaMcsLock *descriptor_;


};

} // namespace X
