#pragma once

#include "common/Module.h"
#include "common/Result.h"

#include <string>

namespace eve::physics {

class Rope3D;

/** @brief Optional physics satellite that owns rope construction, schema, and script bindings. */
class Rope final : public Module {
public:
    Module_REG(Rope);

    /**
     * @brief Creates a straight renderer-independent rope.
     * @return Owning pointer transferred to the caller.
     * @ownership The caller owns and destroys the returned Rope3D.
     * @lifetime Valid until caller destruction; this module retains no reference.
     */
    Rope3D* newRope3D(int particleCount, float startX, float startY, float startZ, float endX, float endY, float endZ);

    /** @brief Registers `physics:rope3d-create@1` for tools and editors. */
    [[nodiscard("check rope schema registration")]] eve::Result<void> registerRope3DCreateSchema();

    /** @brief Validates a versioned creation document and transfers the new rope to the caller. */
    [[nodiscard("check rope creation")]] eve::Result<Rope3D*> newRope3DFromJson(const std::string& json);

    /**
     * @brief Script facade that projects creation failure to an exception.
     * @return Owning pointer transferred to the script VM.
     * @ownership The script VM assumes ownership of the returned Rope3D.
     * @lifetime Valid until the script wrapper is collected.
     */
    Rope3D* newRope3DFromJsonScript(const std::string& json);
};

}  // namespace eve::physics
