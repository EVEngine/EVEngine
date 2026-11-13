#include "rts/RTSOrderState.h"
#include <cmath>
#include <limits>
#include <tuple>
#include "rts/RTSOrderStateInternal.h"
namespace eve::rts {
namespace order_state_internal {
Result<orders::CommandQueue> prepareQueue(const OrderComponent::Snapshot& snapshot) {
    orders::CommandQueue queue;
    auto                 restored = queue.restore(snapshot.queueJson);
    if (!restored) return Result<orders::CommandQueue>::failure(restored.status());
    for (const auto& [id, command] : snapshot.extended) {
        auto valid = command.validate();
        if (!valid) return Result<orders::CommandQueue>::failure(valid.status());
        if (!queue.find(id))
            return Result<orders::CommandQueue>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS order extension references an absent canonical queue record",
                "orders.extended"));
    }
    return Result<orders::CommandQueue>::success(std::move(queue));
}
}  // namespace order_state_internal
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.orders"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
const Value::Array& array(const Value& value) {
    const auto* a = value.getIf<Value::Array>();
    if (!a || a->size() > 1048576) invalid("Invalid orders array");
    return *a;
}
const std::string& text(const Value& value) {
    const auto* s = value.getIf<std::string>();
    if (!s) invalid("Expected orders text");
    return *s;
}
void read(const Value& value, std::string& out) { out = text(value); }
void read(const Value& value, bool& out) {
    const auto* b = value.getIf<bool>();
    if (!b) invalid("Expected command boolean");
    out = *b;
}
void read(const Value& value, int& out) {
    if (!value.isInt64() || value.asInt() < std::numeric_limits<int>::min() ||
        value.asInt() > std::numeric_limits<int>::max())
        invalid("Command integer out of range");
    out = int(value.asInt());
}
void read(const Value& value, OrderKind& out) {
    int n = 0;
    read(value, n);
    if (n < 0 || n > int(OrderKind::SupplyRelay)) invalid("Unknown command kind");
    out = OrderKind(n);
}
void read(const Value& value, double& out) {
    if (!value.isDouble() && !value.isInt64()) invalid("Expected command number");
    out = value.isDouble() ? value.asDouble() : double(value.asInt());
    if (!std::isfinite(out)) invalid("Nonfinite command number");
}
void read(const Value& value, float& out) {
    double d = 0;
    read(value, d);
    if (std::abs(d) > std::numeric_limits<float>::max()) invalid("Command float out of range");
    out = float(d);
}
void read(const Value& value, WorldPosition& out) {
    const auto& a = array(value);
    if (a.size() != 2) invalid("Unknown command position fields");
    read(a[0], out.x);
    read(a[1], out.y);
}
Value scalar(const std::string& value) { return value; }
Value scalar(bool value) { return value; }
Value scalar(int value) { return value; }
Value scalar(OrderKind value) { return int(value); }
Value scalar(double value) { return value; }
Value scalar(float value) { return value; }
Value scalar(WorldPosition value) { return Value::Array{value.x, value.y}; }
// One field ledger for both directions. Native entity targets never enter the row.
template <class T>
auto fields(T& c) {
    return std::tie(c.kind, c.target, c.definitionId, c.priority, c.timeoutSeconds, c.secondaryTarget, c.radius,
                    c.append);
}
Result<void> portable(const RTSOrderState& state) {
    if (state.orders.extended.size() > 1048576 || state.targets.size() > 1048576)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Too many order extensions"));
    for (const auto& [id, c] : state.orders.extended)
        if (c.targetEntity.table || c.targetEntity.id || c.targetEntity.generation ||
            c.targetEntity.type != typeid(void))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Portable command contains native target", "orders.extended"));
    for (const auto& [id, target] : state.targets)
        if (!target.isValid() || !state.orders.extended.contains(id))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Stable order target lacks a command or identity", "orders.targets"));
    return Result<void>::success();
}
}  // namespace
Result<Value> RTSOrderState::encodeState() const {
    try {
        auto p = portable(*this);
        if (!p) return Result<Value>::failure(p.status());
        auto         queue     = take(order_state_internal::prepareQueue(orders));
        auto         json      = take(queue.snapshot());
        auto         canonical = take(Value::fromJson(json));
        Value::Array rows;
        for (const auto& [id, c] : orders.extended) {
            Value::Array row{id};
            std::apply([&](const auto&... v) { (row.push_back(scalar(v)), ...); }, fields(c));
            auto target = targets.find(id);
            row.push_back(target == targets.end() ? Value{} : Value(target->second.format()));
            rows.emplace_back(std::move(row));
        }
        return Result<Value>::success(Value::object({{"schema", "rts.orders"},
                                                     {"version", 1},
                                                     {"queue", std::move(canonical)},
                                                     {"commands", std::move(rows)}}));
    } catch (const Invalid& e) {
        return Result<Value>::failure(e.status);
    }
}
Result<RTSOrderState> RTSOrderState::prepareState(const Value& value) {
    try {
        const auto* o = value.getIf<Value::Object>();
        if (!o || o->size() != 4) invalid("Unknown or missing orders fields");
        for (const auto* key : {"schema", "version", "queue", "commands"})
            if (!o->contains(key)) invalid("Missing orders field");
        if (text(o->at("schema")) != "rts.orders" || !o->at("version").isInt64() || o->at("version").asInt() != 1)
            invalid("Unsupported orders schema or version");
        RTSOrderState result;
        result.orders.queueJson = take(o->at("queue").toJson());
        std::string previous;
        for (const auto& v : array(o->at("commands"))) {
            const auto&    row = array(v);
            CommandSpec    c;
            auto           f     = fields(c);
            constexpr auto count = std::tuple_size_v<decltype(f)>;
            if (row.size() != count + 2) invalid("Unknown or missing command fields");
            auto id = text(row[0]);
            if (id.empty() || (!previous.empty() && id <= previous)) invalid("Duplicated or unordered command id");
            previous      = id;
            std::size_t i = 1;
            std::apply([&](auto&... x) { (read(row[i++], x), ...); }, f);
            if (!row.back().isNull()) {
                const auto& s      = text(row.back());
                auto        target = PersistentId::parse(s);
                if (!target || target->isNil() || target->format() != s) invalid("Invalid command target identity");
                result.targets.emplace(id, SubjectRef::fromPersistentId(*target));
            }
            result.orders.extended.emplace(std::move(id), std::move(c));
        }
        auto queue              = take(order_state_internal::prepareQueue(result.orders));
        result.orders.queueJson = take(queue.snapshot());
        return Result<RTSOrderState>::success(std::move(result));
    } catch (const Invalid& e) {
        return Result<RTSOrderState>::failure(e.status);
    }
}
}  // namespace eve::rts
