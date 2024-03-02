#pragma once

#define CACHELINE_SIZE 64

template <typename T>
using atomic_ptr = std::atomic<T>*;

// Used to generate a pseudo-random number.
// XorShift is supposedly the fasted method to generate random numbers.
// (see https://en.wikipedia.org/wiki/Xorshift)
class XorShift64 {
public:
    explicit XorShift64(uint64_t seed) : state(seed) {}

    // Generate a random 64-bit unsigned integer
    uint64_t next() {
        state ^= (state << 13);
        state ^= (state >> 7);
        state ^= (state << 17);
        // state ^= (state << 21);
        // state ^= (state >> 35);
        // state ^= (state << 4);
        return state;
    }

    // Generate a random float in the range [0, 1)
    float nextFloat() {
        // Convert the 64-bit integer to a floating-point number in the range [0, 1)
        return static_cast<float>(next()) / std::numeric_limits<uint64_t>::max();
    }

    // Generate a random key within the provided range
    uint64_t nextKey(uint64_t min, uint64_t range_size) {
        auto mapped = next() % range_size;
        return static_cast<uint64_t>(mapped) + min;
    }

private:
    uint64_t state;
};
