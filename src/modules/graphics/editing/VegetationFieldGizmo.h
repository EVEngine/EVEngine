#pragma once

#include "common/Result.h"
#include "editing/EditingGizmo.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace eve::graphics {
class VegetationField;
}

namespace eve::graphics_editing {

/** @brief Bounded renderer-neutral viewport projection for one vegetation influence field. */
class VegetationFieldGizmoBuilder {
public:
    /**
     * @brief Build one immutable gizmo from the field's owning element snapshot.
     * @param target Stable editor target identity copied into the result.
     * @param expectedRevision Revision selected by the editor; stale revisions are rejected.
     * @param field Borrowed only during this call; no field or element pointer is retained.
     * @param maximumElements Hard allocation and viewport-work budget in [1,100000].
     * @return Applied snapshot, Conflict for stale input, or Rejected/Failed diagnostics.
     * @thread Worker-safe while no writer mutates field; invokes no callbacks or GPU work.
     */
    [[nodiscard]] Result<editing::GizmoSnapshot> build(std::string target, std::uint64_t expectedRevision,
                                                       const graphics::VegetationField& field,
                                                       std::size_t maximumElements = 10000) const;
};

}  // namespace eve::graphics_editing
