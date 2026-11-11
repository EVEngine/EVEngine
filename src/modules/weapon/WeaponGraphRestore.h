#pragma once
/** @file WeaponGraphRestore.h
 * @brief weapon module-interface: joint native pool/weapon publication with rollback before its no-fail boundary.
 * @remarks No persistent schema, ECS systems, events or script/tool additions. Domain owner indices/clocks remain
 * the coordinator's responsibility. Caller supplies complete authoritative graph correspondences.
 */
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include "weapon/AmmoPoolRestore.h"
#include "weapon/WeaponRestore.h"
namespace eve::weapon {
/** @brief Borrowed immutable member and its actual target incarnation; borrow ends when prepare returns. */
struct WeaponGraphMember {
    std::reference_wrapper<const WeaponCapture> captured;
    ecs::EntityHandle                           target;
};
/** @brief Owning saved pool facts paired with the coordinator's actual target incarnation. */
struct WeaponGraphPool {
    CapturedAmmoPool  captured;
    ecs::EntityHandle target;
};
/** @brief Successfully published graph scope, member count and new weak pool references. */
struct WeaponGraphRestoreReceipt {
    PersistentId                        saveEpoch;
    std::size_t                         weapons = 0;
    std::vector<AmmoPoolRestoreReceipt> pools;
};
/** @brief Fully owned candidates with weak target observations and a preallocated structural rollback journal.
 * @ownership No source or target Table owner retained. Future pool links stay private until publication.
 * @thread Owner thread outside Views/callbacks. Does not execute logic, policies, events or simulation time.
 */
class EVENGINE_API_WORLD PreparedWeaponGraphRestore final {
public:
    /** @brief Discard unpublished candidates. */
    ~PreparedWeaponGraphRestore();
    /** @brief Transfer candidates. */
    PreparedWeaponGraphRestore(PreparedWeaponGraphRestore&&) noexcept;
    /** @brief Replace candidates. */
    PreparedWeaponGraphRestore& operator=(PreparedWeaponGraphRestore&&) noexcept;
    PreparedWeaponGraphRestore(const PreparedWeaponGraphRestore&)            = delete;
    PreparedWeaponGraphRestore& operator=(const PreparedWeaponGraphRestore&) = delete;
    /** @brief Join exact pool facts, weapon facts and explicit body/target correspondences at one epoch.
     * @param saveEpoch Shared non-nil checkpoint scope, including for a valid empty graph.
     * @param world Actual owned destination Table; weakly observed after this call.
     * @param weapons Borrowed source captures; each source instance and target may appear only once.
     * @param pools Owning source facts; archive references and actual target pools must both be unique.
     * @param targets Authoritative stable-domain-to-runtime body correspondences, valid through publication.
     * @return Candidate or epoch, duplicate, conflicting pool facts, missing links or target admission failure.
     * @cost Copies all native facts and reserves journal/receipt storage once per graph restore attempt.
     * @throws std::bad_alloc No target facts, links or capabilities changed.
     */
    [[nodiscard]] static Result<PreparedWeaponGraphRestore> prepare(PersistentId                         saveEpoch,
                                                                    std::shared_ptr<ecs::Table>          world,
                                                                    std::span<const WeaponGraphMember>   weapons,
                                                                    std::span<const WeaponGraphPool>     pools,
                                                                    std::span<const WeaponCaptureTarget> targets);
    /** @brief Preflight all participants, materialize optional attributes with rollback, then publish all facts.
     * @return Receipt or rejection preserving every original pool/weapon and pending candidate.
     * @remarks Old pool links retire and all member links rebind in the same owner-thread publication boundary.
     * Old weapon payloads retire only after the entire graph is published. Source Attribute absence is exact.
     * @cost Linear preflight/publication; allocates only for missing Attributes, amortized per restored member.
     * @throws std::bad_alloc Created attributes are revoked; original facts and candidate remain retryable.
     */
    [[nodiscard]] Result<WeaponGraphRestoreReceipt> commit();

private:
    struct Impl;
    explicit PreparedWeaponGraphRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::weapon
