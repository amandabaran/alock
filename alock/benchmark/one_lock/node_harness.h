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

class Worker : public rome::ClientAdaptor<key_type> {
  
 public:
  static std::unique_ptr<Worker> Create(LockTable* lt, MemoryPool& pool, const X::NodeProto& node, const Peer &self,
                                        int worker_id, const ExperimentParams& params, std::barrier<>* barrier, key_map* kr_map, root_map* root_ptrs) {
    return std::unique_ptr<Worker>(new Worker(lt, pool, node, self, worker_id, params, barrier, kr_map, root_ptrs));
  }

  static absl::StatusOr<ResultProto> Run(
      std::unique_ptr<Worker> worker, const ExperimentParams& experiment_params,
      volatile bool* done) {
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller;
    if (experiment_params.has_max_qps() && experiment_params.max_qps() > 0) {
      qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(
              experiment_params.max_qps());
    }

    auto* worker_ptr = worker.get();

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<key_type>::Create(
        std::move(worker), CreateOpStream(worker_ptr->params_),
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
    ROME_INFO("Stopping worker...");
    ROME_ASSERT_OK(driver->Stop());

    // Output results.
    ResultProto result;
    result.mutable_node()->CopyFrom(worker_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());
    return result;
    // Sleep for a hot sec to let the node receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  X::remote_ptr<LockType> CalcLockAddr(const key_type &key){
    auto min_key = node_.range().low();
    auto max_key = node_.range().high();
    if (key > max_key || key < min_key){
      // find which node key belongs to
      for(const auto &elem : *key_range_map_) {
        auto nid = elem.first;
        auto min_key = elem.second.first;
        auto max_key = elem.second.second; 
        if (key > max_key) continue;
        if (key <= max_key && key > min_key){
          // get root lock pointer of correct node
          root_type root_ptr = root_ptrs_->at(nid);
          // calculate address of desired key and return 
          auto diff = key - min_key;
          auto bytes_to_jump = lock_byte_size_ * diff;
          auto temp_ptr = rome::rdma::remote_ptr<uint8_t>(root_ptr);
          temp_ptr -= bytes_to_jump;
          auto lock_ptr = root_type(temp_ptr);
          return lock_ptr;
        } else if (key == min_key){
          return root_ptrs_->at(nid);
        }
      }
    } else if (key >= min_key && key <= max_key){
        // calculate the address of the desired key
        auto diff = key - min_key;
        auto bytes_to_jump = lock_byte_size_ * diff;
        auto temp_ptr = rome::rdma::remote_ptr<uint8_t>(root_lock_ptr_);
        temp_ptr -= bytes_to_jump;
        auto lock_ptr = root_type(temp_ptr);
        return lock_ptr;
    }
    ROME_DEBUG("ERROR - COULD NOT FIND KEY: {}", key);
    return X::remote_nullptr;
  }

  absl::Status Start() override {
    ROME_INFO("Starting NodeHarness...");
    root_lock_ptr_ = root_ptrs_->at(self_.id);
    auto status = lock_handle_.Init();
    ROME_ASSERT_OK(status);
    barrier_->arrive_and_wait();
    return status;
  }

  absl::Status Apply(const key_type &op) override {
    ROME_DEBUG("Attempting to lock key {}", op);
    X::remote_ptr<LockType> lock_addr = CalcLockAddr(op);
    ROME_DEBUG("Address for lock is {:x}", static_cast<uint64_t>(lock_addr));
    lock_handle_.Lock(lock_addr);
    auto start = util::SystemClock::now();
    if (params_.workload().has_think_time_ns()) {
      while (util::SystemClock::now() - start <
             std::chrono::nanoseconds(params_.workload().think_time_ns()))
       ;
    }
    ROME_DEBUG("Unlocking key {}...", op);
    lock_handle_.Unlock(lock_addr);
    return absl::OkStatus();
  }

    
  absl::Status Stop() override {
    ROME_DEBUG("Stopping...");
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  X::NodeProto ToProto() { return node_; }

 private:
  Worker(LockTable* lt, MemoryPool& pool, const X::NodeProto& node, const Peer &self, 
         int worker_id, const ExperimentParams& params, std::barrier<>* barrier, key_map* kr_map, root_map* root_ptrs)
      : lock_table_(lt), 
        node_(node), 
        self_(self), 
        params_(params), 
        barrier_(barrier), 
        lock_handle_(self, pool, worker_id), 
        key_range_map_(kr_map), 
        root_ptrs_(root_ptrs) {}

  LockTable* lock_table_;
  const X::NodeProto node_;
  const Peer self_;
  const ExperimentParams params_;
  std::barrier<>* barrier_;

  LockHandle lock_handle_; //Handle to interact with descriptors, one per worker
  key_map* key_range_map_;
  root_map* root_ptrs_;
  root_type root_lock_ptr_;
  
  // For generating a random key to lock if stream doesnt work
  std::random_device rd_;
  std::default_random_engine rand_;
};

class NodeHarness {

 public:

  ~NodeHarness() = default;

  static std::unique_ptr<NodeHarness> Create(
      const Peer &self, const std::vector<Peer> &peers, std::unique_ptr<X::Node<X::key_type, LockType>> node,
      const X::NodeProto& node_proto, ExperimentParams params) {
    return std::unique_ptr<NodeHarness>(
        new NodeHarness(self, peers, std::move(node), node_proto, params));
  }

  absl::Status Launch(volatile bool* done, ExperimentParams experiment_params) {
    std::vector<std::unique_ptr<Worker>> workers;
    for (auto i = 0; i < experiment_params.num_threads(); ++i) {
      ROME_DEBUG("Creating Worker {} on Node {}", i, self_.id);
      workers.emplace_back(
          Worker::Create(node_->GetLockTable(), *(node_->GetLockPool()), node_proto_, self_, i, params_, &barrier_, node_->GetKeyRangeMap(), node_->GetRootPtrMap()));
    }

    std::for_each(workers.begin(), workers.end(), [&](auto& worker) {
      results_.emplace_back(std::async([&]() {
        return Worker::Run(std::move(worker), experiment_params, done);
      }));
    });

    std::for_each(results_.begin(), results_.end(),
                  [](auto& result) { result.wait(); });

    for (auto& r : results_) {
      auto result_or = r.get();
      if (!result_or.ok()) {
        ROME_ERROR("{}", result_or.status().message());
      } else {
        result_protos_.push_back(result_or.value());
      }
    }

    ROME_DEBUG("Waiting for clients to disconnect (in destructor)...");
    node_.reset();
    return absl::OkStatus();
  }

  std::vector<ResultProto> GetResults() { return result_protos_; }

  X::NodeProto ToProto() { return node_proto_; }

 private:
  NodeHarness(const Peer &self, const std::vector<Peer> &peers, std::unique_ptr<X::Node<X::key_type, LockType>> node,
                const X::NodeProto& node_proto, ExperimentParams params)
      : self_(self),
        peers_(peers),
        node_(std::move(node)),
        node_proto_(node_proto),
        params_(params),
        barrier_(params.num_threads()) {}

  std::unique_ptr<X::Node<X::key_type, LockType>> node_;
  const X::NodeProto& node_proto_;

  const Peer self_;
  std::vector<Peer> peers_;

  ExperimentParams params_;

  std::barrier<> barrier_;

  std::vector<std::future<absl::StatusOr<ResultProto>>> results_;
  std::vector<ResultProto> result_protos_;
};

