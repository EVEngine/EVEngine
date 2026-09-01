#pragma once

/**
 * @file Scheduling.h
 * @brief Shared metadata for independently deployable command and work queues.
 */

#include <cstdint>
#include <string>

namespace eve::scheduling {

/** @brief Identity, priority and terminal explanation shared by scheduled items. */
struct ItemMetadata {
    std::string id;
    int         priority = 0;
    std::string reason;
};

/** @brief Sequence and explanation shared by queue lifecycle events. */
struct EventMetadata {
    std::uint64_t sequence = 0;
    std::string   reason;
};

}  // namespace eve::scheduling
