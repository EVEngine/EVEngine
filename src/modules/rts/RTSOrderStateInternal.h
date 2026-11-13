#pragma once
#include "orders/CommandQueue.h"
#include "rts/RTSTypes.h"
namespace eve::rts::order_state_internal {
/** @brief Prepare canonical queue and validate every RTS extension without touching ECS or providers. */
[[nodiscard]] Result<orders::CommandQueue> prepareQueue(const OrderComponent::Snapshot& snapshot);
}  // namespace eve::rts::order_state_internal
