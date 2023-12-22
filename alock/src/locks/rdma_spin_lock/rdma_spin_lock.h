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

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

struct alignas(64) RdmaSpinLock {
    uint64_t lock{0};
    uint8_t pad1[CACHELINE_SIZE - sizeof(lock)];
};
static_assert(alignof(RdmaSpinLock) == CACHELINE_SIZE);
static_assert(sizeof(RdmaSpinLock) == CACHELINE_SIZE);

// using RdmaSpinLock = uint64_t;

class RdmaSpinLockHandle{
public:
  RdmaSpinLockHandle(MemoryPool::Peer self, MemoryPool& pool, std::unordered_set<int> local_clients, int64_t local_budget, int64_t remote_budget)
    : self_(self), pool_(pool), local_clients_(local_clients), lock_count_(0) {}

  absl::Status Init() {
    // Preallocate memory for RDMA writes
    local_ = pool_.Allocate<uint64_t>();
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

  bool IsLocked(remote_ptr<RdmaSpinLock> lock) { 
    uint64_t val = static_cast<uint64_t>(pool_.Read(lock));
    if (val == kUnlocked){
      return false;
    }
    return true;
  }

  void Lock(remote_ptr<RdmaSpinLock> lock) {  
    lock_ = decltype(lock_)(lock.id(), lock.address());
    //TODO: switch to read and write to see if CAS introduces an issue with the rdma card because its atomic
    while (pool_.CompareAndSwap(lock_, kUnlocked, self_.id) != kUnlocked) {
      cpu_relax();
    }
    std::atomic_thread_fence(std::memory_order_release);
    return;
  }

  void  Unlock(remote_ptr<RdmaSpinLock> lock) {
    ROME_ASSERT(lock.address() == lock_.address(), "Attempting to unlock spinlock that is not locked.");
    pool_.Write<uint64_t>(lock_, 0, /*prealloc=*/local_);
    std::atomic_thread_fence(std::memory_order_release);
    lock_ = remote_nullptr;
    return;
  }

    
private:

  uint64_t lock_count_;

  static constexpr uint64_t kUnlocked = 0;
  bool is_host_;

  MemoryPool::Peer self_;
  MemoryPool &pool_;
  std::unordered_set<int> local_clients_;

  remote_ptr<uint64_t> lock_;
  remote_ptr<uint64_t> local_;

};

} // namespace X