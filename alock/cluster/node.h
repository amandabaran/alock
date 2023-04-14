#pragma once

#include <cstdint>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/common.h"
#include "alock/datastore.h"

namespace X {

template <typename K, typename V>
class Node {
  using key_type = K; // some int (uint16)
  using lock_type = V; // ALock
  using MemoryPool = rome::rdma::MemoryPool;
  using root_type = remote_ptr<lock_type>;
  using 

 public:
  using ds_type = std::vector<lock_type>;
  ~Node();
  Node(const NodeProto& self, const ClusterProto& cluster, 
        uint32_t num_threads = 1);

  absl::Status CreateALock();

  // absl::Status Prefill(const key_type& min_key, const key_type& max_key);

  ds_type* GetDatastore() { return ds_.get(); }

  static absl::StatusOr<ResultProto> Run(const ExperimentParams &experiment_params, volatile bool *done);
  
 private:
  std::unique_ptr<MemoryPool> pool_;
  std::unique_ptr<ds_type> ds_;

  const ClusterProto cluster_;
  const NodeProto self_;

  Sharder sharder_;

  std::random_device rd_;
  std::default_random_engine rand_;

  struct node_ctx_t {
    std::unique_ptr<root_type> lock_root;
    MemoryPool::conn_type* conn;
  };
  std::map<uint32_t, node_ctx_t> peers_ctx_;

  uint32_t num_threads_;
  // std::vector<std::thread> threads_;
};

}  // namespace X

#include "server_impl.h"