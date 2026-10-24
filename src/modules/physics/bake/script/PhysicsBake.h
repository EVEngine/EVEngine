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
[[nodiscard]] EVENGINE_API_WORLD Result<BakeRecipe> copyScriptBakeRecipe(const ssq::Object& object);
/** @brief Optional VM projection of immutable baked assets and independent players.
 * @ownership ModuleManager owns the facade; the VM owns recipe wrappers and players.
 * @thread VM owner thread only; no retained simulation pointers, callbacks or locks.
 */
class EVENGINE_API_WORLD PhysicsBake final : public Module {
public:
    Module_REG(PhysicsBake);
};
}  // namespace eve::physics
