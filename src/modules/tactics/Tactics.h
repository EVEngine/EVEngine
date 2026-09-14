#pragma once

/** @file Tactics.h @brief Tactics domain composition profile facade. */

#include "common/Module.h"
#include "common/GameplayControl.h"
#include "common/SquirrelOwnership.h"
#include "tactics/TacticsBattle.h"
#include "tactics/TacticsPersistence.h"
#include "tactics/TacticsReplay.h"

#include <cstddef>
#include <vector>

namespace eve::tactics {

/** @brief Owner tag for script-visible battle-session handles. */
struct TacticsBattleSessionTag {};
/** @brief Generation- and module-epoch-qualified battle-session reference. */
using TacticsBattleSessionRef = script::RuntimeHandleRef<TacticsBattleSessionTag>;

/**
 * @brief Module-owned script session referencing one battle aggregate.
 *
 * The session owns destruction responsibility for the listed ECS entities but
 * exposes only its RuntimeHandleRef to scripts. Its destructor observes ECS
 * generations before release, so module-first and proxy-first destruction are safe.
 */
struct TacticsBattleSession {
    ~TacticsBattleSession() noexcept;

    ecs::EntityHandle battle{};
};

/**
 * @brief Owns tactics short-root entities and coordinates checked battle setup.
 *
 * The ECS world owns entity storage; this facade owns the responsibility to
 * destroy handles it creates. Destruction removes units before sides and
 * battles. Links are generation checked, so either explicit destruction order
 * becomes observably stale rather than dangling. Snapshots persist stable
 * identities only; runtime handles are rebuilt by the identity-compatible target.
 *
 * All methods are simulation-thread-affine and invoke no unknown callbacks.
 */
class Tactics : public Module, public IGameplayControlProvider {
public:
    Module_REG(Tactics);

    /** @brief Construct an empty tactics composition profile. */
    Tactics();
    /** @brief Destroy live entities created through this facade. */
    ~Tactics() override;

    /** @copydoc IGameplayControlProvider::gameplayDomain */
    [[nodiscard]] std::string_view gameplayDomain() const noexcept override;
    /** @copydoc IGameplayControlProvider::observeGameplay */
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession& session,
                                                               SubjectRef instance) const override;
    /** @copydoc IGameplayControlProvider::availableGameplayActions */
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession& session, SubjectRef instance, SubjectRef subject) const override;
    /** @copydoc IGameplayControlProvider::submitGameplay */
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession& session,
                                                                 SubjectRef instance,
                                                                 const GameplayCommand& command) override;
    /** @copydoc IGameplayControlProvider::advanceGameplay */
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session,
                                                               SubjectRef instance,
                                                               const SimulationStep& step) override;
    /** @copydoc IGameplayControlProvider::gameplayEvents */
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession& session,
                                                                     SubjectRef instance,
                                                                     std::uint64_t afterSequence) const override;

    /** @brief Create a battle root with a valid stable subject and explicit seed. */
    [[nodiscard]] Result<ecs::EntityHandle> newBattle(SubjectRef subject, std::uint64_t seed = 0);
    /** @brief Create and register a side with a module-owned setup battle. */
    [[nodiscard]] Result<ecs::EntityHandle> newSide(ecs::EntityHandle battle, SubjectRef subject);
    /**
     * @brief Create, register and place a tactical unit atomically.
     * @return Unit handle; failure destroys the candidate and leaves the battle unchanged.
     */
    [[nodiscard]] Result<ecs::EntityHandle> newUnit(ecs::EntityHandle battle, ecs::EntityHandle side,
                                                     SubjectRef subject, LogicalId definition, Cell cell,
                                                     TurnResourceSpec resources = {});

    /** @brief Add one authoritative cell to a module-owned setup battle. */
    [[nodiscard]] Result<void> addCell(ecs::EntityHandle battle, Cell cell, CellState state = {});
    /** @brief Select square-four, square-eight, or axial-hex topology during setup. */
    [[nodiscard]] Result<void> setTopology(ecs::EntityHandle battle, BoardTopology topology);
    /** @brief Start a module-owned battle. */
    [[nodiscard]] Result<void> start(ecs::EntityHandle battle, TurnPolicyKind policy);
    /** @brief Advance one automatic phase using injected deterministic time. */
    [[nodiscard]] Result<BattlePhase> advance(ecs::EntityHandle battle, const SimulationStep& step);
    /** @brief End the active actor turn. */
    [[nodiscard]] Result<void> endTurn(ecs::EntityHandle battle, SubjectRef actor);
    /** @brief Validate and commit movement for the active unit. */
    [[nodiscard]] Result<MoveReceipt> moveUnit(ecs::EntityHandle battle, SubjectRef actor, Cell destination);
    /** @brief Change the active unit's logical facing. */
    [[nodiscard]] Result<void> faceUnit(ecs::EntityHandle battle, SubjectRef actor, int facing);
    /** @brief End the active unit's activation through the wait resolver. */
    [[nodiscard]] Result<void> waitUnit(ecs::EntityHandle battle, SubjectRef actor);
    /** @brief Finish a running battle. */
    [[nodiscard]] Result<void> finish(ecs::EntityHandle battle);
    /** @brief Open a deterministic reaction window. */
    [[nodiscard]] Result<std::size_t> openReaction(ecs::EntityHandle battle, std::uint64_t triggerSequence,
                                                   std::vector<ReactionCandidate> candidates);
    /** @brief Accept one eligible reaction. */
    [[nodiscard]] Result<ReactionReceipt> acceptReaction(ecs::EntityHandle battle, SubjectRef reactor,
                                                          const LogicalId& action);
    /** @brief Decline the top reaction window. */
    [[nodiscard]] Result<void> declineReaction(ecs::EntityHandle battle);
    /** @brief Add one built-in objective during battle setup. */
    [[nodiscard]] Result<void> addObjective(ecs::EntityHandle battle, ObjectiveSpec objective);
    /** @brief Apply a defeat outcome from an RPG/game settlement adapter. */
    [[nodiscard]] Result<void> defeatUnit(ecs::EntityHandle battle, SubjectRef unit);
    /** @brief Consume one value from a named battle random stream. */
    [[nodiscard]] Result<std::uint64_t> roll(ecs::EntityHandle battle, const LogicalId& stream);

    /** @brief Return an owning snapshot of the current battle status. */
    [[nodiscard]] Result<BattleStatus> status(ecs::EntityHandle battle);
    /** @brief Return an owning snapshot of the current battle phase. */
    [[nodiscard]] Result<BattlePhase> phase(ecs::EntityHandle battle);
    /** @brief Return an owning snapshot of the current active unit subject. */
    [[nodiscard]] Result<SubjectRef> activeUnit(ecs::EntityHandle battle);
    /** @brief Capture a sealed complete snapshot of a module-owned battle. */
    [[nodiscard]] Result<SnapshotEnvelope> snapshot(ecs::EntityHandle battle,
                                                    const SnapshotHashProvider& hashProvider);
    /** @brief Transactionally restore a module-owned identity-compatible battle snapshot. */
    [[nodiscard]] Result<void> restore(ecs::EntityHandle battle, const SnapshotEnvelope& snapshot,
                                       const SnapshotHashProvider& hashProvider);
    /** @brief Return accepted commands newer than the supplied battle revision. */
    [[nodiscard]] Result<std::vector<BattleCommand>> commandsFrom(ecs::EntityHandle battle, Revision revision);
    /** @brief Replay checked commands against a module-owned battle. */
    [[nodiscard]] Result<void> replay(ecs::EntityHandle battle, std::span<const BattleCommand> commands);

    /** @brief Return the number of live facade-owned battles. */
    [[nodiscard]] std::size_t battleCount() const noexcept;
    /** @brief Return the number of live facade-owned units. */
    [[nodiscard]] std::size_t unitCount() const noexcept;
    /** @brief Return the number of live facade-owned sides. */
    [[nodiscard]] std::size_t sideCount() const noexcept;

    /** @brief Allocate a script-owned battle session with generation/epoch stale detection. */
    [[nodiscard]] static Result<TacticsBattleSessionRef> newSession(SubjectRef subject, std::uint64_t seed = 0);
    /** @brief Resolve a live script battle session as a synchronous borrowed view. */
    [[nodiscard]] static script::Borrowed<TacticsBattleSession> resolve(TacticsBattleSessionRef reference) noexcept;
    /** @brief Release a script battle session and its ECS aggregate. */
    [[nodiscard]] static Result<void> release(TacticsBattleSessionRef reference);
    /** @brief Return whether a script battle-session reference is stale. */
    [[nodiscard]] static bool isStale(TacticsBattleSessionRef reference) noexcept;

private:
    [[nodiscard]] Battle* resolveBattle(ecs::EntityHandle handle) const noexcept;
    [[nodiscard]] Battle* resolveBattle(SubjectRef subject) const noexcept;
    [[nodiscard]] bool owns(const std::vector<ecs::EntityHandle>& handles,
                            const ecs::EntityHandle&              handle) const noexcept;

    std::vector<ecs::EntityHandle> battles_;
    std::vector<ecs::EntityHandle> units_;
    std::vector<ecs::EntityHandle> sides_;
    script::RuntimeObjectRegistry<TacticsBattleSession, TacticsBattleSessionTag> sessions_;
};

}  // namespace eve::tactics
