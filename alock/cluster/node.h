#pragma once

#include <cstdint>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "alock/cluster/cluster.pb.h"
#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rome/rdma/rdma_memory.h"
#include "common.h"
#include "lock_table.h"


namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

template <typename K, typename V>
class Node {
  using key_type = K; // some int (uint16)
  using lock_type = V; // ALock
  using MemoryPool = rome::rdma::MemoryPool;
  using root_type = remote_ptr<lock_type>;
  using root_map = std::map<uint32_t, root_type>;
  using key_map = std::map<uint32_t, std::pair<key_type, key_type>>;
  
 public:
  ~Node();
  Node(const NodeProto& self, std::vector<MemoryPool::Peer> others, const ClusterProto& cluster, const ExperimentParams& params);

  absl::Status Connect();

  absl::Status Prefill(const key_type& min_key, const key_type& max_key);

  absl::Status FillKeyRangeMap();

  LockTable<K, V>* GetLockTable() { return &lock_table_; }

  key_map* GetKeyRangeMap() { return &key_range_map_; }

  root_map* GetRootPtrMap() { return &root_ptrs_; }

  MemoryPool* GetLockPool(){ return &lock_pool_; }

 private:
  const NodeProto self_;
  std::vector<MemoryPool::Peer> others_;
  const ClusterProto cluster_;
  const ExperimentParams params_;
  bool prefill_;

  MemoryPool lock_pool_;
  LockTable<K,V> lock_table_;
  root_type root_lock_ptr_;
  key_map key_range_map_;
  root_map root_ptrs_;
};

}  // namespace X å

#include "node_impl.h"