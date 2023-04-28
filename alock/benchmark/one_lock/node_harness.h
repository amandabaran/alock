#pragma once

#include <algorithm>
#include <future>
#include <limits>
#include <random>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "alock/benchmark/one_lock/experiment.pb.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rome/util/clocks.h"
#include "rome/util/distribution_util.h"
#include "alock/cluster/node.h"
#include "alock/cluster/lock_table.h"
#include "alock/cluster/common.h"

#include "setup.h"

using ::rome::rdma::MemoryPool;

struct Op {
  key_type key; 
};
// TODO: uniform int stream isn't working right now
// std::make_unique<rome::RandomDistributionStream<std::uniform_int_distribution<key_type>, key_type, key_type>>(0, 100)
class NodeHarness : public rome::ClientAdaptor<rome::NoOp>  {
 using LockTable = X::LockTable<key_type, LockType>;

 public:

  ~NodeHarness() = default;

  static std::unique_ptr<NodeHarness> Create(
      const Peer &self, const std::vector<Peer> &peers, std::shared_ptr<X::Node<X::key_type, LockType>> node,
      const X::NodeProto& node_proto, ExperimentParams params, std::barrier<> *barrier) {
    return std::unique_ptr<NodeHarness>(
        new NodeHarness(self, peers, node, node_proto, params, barrier));
  }

  static void signal_handler(int signal) { 
    ROME_INFO("SIGNAL: ", signal, " HANDLER!!!\n");
    exit(signal);
  }


  // absl::StatusOr<ResultProto> Launch(volatile bool* done, ExperimentParams experiment_params) {

  //   std::vector<std::thread> threads;
  //   for (int i = 0; i < experiment_params.num_threads(); i++){
  //     threads.push_back(std::thread(&NodeHarness::Run, this, experiment_params, done));
  //   }

  //   // Wait for all threads to finish
  //   for (unsigned int i=0; i<threads.size(); ++i) {
  //       if (threads[i].joinable()) {
  //           threads.at(i).join();
  //           ROME_DEBUG("Joined thread {}\n.", i);
  //       }
  //   }
  //   // Make sure all results are collected
  //   // std::for_each(results_.begin(), results_.end(),
  //   //               [](auto& result) { result.wait(); });

  //   for (auto result_or : results_) {
  //     if (!result_or.ok()) {
  //       ROME_ERROR("{}", result_or.status().message());
  //     } else {
  //       result_protos_.push_back(result_or.value());
  //     }
  //   }

  //   ROME_DEBUG("Waiting for nodes to disconnect (in destructor)...");
  //   node_.reset();
  //   return absl::OkStatus();
  // }

  std::vector<ResultProto> GetResults() { return result_protos_; }

  static absl::StatusOr<ResultProto>
  Run(std::unique_ptr<NodeHarness> harness, const ExperimentParams& experiment_params, volatile bool* done) {
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

    auto* harness_ptr = harness.get();

    // Create and start the workload driver
    auto driver = rome::WorkloadDriver<rome::NoOp>::Create(
        std::move(harness), std::make_unique<rome::NoOpStream>(),
        qps_controller.get(),
        std::chrono::milliseconds(experiment_params.sampling_rate_ms()));
    ROME_ASSERT_OK(driver->Start());

    // Sleep while driver is running then stop it.
    if (experiment_params.workload().has_runtime() && experiment_params.workload().runtime() > 0) {
      ROME_INFO("Running workload for {}s", experiment_params.workload().runtime());
      auto runtime = std::chrono::seconds(experiment_params.workload().runtime());
      std::this_thread::sleep_for(runtime);
    } else {
      ROME_INFO("Running workload indefinitely");
      while (!(*done)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
    ROME_INFO("Stopping NodeHarness...");
    ROME_ASSERT_OK(driver->Stop());

    // Output results.
    ResultProto result;
    result.mutable_node()->CopyFrom(harness_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());

    // Sleep for a hot sec to let the node receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // results_.at(std::this_thread::get_id()) = result; 
    return result;
  }

  absl::Status Start() override {
    ROME_INFO("Starting NodeHarness...");
    auto status = lock_handle_.Init(peers_);
    ROME_ASSERT_OK(status);
    barrier_->arrive_and_wait();
    return status;
  }

  absl::Status Apply(const rome::NoOp &op) override {
    // ROME_DEBUG("Attempting to lock key {}", op);
    X::remote_ptr<LockType> lock_addr = lock_table_->GetLock();
    ROME_DEBUG("Address for lock is {}", static_cast<uint64_t>(lock_addr));
    ROME_DEBUG("Locking...");
    lock_handle_.Lock(lock_addr);
    auto start = util::SystemClock::now();
    if (params_.workload().has_think_time_ns()) {
      while (util::SystemClock::now() - start <
             std::chrono::nanoseconds(params_.workload().think_time_ns()))
       ;
    }
    ROME_DEBUG("Unlocking...");
    lock_handle_.Unlock(lock_addr);
    return absl::OkStatus();
  }

    
  absl::Status Stop() override {
    ROME_DEBUG("Stopping...");
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  X::NodeProto ToProto() { return node_proto_; }

 private:
  NodeHarness(const Peer &self, const std::vector<Peer> &peers, std::shared_ptr<X::Node<X::key_type, LockType>> node,
                const X::NodeProto& node_proto, ExperimentParams params, std::barrier<> *barrier)
      : node_(node),
        node_proto_(node_proto),
        self_(self),
        peers_(peers),
        params_(params),
        barrier_(barrier),
        desc_pool_(self, std::make_unique<cm_type>(self.id)),
        lock_handle_(self, *(node_->GetLockPool())) {

          lock_table_ = node_->GetLockTable();
        }

  std::shared_ptr<X::Node<X::key_type, LockType>> node_;
  const X::NodeProto& node_proto_;

  const Peer self_;
  std::vector<Peer> peers_;

  ExperimentParams params_;

  std::barrier<> *barrier_;

  std::vector<absl::StatusOr<ResultProto>> results_;
  std::vector<ResultProto> result_protos_;

  MemoryPool desc_pool_; //NodeHarnesss share descriptor pool

  LockTable* lock_table_;
  // thread_local static //not sure if needed for lockhandle
  LockHandle lock_handle_; //Handle to interact with descriptors, local per NodeHarness

  // For generating a random key to lock if stream doesnt work
  std::random_device rd_;
  std::default_random_engine rand_;
};

