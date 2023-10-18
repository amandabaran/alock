#include "rdma_mcs_lock.h"

constexpr int kInitBudget = 5;

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

RdmaMcsLock::RdmaMcsLock(MemoryPool::Peer self, MemoryPool &pool, std::set<int> local_clients)
    : self_(self), pool_(pool), local_clients_(local_clients) {}

absl::Status RdmaMcsLock::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  is_host_ = self_.id == host.id;
  auto capacity = 1 << 20;
  auto status = pool_.Init(capacity, peers);
  ROME_ASSERT_OK(status);
  
  // Reserve remote memory for the local descriptor.
  desc_pointer_ = pool_.Allocate<McsDescriptor>();
  descriptor_ = reinterpret_cast<McsDescriptor *>(desc_pointer_.address());
  ROME_DEBUG("McsDescriptor @ {:x}", static_cast<uint64_t>(desc_pointer_));

  if (is_host_) {
    // Send all peers the base address of the lock residing on the host
    RemoteObjectProto proto;
    lock_pointer_ = pool_.Allocate<remote_ptr<McsDescriptor>>();
    proto.set_raddr(lock_pointer_.address());

    *(std::to_address(lock_pointer_)) = remote_ptr<McsDescriptor>(0);
    // tell all the peers where to find the addr of the first lock
    for (const auto &p : peers) {
      auto conn_or = pool_.connection_manager()->GetConnection(p.id);
      ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
      status = conn_or.value()->channel()->Send(proto);
      ROME_CHECK_OK(ROME_RETURN(status), status);
    }
  } else {
    // Otherwise, wait until the base address is shared by the host
    auto conn_or = pool_.connection_manager()->GetConnection(host.id);
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    while (got.status().code() == absl::StatusCode::kUnavailable) {
      got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    }
    ROME_CHECK_OK(ROME_RETURN(got.status()), got);
    // set lock pointer to the base address of the lock on the host
    lock_pointer_ = decltype(lock_pointer_)(host.id, got->raddr());

    //Used as preallocated memory for RDMA writes
    prealloc_ = pool_.Allocate<remote_ptr<McsDescriptor>>();
  }
  ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(lock_pointer_));
  return absl::OkStatus();
}

bool RdmaMcsLock::IsLocked() {
  if (is_host_) {
    //since we are the host, get the local addr and just interpret the value
    return std::to_address(*(std::to_address(lock_pointer_))) != 0;
  } else {
    // read in value of host's lock ptr
    auto remote = pool_.Read<remote_ptr<McsDescriptor>>(lock_pointer_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<McsDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
  }
}

void RdmaMcsLock::Lock() {
  ROME_ASSERT_DEBUG(!is_host_, "Unimplemented!");  
  // Set local descriptor to initial values
  descriptor_->budget = -1;
  descriptor_->next = remote_nullptr;
  // swap local descriptor in at the address of the hosts lock pointer
  auto prev =
      pool_.AtomicSwap(lock_pointer_, static_cast<uint64_t>(desc_pointer_));
  if (prev != remote_nullptr) { //someone else has the lock
    auto temp_ptr = remote_ptr<uint8_t>(prev);
    temp_ptr += 64; //temp_ptr = next field of the current tail's descriptor
    // make prev point to the current tail descriptor's next pointer
    prev = remote_ptr<McsDescriptor>(temp_ptr);
    // set the address of the current tail's next field = to the addr of our local descriptor
    pool_.Write<remote_ptr<McsDescriptor>>(
        static_cast<remote_ptr<remote_ptr<McsDescriptor>>>(prev), desc_pointer_,
        prealloc_);
    ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
               static_cast<uint64_t>(prev.id()),
               static_cast<uint64_t>(desc_pointer_.id()));
    // spins, waits for Unlock() to write to the budget
    while (descriptor_->budget < 0) {
      cpu_relax();
    }
    if (descriptor_->budget == 0) {
      ROME_DEBUG("Budget exhausted (id={})",
                 static_cast<uint64_t>(desc_pointer_.id()));
      descriptor_->budget = kInitBudget;
    }
  } else { //no one had the lock, we were swapped in
    // set lock holders descriptor budget to initBudget since we are the first lockholder
    descriptor_->budget = kInitBudget;
  }
  // budget was set to greater than 0, CS can be entered
  ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
             static_cast<uint64_t>(prev), descriptor_->budget,
             static_cast<uint64_t>(desc_pointer_.id()));
  //  make sure Lock operation finished
  std::atomic_thread_fence(std::memory_order_acquire);
}

void RdmaMcsLock::Unlock() {
  std::atomic_thread_fence(std::memory_order_release);
  ROME_ASSERT_DEBUG(!is_host_, "Unimplemented!");
  // if lock_pointer_ == my desc (we are the tail), set it to 0 to unlock
  // otherwise, someone else is contending for lock and we want to give it to them
  // try to swap in a 0 to unlock the descriptor at the addr of lock_pointer, which we expect to currently be equal to our descriptor
  auto prev = pool_.CompareAndSwap(lock_pointer_,
                                   static_cast<uint64_t>(desc_pointer_), 0);
  if (prev != desc_pointer_) {  // if the lock at lock_pointer_ was not equal to our descriptor
    // attempt to hand the lock to prev
    // spin while 
    while (descriptor_->next == remote_nullptr)
      ;
    std::atomic_thread_fence(std::memory_order_acquire);
    // gets a pointer to the next descriptor object
    auto next = const_cast<remote_ptr<McsDescriptor> &>(descriptor_->next);
    //writes to the the next descriptors budget which lets it know it has the lock now
    pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                          descriptor_->budget - 1,
                          static_cast<remote_ptr<uint64_t>>(prealloc_));
  } else { //successful CAS, we unlocked our descriptor
    ROME_DEBUG("[Unlock] Unlocked (id={})",
               static_cast<uint64_t>(desc_pointer_.id()));
  }
}

} // namespace X
