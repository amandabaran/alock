#pragma once

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "rome/rdma/memory_pool/memory_pool.h"
#include "../../util.h"

namespace X {

using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;

static constexpr uint32_t kInitBudget = 5;
static constexpr uint32_t kPoolSize = 22;

#define LOCAL_VICTIM  0
#define REMOTE_VICTIM 1

// Used for tracking status of descriptors by workers
#define FREE_DESC 0
#define IN_USE_DESC 1

#define NEXT_PTR_OFFSET 32
#define DESC_PTR_OFFSET 16
#define VICTIM_OFFSET 32

struct alignas(64) RemoteDescriptor {
    int8_t budget{-1};
    uint8_t pad1[NEXT_PTR_OFFSET - sizeof(budget)];
    remote_ptr<RemoteDescriptor> next{0};
    uint8_t pad2[CACHELINE_SIZE - NEXT_PTR_OFFSET - sizeof(next)];
};
static_assert(alignof(RemoteDescriptor) == CACHELINE_SIZE);
static_assert(sizeof(RemoteDescriptor) == CACHELINE_SIZE);

struct alignas(64) LocalDescriptor {
    int8_t budget{-1}; //budget == -1 indicates its locked, unlocked and passed off when it can proceed to critical section
    uint8_t pad1[NEXT_PTR_OFFSET - sizeof(budget)];
    LocalDescriptor* next{nullptr};
    uint8_t pad2[CACHELINE_SIZE - NEXT_PTR_OFFSET - sizeof(next)];
};
static_assert(alignof(LocalDescriptor) == CACHELINE_SIZE);
static_assert(sizeof(LocalDescriptor) == CACHELINE_SIZE);

struct alignas(64) ALock {
    // pointer to the pointer of the remote tail
    remote_ptr<RemoteDescriptor> r_tail;
    // pad so local tail starts at addr+16
    uint8_t pad1[DESC_PTR_OFFSET - sizeof(r_tail)]; 
    // pointer to the local tail
    remote_ptr<LocalDescriptor> l_tail; 
    // pad so victim starts at addr+32
    uint8_t pad2[VICTIM_OFFSET - DESC_PTR_OFFSET - sizeof(l_tail)]; 
    // node id of the victim
    uint64_t victim; 
    uint8_t pad3[CACHELINE_SIZE - VICTIM_OFFSET - sizeof(victim)]; 
};

static_assert(alignof(ALock) == CACHELINE_SIZE);
static_assert(sizeof(ALock) == CACHELINE_SIZE);

} //namespace X