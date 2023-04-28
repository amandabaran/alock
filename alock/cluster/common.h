#pragma once

#include <string>

#include "rome/logging/logging.h"
#include "alock/cluster/cluster.pb.h"

namespace X {

using key_type = uint64_t;
// using value_type = uint64_t;

static constexpr uint32_t kLockPoolSize = 1 << 22;

}  // namespace X