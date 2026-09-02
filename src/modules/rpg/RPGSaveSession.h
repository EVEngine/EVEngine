#pragma once

/**
 * @file RPGSaveSession.h
 * @brief Transactional composition of persistent RPG state owners.
 */

#include "common/Result.h"

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace eve::inventory {
class Bag;
class EquipmentSet;
}

namespace eve::rpg {

class GameState;
class Tracker;
class RPGActor;
class Party;

/** @brief Definition-reference field kinds supported by direct RPG save ID migrations. */
enum class RPGSaveIdDomain {
    Item,           /**< Item definition IDs in bag and equipment stacks. */
    Quest,          /**< Quest definition IDs in tracker entries. */
    QuestObjective, /**< Objective IDs nested in tracker entries. */
    Skill,          /**< Learned skill definition IDs. */
    Class,          /**< Player class definition ID. */
    Trait,          /**< Player trait definition IDs. */
    TraitSource,    /**< Trait source identifiers when their owner identity changed. */
    EquipmentSlot,  /**< Equipment slot names persisted by InventorySaveSession. */
};

/**
 * @brief A non-owning save boundary for the persistent classic-RPG state owners.
 *
 * The session does not become a second source of truth: it only coordinates the
 * canonical snapshot codecs owned by its participants. More participant types
 * can be added in later schema versions without moving their mutable state here.
 */
class RPGSaveSession {
public:
    RPGSaveSession() = default;

    /**
     * @brief Bind the authoritative participants used by subsequent operations.
     * @param gameState Borrowed game-state owner.
     * @param tracker Borrowed quest-progress owner.
     * @param actor Borrowed preconfigured player actor restored at safe checkpoints.
     * @param bag Borrowed authoritative player bag.
     * @param equipment Borrowed authoritative player equipment set.
     * @remarks All objects must outlive this session. Rebinding invalidates no participant state.
     * @thread Call and use the session on the participants' owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    void bind(GameState &gameState, Tracker &tracker, RPGActor &actor, inventory::Bag &bag,
              inventory::EquipmentSet &equipment) noexcept;

    /**
     * @brief Bind a complete authoritative player party instead of a single compatibility actor.
     * @param party Borrowed ordered party whose live roster must already be configured.
     * @remarks Produces schema version 2 snapshots. All objects must outlive this session; the party owns
     * only generation-safe actor links, while the RPG ECS world retains actor ownership.
     */
    void bindParty(GameState &gameState, Tracker &tracker, Party &party, inventory::Bag &bag,
                   inventory::EquipmentSet &equipment) noexcept;

    /**
     * @brief Configure the exact game-content contract required by this save session.
     * @param contentVersion Non-empty opaque UTF-8 identifier, at most 256 bytes and without controls.
     * @return Applied status, or InvalidArgument without changing the previous identifier.
     * @remarks This identifies definitions/data compatibility, independently of the envelope schema version.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> setContentVersion(std::string_view contentVersion);

    /** @brief Return an owning copy of the configured content contract identifier. */
    std::string getContentVersion() const { return contentVersion_; }

    /**
     * @brief Accept one older content contract that is data-compatible with the configured version.
     * @param contentVersion Exact older content identifier accepted during restore.
     * @return Applied status, or InvalidArgument/Conflict without changing the compatibility set.
     * @remarks This does not rewrite identifiers or definitions. Every participant is still validated
     * against the currently loaded registries before any live state changes. Use only for additive or
     * otherwise codec-compatible content updates; incompatible updates require an explicit transformer.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> allowCompatibleContentVersion(std::string_view contentVersion);

    /**
     * @brief Register one direct old-to-current definition ID rewrite.
     * @param fromContentVersion Exact source content contract accepted by this migration route.
     * @param domain Snapshot field domain to rewrite.
     * @param oldId Exact old identifier.
     * @param newId Exact identifier expected by the currently loaded definitions.
     * @return Applied status, or a structured failure without changing registered rules.
     * @remarks The current content version must already be configured. Rules are pure admission-time
     * transforms over an owning payload copy and are mutually exclusive with compatible-version admission.
     * Unknown new versions and source versions without a route remain rejected; no downgrade is supported.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> addIdRenameMigration(std::string_view fromContentVersion,
                                                         RPGSaveIdDomain domain, std::string_view oldId,
                                                         std::string_view newId);

    /**
     * @brief Register an additive migration that initializes one newly introduced quest.
     * @param fromContentVersion Exact older content identifier whose tracker lacks the quest.
     * @param questId Quest definition present in the current registry and absent from the old snapshot.
     * @return Applied status, or a structured failure without changing registered routes.
     * @remarks During admission, the old tracker payload is rebuilt in current registry order and this
     * quest receives its canonical initial runtime state. Existing quest entries remain byte-equivalent
     * owning values and still pass the strict Tracker codec. The source route may also contain direct ID
     * renames, but cannot be declared directly compatible. Unknown missing or extra quests remain rejected.
     * @thread Owning simulation thread only; the process-local QuestRegistry must not mutate concurrently.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] eve::Result<void> addQuestAdditionMigration(std::string_view fromContentVersion,
                                                               std::string_view questId);

    /**
     * @brief Register an explicit schema-v1 single-actor to schema-v2 party migration.
     * @param fromContentVersion Exact old content contract whose save contains `actor`.
     * @return Applied status, or a structured failure without changing migration policy.
     * @remarks Party member zero receives the old actor checkpoint. Remaining live roster members keep
     * their preconfigured new-content baseline. The route is used only by bindParty() sessions and may
     * coexist with ID and additive-quest migrations for the same source version.
     */
    [[nodiscard]] eve::Result<void> allowSingleActorPartyMigration(
        std::string_view fromContentVersion);

    /**
     * @brief Capture every bound participant into one deterministic versioned JSON document.
     * @return Integrity-sealed `rpg:save-session` JSON: schema v1 for bind(), v2 for bindParty().
     * @remarks The returned value owns all bytes and remains valid independently of the participants.
     * @thread Call on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<std::string> snapshotJson() const;

    /**
     * @brief Parse and fully prepare a save without publishing participant state.
     * @param json UTF-8 JSON produced by snapshotJson().
     * @return Success only when restoreSnapshotJson() can reach its no-fail commit boundary.
     * @remarks Integrity, content compatibility and every participant codec are checked. This is
     * intended for slot selection and backup recovery; it never mutates bound participants.
     * @thread Call on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> validateSnapshotJson(std::string_view json);

    /**
     * @brief Validate all participants and atomically publish a save-session snapshot.
     * @param json UTF-8 JSON produced by snapshotJson().
     * @return Applied state, or a structured failure without modifying either participant.
     * @remarks The strict common envelope rejects unknown fields and verifies its schema-owned,
     * non-cryptographic corruption digest before participant parsing. This detects damaged saves but
     * is not an authentication or anti-cheat boundary.
     * @thread Call on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view json);

private:
    struct MigrationKey {
        std::string fromContentVersion;
        RPGSaveIdDomain domain = RPGSaveIdDomain::Item;
        std::string oldId;

        friend bool operator<(const MigrationKey &left, const MigrationKey &right) noexcept {
            if (left.fromContentVersion != right.fromContentVersion)
                return left.fromContentVersion < right.fromContentVersion;
            if (left.domain != right.domain) return left.domain < right.domain;
            return left.oldId < right.oldId;
        }
    };

    [[nodiscard]] eve::Result<void> restoreSnapshotJsonImpl(std::string_view json, bool publish);

    GameState *gameState_ = nullptr;
    Tracker *tracker_ = nullptr;
    RPGActor *actor_ = nullptr;
    Party *party_ = nullptr;
    inventory::Bag *bag_ = nullptr;
    inventory::EquipmentSet *equipment_ = nullptr;
    std::string contentVersion_;
    std::set<std::string> compatibleContentVersions_;
    std::map<MigrationKey, std::string> idRenameMigrations_;
    std::map<std::string, std::set<std::string>> questAdditionMigrations_;
    std::set<std::string> singleActorPartyMigrations_;
};

}  // namespace eve::rpg
