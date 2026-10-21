#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/bake/collapse/BonePresentation.h"
namespace eve::physics {
struct FractureGeometry;
/** @brief Read-only source protocol for one atomic offline collapse sample.
 * @ownership The caller owns the source; capture borrows it synchronously and retains no source address.
 * Returned poses own values and returned geometry must remain immutable for its entire shared lifetime.
 * @thread Source owner thread, outside simulation steps. All three queries must describe the same stable
 * source state; queries must not mutate/reenter the destination clip or invoke gameplay callbacks.
 * @lifetime The source may be destroyed immediately after capture returns. No registry or world is required.
 */
class EVENGINE_API_BACKENDS CollapseCaptureSource {
public:
    /** @brief Release the protocol object; does not transfer ownership to a recorder. */
    virtual ~CollapseCaptureSource();
    /** @brief Stable positive fragment count for this sample. */
    [[nodiscard]] virtual int boneCount() const noexcept = 0;
    /** @brief Copy one fragment pose or report a failed/stale source query, without changing source state. */
    [[nodiscard]] virtual Result<BonePresentation> bonePresentation(int boneIndex) const = 0;
    /** @brief Share frozen geometry; an empty pointer denotes box-proxy geometry.
     * @ownership Returned sharing extends immutable geometry lifetime independently of the source.
     */
    [[nodiscard]] virtual std::shared_ptr<const FractureGeometry> fragmentGeometry() const noexcept = 0;
};
}  // namespace eve::physics
