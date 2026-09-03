#pragma once

#include "editing/EditingGizmo.h"

#include <string>

namespace eve::animation { class DynamicBoneSolver; class FootIKSolver; }

namespace eve::animation_editing {
/** @brief Feature toggles and safety budget for procedural animation overlays. */
struct ProceduralBoneOverlayOptions { bool showParticles=true,showColliders=true,showFootIK=true; std::size_t maximumPrimitives=8192; };

/** @brief Builds renderer-neutral dynamic-bone and Foot IK debug primitives. */
class ProceduralBoneOverlayBuilder {
public:
    /** @brief Build an owning immutable overlay from optional borrowed solvers. */
    editing::GizmoSnapshot build(std::string target,editing::Revision revision,
                                 const animation::DynamicBoneSolver* dynamicSolver,
                                 const animation::FootIKSolver* footSolver,
                                 const ProceduralBoneOverlayOptions& options={}) const;
};
}
