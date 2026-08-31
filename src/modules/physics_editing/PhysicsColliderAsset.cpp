#include "physics_editing/PhysicsColliderAsset.h"

#include <type_traits>

namespace eve::physics_editing {

static_assert(std::is_polymorphic_v<IPhysicsColliderAssetResolver>);

}  // namespace eve::physics_editing
