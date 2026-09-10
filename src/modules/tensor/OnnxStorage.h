#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "common/Result.h"
namespace eve::tensor {
/** @brief Run-scoped device storage. Readback completes pending work on the device thread.
 * @note Implementations reject access after their run ends; no device handles escape runGpu. */
class OnnxDeviceStorage {
public:
    /** @brief Release owning storage on the device thread. */
    virtual ~OnnxDeviceStorage() = default;
    /** @brief Return an owning host copy, or an explicit device/lifecycle error. */
    [[nodiscard]] virtual Result<std::vector<uint8_t>> readback() const = 0;
};
/** @brief Immutable buffer shared by graph aliases; exactly one storage is authoritative.
 * @note host owns CPU bytes; device owns GPU bytes. Host inputs may be cached by shared identity
 * across calls only when persistent marks an immutable model initializer. Consumers use the device thread. */
struct OnnxBuffer {
    std::shared_ptr<const std::vector<uint8_t>> host;
    std::shared_ptr<OnnxDeviceStorage>          device;
    size_t                                      size = 0;
    bool persistent = false;  // Immutable model initializer, eligible for bounded cross-run caching.
};
/** @brief Actual transfers and submissions made during one GPU run. */
struct OnnxTransferStats {
    size_t shaderCompilations = 0, shaderCacheHits = 0;
    size_t uploads = 0, downloads = 0, submissions = 0;
    size_t uploadedBytes = 0, downloadedBytes = 0;
    size_t bufferAllocations = 0, bufferReuses = 0;
    size_t compilerPeakWorkers = 0;
};
}  // namespace eve::tensor
