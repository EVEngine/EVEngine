#pragma once
#include <array>
#include <cmath>
#include "common/Value.h"
namespace eve {
/**
 * @brief Immutable object-local volume selecting actual rendered surface pixels.
 * @ownership Authoring relation owns the definition; ids are scoped to the measured object.
 * @thread Owner-thread value parsing; no pointers, callbacks or runtime state.
 * @remarks Schema eve.scene.local-region v1; only local_box, unknown fields/versions rejected.
 */
struct EVENGINE_API_FOUNDATION SceneLocalRegion {
    std::string           id, geometryRevision;
    std::array<double, 3> minimum{}, maximum{};
    /** @brief Compare immutable region identity and definition. */
    bool operator==(const SceneLocalRegion&) const = default;
    /** @brief Decode a finite, nonempty local box and immutable geometry revision.
     * @return Owning definition or diagnostic; input borrowed only during this call. */
    [[nodiscard]] static Result<SceneLocalRegion> fromValue(const Value& value);
    /** @brief Copy the immutable definition into its strict owning schema. */
    [[nodiscard]] Value toValue() const;
};
}  // namespace eve
