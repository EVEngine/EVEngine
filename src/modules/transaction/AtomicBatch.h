#pragma once
#include <functional>
#include <memory>
#include <optional>
#include "common/ResourceAccount.h"
#include "common/Value.h"
#include "transaction/Transaction.h"

namespace eve::transaction {
/**
 * @brief Single-use composition of domain participants and one payer's costs.
 * Owns staged participants, never game state. Domain producers add independent
 * effects; payment is merged and committed once through the existing coordinator.
 * Script bindings retain each domain facade until commit/destruction. Entity
 * participants must revalidate handles at prepare/commit. VM owners must outlive
 * this batch; all calls run on the same VM/simulation owner thread without reentry.
 * No script callbacks run during commit. This transient builder is not serializable.
 */
class EVENGINE_API_FOUNDATION AtomicBatch final {
public:
    /** @brief Create an empty owner-thread batch. */
    AtomicBatch();
    /** @brief Release uncommitted stages and retained script owners. */
    ~AtomicBatch();
    AtomicBatch(const AtomicBatch&)            = delete;
    AtomicBatch& operator=(const AtomicBatch&) = delete;
    /** @brief Append owning effects and optional cost without executing them.
     * @cost Linear in effect/cost count; staging amortizes allocation per request.
     * @return Failure poisons the batch so an ignored staging error cannot commit.
     */
    [[nodiscard]] Result<void> add(std::vector<std::unique_ptr<ITransactionParticipant>> effects,
                                   std::optional<resource::CostSpec>                     cost = {});
    /** @brief Select the authoritative payer; a different second payer is rejected.
     * @ownership Borrowed account; native callers retain it through commit. Script
     * producers retain the owning facade using retainScriptOwner.
     */
    [[nodiscard]] Result<void> bindAccount(resource::IResourceAccount&                           account,
                                           std::function<std::optional<Value>(std::string_view)> query = {});
    /** @brief Retain a protocol adapter while its balance owner remains authoritative. */
    [[nodiscard]] Result<void> bindAccount(std::shared_ptr<resource::IResourceAccount>           account,
                                           std::function<std::optional<Value>(std::string_view)> query = {});
    /** @brief Parse a positive-integer resource cost object at the script boundary. */
    [[nodiscard]] static Result<resource::CostSpec> parseCost(const std::string& json);
    /** @brief Add a synchronous read-only lifetime/time precondition.
     * Guards run before account access; scripts cannot supply arbitrary callbacks. */
    [[nodiscard]] Result<void> guard(std::function<Result<void>()> validate);
    /** @brief Preserve a resource floor after the complete merged payment.
     * Multiple contributors combine floors by maximum, not by summing them. */
    [[nodiscard]] Result<void> protect(resource::ResourceCost floor);
    /** @brief Preserve the first admission error and reject further execution. */
    [[nodiscard]] Result<void> reject(Status status);
    /** @brief Query the selected authoritative account for condition evaluation. */
    [[nodiscard]] std::optional<Value> resource(std::string_view name) const;
    /** @brief Commit all stages once, or roll back/compensate via Coordinator.
     * @cost Linear in stages plus their domain preparation costs; one reservation
     * per batch amortizes payment across all effects. Consumes the builder.
     */
    [[nodiscard]] Result<TransactionReceipt> commit();
    /** @brief Retain the current native script method's self for staged lifetimes.
     * @param vm Opaque Squirrel VM handle on the current owner thread.
     * @return Failure if the builder is consumed or VM self cannot be retained.
     */
    [[nodiscard]] Result<void> retainScriptOwner(void* vm);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
namespace script_internal {
void exposeAtomicBatch(ssq::Table& table, ssq::Class& module);
}
}  // namespace eve::transaction
