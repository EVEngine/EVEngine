#include "orders/CommandQueue.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <type_traits>

namespace eve::orders {
namespace {

constexpr size_t           noIndex         = static_cast<size_t>(-1);
constexpr std::string_view snapshotSchema  = "orders.command_queue";
constexpr std::int64_t     snapshotVersion = 1;

/** @brief Script-owned proxy; the queue itself remains module-owned. */
struct ScriptCommandQueue {
    explicit ScriptCommandQueue(CommandQueueHandleRef value) : reference(value) {}
    CommandQueueHandleRef reference;
};

template <class T>
eve::Result<T> orderFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "orders"));
}

template <class T>
bool readObjectMember(const eve::Value::Object& object, std::string_view key, T& result) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) return false;
    if constexpr (std::is_same_v<T, std::string>) {
        if (!it->second.isString()) return false;
        result = it->second.asString();
        return true;
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        if (!it->second.isInt64()) return false;
        result = it->second.asInt();
        return true;
    } else if constexpr (std::is_same_v<T, double>) {
        if (!it->second.isDouble() && !it->second.isInt64()) return false;
        result = it->second.isDouble() ? it->second.asDouble() : static_cast<double>(it->second.asInt());
        return std::isfinite(result);
    } else {
        static_assert(std::is_same_v<T, void>, "unsupported snapshot member type");
    }
}

bool readCounter(const eve::Value::Object& object, std::string_view key, std::uint64_t& result) {
    std::string encoded;
    if (!readObjectMember(object, key, encoded) || encoded.empty()) return false;
    const auto*   first  = encoded.data();
    const auto*   last   = first + encoded.size();
    std::uint64_t value  = 0;
    const auto    parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) return false;
    result = value;
    return true;
}

bool parseOrderState(std::string_view name, OrderState& state) {
    if (name == "queued")
        state = OrderState::Queued;
    else if (name == "active")
        state = OrderState::Active;
    else if (name == "completed")
        state = OrderState::Completed;
    else if (name == "failed")
        state = OrderState::Failed;
    else if (name == "cancelled")
        state = OrderState::Cancelled;
    else
        return false;
    return true;
}

bool readOrderState(const eve::Value::Object& object, std::string_view key, OrderState& result) {
    std::string name;
    return readObjectMember(object, key, name) && parseOrderState(name, result);
}

bool readIndex(const eve::Value& value, size_t limit, size_t& result) {
    if (!value.isInt64() || value.asInt() < 0) return false;
    const auto index = static_cast<std::uint64_t>(value.asInt());
    if (index >= limit || index > std::numeric_limits<size_t>::max()) return false;
    result = static_cast<size_t>(index);
    return true;
}

template <class T>
eve::Result<T> snapshotFailure(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message),
                                                          std::move(path), {}, "orders.snapshot"));
}

bool generatedOrderId(std::string_view id, std::uint64_t& number) {
    constexpr std::string_view prefix = "order-";
    if (!id.starts_with(prefix) || id.size() == prefix.size()) return false;
    const auto digits = id.substr(prefix.size());
    for (const char digit : digits)
        if (digit < '0' || digit > '9') return false;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && number != 0;
}

}  // namespace

void OrderPayload::setString(const std::string& key, const std::string& value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void OrderPayload::setNumber(const std::string& key, double value) {
    if (key.empty() || !std::isfinite(value)) return;
    values_[key] = eve::Value(value);
}

void OrderPayload::setBool(const std::string& key, bool value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void OrderPayload::setNull(const std::string& key) {
    if (!key.empty()) values_[key] = eve::Value();
}

eve::Result<void> OrderPayload::setJson(const std::string& key, std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<void>::failure(*parsed.error());
    return set(key, std::move(parsed).takeValue());
}

eve::Result<void> OrderPayload::set(const std::string& key, eve::Value value) {
    if (key.empty())
        return orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order payload key must not be empty", "key");
    values_[key] = std::move(value);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool OrderPayload::has(const std::string& key) const { return values_.contains(key); }

eve::Result<bool> OrderPayload::erase(const std::string& key) {
    if (key.empty())
        return orderFailure<bool>(eve::DiagnosticCode::InvalidArgument, "order payload key must not be empty", "key");
    return eve::Result<bool>::success(values_.erase(key) != 0);
}

void OrderPayload::clear() { values_.clear(); }

std::string OrderPayload::getJson(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return {};
    auto json = it->second.toJson();
    return json ? std::move(json).takeValue() : std::string{};
}

std::string OrderPayload::toJson() const {
    auto json = eve::Value(values_).toJson();
    return json ? std::move(json).takeValue() : std::string{};
}

std::string stateName(OrderState state) {
    switch (state) {
        case OrderState::Queued: return "queued";
        case OrderState::Active: return "active";
        case OrderState::Completed: return "completed";
        case OrderState::Failed: return "failed";
        case OrderState::Cancelled: return "cancelled";
    }
    return "unknown";
}

std::string CommandQueue::create(const std::string& kind, int priority, double timeoutSeconds, bool activateNow) {
    if (kind.empty()) return {};
    Order              order;
    std::ostringstream id;
    id << "order-" << std::setw(16) << std::setfill('0') << nextId_++;
    order.id             = id.str();
    order.kind           = kind;
    order.priority       = priority;
    order.timeoutSeconds = std::max(0.0, timeoutSeconds);
    orders_.push_back(std::move(order));
    const size_t index = orders_.size() - 1;
    events_.push_back(
        {{nextSequence_++, {}}, orders_[index].id, orders_[index].kind, OrderState::Queued, OrderState::Queued});
    if (activateNow) {
        active_ = index;
        transition(orders_[index], OrderState::Active, {});
    } else {
        queued_.push_back(index);
        std::stable_sort(queued_.begin(), queued_.end(),
                         [this](size_t a, size_t b) { return orders_[a].priority > orders_[b].priority; });
        if (active_ == noIndex) activateNext();
    }
    return orders_[index].id;
}

std::string CommandQueue::appendUnchecked(const std::string& kind, int priority, double timeoutSeconds) {
    return create(kind, priority, timeoutSeconds, false);
}

std::string CommandQueue::replaceUnchecked(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty()) return {};
    cancelUnfinished("replaced");
    return create(kind, priority, timeoutSeconds, true);
}

std::string CommandQueue::interruptUnchecked(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty()) return {};
    if (active_ != noIndex && priority < orders_[active_].priority) return {};
    if (active_ != noIndex) transition(orders_[active_], OrderState::Cancelled, "interrupted");
    active_ = noIndex;
    return create(kind, priority, timeoutSeconds, true);
}

bool CommandQueue::transition(Order& order, OrderState state, const std::string& reason) {
    if (order.state == OrderState::Completed || order.state == OrderState::Failed ||
        order.state == OrderState::Cancelled || order.state == state) {
        return false;
    }
    const OrderState from = order.state;
    order.state           = state;
    order.reason          = reason;
    events_.push_back({{nextSequence_++, reason}, order.id, order.kind, from, state});
    return true;
}

void CommandQueue::activateNext() {
    if (active_ != noIndex || queued_.empty()) return;
    active_ = queued_.front();
    queued_.erase(queued_.begin());
    transition(orders_[active_], OrderState::Active, {});
}

bool CommandQueue::completeUnchecked(const std::string& id) {
    if (active_ == noIndex || orders_[active_].id != id) return false;
    if (!transition(orders_[active_], OrderState::Completed, {})) return false;
    active_ = noIndex;
    activateNext();
    return true;
}

bool CommandQueue::failUnchecked(const std::string& id, const std::string& reason) {
    if (active_ == noIndex || orders_[active_].id != id) return false;
    if (!transition(orders_[active_], OrderState::Failed, reason)) return false;
    active_ = noIndex;
    activateNext();
    return true;
}

bool CommandQueue::cancelUnchecked(const std::string& id, const std::string& reason) {
    if (active_ != noIndex && orders_[active_].id == id) {
        if (!transition(orders_[active_], OrderState::Cancelled, reason)) return false;
        active_ = noIndex;
        activateNext();
        return true;
    }
    const auto it =
        std::find_if(queued_.begin(), queued_.end(), [this, &id](size_t index) { return orders_[index].id == id; });
    if (it == queued_.end()) return false;
    const size_t index = *it;
    queued_.erase(it);
    return transition(orders_[index], OrderState::Cancelled, reason);
}

void CommandQueue::updateUnchecked(double dtSeconds) {
    if (active_ == noIndex || dtSeconds <= 0.0) return;
    Order& order = orders_[active_];
    order.elapsedSeconds += dtSeconds;
    if (order.timeoutSeconds > 0.0 && order.elapsedSeconds >= order.timeoutSeconds) {
        if (!failUnchecked(order.id, "timeout")) return;
    }
}

eve::Result<std::string> CommandQueue::append(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument, "order kind must not be empty", "kind");
    if (!std::isfinite(timeoutSeconds) || timeoutSeconds < 0.0)
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                         "order timeout must be finite and non-negative", "timeoutSeconds");
    const std::string id = appendUnchecked(kind, priority, timeoutSeconds);
    if (id.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::Failed, "order append did not produce an identity");
    return eve::Result<std::string>::success(std::string(id), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> CommandQueue::replace(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument, "order kind must not be empty", "kind");
    if (!std::isfinite(timeoutSeconds) || timeoutSeconds < 0.0)
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                         "order timeout must be finite and non-negative", "timeoutSeconds");
    const std::string id = replaceUnchecked(kind, priority, timeoutSeconds);
    if (id.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::Failed, "order replacement did not produce an identity");
    return eve::Result<std::string>::success(std::string(id), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> CommandQueue::interrupt(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument, "order kind must not be empty", "kind");
    if (!std::isfinite(timeoutSeconds) || timeoutSeconds < 0.0)
        return orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                         "order timeout must be finite and non-negative", "timeoutSeconds");
    if (active_ != noIndex && priority < orders_[active_].priority)
        return orderFailure<std::string>(eve::DiagnosticCode::Conflict,
                                         "order priority cannot preempt the active order", "priority");
    const std::string id = interruptUnchecked(kind, priority, timeoutSeconds);
    if (id.empty())
        return orderFailure<std::string>(eve::DiagnosticCode::Failed, "order interrupt did not produce an identity");
    return eve::Result<std::string>::success(std::string(id), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CommandQueue::complete(const std::string& id) {
    if (id.empty()) return orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order id must not be empty", "id");
    if (!completeUnchecked(id))
        return orderFailure<void>(eve::DiagnosticCode::Conflict, "order is not the active unfinished order", "id");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CommandQueue::fail(const std::string& id, const std::string& reason) {
    if (id.empty()) return orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order id must not be empty", "id");
    if (!failUnchecked(id, reason))
        return orderFailure<void>(eve::DiagnosticCode::Conflict, "order is not the active unfinished order", "id");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CommandQueue::cancel(const std::string& id, const std::string& reason) {
    if (id.empty()) return orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order id must not be empty", "id");
    if (!cancelUnchecked(id, reason))
        return orderFailure<void>(eve::DiagnosticCode::NotFound, "order id is not active or queued", "id");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CommandQueue::update(double dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.0)
        return orderFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                  "order update delta must be finite and non-negative", "dtSeconds");
    const bool hadActiveOrder = active_ != noIndex;
    updateUnchecked(dtSeconds);
    return eve::Result<void>::success(
        eve::Status::success(dtSeconds == 0.0 || !hadActiveOrder ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

void CommandQueue::cancelUnfinished(const std::string& reason) {
    if (active_ != noIndex) transition(orders_[active_], OrderState::Cancelled, reason);
    active_ = noIndex;
    for (size_t index : queued_) transition(orders_[index], OrderState::Cancelled, reason);
    queued_.clear();
}

void CommandQueue::clear() {
    orders_.clear();
    queued_.clear();
    events_.clear();
    active_       = noIndex;
    nextId_       = 1;
    nextSequence_ = 1;
}

eve::OptionalRef<Order> CommandQueue::current() {
    return active_ == noIndex ? eve::OptionalRef<Order>{} : std::ref(orders_[active_]);
}

eve::OptionalRef<const Order> CommandQueue::current() const {
    return active_ == noIndex ? eve::OptionalRef<const Order>{} : std::cref(orders_[active_]);
}

eve::OptionalRef<Order> CommandQueue::find(const std::string& id) {
    const auto it = std::find_if(orders_.begin(), orders_.end(), [&id](const Order& order) { return order.id == id; });
    return it == orders_.end() ? eve::OptionalRef<Order>{} : std::ref(*it);
}

eve::OptionalRef<const Order> CommandQueue::find(const std::string& id) const {
    const auto it = std::find_if(orders_.begin(), orders_.end(), [&id](const Order& order) { return order.id == id; });
    return it == orders_.end() ? eve::OptionalRef<const Order>{} : std::cref(*it);
}

int CommandQueue::queuedCount() const { return static_cast<int>(queued_.size()); }

int CommandQueue::orderCount() const { return static_cast<int>(orders_.size()); }

eve::OptionalRef<Order> CommandQueue::orderAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= orders_.size()) return {};
    return std::ref(orders_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const Order> CommandQueue::orderAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= orders_.size()) return {};
    return std::cref(orders_[static_cast<size_t>(index)]);
}

int CommandQueue::eventCount() const { return static_cast<int>(events_.size()); }

eve::OptionalRef<OrderEvent> CommandQueue::eventAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::ref(events_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const OrderEvent> CommandQueue::eventAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::cref(events_[static_cast<size_t>(index)]);
}

void CommandQueue::clearEvents() { events_.clear(); }

eve::Result<std::string> CommandQueue::snapshot() const {
    eve::Value::Object root;
    root.emplace("active", active_ == noIndex ? eve::Value() : eve::Value(static_cast<std::int64_t>(active_)));
    root.emplace("nextId", eve::Value(std::to_string(nextId_)));
    root.emplace("nextSequence", eve::Value(std::to_string(nextSequence_)));
    root.emplace("schema", eve::Value(std::string(snapshotSchema)));
    root.emplace("version", eve::Value(snapshotVersion));

    eve::Value::Array queued;
    queued.reserve(queued_.size());
    for (const size_t index : queued_) {
        if (index >= orders_.size() || index > static_cast<size_t>(std::numeric_limits<std::int64_t>::max()))
            return snapshotFailure<std::string>("queue contains an invalid queued index", "queued");
        queued.emplace_back(static_cast<std::int64_t>(index));
    }
    root.emplace("queued", eve::Value(std::move(queued)));

    eve::Value::Array orders;
    orders.reserve(orders_.size());
    for (const auto& order : orders_) {
        eve::Value::Object encoded;
        encoded.emplace("elapsedSeconds", eve::Value(order.elapsedSeconds));
        encoded.emplace("id", eve::Value(order.id));
        encoded.emplace("kind", eve::Value(order.kind));
        encoded.emplace("payload", eve::Value(order.payload.values_));
        encoded.emplace("priority", eve::Value(order.priority));
        encoded.emplace("reason", eve::Value(order.reason));
        encoded.emplace("state", eve::Value(stateName(order.state)));
        encoded.emplace("timeoutSeconds", eve::Value(order.timeoutSeconds));
        orders.emplace_back(std::move(encoded));
    }
    root.emplace("orders", eve::Value(std::move(orders)));

    eve::Value::Array events;
    events.reserve(events_.size());
    for (const auto& event : events_) {
        eve::Value::Object encoded;
        encoded.emplace("from", eve::Value(stateName(event.from)));
        encoded.emplace("kind", eve::Value(event.kind));
        encoded.emplace("orderId", eve::Value(event.orderId));
        encoded.emplace("reason", eve::Value(event.reason));
        encoded.emplace("sequence", eve::Value(std::to_string(event.sequence)));
        encoded.emplace("to", eve::Value(stateName(event.to)));
        events.emplace_back(std::move(encoded));
    }
    root.emplace("events", eve::Value(std::move(events)));

    auto encoded = eve::Value(std::move(root)).toJson();
    if (!encoded.ok()) return eve::Result<std::string>::failure(encoded.status());
    return eve::Result<std::string>::success(std::move(encoded).takeValue());
}

eve::Result<void> CommandQueue::restore(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    eve::Value  snapshotValue = std::move(parsed).takeValue();
    const auto* root          = snapshotValue.getIf<eve::Value::Object>();
    if (!root) return snapshotFailure<void>("command queue snapshot must be an object");

    std::string  schema;
    std::int64_t version = 0;
    if (!readObjectMember(*root, "schema", schema) || schema != snapshotSchema ||
        !readObjectMember(*root, "version", version) || version != snapshotVersion)
        return snapshotFailure<void>("unsupported command queue schema or version", "schema");

    const auto ordersIt = root->find("orders");
    const auto queuedIt = root->find("queued");
    const auto activeIt = root->find("active");
    const auto eventsIt = root->find("events");
    if (ordersIt == root->end() || queuedIt == root->end() || activeIt == root->end() || eventsIt == root->end() ||
        !ordersIt->second.isArray() || !queuedIt->second.isArray() || !eventsIt->second.isArray())
        return snapshotFailure<void>("command queue snapshot is missing required arrays");

    CommandQueue candidate;
    if (!readCounter(*root, "nextId", candidate.nextId_) ||
        !readCounter(*root, "nextSequence", candidate.nextSequence_) || candidate.nextId_ == 0 ||
        candidate.nextSequence_ == 0)
        return snapshotFailure<void>("command queue snapshot has invalid counters", "counters");

    const auto&           encodedOrders = *ordersIt->second.getIf<eve::Value::Array>();
    std::set<std::string> ids;
    std::uint64_t         largestId = 0;
    for (const auto& encoded : encodedOrders) {
        const auto* object = encoded.getIf<eve::Value::Object>();
        if (!object) return snapshotFailure<void>("order entry must be an object", "orders");

        Order        order;
        std::int64_t priority = 0;
        if (!readObjectMember(*object, "id", order.id) || !readObjectMember(*object, "kind", order.kind) ||
            !readObjectMember(*object, "priority", priority) || !readOrderState(*object, "state", order.state) ||
            !readObjectMember(*object, "timeoutSeconds", order.timeoutSeconds) ||
            !readObjectMember(*object, "elapsedSeconds", order.elapsedSeconds) ||
            !readObjectMember(*object, "reason", order.reason))
            return snapshotFailure<void>("order entry has an invalid field", "orders");
        const auto payloadIt = object->find("payload");
        if (payloadIt == object->end() || !payloadIt->second.isObject())
            return snapshotFailure<void>("order payload must be an object", "orders.payload");
        if (priority < std::numeric_limits<int>::min() || priority > std::numeric_limits<int>::max() ||
            order.id.empty() || order.kind.empty() || order.timeoutSeconds < 0.0 || order.elapsedSeconds < 0.0 ||
            !ids.insert(order.id).second)
            return snapshotFailure<void>("order entry violates an invariant", "orders");

        std::uint64_t idNumber = 0;
        if (!generatedOrderId(order.id, idNumber) || idNumber >= candidate.nextId_)
            return snapshotFailure<void>("order id does not match the queue counter", "orders.id");
        largestId             = std::max(largestId, idNumber);
        order.priority        = static_cast<int>(priority);
        order.payload.values_ = *payloadIt->second.getIf<eve::Value::Object>();
        candidate.orders_.push_back(std::move(order));
    }
    if (!candidate.orders_.empty() && largestId >= candidate.nextId_)
        return snapshotFailure<void>("nextId must follow every restored order", "nextId");

    std::vector<bool> queuedMembership(candidate.orders_.size(), false);
    const auto&       encodedQueued = *queuedIt->second.getIf<eve::Value::Array>();
    for (const auto& encodedIndex : encodedQueued) {
        size_t index = 0;
        if (!readIndex(encodedIndex, candidate.orders_.size(), index) || queuedMembership[index] ||
            candidate.orders_[index].state != OrderState::Queued)
            return snapshotFailure<void>("queued membership violates an invariant", "queued");
        if (!candidate.queued_.empty()) {
            const size_t previous = candidate.queued_.back();
            if (candidate.orders_[previous].priority < candidate.orders_[index].priority ||
                (candidate.orders_[previous].priority == candidate.orders_[index].priority && previous > index))
                return snapshotFailure<void>("queued order is not deterministically sorted", "queued");
        }
        queuedMembership[index] = true;
        candidate.queued_.push_back(index);
    }

    if (activeIt->second.isNull()) {
        candidate.active_ = noIndex;
    } else if (!readIndex(activeIt->second, candidate.orders_.size(), candidate.active_) ||
               candidate.orders_[candidate.active_].state != OrderState::Active) {
        return snapshotFailure<void>("active order violates an invariant", "active");
    }
    for (size_t index = 0; index < candidate.orders_.size(); ++index) {
        const auto state = candidate.orders_[index].state;
        if ((state == OrderState::Queued) != queuedMembership[index] ||
            (state == OrderState::Active) != (candidate.active_ == index))
            return snapshotFailure<void>("order state and queue membership disagree", "orders");
    }

    const auto&   encodedEvents    = *eventsIt->second.getIf<eve::Value::Array>();
    std::uint64_t previousSequence = 0;
    for (const auto& encoded : encodedEvents) {
        const auto* object = encoded.getIf<eve::Value::Object>();
        if (!object) return snapshotFailure<void>("event entry must be an object", "events");
        OrderEvent  event;
        std::string from;
        std::string to;
        if (!readCounter(*object, "sequence", event.sequence) || !readObjectMember(*object, "orderId", event.orderId) ||
            !readObjectMember(*object, "kind", event.kind) || !readObjectMember(*object, "from", from) ||
            !readObjectMember(*object, "to", to) || !readObjectMember(*object, "reason", event.reason) ||
            !parseOrderState(from, event.from) || !parseOrderState(to, event.to) || event.sequence == 0 ||
            event.sequence <= previousSequence || event.sequence >= candidate.nextSequence_ || event.kind.empty() ||
            !ids.contains(event.orderId))
            return snapshotFailure<void>("event entry violates an invariant", "events");
        previousSequence = event.sequence;
        candidate.events_.push_back(std::move(event));
    }

    *this = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<CommandQueueHandleRef> Orders::newQueueHandle() {
    Orders* module = Orders::create();
    return module->queues_.emplace(std::make_unique<CommandQueue>());
}

eve::script::Borrowed<CommandQueue> Orders::resolve(CommandQueueHandleRef reference) noexcept {
    Orders* module = ModuleManager::getInstance<Orders>("Orders");
    if (!module) return {};
    return module->queues_.resolve(reference);
}

eve::Result<void> Orders::release(CommandQueueHandleRef reference) {
    Orders* module = ModuleManager::getInstance<Orders>("Orders");
    if (!module)
        return orderFailure<void>(eve::DiagnosticCode::StaleHandle, "Orders module is no longer loaded", "queue");
    return module->queues_.erase(reference);
}

bool Orders::isStale(CommandQueueHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Orders* module = ModuleManager::getInstance<Orders>("Orders");
    return !module || module->queues_.isStale(reference);
}

Module_IMPL(Orders, new Orders());

void Orders::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto              payload = table.addClass<OrderPayload>(
        "OrderPayload", std::function<OrderPayload*()>([]() -> OrderPayload* { return nullptr; }), false);
    payload.addFunc("setString", &OrderPayload::setString);
    payload.addFunc("setNumber", &OrderPayload::setNumber);
    payload.addFunc("setBool", &OrderPayload::setBool);
    payload.addFunc("setNull", &OrderPayload::setNull);
    payload.addFunc("setJson", [vm](OrderPayload* value, const std::string& key, const std::string& json) {
        if (!value)
            return eve::script::projectResult(vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                                     "order payload must not be null", "payload"));
        return eve::script::projectResult(vm, value->setJson(key, json));
    });
    payload.addFunc("has", &OrderPayload::has);
    payload.addFunc("erase", [vm](OrderPayload* value, const std::string& key) {
        if (!value)
            return eve::script::projectResult(
                vm,
                orderFailure<bool>(eve::DiagnosticCode::InvalidArgument, "order payload must not be null", "payload"),
                [](bool removed) { return eve::Value(removed); });
        return eve::script::projectResult(vm, value->erase(key), [](bool removed) { return eve::Value(removed); });
    });
    payload.addFunc("clear", &OrderPayload::clear);
    payload.addFunc("getJson", &OrderPayload::getJson);
    payload.addFunc("toJson", &OrderPayload::toJson);

    auto order = table.addClass<Order>("Order", std::function<Order*()>([]() -> Order* { return nullptr; }), false);
    order.addFunc("getId", [](Order* value) { return value ? value->id : std::string{}; });
    order.addFunc("getKind", [](Order* value) { return value ? value->kind : std::string{}; });
    order.addFunc("getPriority", [](Order* value) { return value ? value->priority : 0; });
    order.addFunc("getState", [](Order* value) { return value ? stateName(value->state) : std::string{}; });
    order.addFunc("getTimeout", [](Order* value) { return value ? static_cast<float>(value->timeoutSeconds) : 0.0f; });
    order.addFunc("getElapsed", [](Order* value) { return value ? static_cast<float>(value->elapsedSeconds) : 0.0f; });
    order.addFunc("getReason", [](Order* value) { return value ? value->reason : std::string{}; });
    order.addFunc("getPayload", [](Order* value) -> OrderPayload* { return value ? &value->payload : nullptr; });

    auto event = table.addClass<OrderEvent>(
        "OrderEvent", std::function<OrderEvent*()>([]() -> OrderEvent* { return nullptr; }), false);
    event.addFunc("getSequence",
                  [](OrderEvent* value) { return value ? static_cast<int64_t>(value->sequence) : int64_t{0}; });
    event.addFunc("getOrderId", [](OrderEvent* value) { return value ? value->orderId : std::string{}; });
    event.addFunc("getKind", [](OrderEvent* value) { return value ? value->kind : std::string{}; });
    event.addFunc("getFrom", [](OrderEvent* value) { return value ? stateName(value->from) : std::string{}; });
    event.addFunc("getTo", [](OrderEvent* value) { return value ? stateName(value->to) : std::string{}; });
    event.addFunc("getReason", [](OrderEvent* value) { return value ? value->reason : std::string{}; });

    auto queue = table.addClass<CommandQueue>(
        "CommandQueue", std::function<CommandQueue*()>([]() -> CommandQueue* { return nullptr; }), false);
    queue.addFunc("append", [vm](CommandQueue* value, const std::string& kind, int priority, float timeout) {
        if (!value)
            return eve::script::projectResult(vm,
                                              orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                                        "order queue must not be null", "queue"),
                                              [](std::string&& id) { return eve::Value(std::move(id)); });
        return eve::script::projectResult(vm, value->append(kind, priority, timeout),
                                          [](std::string&& id) { return eve::Value(std::move(id)); });
    });
    queue.addFunc("replace", [vm](CommandQueue* value, const std::string& kind, int priority, float timeout) {
        if (!value)
            return eve::script::projectResult(vm,
                                              orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                                        "order queue must not be null", "queue"),
                                              [](std::string&& id) { return eve::Value(std::move(id)); });
        return eve::script::projectResult(vm, value->replace(kind, priority, timeout),
                                          [](std::string&& id) { return eve::Value(std::move(id)); });
    });
    queue.addFunc("interrupt", [vm](CommandQueue* value, const std::string& kind, int priority, float timeout) {
        if (!value)
            return eve::script::projectResult(vm,
                                              orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                                        "order queue must not be null", "queue"),
                                              [](std::string&& id) { return eve::Value(std::move(id)); });
        return eve::script::projectResult(vm, value->interrupt(kind, priority, timeout),
                                          [](std::string&& id) { return eve::Value(std::move(id)); });
    });
    queue.addFunc("complete", [vm](CommandQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order queue must not be null", "queue"));
        return eve::script::projectResult(vm, value->complete(id));
    });
    queue.addFunc("fail", [vm](CommandQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order queue must not be null", "queue"));
        return eve::script::projectResult(vm, value->fail(id, reason));
    });
    queue.addFunc("cancel", [vm](CommandQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order queue must not be null", "queue"));
        return eve::script::projectResult(vm, value->cancel(id, reason));
    });
    queue.addFunc("update", [vm](CommandQueue* value, float dt) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "order queue must not be null", "queue"));
        return eve::script::projectResult(vm, value->update(dt));
    });
    queue.addFunc("clear", &CommandQueue::clear);
    queue.addFunc("current", [](CommandQueue* value) -> Order* {
        if (!value) return nullptr;
        auto result = value->current();
        return result ? &result->get() : nullptr;
    });
    queue.addFunc("find", [](CommandQueue* value, const std::string& id) -> Order* {
        if (!value) return nullptr;
        auto result = value->find(id);
        return result ? &result->get() : nullptr;
    });
    queue.addFunc("queuedCount", &CommandQueue::queuedCount);
    queue.addFunc("orderCount", &CommandQueue::orderCount);
    queue.addFunc("orderAt", [](CommandQueue* value, int index) -> Order* {
        if (!value) return nullptr;
        auto result = value->orderAt(index);
        return result ? &result->get() : nullptr;
    });
    queue.addFunc("eventCount", &CommandQueue::eventCount);
    queue.addFunc("eventAt", [](CommandQueue* value, int index) -> OrderEvent* {
        if (!value) return nullptr;
        auto result = value->eventAt(index);
        return result ? &result->get() : nullptr;
    });
    queue.addFunc("clearEvents", &CommandQueue::clearEvents);

    // Canonical script ownership path. The returned proxy is an Owned script
    // object (Squirrel release hook); its queue is addressed only by a
    // generation-qualified RuntimeHandleRef and resolves as Borrowed per call.
    auto ownedQueue = table.addClass<ScriptCommandQueue>(
        "OwnedOrderQueue", std::function<ScriptCommandQueue*()>([]() -> ScriptCommandQueue* { return nullptr; }),
        false);
    ownedQueue.addFunc("ownership", [](ScriptCommandQueue*) {
        return std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Owned));
    });
    ownedQueue.addFunc("handle",
                       [](ScriptCommandQueue* value) { return value ? value->reference.packed() : std::uint64_t{0}; });
    ownedQueue.addFunc("isStale",
                       [](ScriptCommandQueue* value) { return !value || Orders::isStale(value->reference); });
    ownedQueue.addFunc("release", [vm](ScriptCommandQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "owned order queue proxy must not be null",
                                       "queue"));
        auto result = Orders::release(value->reference);
        // Retain the coordinates after release so the same script object can
        // report a stale handle and a second release returns StaleHandle.
        return eve::script::projectResult(vm, std::move(result));
    });
    ownedQueue.addFunc("append", [vm](ScriptCommandQueue* value, const std::string& kind, int priority, float timeout) {
        if (!value)
            return eve::script::projectResult(
                vm,
                orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                          "owned order queue proxy must not be null", "queue"),
                [](std::string&& id) { return eve::Value(std::move(id)); });
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound())
            return eve::script::projectResult(vm,
                                              orderFailure<std::string>(eve::DiagnosticCode::StaleHandle,
                                                                        "owned order queue handle is stale", "queue"),
                                              [](std::string&& id) { return eve::Value(std::move(id)); });
        return eve::script::projectResult(vm, queueView->append(kind, priority, timeout),
                                          [](std::string&& id) { return eve::Value(std::move(id)); });
    });
    ownedQueue.addFunc("current", [](ScriptCommandQueue* value) -> Order* {
        if (!value) return nullptr;
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound()) return nullptr;
        auto current = queueView->current();
        return current ? &current->get() : nullptr;
    });
    ownedQueue.addFunc("find", [](ScriptCommandQueue* value, const std::string& id) -> Order* {
        if (!value) return nullptr;
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound()) return nullptr;
        auto order = queueView->find(id);
        return order ? &order->get() : nullptr;
    });
    ownedQueue.addFunc("complete", [vm](ScriptCommandQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "owned order queue proxy must not be null",
                                       "queue"));
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound())
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::StaleHandle, "owned order queue handle is stale", "queue"));
        return eve::script::projectResult(vm, queueView->complete(id));
    });
    ownedQueue.addFunc("snapshot", [vm](ScriptCommandQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                orderFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                          "owned order queue proxy must not be null", "queue"),
                [](std::string&& text) { return eve::Value(std::move(text)); });
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound())
            return eve::script::projectResult(vm,
                                              orderFailure<std::string>(eve::DiagnosticCode::StaleHandle,
                                                                        "owned order queue handle is stale", "queue"),
                                              [](std::string&& text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, queueView->snapshot(),
                                          [](std::string&& text) { return eve::Value(std::move(text)); });
    });
    ownedQueue.addFunc("restore", [vm](ScriptCommandQueue* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "owned order queue proxy must not be null",
                                       "queue"));
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound())
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::StaleHandle, "owned order queue handle is stale", "queue"));
        return eve::script::projectResult(vm, queueView->restore(json));
    });
    ownedQueue.addFunc("update", [vm](ScriptCommandQueue* value, float dt) {
        if (!value)
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::InvalidArgument, "owned order queue proxy must not be null",
                                       "queue"));
        auto queueView = Orders::resolve(value->reference);
        if (!queueView.isBound())
            return eve::script::projectResult(
                vm, orderFailure<void>(eve::DiagnosticCode::StaleHandle, "owned order queue handle is stale", "queue"));
        return eve::script::projectResult(vm, queueView->update(dt));
    });
    ownedQueue.addFunc("clear", [](ScriptCommandQueue* value) {
        if (!value) return;
        auto queueView = Orders::resolve(value->reference);
        if (queueView.isBound()) queueView->clear();
    });

    auto cls = table.addClass(name, Orders::create, false);
    expose(cls);
}

void Orders::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Orders::getName);
    cls.addFunc("newQueueOwned", [vm = cls.getHandle()](Orders*) -> ssq::Table {
        auto reference = Orders::newQueueHandle();
        if (!reference) {
            return eve::script::projectStatusResult(vm, reference.status(), false, false);
        }
        const auto ref = std::move(reference).takeValue();
        auto       object =
            eve::script::makeOwnedSquirrelInstance<ScriptCommandQueue>(vm, std::make_unique<ScriptCommandQueue>(ref));
        if (!object) {
            object.ignore("failed to create owned order queue proxy");
            Orders::release(ref).ignore("rollback failed owned order queue allocation");
            return eve::script::projectStatusResult(vm, object.status(), false, false);
        }
        ssq::Object owned = std::move(object).takeValue();
        auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
        result.set("value", owned);
        return result;
    });
}

}  // namespace eve::orders
