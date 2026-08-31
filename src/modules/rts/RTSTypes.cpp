#include "rts/RTSTypes.h"

#include "attributes/AttributeSet.h"
#include "common/Json.h"
#include "effects/EffectContainer.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"

#include <algorithm>
#include <cmath>
#include <map>
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

bool finitePosition(WorldPosition position) { return std::isfinite(position.x) && std::isfinite(position.y); }

bool handleEqual(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

std::optional<OrderKind> parseOrderKind(std::string_view kind) {
    if (kind == "move") return OrderKind::Move;
    if (kind == "attack") return OrderKind::Attack;
    if (kind == "build") return OrderKind::Build;
    if (kind == "gather") return OrderKind::Gather;
    if (kind == "return_cargo") return OrderKind::ReturnCargo;
    if (kind == "attack_move") return OrderKind::AttackMove;
    if (kind == "stop") return OrderKind::Stop;
    if (kind == "hold_position") return OrderKind::HoldPosition;
    if (kind == "patrol") return OrderKind::Patrol;
    if (kind == "repair") return OrderKind::Repair;
    if (kind == "garrison") return OrderKind::Garrison;
    if (kind == "board_transport") return OrderKind::BoardTransport;
    if (kind == "capture") return OrderKind::Capture;
    if (kind == "attack_ground") return OrderKind::AttackGround;
    if (kind == "resupply") return OrderKind::Resupply;
    if (kind == "escort") return OrderKind::Escort;
    if (kind == "suppress_area") return OrderKind::SuppressArea;
    if (kind == "supply_relay") return OrderKind::SupplyRelay;
    return std::nullopt;
}

Result<OrderRecord> projectOrder(const orders::Order& order, const CommandSpec* extended = nullptr) {
    const auto kind = parseOrderKind(order.kind);
    if (!kind) {
        return failure<OrderRecord>(DiagnosticCode::InvariantViolation, "generic order contains an unknown RTS kind",
                                    "order.kind");
    }

    const std::string json = order.payload.toJson();
    std::string       errorText;
    auto              document = json::Document::parse(json, &errorText);
    if (!document.valid() || !document.root().isObject()) {
        return failure<OrderRecord>(DiagnosticCode::ParseError, "generic order payload is not a JSON object",
                                    "order.payload");
    }

    const auto  root = document.root();
    OrderRecord result;
    result.id            = order.id;
    result.kind          = *kind;
    result.target.x      = root.get("x").asFloat(0.0f);
    result.target.y      = root.get("y").asFloat(0.0f);
    result.definitionId  = root.get("definition").asString();
    result.formationSlot = root.get("formationSlot").asInt(-1);
    result.secondaryTarget.x = root.get("secondaryX").asFloat(0.0f);
    result.secondaryTarget.y = root.get("secondaryY").asFloat(0.0f);
    result.radius = root.get("radius").asFloat(0.0f);
    result.append = root.get("append").asBool(false);
    if (extended != nullptr) {
        result.targetEntity   = extended->targetEntity;
    }
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
    (void)unit.navigation();
    (void)unit.vision();
    (void)unit.worker();
    (void)unit.combat();
    (void)unit.durability();
    (void)unit.capture();
    (void)unit.containment();
    (void)unit.supply();
    (void)unit.morale();
    (void)unit.artillery();
    (void)unit.tactics();
    (void)unit.technology();
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
    (void)building.construction();
    (void)building.integrity();
    (void)building.capture();
    (void)building.dropoff();
    (void)building.rally();
    (void)building.weapon();
    (void)building.combat();
    (void)building.garrison();
    (void)building.supply();
    (void)building.vision();
    (void)building.technology();
}

void touchResourceNodeComponents(ResourceNode& node) {
    (void)node.identity();
    (void)node.position();
    (void)node.stock();
    (void)node.harvest();
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
    (void)faction.strategy();
    (void)faction.workforce();
    (void)faction.productionPolicy();
    (void)faction.intel();
    (void)faction.technology();
}

}  // namespace

const char* combatStanceName(CombatStance stance) noexcept {
    switch (stance) {
        case CombatStance::Passive: return "passive";
        case CombatStance::Defensive: return "defensive";
        case CombatStance::Aggressive: return "aggressive";
    }
    return "defensive";
}

const char* orderKindName(OrderKind kind) noexcept {
    switch (kind) {
        case OrderKind::Move: return "move";
        case OrderKind::Attack: return "attack";
        case OrderKind::Build: return "build";
        case OrderKind::Gather: return "gather";
        case OrderKind::ReturnCargo: return "return_cargo";
        case OrderKind::AttackMove: return "attack_move";
        case OrderKind::Stop: return "stop";
        case OrderKind::HoldPosition: return "hold_position";
        case OrderKind::Patrol: return "patrol";
        case OrderKind::Repair: return "repair";
        case OrderKind::Garrison: return "garrison";
        case OrderKind::BoardTransport: return "board_transport";
        case OrderKind::Capture: return "capture";
        case OrderKind::AttackGround: return "attack_ground";
        case OrderKind::Resupply: return "resupply";
        case OrderKind::Escort: return "escort";
        case OrderKind::SuppressArea: return "suppress_area";
        case OrderKind::SupplyRelay: return "supply_relay";
    }
    return "unknown";
}

Result<void> CommandSpec::validate() const {
    if (!finitePosition(target) || !finitePosition(secondaryTarget)) {
        return failure(DiagnosticCode::InvalidArgument, "RTS command target must contain finite coordinates", "target");
    }
    if (!std::isfinite(timeoutSeconds) || timeoutSeconds < 0.0) {
        return failure(DiagnosticCode::InvalidArgument, "RTS command timeout must be finite and non-negative",
                       "timeoutSeconds");
    }
    if (!std::isfinite(radius) || radius < 0.0f)
        return failure(DiagnosticCode::InvalidArgument, "RTS command radius must be finite and non-negative",
                       "radius");
    switch (kind) {
        case OrderKind::Move:
        case OrderKind::Attack:
        case OrderKind::Build:
        case OrderKind::Gather:
        case OrderKind::ReturnCargo:
        case OrderKind::AttackMove:
        case OrderKind::Stop:
        case OrderKind::HoldPosition:
        case OrderKind::Patrol:
        case OrderKind::Repair:
        case OrderKind::Garrison:
        case OrderKind::BoardTransport:
        case OrderKind::Capture:
        case OrderKind::AttackGround:
        case OrderKind::Resupply:
        case OrderKind::Escort:
        case OrderKind::SuppressArea:
        case OrderKind::SupplyRelay: return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return failure(DiagnosticCode::InvalidArgument, "RTS command kind is invalid", "kind");
}

Result<void> TagSet::add(std::string_view tag) {
    if (tag.empty()) return failure(DiagnosticCode::InvalidArgument, "RTS tag must not be empty", "tag");
    auto it = std::lower_bound(values_.begin(), values_.end(), tag,
                               [](const std::string& value, std::string_view key) { return value < key; });
    if (it != values_.end() && *it == tag) return Result<void>::success(Status::success(StatusCode::NoOp));
    values_.insert(it, std::string(tag));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TagSet::remove(std::string_view tag) {
    if (tag.empty()) return failure(DiagnosticCode::InvalidArgument, "RTS tag must not be empty", "tag");
    auto it = std::lower_bound(values_.begin(), values_.end(), tag,
                               [](const std::string& value, std::string_view key) { return value < key; });
    if (it == values_.end() || *it != tag) return failure(DiagnosticCode::NotFound, "RTS tag is not present", "tag");
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
AttributeComponent::AttributeComponent(AttributeComponent&& other) noexcept            = default;
AttributeComponent& AttributeComponent::operator=(AttributeComponent&& other) noexcept = default;

Result<void> AttributeComponent::setBase(std::string_view attribute, double value) {
    if (attribute.empty() || !std::isfinite(value)) {
        return failure(DiagnosticCode::InvalidArgument, "RTS attribute base requires a non-empty finite value",
                       "attribute");
    }
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.setBase(attribute, value);
}

Result<void> AttributeComponent::modifyBase(std::string_view attribute, double delta) {
    if (attribute.empty() || !std::isfinite(delta)) {
        return failure(DiagnosticCode::InvalidArgument, "RTS attribute delta requires a non-empty finite value",
                       "attribute");
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

bool AttributeComponent::initialized() const noexcept { return impl_ != nullptr && impl_->values.initialized(); }

Result<void> AttributeComponent::initialize(std::span<const eve::attributes::AttributeSnapshotBase> values) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.initialize(values);
}

bool AttributeComponent::isStale() const noexcept { return impl_ != nullptr && impl_->values.isStale(); }

Result<eve::attributes::ModifierId> AttributeComponent::addModifier(eve::attributes::AttributeModifier modifier) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.addModifier(std::move(modifier));
}

Result<eve::attributes::AttributeProjectionSnapshot> AttributeComponent::snapshot(
    std::span<const std::string_view> attributes) const {
    if (!impl_)
        return Result<eve::attributes::AttributeProjectionSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "RTS attribute component is not initialized", "attributes"));
    return impl_->values.snapshot(attributes);
}

Result<void> AttributeComponent::restore(const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                         Revision                                            expectedRevision) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->values.restore(snapshot, expectedRevision);
}

struct OrderComponent::Impl {
    orders::CommandQueue queue;
    std::map<std::string, CommandSpec> extended;
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
OrderComponent::OrderComponent(OrderComponent&& other) noexcept            = default;
OrderComponent& OrderComponent::operator=(OrderComponent&& other) noexcept = default;

Result<std::string> OrderComponent::enqueue(const CommandSpec& command, int formationSlot) {
    auto valid = command.validate();
    if (!valid) return failureFrom<std::string>(valid.status());
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto queued = impl_->queue.append(orderKindName(command.kind), command.priority, command.timeoutSeconds);
    if (!queued) return failureFrom<std::string>(queued.status());
    const std::string id    = std::move(queued).takeValue();
    auto              order = impl_->queue.find(id);
    if (!order)
        return failure<std::string>(DiagnosticCode::InvariantViolation,
                                    "generic order queue lost its newly appended command", "order");
    order->get().payload.setNumber("x", command.target.x);
    order->get().payload.setNumber("y", command.target.y);
    order->get().payload.setString("definition", command.definitionId);
    order->get().payload.setNumber("formationSlot", static_cast<double>(formationSlot));
    order->get().payload.setNumber("secondaryX", command.secondaryTarget.x);
    order->get().payload.setNumber("secondaryY", command.secondaryTarget.y);
    order->get().payload.setNumber("radius", command.radius);
    order->get().payload.setBool("append", command.append);
    impl_->extended[id] = command;
    return Result<std::string>::success(id, Status::success(StatusCode::Applied));
}

Result<std::string> OrderComponent::replace(const CommandSpec& command, int formationSlot) {
    auto valid = command.validate();
    if (!valid) return failureFrom<std::string>(valid.status());
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto queued = impl_->queue.replace(orderKindName(command.kind), command.priority, command.timeoutSeconds);
    if (!queued) return failureFrom<std::string>(queued.status());
    const std::string id    = std::move(queued).takeValue();
    auto              order = impl_->queue.find(id);
    if (!order)
        return failure<std::string>(DiagnosticCode::InvariantViolation,
                                    "generic order queue lost its replacement command", "order");
    order->get().payload.setNumber("x", command.target.x);
    order->get().payload.setNumber("y", command.target.y);
    order->get().payload.setString("definition", command.definitionId);
    order->get().payload.setNumber("formationSlot", static_cast<double>(formationSlot));
    order->get().payload.setNumber("secondaryX", command.secondaryTarget.x);
    order->get().payload.setNumber("secondaryY", command.secondaryTarget.y);
    order->get().payload.setNumber("radius", command.radius);
    order->get().payload.setBool("append", command.append);
    impl_->extended.clear();
    impl_->extended[id] = command;
    return Result<std::string>::success(id, Status::success(StatusCode::Applied));
}

Result<OrderRecord> OrderComponent::current() const {
    if (!impl_ || !impl_->queue.current())
        return failure<OrderRecord>(DiagnosticCode::NotFound, "RTS order queue is idle", "order");
    const auto& order = impl_->queue.current()->get();
    const auto  found = impl_->extended.find(order.id);
    return projectOrder(order, found == impl_->extended.end() ? nullptr : &found->second);
}

Result<void> OrderComponent::complete(std::string_view orderId) {
    if (orderId.empty()) return failure(DiagnosticCode::InvalidArgument, "RTS order id must not be empty", "orderId");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.complete(std::string(orderId)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is not active or is already terminal", "orderId");
    impl_->extended.erase(std::string(orderId));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> OrderComponent::fail(std::string_view orderId, std::string_view reason) {
    if (orderId.empty() || reason.empty())
        return failure(DiagnosticCode::InvalidArgument, "RTS order failure requires an id and reason", "order");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.fail(std::string(orderId), std::string(reason)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is not active", "orderId");
    impl_->extended.erase(std::string(orderId));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> OrderComponent::cancel(std::string_view orderId, std::string_view reason) {
    if (orderId.empty() || reason.empty())
        return failure(DiagnosticCode::InvalidArgument, "RTS order cancellation requires an id and reason", "order");
    if (!impl_ || !impl_->queue.find(std::string(orderId)))
        return failure(DiagnosticCode::NotFound, "RTS order id is not present", "orderId");
    if (!impl_->queue.cancel(std::string(orderId), std::string(reason)).ok())
        return failure(DiagnosticCode::Conflict, "RTS order is already terminal", "orderId");
    impl_->extended.erase(std::string(orderId));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool OrderComponent::empty() const noexcept { return !impl_ || !impl_->queue.current(); }

void OrderComponent::clear() noexcept {
    if (!impl_) return;
    impl_->queue.clear();
    impl_->extended.clear();
}

std::size_t OrderComponent::orderCount() const noexcept {
    return impl_ ? static_cast<std::size_t>(impl_->queue.orderCount()) : 0u;
}

Result<std::string> OrderComponent::snapshot() const {
    if (!impl_) return failure<std::string>(DiagnosticCode::Failed, "RTS order component is unavailable", "orders");
    return impl_->queue.snapshot();
}

Result<void> OrderComponent::restore(std::string_view json) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    orders::CommandQueue candidate;
    auto restored = candidate.restore(json);
    if (!restored) return restored;
    impl_->queue = std::move(candidate);
    impl_->extended.clear();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<OrderComponent::Snapshot> OrderComponent::snapshotState() const {
    if (!impl_)
        return failure<Snapshot>(DiagnosticCode::Failed, "RTS order component is unavailable", "orders");
    auto json = impl_->queue.snapshot();
    if (!json) return failureFrom<Snapshot>(json.status());
    return Result<Snapshot>::success({std::move(json).takeValue(), impl_->extended},
                                     Status::success(StatusCode::Applied));
}

Result<void> OrderComponent::restoreState(const Snapshot& snapshotValue) {
    orders::CommandQueue queue;
    auto restored = queue.restore(snapshotValue.queueJson);
    if (!restored) return restored;
    for (const auto& [id, command] : snapshotValue.extended) {
        auto valid = command.validate();
        if (!valid) return valid;
        if (!queue.find(id))
            return failure(DiagnosticCode::Conflict,
                           "RTS order extension references an absent canonical queue record", "orders.extended");
    }
    auto candidate = std::make_unique<Impl>();
    candidate->queue = std::move(queue);
    candidate->extended = snapshotValue.extended;
    impl_ = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

orders::CommandQueue* OrderComponent::queueForComposition() noexcept {
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
ProductionComponent::ProductionComponent(ProductionComponent&& other) noexcept            = default;
ProductionComponent& ProductionComponent::operator=(ProductionComponent&& other) noexcept = default;

Result<std::string> ProductionComponent::enqueue(std::string_view owner, std::string_view kind,
                                                 std::string_view product, Duration duration, int priority) {
    if (owner.empty() || kind.empty() || product.empty() || duration.nanoseconds() <= 0)
        return failure<std::string>(DiagnosticCode::InvalidArgument,
                                    "RTS production requires non-empty ids and a positive duration", "production");
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->queue.enqueue(owner, kind, product, eve::Value(eve::Value::Object{}), duration.seconds(), priority);
}

Result<void> ProductionComponent::advance(const SimulationStep& step) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->queue.advance(step);
}

std::size_t ProductionComponent::taskCount() const noexcept {
    return impl_ ? static_cast<std::size_t>(impl_->queue.taskCount()) : 0u;
}

OptionalRef<production::ProductionTask> ProductionComponent::taskAt(int index) {
    return impl_ ? impl_->queue.taskAt(index) : OptionalRef<production::ProductionTask>{};
}

OptionalRef<production::ProductionTask> ProductionComponent::find(std::string_view taskId) {
    return impl_ ? impl_->queue.find(taskId) : OptionalRef<production::ProductionTask>{};
}

Result<void> ProductionComponent::pause(std::string_view taskId) {
    if (!impl_) return failure<void>(DiagnosticCode::NotFound, "RTS production task was not found", "taskId");
    return impl_->queue.pause(taskId);
}

Result<void> ProductionComponent::resume(std::string_view taskId) {
    if (!impl_) return failure<void>(DiagnosticCode::NotFound, "RTS production task was not found", "taskId");
    return impl_->queue.resume(taskId);
}

Result<void> ProductionComponent::cancel(std::string_view taskId, std::string_view reason) {
    if (!impl_) return failure<void>(DiagnosticCode::NotFound, "RTS production task was not found", "taskId");
    return impl_->queue.cancel(taskId, reason);
}

std::vector<production::ProductionTask> ProductionComponent::completed(std::string_view kind) const {
    std::vector<production::ProductionTask> result;
    if (!impl_ || kind.empty()) return result;
    for (int index = 0; index < impl_->queue.taskCount(); ++index) {
        const auto task = impl_->queue.taskAt(index);
        if (task && task->get().kind == kind && task->get().state == production::TaskState::Completed)
            result.push_back(task->get());
    }
    return result;
}

Result<std::string> ProductionComponent::snapshot() const {
    if (!impl_)
        return failure<std::string>(DiagnosticCode::Failed, "RTS production component is unavailable", "production");
    return impl_->queue.snapshot();
}

Result<void> ProductionComponent::restore(std::string_view json) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    production::WorkQueue candidate;
    auto restored = candidate.restore(json);
    if (!restored) return restored;
    impl_->queue = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

production::WorkQueue* ProductionComponent::queueForComposition() noexcept {
    if (!impl_) impl_ = std::make_unique<Impl>();
    return &impl_->queue;
}

Unit* Unit::createUnit(SubjectRef subject, LogicalId definition) {
    Unit* unit                = Unit::create();
    unit->identity()->self    = ecs::handle_of(unit);
    unit->identity()->subject = subject;
    unit->definition()->id    = std::move(definition);
    unit->durability()->state.subject = subject;
    unit->durability()->state.health = 1.0;
    unit->durability()->state.maxHealth = 1.0;
    touchUnitComponents(*unit);
    return unit;
}

Building* Building::createBuilding(SubjectRef subject, LogicalId definition) {
    Building* building            = Building::create();
    building->identity()->self    = ecs::handle_of(building);
    building->identity()->subject = subject;
    building->definition()->id    = std::move(definition);
    building->integrity()->state.subject = subject;
    building->integrity()->state.health = 1.0;
    building->integrity()->state.maxHealth = 1.0;
    touchBuildingComponents(*building);
    return building;
}

ResourceNode* ResourceNode::createResourceNode(SubjectRef subject) {
    ResourceNode* node        = ResourceNode::create();
    node->identity()->self    = ecs::handle_of(node);
    node->identity()->subject = subject;
    touchResourceNodeComponents(*node);
    return node;
}

Player* Player::createPlayer(SubjectRef subject) {
    Player* player              = Player::create();
    player->identity()->self    = ecs::handle_of(player);
    player->identity()->subject = subject;
    touchPlayerComponents(*player);
    return player;
}

Faction* Faction::createFaction(SubjectRef subject) {
    Faction* faction             = Faction::create();
    faction->identity()->self    = ecs::handle_of(faction);
    faction->identity()->subject = subject;
    touchFactionComponents(*faction);
    return faction;
}

Match* Match::createMatch(SubjectRef subject) {
    Match* match = Match::create();
    if (match == nullptr) return nullptr;
    match->identity()->self = ecs::handle_of(match);
    match->identity()->subject = subject;
    (void)match->rules();
    (void)match->participants();
    (void)match->state();
    (void)match->events();
    return match;
}

}  // namespace eve::rts
