#pragma once

#include <infiniband/verbs.h>

#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rome/rdma/rdma_memory.h"

#include "a_lock.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class ALockHandle {

public: 

  ALockHandle(MemoryPool::Peer self, MemoryPool& pool, std::unordered_set<int> local_clients, int64_t budget) 
      : self_(self), pool_(pool), local_clients_(local_clients), init_budget_(budget), reaq_count_(0), local_count_(0), remote_count_(0) {}

  absl::Status Init() {
    ROME_DEBUG("BUDGET IS {}", init_budget_);
    r_desc_pointer_ = pool_.Allocate<RemoteDescriptor>();
    r_desc_ = reinterpret_cast<RemoteDescriptor *>(r_desc_pointer_.address());
    ROME_DEBUG("Node {}: RemoteDescriptor @ {:x}", self_.id, static_cast<uint64_t>(r_desc_pointer_));
    l_desc_pointer_ = pool_.Allocate<LocalDescriptor>();
    l_desc_ = *l_desc_pointer_;
    ROME_DEBUG("Node {}: LocalDescriptor @ {:x}", self_.id, static_cast<uint64_t>(l_desc_pointer_));

    // Make sure remote and local descriptors are done allocating
    std::atomic_thread_fence(std::memory_order_release);

    //Used as preallocated memory for RDMA reads/writes
    prealloc_ = pool_.Allocate<ALock>();
    r_prealloc_ = pool_.Allocate<remote_ptr<RemoteDescriptor>>();
    // l_prealloc_ = pool_.Allocate<remote_ptr<LocalDescriptor>>();
    // v_prealloc_ = pool_.Allocate<uint64_t>();
    
    return absl::OkStatus();
  }

  std::vector<uint64_t> GetCounts(){
    return {reaq_count_, local_count_, remote_count_};
  }
 
  void Lock(remote_ptr<ALock> alock){
    ROME_ASSERT(a_lock_pointer_ == remote_nullptr, "Attempting to lock handle that is already locked.");
    a_lock_pointer_ = alock;
    r_tail_ = decltype(r_tail_)(alock.id(), alock.address());
    r_l_tail_ = decltype(r_l_tail_)(alock.id(), alock.address() + TAIL_PTR_OFFSET);
    r_victim_ = decltype(r_victim_)(alock.id(), alock.address() + VICTIM_OFFSET);  
    ROME_TRACE("r_tail_ addr: {:x}", static_cast<uint64_t>(r_tail_));
    ROME_TRACE("r_l_tail_ addr: {:x} calc: {:X}", static_cast<uint64_t>(r_l_tail_), static_cast<uint64_t>(a_lock_pointer_) + TAIL_PTR_OFFSET);
    ROME_TRACE("r_victim_ addr: {:x} calc: {:X}", static_cast<uint64_t>(r_victim_), static_cast<uint64_t>(a_lock_pointer_) + VICTIM_OFFSET);
    if (local_clients_.contains(a_lock_pointer_.id())){
      is_local_ = true;
    } else {
      is_local_ = false;
    }
    ROME_DEBUG("Client {} is_local_ : {}", self_.id, is_local_);
    if (is_local_){ 
      l_r_tail_ = reinterpret_cast<local_ptr<RemoteDescriptor*>>(alock.address());
      l_l_tail_ = reinterpret_cast<local_ptr<LocalDescriptor*>>(alock.address() + TAIL_PTR_OFFSET);
      l_victim_ = reinterpret_cast<local_ptr<uint64_t*>>(alock.address() + VICTIM_OFFSET);
      LocalLock();
    } else {
      RemoteLock();
    }
  }

  void Unlock(remote_ptr<ALock> alock){
    ROME_ASSERT(alock.address() == a_lock_pointer_.address(), "Attempting to unlock alock that is not locked.");
    if (is_local_){
      LocalUnlock();
    } else {
      RemoteUnlock();
    }
    std::atomic_thread_fence(std::memory_order_release);
    a_lock_pointer_ = remote_nullptr;
  }

  void Reacquire(){
    if (is_local_) {
      ROME_INFO("REACQUIRE1 on {}", self_.id);
      LocalPetersons();
    } else {
      ROME_INFO("REACQUIRE2 on {}", self_.id);
      RemotePetersons();
    }
    reaq_count_++;
  }

private: 

  volatile bool inline IsLocked() {
    return a_lock_pointer_ != remote_nullptr;
  }

  inline void LocalPetersons(){
    //set local to victim
    l_victim_->exchange(LOCAL_VICTIM, std::memory_order_acquire);
    while (true){
      //break if remote tail isn't locked
      if (l_r_tail_->load(std::memory_order_acquire) != 0){
        ROME_DEBUG("remote tail is no longer locked, break");
        break;
      }
      //break if local is no longer victim
      if (l_victim_->load(std::memory_order_acquire) != LOCAL_VICTIM){
        ROME_DEBUG("local is no longer victim, break");
        break;
      } 
    }
    return;
  }

  inline void RemotePetersons(){
    // set remote to victim
    auto prev = pool_.AtomicSwap(r_victim_, static_cast<uint64_t>(REMOTE_VICTIM));
    while (true){
      auto remote = pool_.Read<ALock>(a_lock_pointer_, prealloc_);
      auto temp_ptr = remote_ptr<uint8_t>(remote);
      temp_ptr += TAIL_PTR_OFFSET;
      auto local_tail = remote_ptr<remote_ptr<LocalDescriptor>>(temp_ptr);
      //break if local tail isn't locked
      if (static_cast<uint64_t>(*(std::to_address(local_tail))) == 0){
        ROME_DEBUG("local tail is no longer locked, break");
        pool_.Deallocate(remote);
        break;
      }
      temp_ptr = remote_ptr<uint8_t>(remote);
      temp_ptr += VICTIM_OFFSET;
      auto victim = remote_ptr<remote_ptr<LocalDescriptor>>(temp_ptr);
      // break if remote is no longer victim
      if (static_cast<uint64_t>(*(std::to_address(victim))) != REMOTE_VICTIM){
        ROME_DEBUG("remote is no longer victim, break");
        pool_.Deallocate(remote);
        break;
      } 
      //! I THINK IF I DON'T DO THIS, WE WILL RUN OUT OF MEMORY QUICKLY
      pool_.Deallocate(remote);
    }
    // reaches here when local is no longer locked, or remote is no longer victim
    return;
  }

  // inline volatile bool IsRTailLocked(){
  //     // read in value of current r_tail on host
  //     auto remote = pool_.Read<remote_ptr<RemoteDescriptor>>(a_lock_pointer_, r_prealloc_);
  //     // store result of if its locked (0 = unlocked)
  //     auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
  //     ROME_INFO("Read r_tail: r-tail={:x}, locked={} (id={})", static_cast<uint64_t>(a_lock_pointer_), locked, self_.id);
  //     // deallocate the ptr used as a landing spot for reading in (which is created in Read)
  //     auto ptr = remote_ptr<remote_ptr<RemoteDescriptor>>{self_.id, std::to_address(remote)};
  //     pool_.Deallocate(ptr);
  //     return locked;

  // }

  // inline volatile bool IsLTailLocked(){
  //     // remotely read in value of current l_tail on host
  //     auto remote = pool_.Read<remote_ptr<LocalDescriptor>>(a_lock_pointer_ + TAIL_PTR_OFFSET, l_prealloc_);
  //     // store result of if its locked
  //     auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
  //     // deallocate the ptr used as a landing spot for reading in (which is created in Read)
  //     ROME_INFO("Read l_tail: l-tail={:x}, locked={} (id={})", static_cast<uint64_t>(a_lock_pointer_ + TAIL_PTR_OFFSET), locked, self_.id);
  //     auto ptr = remote_ptr<remote_ptr<LocalDescriptor>>{self_.id, std::to_address(remote)};
  //     pool_.Deallocate(ptr);
  //     return locked;
  // }

  // inline volatile bool IsRemoteVictim() {
  //     // read in value of victim var on host
  //     auto remote = pool_.Read<uint64_t>(a_lock_pointer_ + VICTIM_OFFSET, v_prealloc_);
  //     // store result of if its locked
  //     auto check_victim = static_cast<uint64_t>(*(std::to_address(remote))) == REMOTE_VICTIM;
  //     // deallocate the ptr used as a landing spot for reading in (which is created in Read)
  //     ROME_INFO("Read remote victim: victim={:x}, value={} (id={})", static_cast<uint64_t>(a_lock_pointer_ + VICTIM_OFFSET), check_victim, self_.id);
  //     auto ptr = remote_ptr<uint64_t>{self_.id, std::to_address(remote)};
  //     pool_.Deallocate(ptr);
  //     return check_victim;
  // }

  inline bool LockRemoteMcsQueue(){
      ROME_DEBUG("Locking remote MCS queue...");
      r_desc_->budget = -1;
      r_desc_->next = remote_nullptr;
      // swap RemoteDescriptor onto the remote tail of the alock 
      auto prev =
        pool_.AtomicSwap(r_tail_, static_cast<uint64_t>(r_desc_pointer_));
      
      if (prev != remote_nullptr) { //someone else has the lock
          auto temp_ptr = remote_ptr<uint8_t>(prev);
          temp_ptr += NEXT_PTR_OFFSET; //temp_ptr = next field of the current tail's RemoteDescriptor
          // make prev point to the current tail RemoteDescriptor's next pointer
          prev = remote_ptr<RemoteDescriptor>(temp_ptr);
          // set the address of the current tail's next field = to the addr of our local RemoteDescriptor
          pool_.Write<remote_ptr<RemoteDescriptor>>(
              static_cast<remote_ptr<remote_ptr<RemoteDescriptor>>>(prev), r_desc_pointer_,
              r_prealloc_);
          ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
                  static_cast<uint64_t>(prev.id()),
                  static_cast<uint64_t>(r_desc_pointer_.id()));
          // spins locally, waits for current tail/lockholder to write to budget when it unlocks
          while (r_desc_->budget < 0) {
            // cpu_relax();
            ROME_TRACE("Client {} waiting for remote lock", self_.id);
          }
          if (r_desc_->budget == 0) {
              ROME_DEBUG("Remote Budget exhausted (id={})",
                          static_cast<uint64_t>(r_desc_pointer_.id()));
              // Release the lock before trying to continue
              Reacquire();
              r_desc_->budget = init_budget_;
          }
      } else { //no one had the lock, we were swapped in
          // set lock holders RemoteDescriptor budget to initBudget since we are the first lockholder
          r_desc_->budget = init_budget_;
      }
      // budget was set to greater than 0, CS can be entered
      ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
                  static_cast<uint64_t>(prev), r_desc_->budget,
                  static_cast<uint64_t>(r_desc_pointer_.id()));

      //  make sure Lock operation finished
      std::atomic_thread_fence(std::memory_order_acquire);
      ROME_DEBUG("Client {} is first in q on r_tail {:x}", self_.id, static_cast<uint64_t>(a_lock_pointer_));
      return true;
  }

  inline void RemoteLock(){
    ROME_DEBUG("Client {} RemoteLock()", self_.id);
     // Set local descriptors to initial values
    bool is_leader = LockRemoteMcsQueue();
    if (is_leader){
      // returns when remote wins petersons alg
      RemotePetersons();
        // auto prev = pool_.AtomicSwap(a_lock_pointer_ + VICTIM_OFFSET, static_cast<uint8_t>(REMOTE_VICTIM));
        // while (true){
        //     if (!IsRemoteVictim()){
        //       ROME_DEBUG("remote is no longer victim, break");
        //       break;
        //     } 
        //     if (!IsLTailLocked()){
        //       ROME_DEBUG("local tail is no longer locked, break");
        //       break;
        //     }
        //     // cpu_relax();
        // } 
    }
    remote_count_++;
    std::atomic_thread_fence(std::memory_order_release);
    ROME_DEBUG("Remote wins");
  }

  inline bool LockLocalMcsQueue(){
      // to acquire the lock a thread atomically appends its own local node at the
      // tail of the list returning tail's previous contents
      auto prior_node = l_l_tail_->exchange(&l_desc_, std::memory_order_acquire);
      if (prior_node != nullptr) {
          // l_desc_.budget = -1;
          // if the list was not previously empty, it sets the predecessor’s next
          // field to refer to its own local node
          prior_node->next = &l_desc_;
          ROME_DEBUG("[Lock] Local Enqueued: (id={})",
                  static_cast<uint64_t>(self_.id));
          // thread then spins on its local locked field, waiting until its
          // predecessor sets this field to false
          while (l_desc_.budget < 0){
             cpu_relax(); 
             ROME_TRACE("Client {} waiting for local lock", self_.id);
          }

          // If budget exceeded, then reinitialize.
          if (l_desc_.budget == 0) {
              ROME_DEBUG("Local Budget exhausted (id={})",
                          static_cast<uint64_t>(self_.id));
              // Release the lock before trying to continue
              Reacquire();
              l_desc_.budget = init_budget_;
          }
      } else {
        l_desc_.budget = init_budget_;
      }
      // now first in the queue, own the lock and enter the critical section...
      ROME_DEBUG("Client {} is first in q on l_tail {:x}", self_.id, &l_l_tail_);
      return true;
  }

  inline void LocalLock(){
      ROME_DEBUG("Client {} LocalLock()", self_.id);
      // Set local descriptor to inital values
      l_desc_.budget = -1;
      l_desc_.next = nullptr;
      bool is_leader = LockLocalMcsQueue();
      if (is_leader){
          LocalPetersons();
          // l_victim_->exchange(LOCAL_VICTIM, std::memory_order_acquire);
          // while (true){
          //   if (l_victim_->load() != LOCAL_VICTIM){
          //     ROME_DEBUG("local is no longer victim, break");
          //     break;
          //   } 
          //   if (l_r_tail_->load() != 0){
          //     ROME_DEBUG("remote tail is no longer locked, break");
          //     break;
          //   }
          //   // cpu_relax();
          // }
      }
      local_count_++;
      ROME_DEBUG("Local wins, is_leader is {}", is_leader);
      std::atomic_thread_fence(std::memory_order_release);
  }

  inline void RemoteUnlock(){
      // Make sure everything finished before unlocking
      std::atomic_thread_fence(std::memory_order_release);
      // if r_tail_ == my desc (we are the tail), set it to 0 to unlock
      // otherwise, someone else is contending for lock and we want to give it to them
      // try to swap in a 0 to unlock the RemoteDescriptor at the addr of the remote tail, which we expect to currently be equal to our RemoteDescriptor
      auto prev = pool_.CompareAndSwap(r_tail_,
                                      static_cast<uint64_t>(r_desc_pointer_), 0);
    
      // if the descriptor at r_tail_ was not our RemoteDescriptor (other clients have attempted to lock & enqueued since)
      if (prev != r_desc_pointer_) {  
        // attempt to hand the lock to prev

        // make sure next pointer gets set before continuing
        while (r_desc_->next == remote_nullptr)
        ;
        std::atomic_thread_fence(std::memory_order_acquire);
        // gets a pointer to the next RemoteDescriptor object
        auto next = const_cast<remote_ptr<RemoteDescriptor> &>(r_desc_->next);
        //writes to the the next descriptors budget which lets it know it has the lock now
        pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            r_desc_->budget - 1,
                            static_cast<remote_ptr<uint64_t>>(r_prealloc_));
      } 
      
      //else: successful CAS, we unlocked our RemoteDescriptor and no one is queued after us
      ROME_DEBUG("[Unlock] Unlocked (id={}), budget={:x}",
                  static_cast<uint64_t>(r_desc_pointer_.id()),
                  r_desc_->budget);
      return;
  }

  inline void LocalUnlock(){
      std::atomic_thread_fence(std::memory_order_release);
      ROME_DEBUG("LocalUnLock()");
      //...leave the critical section
      // check whether this thread's local node’s next field is null
      if (l_desc_.next == nullptr) {
        // if so, then either:
        //  1. no other thread is contending for the lock
        //  2. there is a race condition with another thread about to
        // in order to distinguish between these cases atomic compare exchange the tail field
        // if the call succeeds, then no other thread is trying to acquire the lock,
        // tail is set to nullptr, and unlock() returns
        LocalDescriptor* p = &l_desc_;
        if (l_l_tail_->compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {                  
            return;
        }
        // otherwise, another thread is in the process of trying to acquire the
        // lock, so spins waiting for it to finish
        while (l_desc_.next == nullptr) { cpu_relax(); }
      }
      // in either case, once the successor has appeared, the unlock() method sets
      // its successor’s budget, indicating that the lock is now free
      l_desc_.next->budget = l_desc_.budget - 1;
      // at this point no other thread can access this node and it can be reused
      // std::atomic_thread_fence(std::memory_order_release); //didnt do shit
      l_desc_.next = nullptr;
  }

  uint64_t reaq_count_;
  uint64_t local_count_;
  uint64_t remote_count_;
  int64_t init_budget_;
  bool is_local_; //resued for each call to lock for easy check on whether worker is local to key we are attempting to lock
  std::unordered_set<int> local_clients_; 
  
  MemoryPool::Peer self_;
  MemoryPool& pool_; // pool of alocks that the handle is local to (initalized in cluster/node_impl.h)

  //Pointer to alock to allow it to be read/write via rdma
  remote_ptr<ALock> a_lock_pointer_;
  
  // Access to fields remotely
  remote_ptr<remote_ptr<RemoteDescriptor>> r_tail_;
  remote_ptr<remote_ptr<LocalDescriptor>> r_l_tail_;
  remote_ptr<uint64_t> r_victim_;

  // Access to fields locally
  volatile local_ptr<RemoteDescriptor*> l_r_tail_;
  volatile local_ptr<LocalDescriptor*> l_l_tail_;
  volatile local_ptr<uint64_t*> l_victim_;
  
  // Prealloc used for rdma writes of rdma descriptor in RemoteUnlock
  remote_ptr<ALock> prealloc_;
  remote_ptr<remote_ptr<RemoteDescriptor>> r_prealloc_;
  // remote_ptr<remote_ptr<LocalDescriptor>> l_prealloc_;
  // remote_ptr<uint64_t> v_prealloc_;

  // Pointers to pre-allocated descriptor to be used locally
  remote_ptr<LocalDescriptor> l_desc_pointer_;
  LocalDescriptor l_desc_;

  // Pointers to pre-allocated descriptor to be used remotely
  remote_ptr<RemoteDescriptor> r_desc_pointer_;
  volatile RemoteDescriptor* r_desc_;

};

} //namespace X
