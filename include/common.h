inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }

using key_type = uint64_t;

#define CACHELINE_SIZE 64
