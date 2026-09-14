#pragma once

#include "common/Time.h"

/**
 * @file RTSProductionAction.h
 * @brief Atomic RTS build/production composition over the shared protocols.
 */

#include "action/Action.h"
#include "common/ResourceAccount.h"
#include "common/Value.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "transaction/AtomicResourcePayment.h"
#include "transaction/Transaction.h"

#include <string>

namespace eve::rts {

class Building;

/** @brief Resource amount protected from production requests below a configured priority. */
struct RTSProductionResourceReserve {
    resource::ResourceCost resource;
    int minimumPriority = 0;
};

/**
 * @brief Caller-owned inputs for one atomic RTS build or production command.
 *
 * The request intentionally contains references to the existing order queue,
 * production queue, Action runtime and economy account. It does not create a
 * second queue or balance store. All pointers are borrowed for the synchronous
 * call and must remain valid until the returned Result is consumed.
 */
struct RTSBuildRequest {
    production::WorkQueue*      production = nullptr;
    orders::CommandQueue*       orders     = nullptr;
    action::ActionRuntime*      action     = nullptr;
    resource::IResourceAccount* account    = nullptr;

    resource::CostSpec cost;
    std::string        owner;
    std::string        productionKind = "build";
    std::string        product;
    eve::Value         context  = eve::Value(eve::Value::Object{});
    Duration           duration = Duration::zero();
    int                priority = 0;
    std::vector<RTSProductionResourceReserve> resourceReserves;

    std::string orderKind           = "build";
    int         orderPriority       = 0;
    double      orderTimeoutSeconds = 0.0;

    /** @brief Optional custom action definition; an invalid id gets a build id. */
    action::ActionDefinition actionDefinition;
    /** @brief Optional custom action request; its action id is normalized below. */
    action::ActionRequest actionRequest;
    /** @brief Optional domain effect staged in the same transaction. */
    transaction::ITransactionParticipant* effect = nullptr;
    /** @brief Tick used when the Action lifecycle is advanced. */
    SimulationTick tick = SimulationTick(1);
    /** @brief Duration consumed by the Action lifecycle at commit. */
    Duration actionDelta = Duration::zero();
    /** @brief Optional stable transaction id; empty derives from the product. */
    std::string transactionId;
};

/** @brief Result of an accepted atomic RTS build/production command. */
struct RTSBuildReceipt {
    transaction::TransactionReceipt transaction;
    std::string                     orderId;
    std::string                     productionTaskId;
    action::ActionExecutionId       actionExecution;
};

/** @brief Caller-owned inputs for atomically cancelling one paid production task. */
struct RTSCancelProductionRequest {
    production::WorkQueue*      production = nullptr;
    orders::CommandQueue*       orders = nullptr;
    resource::IResourceAccount* account = nullptr;
    resource::CostSpec          refund;
    std::string                 productionTaskId;
    std::string                 orderId;
    std::string                 reason = "production cancelled";
};

/** @brief Result of a production cancellation whose queues and refund all committed. */
struct RTSCancelProductionReceipt {
    resource::Receipt refund;
    std::string       productionTaskId;
    std::string       orderId;
};

/**
 * @brief Coordinates Orders, Production, Action and resource payment.
 *
 * Every mutable subsystem is staged before the account is debited. Queue and
 * production snapshots are restored on compensation. The Action lifecycle is
 * part of the same participant set as the domain changes, and the shared
 * payment facade appends the debit to that set, so a failed Action or payment
 * cannot leave a charged, partially published build. This is the preferred
 * RTS build entry point; callers do not manually combine payment and execution.
 */
class RTSProductionActionAdapter final {
public:
    /**
     * @brief Run the canonical build transaction against a live RTS Building.
     * @param building Borrowed Building whose Orders and Production components are authoritative.
     * @param action Borrowed shared ActionRuntime used for the action lifecycle.
     * @param account Borrowed authoritative economy account.
     * @param cost Positive resource cost copied into the transaction.
     * @param product Stable product identifier to enqueue.
     * @param duration Positive deterministic production duration.
     * @param productionKind Domain production kind, defaulting to `unit`.
     * @param priority Queue priority for the production task.
     * @param transactionId Optional transaction correlation id.
     * @param resourceReserves Faction/game-owned floors applied before the canonical transaction.
     * @return Complete build receipt, or a failure without partial queue/payment state.
     */
    [[nodiscard]] static eve::Result<RTSBuildReceipt> build(Building& building, action::ActionRuntime& action,
                                                            resource::IResourceAccount& account,
                                                            resource::CostSpec cost, std::string product,
                                                            Duration duration, std::string productionKind = "unit",
                                                            int priority = 0, std::string transactionId = {},
                                                            std::vector<RTSProductionResourceReserve> resourceReserves = {});

    /**
     * @brief Atomically enqueue an RTS build/production action and charge cost.
     * @param request Borrowed composition input.
     * @return A complete transaction receipt and created identities, or a
     *         failure with no observable queue/payment partial state.
     */
    [[nodiscard]] static eve::Result<RTSBuildReceipt> build(RTSBuildRequest request);

    /**
     * @brief Cancel a building production task and refund its complete canonical cost atomically.
     * @param building Borrowed building owning both authoritative queues.
     * @param account Borrowed authoritative resource account.
     * @param productionTaskId Stable task id returned by build().
     * @param orderId Stable order id returned by build().
     * @param refund Exact multi-resource cost originally paid by the task.
     * @param reason Retained cancellation reason for both canonical queues.
     * @return Refund receipt, or failure with both queues restored and no refund applied.
     */
    [[nodiscard]] static eve::Result<RTSCancelProductionReceipt> cancel(
        Building& building, resource::IResourceAccount& account, std::string productionTaskId,
        std::string orderId, resource::CostSpec refund, std::string reason = "production cancelled");

    /** @brief Cancel through explicitly injected canonical production, order and account ports. */
    [[nodiscard]] static eve::Result<RTSCancelProductionReceipt> cancel(RTSCancelProductionRequest request);
};

}  // namespace eve::rts
