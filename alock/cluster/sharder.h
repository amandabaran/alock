#pragma once

#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/common.h"

namespace X {

// Class that determines which node a given key is on based on the range it falls into
class Sharder {
 public:
  explicit Sharder(const ClusterProto& cluster) {
    for (auto n : cluster.nodes()) {
      shards_.emplace(n.range().low(), n.nid());
    }
  }

  uint32_t GetShard(const key_type& key) {
    auto iter =  shards_.lower_bound(key);
    ROME_ASSERT(iter != shards_.end(), "Failed to lookup shard for key {}", key);
    return iter->second;
  }

 private:
  std::map<key_type, uint32_t, std::greater<key_type>> shards_;
};

}  // namespace X
