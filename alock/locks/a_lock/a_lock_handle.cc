#include "a_lock_handle.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

ALockHandle::ALockHandle(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), lock_pool_(pool) {}

absl::Status ALockHandle::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  auto capacity = 1 << kPoolSize;
  // Allocate pool for Remote Descriptors
  auto status = desc_pool_->Init(capacity, peers);
  // ROME_ASSERT_OK(status);
  ROME_CHECK_OK(ROME_RETURN(status), status);
 
  // allocate local and remote descriptors for this worker to use
  AllocateDescriptors();

  //TODO: Figure out how/if descriptor pool needs to be shared amongst workers

  //Used as preallocated memory for RDMA writes
  prealloc_ = desc_pool_->Allocate<remote_ptr<RdmaDescriptor>>();
  
  return absl::OkStatus();
}

void ALockHandle::AllocateDescriptors(){
  // Pointer to first remote descriptor
  r_desc_pointer_ = desc_pool_->Allocate<RdmaDescriptor>();
  r_desc_ = reinterpret_cast<RdmaDescriptor *>(r_desc_pointer_.address());
  ROME_INFO("First RdmaDescriptor @ {:x}", static_cast<uint64_t>(r_desc_pointer_));
  r_bitset[0] = 0;
  for (int i = 1; i < DESCS_PER_CLIENT; i++){
    auto temp = desc_pool_->Allocate<RdmaDescriptor>();
    ROME_DEBUG("RdmaDescriptor @ {:x}", static_cast<uint64_t>(temp));
    r_bitset[i] = 0;
  }
  
  // Make sure all rdma descriptors are allocated first in contiguous memory
  std::atomic_thread_fence(std::memory_order_release);

  // Pointer to first local descriptor
  l_desc_pointer_ = desc_pool_->Allocate<LocalDescriptor>();
  l_desc_ = reinterpret_cast<LocalDescriptor *>(l_desc_pointer_.address());
  ROME_DEBUG("First LocalDescriptor @ {:x}", static_cast<uint64_t>(l_desc_pointer_));
  l_bitset[0] = 0;
  for (int i = 1; i < DESCS_PER_CLIENT; i++){
    auto temp = desc_pool_->Allocate<LocalDescriptor>();
    ROME_DEBUG("LocalDescriptor @ {:x}", static_cast<uint64_t>(temp));
    l_bitset[i] = 0;
  }

  // Make sure all local descriptors are done allocating
  std::atomic_thread_fence(std::memory_order_release);
}

bool ALockHandle::IsLocked() {
  return (a_lock_pointer_ != remote_nullptr);
}

bool inline ALockHandle::IsLocal(){
  return ((a_lock_pointer_).id() == self_.id);
}

bool ALockHandle::IsRTailLocked(){
    // read in value of current r_tail on host
    auto remote = lock_pool_.Read<remote_ptr<RdmaDescriptor>>(r_tail_);
    // store result of if its locked (0 = unlocked)
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<RdmaDescriptor>>{self_.id, std::to_address(remote)};
    lock_pool_.Deallocate(ptr);
    return locked;

}

bool ALockHandle::IsLTailLocked(){
    // remotely read in value of current l_tail on host
    auto remote = lock_pool_.Read<remote_ptr<LocalDescriptor>>(r_l_tail_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<LocalDescriptor>>{self_.id, std::to_address(remote)};
    lock_pool_.Deallocate(ptr);
    return locked;
}

bool ALockHandle::IsRemoteVictim() {
    // read in value of victim var on host
    auto remote = lock_pool_.Read<uint64_t>(r_victim_);
    // store result of if its locked
    auto check_victim = static_cast<uint64_t>(*(std::to_address(remote))) == REMOTE_VICTIM;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<uint64_t>{self_.id, std::to_address(remote)};
    lock_pool_.Deallocate(ptr);
    return check_victim;
}

void ALockHandle::LockRemoteMcsQueue(){
    ROME_DEBUG("Locking remote MCS queue...");
  
    // Set local RdmaDescriptor to initial values
    r_desc_->budget = -1;
    r_desc_->next = remote_nullptr;

    // swap RdmaDescriptor onto the remote tail of the alock 
    auto prev =
      lock_pool_.AtomicSwap(r_tail_, static_cast<uint64_t>(r_desc_pointer_));
    
    if (prev != remote_nullptr) { //someone else has the lock
        auto temp_ptr = remote_ptr<uint8_t>(prev);
        temp_ptr += NEXT_PTR_OFFSET; //temp_ptr = next field of the current tail's RdmaDescriptor
        // make prev point to the current tail RdmaDescriptor's next pointer
        prev = remote_ptr<RdmaDescriptor>(temp_ptr);
        // set the address of the current tail's next field = to the addr of our local RdmaDescriptor
        lock_pool_.Write<remote_ptr<RdmaDescriptor>>(
            static_cast<remote_ptr<remote_ptr<RdmaDescriptor>>>(prev), r_desc_pointer_,
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
            Reacquire(false);
            r_desc_->budget = kInitBudget;
        }
    } else { //no one had the lock, we were swapped in
        // set lock holders RdmaDescriptor budget to initBudget since we are the first lockholder
        r_desc_->budget = kInitBudget;
        is_r_leader_ = true;
    }
    // budget was set to greater than 0, CS can be entered
    ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
                static_cast<uint64_t>(prev), r_desc_->budget,
                static_cast<uint64_t>(r_desc_pointer_.id()));

    //  make sure Lock operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
}

void ALockHandle::RemoteLock(){
    LockRemoteMcsQueue();
    if (is_r_leader_){
        auto prev = lock_pool_.AtomicSwap(r_victim_, static_cast<uint8_t>(REMOTE_VICTIM));
        //! this is remotely spinning on the victim var?? --> but competition is only among two at this point
        while (IsRemoteVictim() && IsLTailLocked()){
            cpu_relax();
        } 
    }
}

void ALockHandle::LockLocalMcsQueue(){
    ROME_DEBUG("LOCKING LOCAL MCS QUEUE...");
    // to acquire the lock a thread atomically appends its own local node at the
    // tail of the list returning tail's previous contents
    // ROME_DEBUG("l_tail_ @ {:x}", (&l_tail_));
    auto prior_node = l_l_tail_.exchange(l_desc_, std::memory_order_acquire);
    if (prior_node != nullptr) {
        l_desc_->budget = -1;
        // if the list was not previously empty, it sets the predecessor’s next
        // field to refer to its own local node
        prior_node->next = l_desc_;
        // thread then spins on its local locked field, waiting until its
        // predecessor sets this field to false
        //!STUCK IN THIS LOOP!!!!!!!! 
        while (l_desc_->budget < 0) {
            cpu_relax();
            ROME_DEBUG("WAITING IN HERE...");
        }

        // If budget exceeded, then reinitialize.
        if (l_desc_->budget == 0) {
            ROME_DEBUG("Local Budget exhausted (id={})",
                        static_cast<uint64_t>(l_desc_pointer_.id()));
            // Release the lock before trying to continue
            Reacquire(true);
            l_desc_->budget = kInitBudget;
        }
    }
    // now first in the queue, own the lock and enter the critical section...
    // is_leader_ = true;

}

void ALockHandle::LocalLock(){
  ROME_DEBUG("ALockHandle::LocalLock()");
    LockLocalMcsQueue();
    if (is_l_leader_){
        auto prev = l_victim_.exchange(LOCAL_VICTIM, std::memory_order_acquire);
        while (*l_victim_== LOCAL_VICTIM && IsRTailLocked()){
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
    // try to swap in a 0 to unlock the RdmaDescriptor at the addr of the remote tail, which we expect to currently be equal to our RdmaDescriptor
    auto prev = lock_pool_.CompareAndSwap(r_tail_,
                                    static_cast<uint64_t>(r_desc_pointer_), 0);
   
    // if the descriptor at r_tail_ was not our RdmaDescriptor (other clients have attempted to lock & enqueued since)
    if (prev != r_desc_pointer_) {  
        // attempt to hand the lock to prev

        // make sure next pointer gets set before continuing
        while (r_desc_->next == remote_nullptr)
        ;
        std::atomic_thread_fence(std::memory_order_acquire);

        // gets a pointer to the next RdmaDescriptor object
        auto next = const_cast<remote_ptr<RdmaDescriptor> &>(r_desc_->next);
        //writes to the the next descriptors budget which lets it know it has the lock now
        lock_pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            r_desc_->budget - 1,
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    //else: successful CAS, we unlocked our RdmaDescriptor and no one is queued after us
    // is_locked_ = false;
    is_r_leader_ = false;
    ROME_DEBUG("[Unlock] Unlocked (id={})",
                static_cast<uint64_t>(r_desc_pointer_.id()));
}

void ALockHandle::LocalUnlock(){
    std::atomic_thread_fence(std::memory_order_release);
    ROME_DEBUG("ALockHandle::LocalUnLock()");
    //...leave the critical section
    // check whether this thread's local node’s next field is null
    if (l_desc_->next == nullptr) {
        // if so, then either:
        //  1. no other thread is contending for the lock
        //  2. there is a race condition with another thread about to
        // to distinguish between these cases atomic compare exchange the tail field
        // if the call succeeds, then no other thread is trying to acquire the lock,
        // tail is set to nullptr, and unlock() returns
        LocalDescriptor* p = l_desc_;
        if (l_l_tail_.compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {
            return;
        }
        // otherwise, another thread is in the process of trying to acquire the
        // lock, so spins waiting for it to finish
        while (l_desc_->next == nullptr) {
        };
    }
    // in either case, once the successor has appeared, the unlock() method sets
    // its successor’s locked field to false, indicating that the lock is now free
    l_desc_->next->budget = l_desc_->budget - 1;
    //! NOT sure if this is reasonable
    is_l_leader_ = false;
    // ! THIS IS HOW WE KNOW WHEN TO ALLOW REUSING (CHECK IF NEXT IS NULLPTR AND ALWAYS MOVE FORWARD)
    // at this point no other thread can access this node and it can be reused
    l_desc_->next = nullptr;
}

//Eventually will take in remote_ptr<ALock> once using sharding or just an offset?
void ALockHandle::Lock(remote_ptr<ALock> alock){
  ROME_ASSERT(a_lock_pointer_ == remote_nullptr, "Attempting to lock handle that is already locked.")
  a_lock_pointer_ = alock;
  r_tail_ = decltype(r_tail_)(alock.id(), alock.address());
  r_l_tail_ = decltype(r_l_tail_)(alock.id(), alock.address() + DESC_PTR_OFFSET);
  r_victim_ = decltype(r_victim_)(alock.id(), alock.address() + VICTIM_OFFSET);   
  if (IsLocal()){ 
    ROME_DEBUG("ALock is LOCAL to node {}", static_cast<uint64_t>(self_.id));
    l_l_tail_ = reinterpret_cast<LocalDescriptor*>(alock.address() + DESC_PTR_OFFSET);
    l_victim_ = reinterpret_cast<uint64_t*>(alock.address() + VICTIM_OFFSET);
    LocalLock();
  } else {
    ROME_DEBUG("ALock is REMOTE");
    RemoteLock();
  }
}

void ALockHandle::Unlock(remote_ptr<ALock> alock){
  ROME_ASSERT(alock.address() == a_lock_pointer_.address(), "Attempting to unlock alock that is not locked.")
  if (IsLocal()){
    LocalUnlock();
  } else {
    RemoteUnlock();
  }
  a_lock_pointer_ = remote_nullptr;
}

void ALockHandle::Reacquire(bool isLocal){
  if (IsLocal()){
    while (*l_victim_== LOCAL_VICTIM && IsRTailLocked()){
        cpu_relax();
    } 
  } else {
    while (IsRemoteVictim() && IsLTailLocked()){
        cpu_relax();
    } 
  }
}

}
