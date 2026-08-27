#include "vehicle/VehicleOrderQueueAdapter.h"

#include "orders/CommandQueue.h"

#include <unordered_map>
#include <utility>

namespace eve::vehicle {

namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "vehicle.orders"));
}

}  // namespace

struct VehicleOrderQueueAdapter::Impl {
    orders::CommandQueue queue;
    std::unordered_map<std::string, VehicleOrder> payloads;
};

void VehicleOrderQueueAdapter::pruneTerminalPayloads() {
    for (auto it = impl_->payloads.begin(); it != impl_->payloads.end();) {
        auto recordRef = impl_->queue.find(it->first);
        const orders::Order* record = recordRef ? &recordRef->get() : nullptr;
        if (record == nullptr || (record->state != orders::OrderState::Queued &&
                                  record->state != orders::OrderState::Active)) {
            it = impl_->payloads.erase(it);
        } else {
            ++it;
        }
    }
}

VehicleOrderQueueAdapter::VehicleOrderQueueAdapter() : impl_(std::make_unique<Impl>()) {}

VehicleOrderQueueAdapter::~VehicleOrderQueueAdapter() = default;

VehicleOrderQueueAdapter::VehicleOrderQueueAdapter(const VehicleOrderQueueAdapter& other)
    : impl_(std::make_unique<Impl>(*other.impl_)) {}

VehicleOrderQueueAdapter& VehicleOrderQueueAdapter::operator=(const VehicleOrderQueueAdapter& other) {
    if (this != &other) *impl_ = *other.impl_;
    return *this;
}

VehicleOrderQueueAdapter::VehicleOrderQueueAdapter(VehicleOrderQueueAdapter&&) noexcept = default;

VehicleOrderQueueAdapter& VehicleOrderQueueAdapter::operator=(VehicleOrderQueueAdapter&&) noexcept = default;

eve::Result<std::string> VehicleOrderQueueAdapter::append(const VehicleOrder& order, int priority,
                                                          double timeoutSeconds) {
    auto id = impl_->queue.append(vehicleOrderTypeName(order.type), priority, timeoutSeconds);
    if (!id) return id;
    const std::string key = std::move(id).takeValue();
    impl_->payloads.emplace(key, order);
    return eve::Result<std::string>::success(std::move(key),
                                             eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> VehicleOrderQueueAdapter::replace(const VehicleOrder& order, int priority,
                                                           double timeoutSeconds) {
    auto id = impl_->queue.replace(vehicleOrderTypeName(order.type), priority, timeoutSeconds);
    if (!id) return id;
    const std::string key = std::move(id).takeValue();
    pruneTerminalPayloads();
    impl_->payloads.emplace(key, order);
    return eve::Result<std::string>::success(std::move(key),
                                             eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> VehicleOrderQueueAdapter::interrupt(const VehicleOrder& order, int priority,
                                                             double timeoutSeconds) {
    auto id = impl_->queue.interrupt(vehicleOrderTypeName(order.type), priority, timeoutSeconds);
    if (!id) return id;
    const std::string key = std::move(id).takeValue();
    pruneTerminalPayloads();
    impl_->payloads.emplace(key, order);
    return eve::Result<std::string>::success(std::move(key),
                                             eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> VehicleOrderQueueAdapter::completeCurrent() {
    auto currentRef = impl_->queue.current();
    orders::Order* current = currentRef ? &currentRef->get() : nullptr;
    if (current == nullptr)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "vehicle order queue has no active order", "order");
    auto completed = impl_->queue.complete(current->id);
    if (completed) pruneTerminalPayloads();
    return completed;
}

eve::Result<void> VehicleOrderQueueAdapter::failCurrent(const std::string& reason) {
    auto currentRef = impl_->queue.current();
    orders::Order* current = currentRef ? &currentRef->get() : nullptr;
    if (current == nullptr)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "vehicle order queue has no active order", "order");
    auto failed = impl_->queue.fail(current->id, reason);
    if (failed) pruneTerminalPayloads();
    return failed;
}

eve::Result<void> VehicleOrderQueueAdapter::cancel(const std::string& id, const std::string& reason) {
    auto cancelled = impl_->queue.cancel(id, reason);
    if (cancelled) pruneTerminalPayloads();
    return cancelled;
}

void VehicleOrderQueueAdapter::update(double dtSeconds) {
    auto updated = impl_->queue.update(dtSeconds);
    updated.ignore("VehicleOrderQueueAdapter exposes a legacy void update boundary");
    pruneTerminalPayloads();
}

void VehicleOrderQueueAdapter::clear() {
    impl_->queue.clear();
    impl_->payloads.clear();
}

const VehicleOrder* VehicleOrderQueueAdapter::current() const {
    auto recordRef = impl_->queue.current();
    const orders::Order* record = recordRef ? &recordRef->get() : nullptr;
    if (record == nullptr) return nullptr;
    const auto it = impl_->payloads.find(record->id);
    return it == impl_->payloads.end() ? nullptr : &it->second;
}

int VehicleOrderQueueAdapter::activeOrQueuedCount() const {
    int count = 0;
    for (int i = 0; i < impl_->queue.orderCount(); ++i) {
        auto recordRef = impl_->queue.orderAt(i);
        const orders::Order* record = recordRef ? &recordRef->get() : nullptr;
        if (record != nullptr && (record->state == orders::OrderState::Queued ||
                                  record->state == orders::OrderState::Active)) {
            ++count;
        }
    }
    return count;
}

void VehicleOrderQueueAdapter::syncCompatibility(VehicleEntity::Orders& legacy) const {
    legacy.queue.clear();
    legacy.current = -1;
    for (int i = 0; i < impl_->queue.orderCount(); ++i) {
        auto recordRef = impl_->queue.orderAt(i);
        const orders::Order* record = recordRef ? &recordRef->get() : nullptr;
        if (record == nullptr || (record->state != orders::OrderState::Queued &&
                                  record->state != orders::OrderState::Active)) {
            continue;
        }
        const auto it = impl_->payloads.find(record->id);
        if (it == impl_->payloads.end()) continue;
        if (record->state == orders::OrderState::Active) legacy.current = static_cast<int>(legacy.queue.size());
        legacy.queue.push_back(it->second);
    }
}

VehicleEntity::Orders::Orders() : adapter(std::make_unique<VehicleOrderQueueAdapter>()) {}

VehicleEntity::Orders::~Orders() = default;

VehicleEntity::Orders::Orders(const Orders& other)
    : queue(other.queue),
      current(other.current),
      adapter(other.adapter == nullptr ? std::make_unique<VehicleOrderQueueAdapter>()
                                       : std::make_unique<VehicleOrderQueueAdapter>(*other.adapter)) {}

VehicleEntity::Orders& VehicleEntity::Orders::operator=(const Orders& other) {
    if (this == &other) return *this;
    queue   = other.queue;
    current = other.current;
    if (other.adapter == nullptr) {
        adapter = std::make_unique<VehicleOrderQueueAdapter>();
    } else if (adapter == nullptr) {
        adapter = std::make_unique<VehicleOrderQueueAdapter>(*other.adapter);
    } else {
        *adapter = *other.adapter;
    }
    return *this;
}

VehicleEntity::Orders::Orders(Orders&&) noexcept = default;

VehicleEntity::Orders& VehicleEntity::Orders::operator=(Orders&&) noexcept = default;

}  // namespace eve::vehicle
