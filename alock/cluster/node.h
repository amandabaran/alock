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
#include "sharder.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;

template <typename K, typename V>
class Node {
  using key_type = K; // some int (uint16)
  using lock_type = V; // ALock
  using MemoryPool = rome::rdma::MemoryPool;
  using root_type = remote_ptr<lock_type>;
  
 public:
  using ds_type = std::vector<key_type>;

  ~Node();
  Node(const NodeProto& self, const ClusterProto& cluster, 
        uint32_t num_threads, bool prefill);

  absl::Status Prefill(const key_type& min_key, const key_type& max_key);

  ds_type* GetDatastore() { return keys_.get(); }

 private:
  std::unique_ptr<MemoryPool> lock_pool_;

  // index of key is used to find correct offset into lock_pool_
  std::unique_ptr<ds_type> keys_;
  remote_ptr<lock_type> root_lock_ptr_;

  const ClusterProto cluster_;
  const NodeProto self_;

  Sharder sharder_;

  struct node_ctx_t {
    std::unique_ptr<root_type> lock_root;
    // ! Not really sure below is needed?
    MemoryPool::conn_type* conn;
  };
  std::map<uint32_t, node_ctx_t> peers_ctx_;

  uint32_t num_threads_;
  // std::vector<std::thread> threads_;
};

}  // namespace X