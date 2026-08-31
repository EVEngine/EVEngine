#pragma once

#include "common/Result.h"

#include <string_view>

namespace eve::rpg {

class GameState;

/**
 * @brief Non-owning typed adapter for persistent per-map object lifecycle state.
 *
 * GameState remains the sole authoritative owner and therefore participates in existing save-session
 * transactions without another snapshot. This adapter only standardizes stable IDs and mutation semantics.
 */
class WorldState {
public:
    /**
     * @brief Bind an adapter to one authoritative game state.
     * @param gameState State owner that must outlive this adapter.
     * @ownership Borrowed; the adapter never destroys or retains ownership of GameState.
     * @thread Create and use on the GameState owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    explicit WorldState(GameState& gameState) noexcept : gameState_(&gameState) {}

    /**
     * @brief Query whether a stable object has been consumed in one map.
     * @param mapId Stable non-empty content ID.
     * @param objectId Stable non-empty object ID unique within the map.
     * @return False for invalid IDs or objects without a consumed fact.
     */
    [[nodiscard]] bool isObjectConsumed(std::string_view mapId, std::string_view objectId) const;

    /**
     * @brief Idempotently publish the consumed fact for one stable map object.
     * @return Applied for a new fact, NoOp when already consumed, or InvalidArgument without mutation.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> consumeObject(std::string_view mapId, std::string_view objectId);

    /**
     * @brief Idempotently clear the consumed fact for one stable map object.
     * @return Applied when a fact changed, NoOp when already clear, or InvalidArgument without mutation.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> resetObject(std::string_view mapId, std::string_view objectId);

private:
    GameState* gameState_ = nullptr;
};

}  // namespace eve::rpg
