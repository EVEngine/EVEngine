#pragma once

#include "common/Export.h"

namespace eve::boot {

/**
 * @brief Register the graphics-backend function that starts VkInstance creation.
 *
 * Graphics registers this at static-init. Cmdline then calls
 * startVulkanInstanceWarmup() at the beginning of `eve run` so instance
 * creation overlaps Runtime/script boot. No-op when graphics is trimmed.
 */
EVENGINE_API void registerVulkanInstanceWarmup(void (*start)());

/** @brief Kick off asynchronous VkInstance creation if a backend registered. */
EVENGINE_API void startVulkanInstanceWarmup();

}  // namespace eve::boot
