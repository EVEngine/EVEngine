#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>

namespace eve {

/** @brief Value result for a procedural world-surface projection query. */
struct EVENGINE_API ProcgenSurfaceHit {
    bool         hit      = false;
    std::int64_t objectId = 0;
    float        x        = 0.f;
    float        y        = 0.f;
    float        z        = 0.f;
    float        normalX  = 0.f;
    float        normalY  = 1.f;
    float        normalZ  = 0.f;
};

/** @brief Optional synchronous world-query capability for procedural generation. */
class EVENGINE_API IProcgenWorldQuery {
public:
    static constexpr const char* capabilityName = "IProcgenWorldQuery";
    virtual ~IProcgenWorldQuery() = default;

    /**
     * @brief Project vertically and return a checked hit or miss value.
     * @param x World X coordinate.
     * @param z World Z coordinate.
     * @param maxY Inclusive projection start height.
     * @param minY Inclusive projection end height.
     * @param maskBits Provider-defined collision or query mask.
     * @return Structured execution result; a successful miss has `hit` false.
     * @note Implementations must not retain caller data and run on the caller's generation thread.
     */
    [[nodiscard]] virtual Result<ProcgenSurfaceHit> projectDown(
        float x, float z, float maxY, float minY, std::uint64_t maskBits) = 0;
};

}  // namespace eve
