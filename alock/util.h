#pragma once

#define CACHELINE_SIZE 64

template <typename T>
using atomic_ptr = std::atomic<T>*;