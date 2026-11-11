#pragma once
/** @file AmmoPoolRestore.h
 * @brief weapon module-interface: native shared-pool facts and derived lifetime published together.
 * @remarks No persistent schema, systems, events or script/tool bindings. Joint coordinator owns archive mapping.
 */
#include <memory>
#include "weapon/WeaponCapture.h"
namespace eve::weapon {
/** @brief Published checkpoint identity and weak target reference for subsequent weapon link preparation.
 * @ownership No pool/Table ownership. Old links are stale; this reference observes the new lifetime record.
 */
struct AmmoPoolRestoreReceipt {
    PersistentId archiveId;
    AmmoPoolLink pool;
};
/** @brief Unpublished complete pool facts and a fresh derived lifetime record.
 * @ownership Owns copied facts and the unpublished record, weakly observes the actual target Table.
 * @thread Owner thread outside Views/callbacks; invokes no weapon logic, events or other providers.
 */
class EVENGINE_API_WORLD PreparedAmmoPoolRestore final {
public:
    /** @brief Cancel unpublished facts. */
    ~PreparedAmmoPoolRestore();
    /** @brief Transfer candidate. */
    PreparedAmmoPoolRestore(PreparedAmmoPoolRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedAmmoPoolRestore& operator=(PreparedAmmoPoolRestore&&) noexcept;
    PreparedAmmoPoolRestore(const PreparedAmmoPoolRestore&)            = delete;
    PreparedAmmoPoolRestore& operator=(const PreparedAmmoPoolRestore&) = delete;
    /** @brief Prepare exact facts and lifetime replacement for a live canonical pool.
     * @param captured Owning checkpoint facts, including non-nil archiveId; names are descriptive only.
     * @param world Actual owned destination Table; borrowed synchronously and weakly observed afterwards.
     * @param target Actual destination selected by the coordinator's authoritative archive mapping.
     * @return Candidate or invalid identity, busy/stale/foreign target or missing components, without publication.
     * @cost Copies pool strings and allocates one fresh lifetime record once per pool restore attempt.
     * @throws std::bad_alloc Source/target and all existing links remain unchanged.
     */
    [[nodiscard]] static Result<PreparedAmmoPoolRestore> prepare(const CapturedAmmoPool&     captured,
                                                                 std::shared_ptr<ecs::Table> world,
                                                                 AmmoPoolEntity&             target);
    /** @brief Swap both facts and lifetime, retiring old links and returning the only newly admitted reference.
     * @return Receipt or stale/foreign/busy/consumed target; rejection preserves candidate, target and old links.
     * @remarks Does not create pools or rebind weapons. Coordinator joins all weapon links and owner indices
     * before exposing the restored graph; lifetime changes while pending reject even on the same entity handle.
     * @cost Constant publication and old-string/record retirement per pool; success allocates no storage.
     */
    [[nodiscard]] Result<AmmoPoolRestoreReceipt> commit();

private:
    friend class PreparedWeaponGraphRestore;
    /** @brief Revalidate original target without consuming the candidate. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Borrow immutable prepared facts until publication; candidate must be live. */
    [[nodiscard]] const CapturedAmmoPool& captured() const noexcept;
    /** @brief Mint an unpublished internal reference; graph never exposes it before lifetime publication. */
    [[nodiscard]] AmmoPoolLink futureLink() const noexcept;
    /** @brief Publish after successful graph-wide validation; no intervening callbacks or structural mutation. */
    [[nodiscard]] AmmoPoolRestoreReceipt publish() noexcept;
    struct Impl;
    explicit PreparedAmmoPoolRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::weapon
