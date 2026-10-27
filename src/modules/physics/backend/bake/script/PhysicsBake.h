#pragma once
#include "common/Module.h"
namespace ssq {
class Object;
}
namespace eve::physics {
class BakeRecipe;
/** @brief Copy a type-checked VM recipe for a recording admission.
 * @ownership Borrows the rooted object only during the call; returned recipe owns all metadata.
 * @thread VM owner thread, no callbacks or retained VM references.
 * @cost Linear in bounded recipe metadata, once per recording admission.
 * @return Owning recipe or checked wrong-type/null/allocation diagnostic.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<BakeRecipe> copyScriptBakeRecipe(const ssq::Object& object);
/** @brief Compatibility-only VM projection; Physics owns the canonical entry. Optional projection of immutable baked
 * assets and independent players.
 * @ownership The process owns the stateless facade; the VM owns recipe wrappers and players.
 * @thread VM owner thread only; no retained simulation pointers, callbacks or locks.
 */
class EVENGINE_API_BACKENDS PhysicsBake final {
public:
    SSQ_REG
    /** @brief Borrow the stateless compatibility facade for the Squirrel factory ABI.
     * @ownership Process-owned; no ownership transfers and no source state is retained.
     * @lifetime Valid for the process lifetime. @nullable No.
     * @thread Call from the VM owner thread; no callbacks or module registration.
     * @return The always-present compatibility facade; use the subsystem service in scripts.
     */
    [[nodiscard]] static PhysicsBake* create();
    static const char*                name;
};
}  // namespace eve::physics
