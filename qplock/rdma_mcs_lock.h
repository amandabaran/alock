#pragma once

#include <infiniband/verbs.h>

#include <cstdint>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "util.h"
#include "qplock/qplock/qplock.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class RdmaMcsLock {
public:
  using conn_type = MemoryPool::conn_type;

  struct alignas(128) Descriptor {
    long int budget{-1};
    uint8_t pad1[CACHELINE_SIZE - sizeof(budget)];
    remote_ptr<Descriptor> next{0};
    uint8_t pad2[CACHELINE_SIZE - sizeof(uintptr_t)];
  };
  static_assert(alignof(Descriptor) == 128);
  static_assert(sizeof(Descriptor) == 128);

  // Change constructor to take in memeory of the glock?
  RdmaMcsLock(MemoryPool::Peer self, MemoryPool& pool);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  bool is_host_;
  MemoryPool::Peer self_;
  MemoryPool &pool_;

  // Pointer to the qplock object, store address in constructor
  remote_ptr<remote_ptr<QPLock>> glock_; 

  // other nodes in system use this to access the desc?
  remote_ptr<remote_ptr<Descriptor>> lock_pointer_;
  
  // Used as landing spot for peers
  remote_ptr<remote_ptr<Descriptor>> prealloc_;

  //Pointer to desc to allow it to be read/write via rdma
  remote_ptr<Descriptor> desc_pointer_;
  volatile Descriptor *descriptor_;
};

} // namespace X
