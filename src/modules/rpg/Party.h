#pragma once

/** @file Party.h @brief Generation-safe ordered RPG party relationships. */

#include "common/ECS.h"
#include "common/Result.h"
#include "rpg/RPGActor.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::rpg {

class Battle;

/**
 * @brief Authoritative ordered relationship between stable party member IDs and RPG actors.
 * @ownership Party owns only stable IDs and generation-qualified links; the RPG ECS world owns actors.
 * @thread Use on the actors' owning simulation thread.
 */
class Party {
public:
    /**
     * @brief Append one unique stable member ID and live actor as one atomic roster mutation.
     * @return Applied, or a structured failure preserving the roster.
     * @ownership The actor remains ECS-owned and must outlive its membership or be detected as stale.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> addMember(const std::string &memberId, RPGActor *actor);
    /** @brief Remove one member ID without destroying its actor. */
    [[nodiscard]] eve::Result<void> removeMember(const std::string &memberId);
    /** @brief Clear every relationship without destroying actors. */
    void clear();

    int count() const;
    bool contains(const std::string &memberId) const;
    bool hasStaleMembers() const;
    std::string getMemberId(int index) const;
    /**
     * @brief Resolve a member by ordered index.
     * @return Borrowed nullable actor; null means invalid index or stale generation.
     * @ownership The RPG ECS world owns the actor.
     * @lifetime Valid until actor/world destruction; do not retain beyond structural mutation.
     */
    RPGActor *getMemberActor(int index) const;
    /**
     * @brief Resolve a member by stable ID.
     * @return Borrowed nullable actor; null means unknown ID or stale generation.
     * @ownership The RPG ECS world owns the actor.
     * @lifetime Valid until actor/world destruction; do not retain beyond structural mutation.
     */
    RPGActor *findMemberActor(const std::string &memberId) const;

    /**
     * @brief Add every live roster member to a battle side after validating the complete roster.
     * @return Member count, or a structured failure without changing the battle.
     * @remarks Empty or stale rosters are rejected before the first Battle mutation.
     */
    [[nodiscard]] eve::Result<int> addToBattle(Battle *battle, int side) const;

    /**
     * @brief Atomically restore every live member at a safe checkpoint.
     * @param healthResource Required resource used for defeat/revival (for example `hp`).
     * @param healthRatio Target fraction of each member's current maximum, in (0, 1].
     * @param secondaryResource Optional secondary resource (for example `mp`), or empty to skip.
     * @param secondaryRatio Target secondary fraction in [0, 1].
     * @return Recovered member count, or a structured failure leaving every member unchanged.
     * @remarks The complete roster and all resource maxima are validated before any mutation.
     * @reentrancy Does not invoke callbacks or scripts; Vitals events are appended during commit.
     */
    [[nodiscard]] eve::Result<int> recoverAtCheckpoint(const std::string &healthResource,
                                                        double healthRatio,
                                                        const std::string &secondaryResource = {},
                                                        double secondaryRatio = 0.0);

    /** @brief Capture ordered member IDs and every actor safe checkpoint into one versioned document. */
    [[nodiscard]] eve::Result<std::string> checkpointJson() const;
    /**
     * @brief Validate and atomically restore every existing party member actor.
     * @remarks Member IDs and order must exactly match the live roster; roster relationships are not recreated.
     */
    [[nodiscard]] eve::Result<void> restoreCheckpointJson(std::string_view json);

private:
    struct Member {
        std::string       id;
        ecs::EntityHandle actor;
    };
    struct CheckpointCandidate {
        std::vector<RPGActor::CheckpointCandidate> actors;
    };
    [[nodiscard]] eve::Result<CheckpointCandidate> prepareCheckpointJson(std::string_view json) const;
    void commitCheckpoint(CheckpointCandidate candidate) noexcept;
    std::vector<Member> members_;
    friend class RPGSaveSession;
};

}  // namespace eve::rpg
