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
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/clocks.h"
#include "rome/util/distribution_util.h"
#include "alock/cluster/node.h"

#include "setup.h"

class Worker : public rome::ClientAdaptor<rome::NoOp>  {
  using LockTable = X::LockTable<X::key_type, LockType>;

 public:
  static std::unique_ptr<Worker> Create(LockTable* ds, const X::NodeProto& node_proto,
                                        const ExperimentParams& params,
                                        std::barrier<>* barrier) {
    return std::unique_ptr<Worker>(new Worker(ds, node_proto, params, barrier));
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

    // Create and start the workload driver
    auto driver = rome::WorkloadDriver<rome::NoOp>::Create(
        std::move(worker), std::make_unique<rome::NoOpStream>(),
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
    result.mutable_worker()->CopyFrom(worker_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());

    // Sleep for a hot sec to let the node receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return result;
  }

  absl::Status Start() override {
    // ds_->RegisterThisThread();
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  absl::Status Apply(const rome::NoOp &op) override {
    ROME_DEBUG("Locking...");
    lock_handle_.Lock();
    auto start = util::SystemClock::now();
    if (experiment_params_.workload().has_think_time_ns()) {
      while (util::SystemClock::now() - start <
             std::chrono::nanoseconds(experiment_params_.workload().think_time_ns()))
       ;
    }
    ROME_DEBUG("Unlocking...");
    lock_handle_.Unlock();
    return absl::OkStatus();
  }

    
  absl::Status Stop() override {
    // ds_->DelistThisThread();
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  X::NodeProto ToProto() { return node_proto_; }

 private:
  Worker(LockTable* ds, const X::NodeProto& node_proto, const ExperimentParams& params,
         std::barrier<>* barrier)
      : lock_table_(ds), node_proto_(node_proto), experiment_params_(params), barrier_(barrier), lock_handle_() {}

  LockTable* lock_table_;
  const X::NodeProto node_proto_;
  const ExperimentParams experiment_params_;
  std::barrier<>* barrier_;
  LockType lock_handle_; //Handle to interact with descriptors, local per worker

  std::random_device rd_;
  std::default_random_engine rand_;
};

class NodeHarness {
 public:
  ~NodeHarness() = default;

  static std::unique_ptr<NodeHarness> Create(
      std::unique_ptr<X::Node<X::key_type, LockType>> node,
      const X::NodeProto& node_proto, ExperimentParams params) {
    return std::unique_ptr<NodeHarness>(
        new NodeHarness(std::move(node), node_proto, params));
  }

  absl::Status Launch(volatile bool* done, ExperimentParams experiment_params) {
    std::vector<std::unique_ptr<Worker>> workers;
    for (auto i = 0; i < experiment_params.num_threads(); ++i) {
      workers.emplace_back(
          Worker::Create(node_->GetLockTable(), node_proto_, params_, &barrier_));
    }

    std::for_each(workers.begin(), workers.end(), [&](auto& worker) {
      results_.emplace_back(std::async([&]() {
        return Worker::Run(std::move(worker), experiment_params, done);
      }));
    });

    // Wait for all worker threads to finish
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

 private:
  NodeHarness(std::unique_ptr<X::Node<X::key_type, LockType>> node,
                const X::NodeProto& node_proto, ExperimentParams params)
      : node_(std::move(node)),
        node_proto_(node_proto),
        params_(params),
        barrier_(params.num_threads()) {}

  std::unique_ptr<X::Node<X::key_type, LockType>> node_;
  const X::NodeProto& node_proto_;
  ExperimentParams params_;

  std::barrier<> barrier_;
  std::vector<std::future<absl::StatusOr<ResultProto>>> results_;
  std::vector<ResultProto> result_protos_;
};

