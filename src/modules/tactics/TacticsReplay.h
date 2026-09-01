#pragma once

/** @file TacticsReplay.h @brief Deterministic accepted-command replay. */

#include "tactics/TacticsBattle.h"

#include <span>

namespace eve::tactics {

/**
 * @brief Replays accepted commands against an identity-compatible starting battle.
 *
 * Replay is simulation-thread-affine and invokes no unknown callbacks. Every
 * command carries its expected and resulting revision; divergence stops at the
 * first command and leaves the successfully applied prefix observable.
 */
class BattleReplay final {
public:
    /** @brief Return an owning copy of commands whose result is newer than fromRevision. */
    [[nodiscard]] static std::vector<BattleCommand> commandsFrom(Battle& battle, Revision fromRevision);

    /**
     * @brief Apply commands in sequence and verify revision/sequence agreement after every commit.
     * @return Applied for a non-empty sequence, NoOp for an empty sequence, or the first divergence.
     */
    [[nodiscard]] static Result<void> replay(Battle& battle, std::span<const BattleCommand> commands);
};

}  // namespace eve::tactics
