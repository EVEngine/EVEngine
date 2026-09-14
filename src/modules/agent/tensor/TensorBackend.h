#pragma once
#include <memory>
#include "common/Result.h"

namespace eve::agent {
class IPolicyBackend;
class IGpuPolicyBackend;
/**
 * @brief Create an owning tensor eager CPU inference provider; caller owns registration.
 * @return Unique CPU-only provider; inputs/outputs follow IPolicyBackend.
 * @remarks Owner-thread execution, no callbacks; caller must revoke before destroying.
 */
[[nodiscard]] Result<std::unique_ptr<IPolicyBackend>> makeTensorBackend();
/** @brief Create an owning GPU provider; compile lazily on the device owner thread.
 * @return GPU-only provider; execution returns an error when the device or graph is unsupported.
 * @remarks Revoke and destroy before Graphics device teardown. No device or caller data is owned by registration.
 */
[[nodiscard]] Result<std::unique_ptr<IGpuPolicyBackend>> makeGpuBackend();
}  // namespace eve::agent
