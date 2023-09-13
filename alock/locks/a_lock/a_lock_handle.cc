#include "a_lock_handle.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

ALockHandle::ALockHandle(MemoryPool::Peer self, MemoryPool &pool, int worker_id)
    : self_(self), pool_(pool), worker_id_(worker_id) {}

absl::Status ALockHandle::Init() {
  // allocate local and remote descriptors for this worker to use
  r_desc_pointer_ = pool_.Allocate<RemoteDescriptor>();
  r_desc_ = reinterpret_cast<RemoteDescriptor *>(r_desc_pointer_.address());
  ROME_DEBUG("Worker {} on Node {}: RemoteDescriptor @ {:x}", worker_id_, self_.id, static_cast<uint64_t>(r_desc_pointer_));
  l_desc_pointer_ = pool_.Allocate<LocalDescriptor>();
  l_desc_ = *l_desc_pointer_;
  ROME_DEBUG("Worker {} on Node {}: LocalDescriptor @ {:x}", worker_id_, self_.id, static_cast<uint64_t>(l_desc_pointer_));
 
  // Set local descriptors to initial values
  r_desc_->budget = -1;
  r_desc_->next = remote_nullptr;
  l_desc_.budget = -1;
  l_desc_.next = nullptr;

  // Make sure remote and local descriptors are done allocating
  std::atomic_thread_fence(std::memory_order_release);

  //Used as preallocated memory for RDMA writes
  prealloc_ = pool_.Allocate<remote_ptr<RemoteDescriptor>>();
  
  return absl::OkStatus();
}

bool inline ALockHandle::IsLocked() {
  return (a_lock_pointer_ != remote_nullptr);
}

bool ALockHandle::IsRTailLocked(){
    // read in value of current r_tail on host
    auto remote = pool_.Read<remote_ptr<RemoteDescriptor>>(r_tail_);
    // store result of if its locked (0 = unlocked)
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    ROME_DEBUG("Read r_tail: r-tail={:x}, locked={}", static_cast<uint64_t>(remote), locked);
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr = remote_ptr<remote_ptr<RemoteDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;

}

bool ALockHandle::IsLTailLocked(){
    // remotely read in value of current l_tail on host
    auto remote = pool_.Read<remote_ptr<LocalDescriptor>>(r_l_tail_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    ROME_DEBUG("Read l_tail: l-tail={:x}, locked={}", static_cast<uint64_t>(remote), locked);
    auto ptr = remote_ptr<remote_ptr<LocalDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
}

bool ALockHandle::IsRemoteVictim() {
    // read in value of victim var on host
    auto remote = pool_.Read<uint64_t>(r_victim_);
    // store result of if its locked
    auto check_victim = static_cast<uint64_t>(*(std::to_address(remote))) == REMOTE_VICTIM;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    ROME_DEBUG("Read victim: victim={:x}, value={}", static_cast<uint64_t>(remote), check_victim);
    auto ptr = remote_ptr<uint64_t>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return check_victim;
}

bool ALockHandle::LockRemoteMcsQueue(){
    ROME_DEBUG("Locking remote MCS queue...");
    
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
            prealloc_);
        ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
                static_cast<uint64_t>(prev.id()),
                static_cast<uint64_t>(r_desc_pointer_.id()));
        // spins locally, waits for current tail/lockholder to write to budget when it unlocks
        while (r_desc_->budget < 0) {
            cpu_relax();
        }
        if (r_desc_->budget == 0) {
            ROME_DEBUG("Remote Budget exhausted (id={})",
                        static_cast<uint64_t>(r_desc_pointer_.id()));
            // Release the lock before trying to continue
            Reacquire();
            r_desc_->budget = kInitBudget;
        }
    } else { //no one had the lock, we were swapped in
        // set lock holders RemoteDescriptor budget to initBudget since we are the first lockholder
        r_desc_->budget = kInitBudget;
    }
    // budget was set to greater than 0, CS can be entered
    ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
                static_cast<uint64_t>(prev), r_desc_->budget,
                static_cast<uint64_t>(r_desc_pointer_.id()));

    //  make sure Lock operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
    return true;
}

void ALockHandle::RemoteLock(){
    bool is_leader = LockRemoteMcsQueue();
    if (is_leader){
        auto prev = pool_.AtomicSwap(r_victim_, static_cast<uint8_t>(REMOTE_VICTIM));
        //! this is remotely spinning on the victim var?? --> but competition is only among two at this point
        while (IsRemoteVictim() && IsLTailLocked()){
            cpu_relax();
        } 
    }
}

bool ALockHandle::LockLocalMcsQueue(){
    ROME_DEBUG("LOCKING LOCAL MCS QUEUE...");
    // to acquire the lock a thread atomically appends its own local node at the
    // tail of the list returning tail's previous contents
    auto prior_node = l_l_tail_->exchange(&l_desc_, std::memory_order_acquire);
    if (prior_node != nullptr) {
        // l_desc_.budget = -1;
        // if the list was not previously empty, it sets the predecessor’s next
        // field to refer to its own local node
        prior_node->next = &l_desc_;
        // thread then spins on its local locked field, waiting until its
        // predecessor sets this field to false
        while (l_desc_.budget < 0) cpu_relax(); 

        // If budget exceeded, then reinitialize.
        if (l_desc_.budget == 0) {
            ROME_DEBUG("Local Budget exhausted (id={})",
                        static_cast<uint64_t>(self_.id));
            // Release the lock before trying to continue
            Reacquire();
            l_desc_.budget = kInitBudget;
        }
    } else {
      l_desc_.budget = kInitBudget;
    }
    // now first in the queue, own the lock and enter the critical section...
    return true;
}

void ALockHandle::LocalLock(){
    ROME_DEBUG("ALockHandle::LocalLock()");
    bool is_leader = LockLocalMcsQueue();
    if (is_leader){
        auto prev = l_victim_->exchange(LOCAL_VICTIM, std::memory_order_acquire);
        while (l_victim_->load() == LOCAL_VICTIM && l_r_tail_->load() != 0){
            cpu_relax();
        } 
    }
    std::atomic_thread_fence(std::memory_order_release);
}

void ALockHandle::RemoteUnlock(){
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
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    
    //else: successful CAS, we unlocked our RemoteDescriptor and no one is queued after us
    ROME_DEBUG("[Unlock] Unlocked (id={}), budget={:x}",
                static_cast<uint64_t>(r_desc_pointer_.id()),
                r_desc_->budget);

}

void ALockHandle::LocalUnlock(){
    std::atomic_thread_fence(std::memory_order_release);
    ROME_DEBUG("ALockHandle::LocalUnLock()");
    //...leave the critical section
    // check whether this thread's local node’s next field is null
    if (l_desc_.next == nullptr) {
        // if so, then either:
        //  1. no other thread is contending for the lock
        //  2. there is a race condition with another thread about to
        // to distinguish between these cases atomic compare exchange the tail field
        // if the call succeeds, then no other thread is trying to acquire the lock,
        // tail is set to nullptr, and unlock() returns
        LocalDescriptor* p = &l_desc_;
        if (l_l_tail_->compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {
            return;
        }
        // otherwise, another thread is in the process of trying to acquire the
        // lock, so spins waiting for it to finish
        while (l_desc_.next == nullptr) {
        };
    }
    // in either case, once the successor has appeared, the unlock() method sets
    // its successor’s locked field to false, indicating that the lock is now free
    l_desc_.next->budget = l_desc_.budget - 1;
    // at this point no other thread can access this node and it can be reused
    l_desc_.next = nullptr;
}

void ALockHandle::Lock(remote_ptr<ALock> alock){
  ROME_ASSERT(a_lock_pointer_ == remote_nullptr, "Attempting to lock handle that is already locked.")
  a_lock_pointer_ = alock;
  r_tail_ = decltype(r_tail_)(alock.id(), alock.address());
  r_l_tail_ = decltype(r_l_tail_)(alock.id(), alock.address() + DESC_PTR_OFFSET);
  r_victim_ = decltype(r_victim_)(alock.id(), alock.address() + VICTIM_OFFSET);   
  is_local_ = ((a_lock_pointer_).id() == self_.id);
  if (is_local_){ 
    a_lock_ = decltype(a_lock_)(alock.raw());
    l_r_tail_ = reinterpret_cast<local_ptr<RemoteDescriptor*>>(alock.address());
    ROME_DEBUG("l_r_tail addr is {:x}", reinterpret_cast<uint64_t>(&(*l_r_tail_->load())));
    l_l_tail_ = reinterpret_cast<local_ptr<LocalDescriptor*>>(alock.address() + DESC_PTR_OFFSET);
    ROME_DEBUG("l_l_tail_ addr is {:x}", reinterpret_cast<uint64_t>(&(*l_l_tail_->load())));
    l_victim_ = reinterpret_cast<local_ptr<uint64_t*>>(alock.address() + VICTIM_OFFSET);
    ROME_DEBUG("l_victim_ addr is {:x}", reinterpret_cast<uint64_t>(&(*l_victim_->load())));
    LocalLock();
  } else {
    RemoteLock();
  }
}

void ALockHandle::Unlock(remote_ptr<ALock> alock){
  ROME_ASSERT(alock.address() == a_lock_pointer_.address(), "Attempting to unlock alock that is not locked.")
  if (is_local_){
    LocalUnlock();
  } else {
    RemoteUnlock();
  }
  a_lock_pointer_ = remote_nullptr;
}

void ALockHandle::Reacquire(){
  if (is_local_) {
    while (l_victim_->load() == LOCAL_VICTIM && IsRTailLocked()){
        cpu_relax();
    } 
  } else {
    while (IsRemoteVictim() && IsLTailLocked()){
        cpu_relax();
    } 
  }
}

}
