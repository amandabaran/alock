#include "rdma_spin_lock.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class RdmaSpinLockHandle{
  public:
    RdmaSpinLockHandle(MemoryPool::Peer self, MemoryPool& pool)
     : self_(self), pool_(pool) {}

    absl::Status Init() {
      // Preallocate memory for RDMA writes
      local_ = pool_.Allocate<RdmaSpinLock>();
      return absl::OkStatus();
    }

    bool IsLocked() { return lock_ != remote_nullptr; }

    void Lock(remote_ptr<RdmaSpinLock> lock) {
      lock_ = lock;
      while (pool_.CompareAndSwap(static_cast<remote_ptr<uint64_t>>(lock), kUnlocked, self_.id) != kUnlocked) {
        cpu_relax();
      }
    }

    void  Unlock(remote_ptr<RdmaSpinLock> lock) {
      ROME_ASSERT(lock.address() == lock_.address(), "Attempting to unlock spinlock that is not locked.");
      pool_.Write<uint64_t>((static_cast<remote_ptr<uint64_t>>(lock)), kUnlocked, /*prealloc=*/static_cast<remote_ptr<uint64_t>>(local_));
      std::atomic_thread_fence(std::memory_order_release);
      lock_ = remote_nullptr;
    }

    
private:

  static constexpr uint64_t kUnlocked = 0;
  bool is_host_;

  MemoryPool::Peer self_;
  MemoryPool &pool_;

  remote_ptr<RdmaSpinLock> lock_;
  remote_ptr<RdmaSpinLock> local_;

};

} // namespace X
