#pragma once

#include "common/Module.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::orders {

/** @brief Lifecycle state of a generic order. */
enum class OrderState { Queued, Active, Completed, Failed, Cancelled };

/**
 * @brief JSON-compatible key/value payload carried by an order.
 *
 * Values are stored as canonical JSON fragments. The typed setters cover the
 * scalar values most scripts need; setJson() permits arrays and nested objects
 * without coupling this L0 module to a JSON library.
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
    /** @brief Sets a pre-encoded JSON value (scalar, array, or object). */
    bool setJson(const std::string& key, const std::string& json);
    /** @brief Returns true when the payload contains key. */
    bool has(const std::string& key) const;
    /** @brief Removes key and returns whether it existed. */
    bool erase(const std::string& key);
    /** @brief Removes every field. */
    void clear();
    /** @brief Returns the canonical JSON fragment for key, or an empty string. */
    std::string getJson(const std::string& key) const;
    /** @brief Serializes the complete payload as a deterministic JSON object. */
    std::string toJson() const;

private:
    friend class OrderQueue;
    std::map<std::string, std::string> values_;
};

/** @brief One entity-agnostic command and its lifecycle metadata. */
struct Order {
    std::string  id;
    std::string  kind;
    int          priority       = 0;
    OrderState   state          = OrderState::Queued;
    double       timeoutSeconds = 0.0;
    double       elapsedSeconds = 0.0;
    std::string  reason;
    OrderPayload payload;
};

/** @brief Deterministically sequenced order lifecycle event. */
struct OrderEvent {
    uint64_t    sequence = 0;
    std::string orderId;
    std::string kind;
    OrderState  from = OrderState::Queued;
    OrderState  to   = OrderState::Queued;
    std::string reason;
};

/**
 * @brief Generic priority order queue with no gameplay or entity semantics.
 *
 * Queued orders are selected by descending priority and then insertion order.
 * The queue owns returned Order pointers; pointers remain valid until clear().
 */
class OrderQueue {
public:
    /** @brief Appends an order and returns its stable queue-local id. */
    std::string append(const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);
    /** @brief Cancels all unfinished orders, then starts a new order. */
    std::string replace(const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);
    /**
     * @brief Preempts the active order when priority permits and starts a new one.
     * @return New order id; an empty string when kind is empty or priority is
     * lower than the active order.
     */
    std::string interrupt(const std::string& kind, int priority = 0, double timeoutSeconds = 0.0);

    /** @brief Completes the matching active order. */
    bool complete(const std::string& id);
    /** @brief Fails the matching active order with a reason. */
    bool fail(const std::string& id, const std::string& reason);
    /** @brief Cancels an active or queued order. */
    bool cancel(const std::string& id, const std::string& reason = "cancelled");
    /** @brief Advances the active timeout; non-positive dt is ignored. */
    void update(double dtSeconds);
    /** @brief Resets the queue, retained history, ids, and event sequences. */
    void clear();

    /** @brief Returns the active order, or nullptr. */
    Order* current();
    /** @brief Returns an order by stable id, including terminal history. */
    Order* find(const std::string& id);
    /** @brief Returns the number of queued orders, excluding the active order. */
    int queuedCount() const;
    /** @brief Returns the total retained order count. */
    int orderCount() const;
    /** @brief Returns an order in creation order, or nullptr. */
    Order* orderAt(int index);

    /** @brief Returns the number of retained deterministic events. */
    int eventCount() const;
    /** @brief Returns an event by sequence order, or nullptr. */
    OrderEvent* eventAt(int index);
    /** @brief Clears retained events without resetting their sequence counter. */
    void clearEvents();

private:
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

/** @brief Script module factory for generic OrderQueue objects. */
class Orders : public Module {
public:
    Module_REG(Orders);
    Orders()           = default;
    ~Orders() override = default;

    /** @brief Allocates a module-owned generic order queue. */
    static OrderQueue* newQueue();

private:
    std::vector<std::unique_ptr<OrderQueue>> queues_;
};

}  // namespace eve::orders
