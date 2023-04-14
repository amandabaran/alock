#pragma once

#include <barrier>
#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/clocks.h"
#include "setup.h"

namespace X {

using ::rome::rdma::MemoryPool;

template <typename K, typename V>
Node<K, V>::~Node(){
  //Disconnect ? shutdown?
}

template <typename K, typename V>
Node<K, V>::Node(const NodeProto& self, const ClusterProto& cluster, uint32_t num_threads)
    : self_(self),
      cluster_(cluster),
      sharder_(cluster),
      num_threads_(num_threads_),
      rd_(),
      rand_(rd_()) {
  auto nid = self.node().nid();
  auto name = self.node().name();
  auto port = self.node().port();

  auto peer = MemoryPool::Peer(nid, name, port);
  auto cm = std::make_unique<MemoryPool::cm_type>(peer.id);
  pool_ = std::make_unique<MemoryPool>(peer, std::move(cm));
}

template <typename K, typename V>
void Node<K, V>::Connect() {
  // Init `MemoryPool` for ALocks
  ROME_ASSERT_OK(pool_->Init(kPoolSize, peers));

  // TODO: use this to extend to multiple locks
  // ds_ = std::make_unique<ds_type>(server.range().low(), server.range().high(),
  //                                   pool_.get());
  // ROME_DEBUG("Root at {}", ds_->GetRemotePtr());

  RemoteObjectProto proto;
  auto a_lock_pointer_ = pool_.Allocate<ALock>();
  proto.set_raddr(a_lock_pointer_.address());
  ds_.emplace(a_lock_pointer);

  ROME_DEBUG("Self Lock pointer {:x}", static_cast<uint64_t>(a_lock_pointer_));

  // tell all the peers where to find the addr of the first lock
  for (auto n : cluster_.nodes()) {
    // Dont need to send lock/get lock from self
    if (n.node().nid() == self_.node().nid()) { continue; }

    // Send all peers the root of the alock on self
    auto conn_or = pool_.connection_manager()->GetConnection(n.node().nid());
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    status = conn_or.value()->channel()->Send(proto);
    ROME_CHECK_OK(ROME_RETURN(status), status);

    // Wait until roots of all other alocks on other nodes are shared
    auto conn_or = desc_pool_->connection_manager()->GetConnection(host.id);
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    while (got.status().code() == absl::StatusCode::kUnavailable) {
      got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
    }
    ROME_CHECK_OK(ROME_RETURN(got.status()), got);
    // set lock pointer to the base address of the lock on the host
    a_lock_pointer_ = decltype(a_lock_pointer_)(host.id, got->raddr());
    ROME_DEBUG("Node {} Lock pointer {:x}", n.node().nid(), static_cast<uint64_t>(a_lock_pointer_));

    peers_ctx_.emplace(
        n.node().nid(),
        node_ctx_t{std::move(a_lock_pointer), conn_or});
  }
  return absl::OkStatus();
}

// TODO: extend to multiple locks
// template <typename K, typename V>
// absl::Status Node<K, V>::Prefill(const key_type& min_key,
//                                    const key_type& max_key) {
//   ROME_INFO("Prefilling... [{}, {})", min_key, max_key);
//   auto target_fill = (max_key - min_key) / 2;

//   std::random_device rd;
//   std::default_random_engine rand(rd());
//   std::uniform_int_distribution<key_type> keys(min_key, max_key);

//   using std::chrono::system_clock;
//   auto last = system_clock::now();
//   size_t count = 0;
//   while (count < target_fill) {
//     auto k = keys(rand);
//     auto v = pool.Allocate<ALock>();
//     auto success = ds_->Insert(k, v);
//     if (success) ++count;

//     auto curr = system_clock::now();
//     if (curr - last > std::chrono::seconds(1)) {
//       ROME_INFO("Inserted {}/{} elements", count, target_fill);
//       last = curr;
//     }
//   }
//   ROME_INFO("Finished prefilling");

//   return absl::OkStatus();
// }

  static absl::StatusOr<ResultProto>
  Run(std::unique_ptr<Client> client, const ExperimentParams &experiment_params,
      volatile bool *done) {
    //Signal Handler
    signal(SIGINT, signal_handler);
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller;
    if (experiment_params.has_max_qps() && experiment_params.max_qps() > 0) {
      qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(
              experiment_params.max_qps());
    }

    auto *client_ptr = client.get();

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<rome::NoOp>::Create(
        std::move(client), std::make_unique<rome::NoOpStream>(),
        qps_controller.get(),
        std::chrono::milliseconds(experiment_params.sampling_rate_ms()));
    ROME_ASSERT_OK(driver->Start());
    // if (!(driver->Start()).ok()){
    //   ROME_INFO("ABORT!!\n");
    //   raise(SIGINT);
    // }
    
    // NOT WORKING PROPERLY RN
    // // Sleep while driver is running then stop it.
    if (experiment_params.workload().has_runtime() &&
        experiment_params.workload().runtime() > 0) {
      ROME_INFO("Running workload for {}s", experiment_params.workload().runtime());
      auto runtime = std::chrono::seconds(experiment_params.workload().runtime());
      std::this_thread::sleep_for(runtime);
    } else {
      ROME_INFO("Running workload indefinitely");
      while (!(*done)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
    // Do this instead of above stuff but also doesnt solve issue?
    // std::this_thread::sleep_for(std::chrono::seconds(10));
    ROME_INFO("Stopping client...");
    ROME_ASSERT_OK(driver->Stop());

    // Output results.
    ResultProto result;
    result.mutable_experiment_params()->CopyFrom(experiment_params);
    result.mutable_client()->CopyFrom(client_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());

    // Sleep for a hot sec to let the server receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return result;
  }

  absl::Status Start() override {
    ROME_INFO("Starting client...");
    auto status = lock_.Init(host_, peers_);
    ROME_ASSERT_OK(status); //abort if init doesn't complete properly
    barrier_->arrive_and_wait(); //waits for all cliens to get lock Initialized, addr from host
    return status;
  }

  absl::Status Apply(const rome::NoOp &op) override {
    ROME_DEBUG("Locking...");
    lock_.Lock();
    auto start = util::SystemClock::now();
    if (experiment_params_.workload().has_think_time_ns()) {
      while (util::SystemClock::now() - start <
             std::chrono::nanoseconds(experiment_params_.workload().think_time_ns()))
       ;
    }
    ROME_DEBUG("Unlocking...");
    lock_.Unlock();
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    ROME_DEBUG("Stopping...");
    // Announce done.
    auto conn = pool_.connection_manager()->GetConnection(host_.id);
    ROME_CHECK_OK(ROME_RETURN(util::InternalErrorBuilder()
                              << "Failed to retrieve server connection"),
                  conn);
    auto e = AckProto();
    auto sent = conn.value()->channel()->Send(e);

    // Wait for all other clients.
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  NodeProto ToProto() {
    NodeProto client;
    *client.mutable_private_hostname() = self_.address;
    client.set_nid(self_.id);
    client.set_port(self_.port);
    return client;
  }

private:
  Node(const Peer &self, const std::vector<Peer> &peers,
         const ExperimentParams &experiment_params, std::barrier<> *barrier)
      : experiment_params_(experiment_params), self_(self), peers_(peers), barrier_(barrier),
        pool_(self_, std::make_unique<cm_type>(self.id)), lock_(self_, pool_) {}

  ExperimentParams experiment_params_;

  const Peer self_;
  std::vector<Peer> peers_;
  std::barrier<> *barrier_;

  MemoryPool pool_;
  LockType lock_;

} //namespace X