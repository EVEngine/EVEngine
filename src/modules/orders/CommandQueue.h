#pragma once

#include "common/Module.h"
#include "common/BorrowedRef.h"
#include "common/Result.h"
#include "common/Scheduling.h"
#include "common/SquirrelOwnership.h"
#include "common/Value.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::orders {

/** @brief Owner tag for non-ECS order queue handles. */
struct CommandQueueHandleTag {};
/** @brief Generation-qualified reference to a module-owned order queue. */
using CommandQueueHandleRef = eve::script::RuntimeHandleRef<CommandQueueHandleTag>;

/** @brief Lifecycle state of a generic order. */
enum class OrderState { Queued, Active, Completed, Failed, Cancelled };

/**
 * @brief JSON-compatible key/value payload carried by an order.
 *
 * Values use the engine's canonical owning dynamic type. JSON text is accepted
 * only at the parsing boundary and becomes typed data before storage.
 */
class OrderPayload {
public:
    /** @brief Sets a JSON string value. */
    void setString(const std::string& key, const std::string& value);
    /** @brief Sets a JSON number value. */
    void setNumber(const std::string& key, double value);
    /** @brief Sets a JSON boolean value. */
    void setBool(const std::string& key, bool value);
    /** @brief Sets a JSON null value. */
    void setNull(const std::string& key);
    /** @brief Parses and stores one unchecked JSON value. */
    [[nodiscard]] eve::Result<void> setJson(const std::string& key, std::string_view json);
    /** @brief Stores an already validated canonical value. */
    [[nodiscard]] eve::Result<void> set(const std::string& key, eve::Value value);
    /** @brief Returns true when the payload contains key. */
    bool has(const std::string& key) const;
    /** @brief Removes key and reports whether it existed. */
    [[nodiscard]] eve::Result<bool> erase(const std::string& key);
    /** @brief Removes every field. */
    void clear();
    /** @brief Returns the canonical JSON fragment for key, or an empty string. */
    std::string getJson(const std::string& key) const;
    /** @brief Serializes the complete payload as a deterministic JSON object. */
    std::string toJson() const;

private:
    friend class CommandQueue;
    eve::Value::Object values_;
};

/** @brief One entity-agnostic command and its lifecycle metadata. */
struct Order : eve::scheduling::ItemMetadata {
    std::string  kind;
    OrderState   state          = OrderState::Queued;
    double       timeoutSeconds = 0.0;
    double       elapsedSeconds = 0.0;
    OrderPayload payload;
};

/** @brief Deterministically sequenced order lifecycle event. */
struct OrderEvent : eve::scheduling::EventMetadata {
    std::string orderId;
    std::string kind;
    OrderState  from = OrderState::Queued;
    OrderState  to   = OrderState::Queued;
};

/**
 * @brief Generic priority order queue with no gameplay or entity semantics.
 *
 * Queued orders are selected by descending priority and then insertion order.
 * The queue owns returned Order pointers; pointers remain valid until clear().
 */
class CommandQueue {
public:
    /** @brief Appends a command and returns its stable identity or validation failure. */
    [[nodiscard]] eve::Result<std::string> append(
        const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);
    /** @brief Cancels unfinished commands and starts a replacement. */
    [[nodiscard]] eve::Result<std::string> replace(
        const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);
    /** @brief Preempts the active command when priority permits. */
    [[nodiscard]] eve::Result<std::string> interrupt(
        const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);
    /** @brief Completes the active command. */
    [[nodiscard]] eve::Result<void> complete(const std::string& id);
    /** @brief Fails the active command. */
    [[nodiscard]] eve::Result<void> fail(const std::string& id, const std::string& reason);
    /** @brief Cancels an active or queued command. */
    [[nodiscard]] eve::Result<void> cancel(
        const std::string& id, const std::string& reason = "cancelled");
    /**
     * @brief Checked simulation update.
     * @param dtSeconds Non-negative simulation delta; zero is a successful no-op.
     * @return Applied/NoOp or a validation failure for non-finite/negative time.
     */
    [[nodiscard]] eve::Result<void> update(double dtSeconds);
    /** @brief Resets the queue, retained history, ids, and event sequences. */
    void clear();

    /**
     * @brief Returns the active order, or a null observation when the queue is idle.
     * @return Borrowed mutable view owned by this queue; nullable when no order is active.
     * @ownership CommandQueue retains ownership and callers must not delete the result.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy The returned object must not be used from a re-entrant callback.
     */
    [[nodiscard]] eve::OptionalRef<Order> current();
    /**
     * @brief Returns the active order as a read-only observation, or null when idle.
     * @return Borrowed nullable pointer owned by this queue.
     * @ownership The queue owns the order; callers never release it.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy Do not retain this observation across callbacks or queue mutation.
     */
    [[nodiscard]] eve::OptionalRef<const Order> current() const;
    /**
     * @brief Returns an order by stable id, including terminal history.
     * @return Borrowed nullable mutable pointer owned by this queue.
     * @ownership CommandQueue owns every returned order; callers must not delete it.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy Do not use the pointer after a re-entrant queue mutation.
     */
    [[nodiscard]] eve::OptionalRef<Order> find(const std::string& id);
    /**
     * @brief Returns an order by stable id as a read-only observation.
     * @return Borrowed nullable pointer owned by this queue.
     * @ownership CommandQueue owns the order and retains it as history.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy The observation is not valid across re-entrant mutation.
     */
    [[nodiscard]] eve::OptionalRef<const Order> find(const std::string& id) const;
    /** @brief Returns the number of queued orders, excluding the active order. */
    int queuedCount() const;
    /** @brief Returns the total retained order count. */
    int orderCount() const;
    /**
     * @brief Returns an order in creation order, or null for an invalid index.
     * @return Borrowed nullable mutable pointer owned by this queue.
     * @ownership CommandQueue owns the order; callers must not delete it.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy Do not retain across callbacks or queue mutation.
     */
    [[nodiscard]] eve::OptionalRef<Order> orderAt(int index);
    /**
     * @brief Returns an order in creation order as a read-only observation.
     * @return Borrowed nullable pointer owned by this queue.
     * @ownership CommandQueue retains the order; callers never release it.
     * @lifetime Valid until the next mutating queue operation or clear().
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy Do not retain across callbacks or queue mutation.
     */
    [[nodiscard]] eve::OptionalRef<const Order> orderAt(int index) const;

    /** @brief Returns the number of retained deterministic events. */
    int eventCount() const;
    /**
     * @brief Returns an event by sequence order, or null for an invalid index.
     * @return Borrowed nullable mutable pointer owned by this queue's event log.
     * @ownership CommandQueue owns retained events; callers must not delete the result.
     * @lifetime Valid until clearEvents(), clear(), or another operation that reallocates the log.
     * @thread Call on the queue's owning simulation thread.
     * @reentrancy Do not retain across event callbacks or queue mutation.
     */
    [[nodiscard]] eve::OptionalRef<OrderEvent> eventAt(int index);
    /** @brief Clears retained events without resetting their sequence counter. */
    void clearEvents();

    /**
     * @brief Serializes the complete command queue as deterministic JSON.
     * @return A canonical JSON snapshot containing schema/version, every order,
     *         queue membership, active order, event history, and both counters.
     */
    [[nodiscard]] eve::Result<std::string> snapshot() const;

    /**
     * @brief Restores a command queue transactionally from a JSON snapshot.
     * @param json JSON encoded by snapshot().
     * @return Success, or a parse/schema/invariant failure. On failure this
     *         queue remains unchanged.
     */
    [[nodiscard]] eve::Result<void> restore(std::string_view json);

private:
    std::string appendUnchecked(const std::string& kind, int priority, double timeoutSeconds);
    std::string replaceUnchecked(const std::string& kind, int priority, double timeoutSeconds);
    std::string interruptUnchecked(const std::string& kind, int priority, double timeoutSeconds);
    bool completeUnchecked(const std::string& id);
    bool failUnchecked(const std::string& id, const std::string& reason);
    bool cancelUnchecked(const std::string& id, const std::string& reason);
    void updateUnchecked(double dtSeconds);
    std::string create(const std::string& kind, int priority, double timeoutSeconds, bool activateNow);
    void        activateNext();
    bool        transition(Order& order, OrderState state, const std::string& reason);
    void        cancelUnfinished(const std::string& reason);

    uint64_t               nextId_       = 1;
    uint64_t               nextSequence_ = 1;
    std::deque<Order>      orders_;
    std::vector<size_t>    queued_;
    size_t                 active_ = static_cast<size_t>(-1);
    std::deque<OrderEvent> events_;
};

/** @brief Returns the stable lowercase name of a state. */
std::string stateName(OrderState state);

/** @brief Script module factory for generic CommandQueue objects. */
class Orders : public Module {
public:
    Module_REG(Orders);
    Orders()           = default;
    ~Orders() override = default;

    /**
     * @brief Allocates a queue and returns its generation-qualified ownership reference.
     * @return A handle plus module-lifetime epoch; the Orders module owns the queue.
     * @remarks The handle becomes stale after release, module unload, or a new
     *          Orders module instance. Callers never delete the resolved queue.
     */
    [[nodiscard]] static eve::Result<CommandQueueHandleRef> newQueueHandle();

    /** @brief Resolves a live queue as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<CommandQueue> resolve(
        CommandQueueHandleRef reference) noexcept;

    /** @brief Releases a queue owned by the Orders module. */
    [[nodiscard]] static eve::Result<void> release(CommandQueueHandleRef reference);

    /** @brief Reports whether a queue reference is invalid for the current module. */
    [[nodiscard]] static bool isStale(CommandQueueHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<CommandQueue, CommandQueueHandleTag> queues_;
};

}  // namespace eve::orders
