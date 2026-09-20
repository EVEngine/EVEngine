#pragma once

#include "common/Module.h"

namespace eve::procgen_animation {

/** @brief Optional script composition for procedural meshes and skeletal animation. */
class EVENGINE_API_ORCHESTRATION ProcgenAnimation : public Module {
public:
    Module_REG(ProcgenAnimation);
};

}  // namespace eve::procgen_animation
