#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <infiniband/verbs.h>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "../../../util.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

using SpinLock = uint64_t;

template <typename T>
using local_ptr = std::atomic<T>*;

class SpinLockHandle{
  public:
    SpinLockHandle(MemoryPool::Peer self, MemoryPool& pool)
     : self_(self), pool_(pool) {}

    absl::Status Init() {
      // Preallocate memory for RDMA writes
      p = self_.id;
      std::atomic_thread_fence(std::memory_order_release);
      return absl::OkStatus();
    }

    bool IsLocked() { return lock_ != remote_nullptr; }

    void Lock(SpinLock* lock) {
      return;
      lock_ = lock;
      ROME_DEBUG("Attempting to lock addr lock_: {}, lock: {:x}", *lock_, lock_.address());
      
      while (lock_.compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {
        cpu_relax();
      }
      std::atomic_thread_fence(std::memory_order_release);
    }

    void  Unlock(SpinLock* lock) {
      lock_ = lock;
      ROME_ASSERT(lock.address() == lock_.address(), "Attempting to unlock spinlock that is not locked.");
      lock_.store(kUnlocked, std::memory_order_release);
      std::atomic_thread_fence(std::memory_order_release);
      lock_ = nullptr;
    }

    
private:

  static constexpr uint64_t kUnlocked = 0;
  bool is_host_;

  MemoryPool::Peer self_;
  MemoryPool &pool_;

  local_ptr<SpinLock> lock_;
  SpinLock* p;
  SpinLock local_;

};

} // namespace X