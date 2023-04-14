#pragma once

#include "alock/cluster/cluster.pb.h"
#include "alock/cluster/common.h"

namespace X {

// Class that determines which node a given key is on based on the range it falls into
template <typename K, typename V>
class Datastore {
 public:
  Datastore(const ClusterProto& cluster) {
    for (auto n : cluster.nodes()) {
      shards_.emplace(n.range().low(), n.node().nid());
    }
  }

  template <typename K, typename V>
  Datastore<K, V>::Insert(K key, V lock){
    // TODO: check that this is storing the address of the locks
    map_.insert({key, lock});
  }

 private:
  std::unordered_map<K, std::unique_ptr<V>> map_;

};

}  // namespace X
