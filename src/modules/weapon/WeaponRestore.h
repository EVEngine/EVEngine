#pragma once
/** @file WeaponRestore.h
 * @brief weapon module-interface: native weapon components restored together, with explicit target/pool links.
 * @remarks No systems, events, script/tool bindings or persistent schema. Pool facts and domain identities are
 * admitted by the joint coordinator before weapon publication; this participant never guesses from pool names.
 */
#include <memory>
#include <span>
#include "weapon/WeaponCapture.h"
namespace eve::weapon {
/** @brief Coordinator-owned correspondence to an already restored target pool; no ownership transfer.
 * @remarks The coordinator establishes archive identity. Weapon validates live weak link and exact saved facts.
 */
struct WeaponRestorePool {
    PersistentId archiveId;
    AmmoPoolLink pool;
};
/** @brief Unpublished complete weapon facts weakly bound to one actual destination incarnation.
 * @ownership Owns immutable definition/stages and native attribute candidate. Retains no source or target owner.
 * @thread Owner thread outside Views/callbacks; no time advancement, logic or event sinks execute.
 */
class EVENGINE_API_WORLD PreparedWeaponRestore final {
public:
    /** @brief Discard unpublished facts. */
    ~PreparedWeaponRestore();
    /** @brief Transfer candidate. */
    PreparedWeaponRestore(PreparedWeaponRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedWeaponRestore& operator=(PreparedWeaponRestore&&) noexcept;
    PreparedWeaponRestore(const PreparedWeaponRestore&)            = delete;
    PreparedWeaponRestore& operator=(const PreparedWeaponRestore&) = delete;
    /** @brief Prepare full native facts and explicit links for an existing canonical weapon or blank placeholder.
     * @param captured Detached source; no source lifetime is retained.
     * @param world Actual owned destination Table; result observes it weakly, never fabricates ownership.
     * @param target Actual destination weapon. A non-nil existing instance id must match the captured instance; nil
     * requires unclaimed identity/definition metadata.
     * @param targets Authoritative domain identities mapped to live destination incarnations. Coordinator keeps
     * identity correspondence valid until publication; weapon validates world/generation only.
     * @param pools Authoritative archive mappings to already restored pool facts in this Table.
     * @return Candidate or moved source, busy/stale/foreign/conflicting target or missing links/components.
     * @cost Copies definition, metadata and complete native attributes once per weapon restore attempt.
     * @throws std::bad_alloc Preparation leaves source and destination unchanged.
     */
    [[nodiscard]] static Result<PreparedWeaponRestore> prepare(const WeaponCapture&        captured,
                                                               std::shared_ptr<ecs::Table> world, WeaponEntity& target,
                                                               std::span<const WeaponCaptureTarget> targets,
                                                               std::span<const WeaponRestorePool>   pools);
    /** @brief Publish definition/binding, resource/phase/request/pool link, aim, identity and exact attributes.
     * @return Applied or stale/conflicting/busy target. Rejection leaves target and candidate intact.
     * @remarks May create or remove the optional Attributes component outside Views. New-component construction
     * rolls back on failure. Pool facts are checked but never changed; coordinator joins roots, owner indices, clocks
     * and services.
     * @cost Allocates only when materializing missing Attributes; remaining publication swaps owned storage.
     * @throws std::bad_alloc Target remains unchanged and candidate can retry.
     */
    [[nodiscard]] Result<void> commit();

private:
    friend class PreparedWeaponGraphRestore;
    /** @brief Prepare owned facts before standalone or graph-specific pool admission. */
    [[nodiscard]] static Result<PreparedWeaponRestore> prepareFacts(const WeaponCapture&                 captured,
                                                                    std::shared_ptr<ecs::Table>          world,
                                                                    WeaponEntity&                        target,
                                                                    std::span<const WeaponCaptureTarget> targets);
    /** @brief Revalidate target incarnation and requested body without touching attributes or pools. */
    [[nodiscard]] Result<void> validateCore() const;
    /** @brief Validate already published standalone pool correspondence. */
    [[nodiscard]] Result<void> validatePool() const;
    /** @brief Bind only a graph's private, preadmitted future pool reference. */
    void bindPreparedPool(AmmoPoolLink link) noexcept;
    /** @brief Materialize optional attributes into a preallocated rollback journal; may throw before publication. */
    void materializeAttributes(std::span<ecs::EntityHandle> journal, std::size_t& count);
    /** @brief Check native attribute admission after all structural materialization has finished. */
    [[nodiscard]] Result<void> validateAttributes() const;
    /** @brief Revoke only optional components created by this attempt, never original data. */
    static void cancelAttributes(std::span<const ecs::EntityHandle> journal) noexcept;
    /** @brief Publish after all graph participants validate; no callbacks, allocation or structural creation. */
    void publish() noexcept;
    /** @brief Consume after all participating facts are published, deferring old payload retirement. */
    void consume() noexcept;
    struct Impl;
    explicit PreparedWeaponRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::weapon
