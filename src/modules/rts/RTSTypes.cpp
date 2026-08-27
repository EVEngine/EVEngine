#include "rts/RTSTypes.h"

#include "attributes/AttributeSet.h"
#include "common/Json.h"
#include "effects/EffectContainer.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool finitePosition(WorldPosition position) {
    return std::isfinite(position.x) && std::isfinite(position.y);
}

bool handleEqual(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

std::optional<OrderKind> parseOrderKind(std::string_view kind) {
    if (kind == "move") return OrderKind::Move;
    if (kind == "attack") return OrderKind::Attack;
    if (kind == "build") return OrderKind::Build;
    if (kind == "gather") return OrderKind::Gather;
    return std::nullopt;
}

Result<OrderRecord> projectOrder(const orders::Order& order) {
    const auto kind = parseOrderKind(order.kind);
    if (!kind) {
        return failure<OrderRecord>(DiagnosticCode::InvariantViolation,
                                    "generic order contains an unknown RTS kind", "order.kind");
    }

    const std::string json = order.payload.toJson();
    std::string errorText;
    auto document = json::Document::parse(json, &errorText);
    if (!document.valid() || !document.root().isObject()) {
        return failure<OrderRecord>(DiagnosticCode::ParseError,
                                    "generic order payload is not a JSON object", "order.payload");
    }

    const auto root = document.root();
    OrderRecord result;
    result.id = order.id;
    result.kind = *kind;
    result.target.x = root.get("x").asFloat(0.0f);
    result.target.y = root.get("y").asFloat(0.0f);
    result.definitionId = root.get("definition").asString();
    result.formationSlot = root.get("formationSlot").asInt(-1);
    if (!finitePosition(result.target)) {
        return failure<OrderRecord>(DiagnosticCode::InvariantViolation,
                                    "generic order payload contains a non-finite target", "order.payload");
    }
    return Result<OrderRecord>::success(std::move(result));
}

void touchUnitComponents(Unit& unit) {
    (void)unit.identity();
    (void)unit.definition();
    (void)unit.attributes();
    (void)unit.tags();
    (void)unit.effects();
    (void)unit.orders();
    (void)unit.sensing();
    (void)unit.steering();
    (void)unit.crowd();
    (void)unit.weapon();
    (void)unit.action();
    (void)unit.settlement();
    (void)unit.faction();
    (void)unit.motion();
}

void touchBuildingComponents(Building& building) {
    (void)building.identity();
    (void)building.definition();
    (void)building.placement();
    (void)building.production();
    (void)building.economy();
    (void)building.orders();
    (void)building.tags();
    (void)building.effects();
    (void)building.settlement();
    (void)building.faction();
}

void touchPlayerComponents(Player& player) {
    (void)player.identity();
    (void)player.authority();
    (void)player.economy();
    (void)player.social();
    (void)player.selection();
    (void)player.eventStream();
}

void touchFactionComponents(Faction& faction) {
    (void)faction.identity();
    (void)faction.authority();
    (void)faction.economy();
    (void)faction.social();
    (void)faction.members();
    (void)faction.eventStream();
}

}  // namespace

const char* orderKindName(OrderKind kind) noexcept {
    switch (kind) {
    case OrderKind::Move: return "move";
    case OrderKind::Attack: return "attack";
    case OrderKind::Build: return "build";
    case OrderKind::Gather: return "gather";
    }
    return "unknown";
}

Result<void> CommandSpec::validate() const {
    if (!finitePosition(target)) {
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS command target must contain finite coordinates", "target");
    }
    if (!std::isfinite(timeoutSeconds) || timeoutSeconds < 0.0) {
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS command timeout must be finite and non-negative", "timeoutSeconds");
    }
    switch (kind) {
    case OrderKind::Move:
    case OrderKind::Attack:
    case OrderKind::Build:
    case OrderKind::Gather:
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return failure(DiagnosticCode::InvalidArgument, "RTS command kind is invalid", "kind");
}

Result<void> TagSet::add(std::string_view tag) {
    if (tag.empty())
        return failure(DiagnosticCode::InvalidArgument, "RTS tag must not be empty", "tag");
    auto it = std::lower_bound(values_.begin(), values_.end(), tag,
                               [](const std::string& value, std::string_view key) { return value < key; });
    if (it != values_.end() && *it == tag)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    values_.insert(it, std::string(tag));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TagSet::remove(std::string_view tag) {
    if (tag.empty())
        return failure(DiagnosticCode::InvalidArgument, "RTS tag must not be empty", "tag");
    auto it = std::lower_bound(values_.begin(), values_.end(), tag,
                               [](const std::string& value, std::string_view key) { return value < key; });
    if (it == values_.end() || *it != tag)
        return failure(DiagnosticCode::NotFound, "RTS tag is not present", "tag");
    values_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool TagSet::contains(std::string_view tag) const noexcept {
    const auto it = std::lower_bound(values_.begin(), values_.end(), tag,
                                     [](const std::string& value, std::string_view key) { return value < key; });
    return it != values_.end() && *it == tag;
}

struct AttributeComponent::Impl {
    attributes::AttributeProjection values;
};

AttributeComponent::AttributeComponent() : impl_(std::make_unique<Impl>()) {}
AttributeComponent::~AttributeComponent() = default;
AttributeComponent::AttributeComponent(const AttributeComponent& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}
AttributeComponent& AttributeComponent::operator=(const AttributeComponent& other) {
    if (this == &other) return *this;
    impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr;
    return *this;
}
AttributeComponent::AttributeComponent(AttributeComponent&& other) noexcept = default;
AttributeComponent& AttributeComponent::operator=(AttributeComponent&& other) noexcept = default;

Result<void> AttributeComponent::setBase(std::string_view attribute, double value) {
    if (attribute.empty() || !std::isfinite(value)) {
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS attribute base requires a non-empty finite value", "attribute");
    }
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.setBase(attribute, value);
}

Result<void> AttributeComponent::modifyBase(std::string_view attribute, double delta) {
    if (attribute.empty() || !std::isfinite(delta)) {
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS attribute delta requires a non-empty finite value", "attribute");
    }
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.modifyBase(attribute, delta);
}

bool AttributeComponent::has(std::string_view attribute) const {
    return impl_ && impl_->values.has(std::string(attribute));
}

Result<double> AttributeComponent::getFinal(std::string_view attribute, double fallback) const {
    if (attribute.empty() || !std::isfinite(fallback)) {
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "RTS attribute query requires a non-empty finite key/fallback", "attribute");
    }
    if (!impl_) return Result<double>::success(fallback, Status::success(StatusCode::NoOp));
    return impl_->values.getFinal(attribute, fallback);
}

std::size_t AttributeComponent::modifierCount() const noexcept {
    return impl_ ? static_cast<std::size_t>(impl_->values.modifierCount()) : 0u;
}

const eve::attributes::AttributeModifier* AttributeComponent::modifierAt(int index) const noexcept {
    return impl_ ? impl_->values.modifierAt(index) : nullptr;
}

Result<void> AttributeComponent::bindOwner(ecs::EntityHandle owner) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.bindOwner(owner);
}

bool AttributeComponent::initialized() const noexcept {
    return impl_ != nullptr && impl_->values.initialized();
}

Result<void> AttributeComponent::initialize(
    std::span<const eve::attributes::AttributeSnapshotBase> values) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.initialize(values);
}

bool AttributeComponent::isStale() const noexcept {
    return impl_ != nullptr && impl_->values.isStale();
}

Result<eve::attributes::ModifierId> AttributeComponent::addModifier(
    eve::attributes::AttributeModifier modifier) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.addModifier(std::move(modifier));
}

Result<eve::attributes::AttributeProjectionSnapshot> AttributeComponent::snapshot(
    std::span<const std::string_view> attributes) const {
    if (!impl_)
        return Result<eve::attributes::AttributeProjectionSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation,
                              "RTS attribute component is not initialized", "attributes"));
    return impl_->values.snapshot(attributes);
}

Result<void> AttributeComponent::restore(
    const eve::attributes::AttributeProjectionSnapshot& snapshot,
    Revision expectedRevision) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.restore(snapshot, expectedRevision);
}

struct OrderComponent::Impl {
    orders::CommandQueue queue;
};

OrderComponent::OrderComponent() : impl_(std::make_unique<Impl>()) {}
OrderComponent::~OrderComponent() = default;
OrderComponent::OrderComponent(const OrderComponent& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}
OrderComponent& OrderComponent::operator=(const OrderComponent& other) {
    if (this == &other) return *this;
    impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr;
    return *this;
}
OrderComponent::OrderComponent(OrderComponent&& other) noexcept = default;
OrderComponent& OrderComponent::operator=(OrderComponent&& other) noexcept = default;

Result<std::string> OrderComponent::enqueue(const CommandSpec& command, int formationSlot) {
    auto valid = command.validate();
    if (!valid) return failureFrom<std::string>(valid.status());
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto queued = impl_->queue.append(orderKindName(command.kind), command.priority,
                                      command.timeoutSeconds);
    if (!queued) return failureFrom<std::string>(queued.status());
    const std::string id = std::move(queued).takeValue();
    auto order = impl_->queue.find(id);
    if (!order)
        return failure<std::string>(DiagnosticCode::InvariantViolation,
                                    "generic order queue lost its newly appended command", "order");
    order->get().payload.setNumber("x", command.target.x);
    order->get().payload.setNumber("y", command.target.y);
    order->get().payload.setString("definition", command.definitionId);
    order->get().payload.setNumber("formationSlot", static_cast<double>(formationSlot));
    return Result<std::string>::success(id, Status::success(StatusCode::Applied));
}

Result<std::string> OrderComponent::replace(const CommandSpec& command, int formationSlot) {
    auto valid = command.validate();
    if (!valid) return failureFrom<std::string>(valid.status());
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto queued = impl_->queue.replace(orderKindName(command.kind), command.priority,
                                       command.timeoutSeconds);
    if (!queued) return failureFrom<std::string>(queued.status());
    const std::string id = std::move(queued).takeValue();
    auto order = impl_->queue.find(id);
    if (!order)
        return failure<std::string>(DiagnosticCode::InvariantViolation,
                                    "generic order queue lost its replacement command", "order");
    order->get().payload.setNumber("x", command.target.x);
    order->get().payload.setNumber("y", command.target.y);
    order->get().payload.setString("definition", command.definitionId);
    order->get().payload.setNumber("formationSlot", static_cast<double>(formationSlot));
    return Result<std::string>::success(id, Status::success(StatusCode::Applied));
}

Result<OrderRecord> OrderComponent::current() const {
    if (!impl_ || !impl_->queue.current())
        return failure<OrderRecord>(DiagnosticCode::NotFound, "RTS order queue is idle", "order");
    return projectOrder(impl_->queue.current()->get());
}

Result<void> OrderComponent::complete(std::string_view orderId) {
    if (orderId.empty())
        return failure(DiagnosticCode::InvalidArgument, "RTS order id must not be empty", "orderId");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.complete(std::string(orderId)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is not active or is already terminal", "orderId");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> OrderComponent::fail(std::string_view orderId, std::string_view reason) {
    if (orderId.empty() || reason.empty())
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS order failure requires an id and reason", "order");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.fail(std::string(orderId), std::string(reason)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is not active", "orderId");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> OrderComponent::cancel(std::string_view orderId, std::string_view reason) {
    if (orderId.empty() || reason.empty())
        return failure(DiagnosticCode::InvalidArgument,
                       "RTS order cancellation requires an id and reason", "order");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.cancel(std::string(orderId), std::string(reason)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is already terminal", "orderId");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool OrderComponent::empty() const noexcept {
    return !impl_ || !impl_->queue.current();
}

std::size_t OrderComponent::orderCount() const noexcept {
    return impl_ ? static_cast<std::size_t>(impl_->queue.orderCount()) : 0u;
}

orders::CommandQueue *OrderComponent::queueForComposition() noexcept {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return &impl_->queue;
}

struct ProductionComponent::Impl {
    production::WorkQueue queue;

    Impl() = default;
    Impl(const Impl& other) {
        auto snapshot = other.queue.snapshot();
        if (snapshot) (void)queue.restore(snapshot.value());
    }
    Impl& operator=(const Impl& other) {
        if (this == &other) return *this;
        auto snapshot = other.queue.snapshot();
        if (snapshot) (void)queue.restore(snapshot.value());
        return *this;
    }
};

ProductionComponent::ProductionComponent() : impl_(std::make_unique<Impl>()) {}
ProductionComponent::~ProductionComponent() = default;
ProductionComponent::ProductionComponent(const ProductionComponent& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}
ProductionComponent& ProductionComponent::operator=(const ProductionComponent& other) {
    if (this == &other) return *this;
    impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr;
    return *this;
}
ProductionComponent::ProductionComponent(ProductionComponent&& other) noexcept = default;
ProductionComponent& ProductionComponent::operator=(ProductionComponent&& other) noexcept = default;

Result<std::string> ProductionComponent::enqueue(std::string_view owner, std::string_view kind,
                                                 std::string_view product, Duration duration,
                                                 int priority) {
    if (owner.empty() || kind.empty() || product.empty() || duration.nanoseconds() <= 0)
        return failure<std::string>(DiagnosticCode::InvalidArgument,
                                    "RTS production requires non-empty ids and a positive duration", "production");
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->queue.enqueue(owner, kind, product, eve::Value(eve::Value::Object{}),
                                duration.seconds(), priority);
}

Result<void> ProductionComponent::advance(const SimulationStep& step) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->queue.advance(step);
}

std::size_t ProductionComponent::taskCount() const noexcept {
    return impl_ ? static_cast<std::size_t>(impl_->queue.taskCount()) : 0u;
}

production::WorkQueue *ProductionComponent::queueForComposition() noexcept {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return &impl_->queue;
}

Unit* Unit::createUnit(SubjectRef subject, LogicalId definition) {
    Unit* unit = Unit::create();
    unit->identity()->self = ecs::handle_of(unit);
    unit->identity()->subject = subject;
    unit->definition()->id = std::move(definition);
    touchUnitComponents(*unit);
    return unit;
}

Building* Building::createBuilding(SubjectRef subject, LogicalId definition) {
    Building* building = Building::create();
    building->identity()->self = ecs::handle_of(building);
    building->identity()->subject = subject;
    building->definition()->id = std::move(definition);
    touchBuildingComponents(*building);
    return building;
}

Player* Player::createPlayer(SubjectRef subject) {
    Player* player = Player::create();
    player->identity()->self = ecs::handle_of(player);
    player->identity()->subject = subject;
    touchPlayerComponents(*player);
    return player;
}

Faction* Faction::createFaction(SubjectRef subject) {
    Faction* faction = Faction::create();
    faction->identity()->self = ecs::handle_of(faction);
    faction->identity()->subject = subject;
    touchFactionComponents(*faction);
    return faction;
}

}  // namespace eve::rts
