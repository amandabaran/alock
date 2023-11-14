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
#include "alock/src/cluster/node.h"
#include "alock/src/cluster/lock_table.h"
#include "alock/src/cluster/common.h"

#include "setup.h"

using ::rome::rdma::MemoryPool;

class Client : public rome::ClientAdaptor<key_type> {

 public:

  ~Client() = default;

  static std::unique_ptr<Client> Create(const Peer &self, const X::NodeProto& node_proto, const X::ClusterProto& cluster, ExperimentParams params, std::barrier<> *barrier,
          MemoryPool& pool, key_map* kr_map, root_map* root_ptr_map, std::unordered_set<int> locals) {
    return std::unique_ptr<Client>(new Client(self, node_proto, cluster, params, barrier, pool, kr_map, root_ptr_map, locals));
  }

  static void signal_handler(int signal) { 
    // this->Stop();
    // Wait for all clients to be done shutting down
    ROME_INFO("\nSIGNAL HANDLER\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    exit(signal);
  }

  static absl::StatusOr<ResultProto> Run(
      std::unique_ptr<Client> client, const ExperimentParams& experiment_params,
      volatile bool* done) {

    //Signal Handler
    signal(SIGINT, signal_handler);

    //Sleep for a second in case all clients aren't done connecting
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller;
    if (experiment_params.has_max_qps() && experiment_params.max_qps() > 0) {
      qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(
              experiment_params.max_qps());
    }

    auto *client_ptr = client.get();

    auto stream = CreateOpStream(experiment_params);

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<key_type>::Create(
        std::move(client), std::move(stream),
        qps_controller.get(),
        std::chrono::milliseconds(experiment_params.sampling_rate_ms()));
    ROME_ASSERT_OK(driver->Start());

    // Sleep while driver is running
    ROME_INFO("Running workload for {}s", experiment_params.workload().runtime());
    auto runtime = std::chrono::seconds(experiment_params.workload().runtime());
    std::this_thread::sleep_for(runtime + std::chrono::seconds(2));
    
    ROME_INFO("Stopping client {}...", client_ptr->self_.id);
    ROME_ASSERT_OK(driver->Stop());
    // Output results.
    ResultProto result;
    result.mutable_experiment_params()->CopyFrom(experiment_params);
    result.mutable_client()->CopyFrom(client_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());

    // Sleep for a hot sec to let the node receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return result; 
  }

  X::remote_ptr<LockType> CalcLockAddr(const key_type &key){
    auto min_key = node_proto_.range().low();
    auto max_key = node_proto_.range().high();
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
    ROME_ERROR("ERROR - COULD NOT FIND KEY: {}", key);
    return X::remote_nullptr;
  }

  absl::Status Start() override {
    ROME_DEBUG("Starting Client...");
    root_lock_ptr_ = root_ptrs_->at(self_.id);
    auto status = lock_handle_.Init();
    ROME_ASSERT_OK(status);
    barrier_->arrive_and_wait(); //waits for all clients to init lock handle
    return status;
  }

  absl::Status Apply(const key_type &op) override {
    // key_type k = 6;
    ROME_DEBUG("Client {} attempting to lock key {}", self_.id, op);    
    X::remote_ptr<LockType> lock_addr = CalcLockAddr(op);
    ROME_TRACE("Address for lock is {:x}", static_cast<uint64_t>(lock_addr));
    lock_handle_.Lock(lock_addr);
    auto start = util::SystemClock::now();
    if (params_.workload().has_think_time_ns()) {
      while (util::SystemClock::now() - start <
             std::chrono::nanoseconds(params_.workload().think_time_ns()))
       ;
    }
    ROME_TRACE("Client {} unlocking key {}...", self_.id, op);
    lock_handle_.Unlock(lock_addr);
    ROME_DEBUG("Unlocked key {}", op);
    return absl::OkStatus();
  }

    
  absl::Status Stop() override {
    std::vector<uint64_t> counts = lock_handle_.GetCounts();
    ROME_INFO("COUNTs: reaq: {}, local: {}, remote: {}", counts[0], counts[1], counts[2]);
    ROME_DEBUG("Stopping...");
    // Waits for all other co located clients (threads)
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  X::NodeProto ToProto() {  
    return node_proto_;
  }

 private:
  Client(const Peer &self, const X::NodeProto& node_proto, const X::ClusterProto& cluster, ExperimentParams params, std::barrier<> *barrier,
          MemoryPool& pool, key_map* kr_map, root_map* root_ptr_map, std::unordered_set<int> locals)
      : self_(self),
        node_proto_(node_proto),
        cluster_(cluster),
        params_(params),
        barrier_(barrier),
        pool_(pool),
        key_range_map_(kr_map), 
        root_ptrs_(root_ptr_map),
        local_clients_(locals),
        lock_handle_(self, pool_, locals, params.budget()), 
        root_lock_ptr_(root_ptrs_->at(self.id)) {}

  const Peer self_;
  const ExperimentParams params_;
  std::barrier<>* barrier_;

  MemoryPool& pool_;
  LockHandle lock_handle_; //Handle to interact with descriptors, one per worker
  key_map* key_range_map_;
  root_map* root_ptrs_;
  root_type root_lock_ptr_;
  std::unordered_set<int> local_clients_;
  
  // For generating a random key to lock if stream doesnt work
  std::random_device rd_;
  std::default_random_engine rand_;

  const X::NodeProto& node_proto_;
  const X::ClusterProto& cluster_; 
};

