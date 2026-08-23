#include "orders/Orders.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::orders {
namespace {

constexpr size_t noIndex = static_cast<size_t>(-1);

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

bool plausibleJsonValue(const std::string& json) {
    const auto first = json.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    const auto last = json.find_last_not_of(" \t\r\n");
    const char head = json[first];
    const char tail = json[last];
    if ((head == '{' && tail == '}') || (head == '[' && tail == ']') || (head == '"' && tail == '"')) {
        return true;
    }
    if (json.substr(first, last - first + 1) == "true" || json.substr(first, last - first + 1) == "false" ||
        json.substr(first, last - first + 1) == "null") {
        return true;
    }
    char* end = nullptr;
    std::strtod(json.c_str() + first, &end);
    return end == json.c_str() + last + 1;
}

}  // namespace

void OrderPayload::setString(const std::string& key, const std::string& value) {
    if (!key.empty()) values_[key] = escapeJson(value);
}

void OrderPayload::setNumber(const std::string& key, double value) {
    if (key.empty() || !std::isfinite(value)) return;
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    values_[key] = out.str();
}

void OrderPayload::setBool(const std::string& key, bool value) {
    if (!key.empty()) values_[key] = value ? "true" : "false";
}

void OrderPayload::setNull(const std::string& key) {
    if (!key.empty()) values_[key] = "null";
}

bool OrderPayload::setJson(const std::string& key, const std::string& json) {
    if (key.empty() || !plausibleJsonValue(json)) return false;
    const auto first = json.find_first_not_of(" \t\r\n");
    const auto last  = json.find_last_not_of(" \t\r\n");
    values_[key]     = json.substr(first, last - first + 1);
    return true;
}

bool OrderPayload::has(const std::string& key) const { return values_.contains(key); }

bool OrderPayload::erase(const std::string& key) { return values_.erase(key) != 0; }

void OrderPayload::clear() { values_.clear(); }

std::string OrderPayload::getJson(const std::string& key) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::string{} : it->second;
}

std::string OrderPayload::toJson() const {
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto& [key, value] : values_) {
        if (!first) out << ',';
        first = false;
        out << escapeJson(key) << ':' << value;
    }
    out << '}';
    return out.str();
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

std::string OrderQueue::create(const std::string& kind, int priority, double timeoutSeconds, bool activateNow) {
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
        {nextSequence_++, orders_[index].id, orders_[index].kind, OrderState::Queued, OrderState::Queued, {}});
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

std::string OrderQueue::append(const std::string& kind, int priority, double timeoutSeconds) {
    return create(kind, priority, timeoutSeconds, false);
}

std::string OrderQueue::replace(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty()) return {};
    cancelUnfinished("replaced");
    return create(kind, priority, timeoutSeconds, true);
}

std::string OrderQueue::interrupt(const std::string& kind, int priority, double timeoutSeconds) {
    if (kind.empty()) return {};
    if (active_ != noIndex && priority < orders_[active_].priority) return {};
    if (active_ != noIndex) transition(orders_[active_], OrderState::Cancelled, "interrupted");
    active_ = noIndex;
    return create(kind, priority, timeoutSeconds, true);
}

bool OrderQueue::transition(Order& order, OrderState state, const std::string& reason) {
    if (order.state == OrderState::Completed || order.state == OrderState::Failed ||
        order.state == OrderState::Cancelled || order.state == state) {
        return false;
    }
    const OrderState from = order.state;
    order.state           = state;
    order.reason          = reason;
    events_.push_back({nextSequence_++, order.id, order.kind, from, state, reason});
    return true;
}

void OrderQueue::activateNext() {
    if (active_ != noIndex || queued_.empty()) return;
    active_ = queued_.front();
    queued_.erase(queued_.begin());
    transition(orders_[active_], OrderState::Active, {});
}

bool OrderQueue::complete(const std::string& id) {
    if (active_ == noIndex || orders_[active_].id != id) return false;
    if (!transition(orders_[active_], OrderState::Completed, {})) return false;
    active_ = noIndex;
    activateNext();
    return true;
}

bool OrderQueue::fail(const std::string& id, const std::string& reason) {
    if (active_ == noIndex || orders_[active_].id != id) return false;
    if (!transition(orders_[active_], OrderState::Failed, reason)) return false;
    active_ = noIndex;
    activateNext();
    return true;
}

bool OrderQueue::cancel(const std::string& id, const std::string& reason) {
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

void OrderQueue::update(double dtSeconds) {
    if (active_ == noIndex || dtSeconds <= 0.0) return;
    Order& order = orders_[active_];
    order.elapsedSeconds += dtSeconds;
    if (order.timeoutSeconds > 0.0 && order.elapsedSeconds >= order.timeoutSeconds) {
        fail(order.id, "timeout");
    }
}

void OrderQueue::cancelUnfinished(const std::string& reason) {
    if (active_ != noIndex) transition(orders_[active_], OrderState::Cancelled, reason);
    active_ = noIndex;
    for (size_t index : queued_) transition(orders_[index], OrderState::Cancelled, reason);
    queued_.clear();
}

void OrderQueue::clear() {
    orders_.clear();
    queued_.clear();
    events_.clear();
    active_       = noIndex;
    nextId_       = 1;
    nextSequence_ = 1;
}

Order* OrderQueue::current() { return active_ == noIndex ? nullptr : &orders_[active_]; }

Order* OrderQueue::find(const std::string& id) {
    const auto it = std::find_if(orders_.begin(), orders_.end(), [&id](const Order& order) { return order.id == id; });
    return it == orders_.end() ? nullptr : &*it;
}

int OrderQueue::queuedCount() const { return static_cast<int>(queued_.size()); }

int OrderQueue::orderCount() const { return static_cast<int>(orders_.size()); }

Order* OrderQueue::orderAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= orders_.size()) return nullptr;
    return &orders_[static_cast<size_t>(index)];
}

int OrderQueue::eventCount() const { return static_cast<int>(events_.size()); }

OrderEvent* OrderQueue::eventAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return nullptr;
    return &events_[static_cast<size_t>(index)];
}

void OrderQueue::clearEvents() { events_.clear(); }

OrderQueue* Orders::newQueue() {
    Orders* module = Orders::create();
    module->queues_.push_back(std::make_unique<OrderQueue>());
    return module->queues_.back().get();
}

Module_IMPL(Orders, new Orders());

void Orders::expose(ssq::Table& table) {
    auto payload = table.addClass<OrderPayload>(
        "OrderPayload", std::function<OrderPayload*()>([]() -> OrderPayload* { return nullptr; }), false);
    payload.addFunc("setString", &OrderPayload::setString);
    payload.addFunc("setNumber", &OrderPayload::setNumber);
    payload.addFunc("setBool", &OrderPayload::setBool);
    payload.addFunc("setNull", &OrderPayload::setNull);
    payload.addFunc("setJson", &OrderPayload::setJson);
    payload.addFunc("has", &OrderPayload::has);
    payload.addFunc("erase", &OrderPayload::erase);
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

    auto queue = table.addClass<OrderQueue>(
        "OrderQueue", std::function<OrderQueue*()>([]() -> OrderQueue* { return nullptr; }), false);
    queue.addFunc("append", [](OrderQueue* value, const std::string& kind, int priority, float timeout) {
        return value ? value->append(kind, priority, timeout) : std::string{};
    });
    queue.addFunc("replace", [](OrderQueue* value, const std::string& kind, int priority, float timeout) {
        return value ? value->replace(kind, priority, timeout) : std::string{};
    });
    queue.addFunc("interrupt", [](OrderQueue* value, const std::string& kind, int priority, float timeout) {
        return value ? value->interrupt(kind, priority, timeout) : std::string{};
    });
    queue.addFunc("complete", &OrderQueue::complete);
    queue.addFunc("fail", &OrderQueue::fail);
    queue.addFunc("cancel", &OrderQueue::cancel);
    queue.addFunc("update", &OrderQueue::update);
    queue.addFunc("clear", &OrderQueue::clear);
    queue.addFunc("current", [](OrderQueue* value) -> Order* { return value ? value->current() : nullptr; });
    queue.addFunc("find", &OrderQueue::find);
    queue.addFunc("queuedCount", &OrderQueue::queuedCount);
    queue.addFunc("orderCount", &OrderQueue::orderCount);
    queue.addFunc("orderAt", &OrderQueue::orderAt);
    queue.addFunc("eventCount", &OrderQueue::eventCount);
    queue.addFunc("eventAt", &OrderQueue::eventAt);
    queue.addFunc("clearEvents", &OrderQueue::clearEvents);

    auto cls = table.addClass(name, Orders::create, false);
    expose(cls);
}

void Orders::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Orders::getName);
    cls.addFunc("newQueue", [](Orders*) { return Orders::newQueue(); });
}

}  // namespace eve::orders
