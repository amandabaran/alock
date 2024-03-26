#pragma once

#include <algorithm>

#include "experiment.h"

inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }

#define CACHELINE_SIZE 64

auto calcThreadKeyRange(const BenchmarkParams &params, uint16_t node_id){
  int total_keys = params.max_key - params.min_key + 1;
  int keys_per_node = total_keys / params.node_count;
  int keys_per_thread = total_keys / (params.node_count * params.thread_count);
  auto nodeid = ceil(node_id/params.thread_count);
  auto node_low = ((nodeid -1) * keys_per_node) + 1;
  auto low = ((node_id -1) * keys_per_thread) + node_low;
  auto high = low + keys_per_thread - 1;

  // return pair of key range to be populated by thread
  auto result = std::make_pair(low, high);
  return result;
}

auto calcLocalNodeRange(const BenchmarkParams &params, uint16_t node_id){
  int total_keys = params.max_key - params.min_key + 1;
  int keys_per_node = total_keys / params.node_count;

  //determine cloud lab physcial node id
  auto nodeid = ceil(node_id/params.thread_count);
  auto low = ((nodeid -1) * keys_per_node) + 1;
  auto high = low + keys_per_node - 1;

  // return pair of node's entire local key range
  auto result = std::make_pair(low, high);
  return result;
}

