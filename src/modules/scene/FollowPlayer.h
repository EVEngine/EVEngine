#pragma once
#include "common/Export.h"


#include "common/Result.h"

namespace ssq {
class Table;
}

namespace eve::scene {

/** @brief Pcg FollowPlayerSystem configuration independent of render providers. */
struct FollowPlayerSettings {
    bool followPlayer = true;
    bool waterObject = false;
    bool useOffset = false;
    float offsetX = 1250.f;
    float offsetY = 200.f;
    float offsetZ = 300.f;
    bool useScale = false;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float scaleZ = 1.f;
};

/** @brief Per-frame player observation supplied by the application. */
struct FollowPlayerInput {
    bool hasPlayer = false;
    float playerX = 0.f;
    float playerY = 0.f;
    float playerZ = 0.f;
};

/** @brief Provider-neutral transform writes produced for every followed object. */
struct FollowPlayerOutput {
    bool applyPosition = false;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    bool applyScale = false;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float scaleZ = 1.f;
};

/**
 * @brief Evaluate Pcg FollowPlayerSystem LateUpdate transform semantics.
 * @param output Required caller-owned output replaced atomically on success.
 * @param settings Finite caller-owned configuration.
 * @param input Finite player observation; hasPlayer=false suppresses position writes.
 * @return Success, or InvalidArgument without modifying output.
 * @ownership Inputs and output remain caller-owned; no pointers are retained.
 * @thread Any thread; this function has no callbacks or global state.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> evaluateFollowPlayer(FollowPlayerOutput*         output,
                                                                      const FollowPlayerSettings* settings,
                                                                      const FollowPlayerInput*    input);

/** @brief Register FollowPlayer value types and evaluator with the root script table. */
void exposeFollowPlayerBindings(ssq::Table& table);
}
