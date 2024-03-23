#pragma once

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <remus/rdma/rdma.h>
#include "common.h"

using namespace remus::rdma;

struct alignas(64) RdmaSpinLock {
    uint64_t lock{0};
    uint8_t pad1[CACHELINE_SIZE - sizeof(lock)];
};
static_assert(alignof(RdmaSpinLock) == CACHELINE_SIZE);
static_assert(sizeof(RdmaSpinLock) == CACHELINE_SIZE);

// using RdmaSpinLock = uint64_t;

class RdmaSpinLockHandle{
public:
  RdmaSpinLockHandle(Peer self, std::shared_ptr<rdma_capability> pool, std::unordered_set<int> local_clients, int64_t local_budget, int64_t remote_budget)
    : self_(self), pool_(pool), local_clients_(local_clients), lock_count_(0) {}

  remus::util::Status Init() {
    // Preallocate memory for RDMA writes
    local_ = pool_->Allocate<uint64_t>();
    std::atomic_thread_fence(std::memory_order_release);
    return remus::util::Status::Ok();
  }

  uint64_t GetReaqCount(){
    return 0;
  }

  remus::metrics::MetricProto GetLocalLatSummary() { 
    remus::metrics::Summary<double> local("local_lat", "ns", 1000);
    return local.ToProto(); 
  }
  remus::metrics::MetricProto GetRemoteLatSummary() { 
    remus::metrics::Summary<double> remote("local_lat", "ns", 1000);
    return remote.ToProto(); 
  }

  bool IsLocked(rdma_ptr<RdmaSpinLock> lock) { 
    uint64_t val = static_cast<uint64_t>(pool_->Read(lock));
    if (val == kUnlocked){
      return false;
    }
    return true;
  }

  void Lock(rdma_ptr<RdmaSpinLock> lock) {  
    lock_ = decltype(lock_)(lock.id(), lock.address());
    //?: IDEA: switch to read and write to see if CAS introduces an issue with the rdma card because its atomic
    while (pool_->CompareAndSwap(lock_, kUnlocked, self_.id) != kUnlocked) {
      cpu_relax();
    }
    std::atomic_thread_fence(std::memory_order_release);
    return;
  }

  void  Unlock(rdma_ptr<RdmaSpinLock> lock) {
    REMUS_ASSERT(lock.address() == lock_.address(), "Attempting to unlock spinlock that is not locked.");
    pool_->Write<uint64_t>(lock_, 0, /*prealloc=*/local_);
    std::atomic_thread_fence(std::memory_order_release);
    lock_ = nullptr;
    return;
  }

    
private:

  uint64_t lock_count_;

  static constexpr uint64_t kUnlocked = 0;
  bool is_host_;

  Peer self_;
  std::shared_ptr<rdma_capability> pool_;
  std::unordered_set<int> local_clients_;

  rdma_ptr<uint64_t> lock_;
  rdma_ptr<uint64_t> local_;

};
