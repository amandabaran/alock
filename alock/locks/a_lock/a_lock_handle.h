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

template <typename T>
using local_ptr = std::atomic<T>*;

class ALockHandle{
public:
  using conn_type = MemoryPool::conn_type;

  ALockHandle(MemoryPool::Peer self, MemoryPool& lock_pool, int worker_id);

  absl::Status Init();

  bool inline IsLocked();
  void Lock(remote_ptr<ALock> alock);
  void Unlock(remote_ptr<ALock> alock);

private:  
  bool IsRTailLocked();
  bool IsLTailLocked();
  bool IsRemoteVictim();
 
  bool LockRemoteMcsQueue();
  void RemoteLock();
  bool LockLocalMcsQueue();
  void LocalLock();
  void RemoteUnlock();
  void LocalUnlock();

  void Reacquire();

  //! ALL OF THIS IS THREAD LOCAL AND APPLIES TO ONE LOCKING OPERATION ---> NEED TO RETHINK SOME THINGS
  bool is_local_; //resued for each call to lock for easy check on whether worker is local to key we are attempting to lock
  
  MemoryPool::Peer self_;
  int worker_id_;
  MemoryPool &pool_; // pool of alocks that the handle is local to (initalized in cluster/node_impl.h)

  //Pointer to alock to allow it to be read/write via rdma
  remote_ptr<ALock> a_lock_pointer_;
  volatile ALock *a_lock_;

  // TODO: NOT SURE ITS NECESSARY TO STORE ALL THIS STUFF SINCE WE KNOW THE OFFSETS....
  // Access to fields remotely
  remote_ptr<remote_ptr<RemoteDescriptor>> r_tail_;
  remote_ptr<remote_ptr<LocalDescriptor>> r_l_tail_;
  remote_ptr<uint64_t> r_victim_;

  // Access to fields locally
  local_ptr<RemoteDescriptor*> l_r_tail_;
  local_ptr<LocalDescriptor*> l_l_tail_;
  local_ptr<uint64_t*> l_victim_;
  
  // Prealloc used for rdma writes of rdma descriptor in RemoteUnlock
  remote_ptr<remote_ptr<RemoteDescriptor>> prealloc_;

  // Pointers to pre-allocated descriptor to be used locally
  remote_ptr<LocalDescriptor> l_desc_pointer_;
  LocalDescriptor l_desc_;

  // Pointers to pre-allocated descriptor to be used remotely
  remote_ptr<RemoteDescriptor> r_desc_pointer_;
  volatile RemoteDescriptor* r_desc_;

  
};

} // namespace X
