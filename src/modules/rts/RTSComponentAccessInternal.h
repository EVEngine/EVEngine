#pragma once
#include "common/ECS.h"
namespace eve::rts::detail {
/** @brief Borrow an existing component for one owner-thread call, without materializing absent state.
 * @lifetime The entity owns the result; never retain it across structural mutation or callbacks.
 */
template <class Component, class Entity>
Component* existingComponent(Entity& entity) noexcept {
    auto* buffer = entity.getComponentManager().template getComponentBuffer<Component>();
    return buffer && buffer->has(entity.id) ? buffer->ptr(entity.id) : nullptr;
}
}  // namespace eve::rts::detail
