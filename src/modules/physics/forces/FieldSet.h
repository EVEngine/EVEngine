#pragma once
#include <cstdint>
#include <variant>
#include "common/Export.h"
#include "physics/forces/ForceBatch.h"

namespace eve::physics {
/** @brief Field membership independent of collision filtering; excludes take precedence over include-any. */
struct FieldFilter {
    std::uint64_t includeAny = UINT64_MAX;
    std::uint64_t excludeAny = 0;
};
/** @brief Owned field definition; insertion order determines floating-point accumulation order. */
struct FieldDefinition {
    std::variant<DirectionalField, DirectionalForceField, RadialField> model;
    FieldFilter                                                        filter;
};
/** @brief Borrow-free recipient identity and application-defined category bits, sampled at body center of mass. */
struct FieldTarget {
    PhysicsLink   body;
    std::uint64_t categories = 1;
};
/** @brief Immutable field collection preparing one force batch for a set of dynamic recipients.
 * @ownership Owns copied model values only; requests and worlds are borrowed for the call, never retained.
 * @thread Build on any thread with immutable inputs; prepare on the simulation owner, outside callbacks.
 * @reentrancy No callbacks or locks. Failures never mutate worlds; prepare does not wake bodies.
 * @details Categories are explicit caller data, not a second copy of backend collision masks.
 * Fields evaluate in definition order; duplicate recipient handles reject rather than double-apply.
 * Prepared forces capture current body mass/position; commit promptly before stepping or changing body state.
 */
class EVENGINE_API_WORLD FieldSet {
public:
    /** @brief Validate and copy definitions; invalid models reject the entire set, even when filtered out.
     * @return Owned immutable collection or InvalidArgument.
     * @cost Linear validation and storage in number of definitions; build once and reuse across steps.
     */
    [[nodiscard]] static Result<FieldSet> build(std::span<const FieldDefinition> fields);
    /** @brief Sample all matching fields at each planar body center, then prepare a single atomic batch.
     * @return Move-only batch or invalid/stale/duplicate recipient or overflowing force failure.
     * @cost O(fields * targets + targets log targets) evaluation and linear temporary storage per step.
     */
    [[nodiscard]] Result<ForceBatch> prepare(World& world, std::span<const FieldTarget> targets) const;
    /** @brief Sample all matching fields at each spatial body center, then prepare a single atomic batch.
     * @return Move-only batch or invalid/stale/duplicate recipient or overflowing force failure.
     * @cost O(fields * targets + targets log targets) evaluation and linear temporary storage per step.
     */
    [[nodiscard]] Result<ForceBatch> prepare(World3D& world, std::span<const FieldTarget> targets) const;

private:
    explicit FieldSet(std::span<const FieldDefinition> fields) : fields_(fields.begin(), fields.end()) {}
    std::vector<FieldDefinition> fields_;
};
}  // namespace eve::physics
