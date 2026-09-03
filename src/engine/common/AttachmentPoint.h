#pragma once

#include "common/Result.h"

#include <string_view>

namespace eve {

/** @brief Plain world-space point shared by animation consumers without a math-library dependency. */
struct AttachmentPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/**
 * @brief Read-only cross-module contract for sampling named animated attachment points.
 * @ownership Implementations retain all pose data; returned points are values.
 * @thread Calls are affine to the source's animation update thread.
 * @reentrancy Implementations must not invoke callbacks.
 */
class IAttachmentPointSource {
public:
    virtual ~IAttachmentPointSource() = default;
    /**
     * @brief Resolve a semantic or native attachment name and transform a local offset to world space.
     * @param name Stable semantic or source-native attachment name.
     * @param localOffset Point in attachment-local coordinates.
     * @return World-space point or a structured stale/missing attachment diagnostic.
     */
    [[nodiscard]] virtual Result<AttachmentPoint> sampleAttachmentPoint(
        std::string_view name, AttachmentPoint localOffset = {}) const = 0;
};

}  // namespace eve
