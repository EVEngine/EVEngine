#pragma once
/** @brief Compatibility include; canonical checked value/playback API lives in physics_bake_collapse. */
#include "physics/bake/collapse/CollapseAnimation.h"
namespace ssq {
class Table;
}
namespace eve::physics {
/** @brief Register owning animation and sampled pose script types in the destruction VM module. */
EVENGINE_API_DOMAINS void exposeCollapseAnimation(ssq::Table& table);
}  // namespace eve::physics
