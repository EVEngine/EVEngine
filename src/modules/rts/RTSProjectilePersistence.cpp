/** @file RTSProjectilePersistence.cpp
 * @brief rts module-interface: complete rts.projectiles v1 portable pool/payload closure; no ECS, callbacks or
 * services.
 * @remarks Pool handles are durable correspondences under this RTS root; native targets are rebound separately.
 */
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include "rts/RTSSystems.h"
namespace eve::rts {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "rts.projectiles"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
const Value::Array& array(const Value& v, std::size_t limit = 1048576) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > limit) invalid("Invalid projectile array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t size) {
    const auto& a = array(v);
    if (a.size() != size) invalid("Unknown or missing projectile tuple fields");
    return a;
}
const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected projectile text");
    return *s;
}
std::uint64_t counter(const Value& v, std::uint64_t max = UINT64_MAX) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    const auto    p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s || n > max)
        invalid("Invalid projectile counter");
    return n;
}
Value scalar(const std::string& v) { return v; }
Value scalar(bool v) { return v; }
Value scalar(double v) { return v; }
Value scalar(float v) { return v; }
Value scalar(std::uint64_t v) { return std::to_string(v); }
Value scalar(SubjectRef v) { return v.format(); }
Value scalar(WorldPosition v) { return Value::Array{v.x, v.y}; }
Value scalar(const std::vector<std::string>& tags) {
    Value::Array a;
    for (const auto& t : tags) a.emplace_back(t);
    return a;
}
void read(const Value& v, std::string& out) { out = text(v); }
void read(const Value& v, bool& out) {
    const auto* b = v.getIf<bool>();
    if (!b) invalid("Expected projectile bool");
    out = *b;
}
void read(const Value& v, double& out) {
    if (!v.isDouble() && !v.isInt64()) invalid("Expected projectile number");
    out = v.isDouble() ? v.asDouble() : double(v.asInt());
    if (!std::isfinite(out)) invalid("Nonfinite projectile number");
}
void read(const Value& v, float& out) {
    double d = 0;
    read(v, d);
    if (std::abs(d) > std::numeric_limits<float>::max()) invalid("Projectile float exceeds range");
    out = float(d);
}
void read(const Value& v, std::uint64_t& out) { out = counter(v); }
void read(const Value& v, SubjectRef& out) {
    const auto& s  = text(v);
    auto        id = PersistentId::parse(s);
    if (!id || id->format() != s) invalid("Invalid projectile subject");
    out = SubjectRef::fromPersistentId(*id);
}
void read(const Value& v, WorldPosition& out) {
    const auto& a = tuple(v, 2);
    read(a[0], out.x);
    read(a[1], out.y);
}
void read(const Value& v, std::vector<std::string>& out) {
    for (const auto& t : array(v)) out.push_back(text(t));
}
// The same typed ledger drives both directions; relationships never contain process handles.
template <class P>
auto payloadFields(P& p) {
    return std::tie(p.key, p.source, p.faction, p.target, p.observer, p.targetPoint, p.targetHeight, p.damageType,
                    p.damage, p.radius, p.splashMinimumDamageFactor, p.targetsGround, p.targetsAir, p.friendlyFire,
                    p.blockedByObstacles, p.requiredTargetTags, p.excludedTargetTags, p.trajectoryTarget);
}
Value payload(const RTSProjectilePayloadSnapshot& p) {
    Value::Array a;
    std::apply([&](const auto&... x) { (a.push_back(scalar(x)), ...); }, payloadFields(p));
    return a;
}
RTSProjectilePayloadSnapshot payload(const Value& v) {
    RTSProjectilePayloadSnapshot p;
    auto                         fields = payloadFields(p);
    const auto&                  a      = tuple(v, std::tuple_size_v<decltype(fields)>);
    std::size_t                  i      = 0;
    std::apply([&](auto&... x) { (read(a[i++], x), ...); }, fields);
    return p;
}
Value                   point(weapon::ProjectilePoint p) { return Value::Array{p.x, p.y, p.z}; }
weapon::ProjectilePoint point(const Value& v) {
    const auto&             a = tuple(v, 3);
    weapon::ProjectilePoint p;
    read(a[0], p.x);
    read(a[1], p.y);
    read(a[2], p.z);
    return p;
}
Value trajectory(const weapon::ProjectileState& s) {
    return Value::Array{std::to_string(s.handle.slot),
                        std::to_string(s.handle.generation),
                        s.definitionId.format(),
                        int(s.mode),
                        point(s.position),
                        point({s.velocity.x, s.velocity.y, s.velocity.z}),
                        s.gravity,
                        s.maxTurnRateDegrees,
                        std::to_string(s.age.nanoseconds()),
                        std::to_string(s.lifetime.nanoseconds())};
}
weapon::ProjectileState trajectory(const Value& v) {
    const auto&             a = tuple(v, 10);
    weapon::ProjectileState s;
    s.handle      = {std::uint32_t(counter(a[0], UINT32_MAX)), std::uint32_t(counter(a[1], UINT32_MAX))};
    const auto id = LogicalId::parse(text(a[2]));
    if (!id) invalid("Invalid projectile definition");
    s.definitionId = *id;
    if (!a[3].isInt64() || a[3].asInt() < 0 || a[3].asInt() > int(weapon::ProjectileMode::Homing))
        invalid("Unknown projectile mode");
    s.mode              = weapon::ProjectileMode(a[3].asInt());
    s.position          = point(a[4]);
    const auto velocity = point(a[5]);
    s.velocity          = {velocity.x, velocity.y, velocity.z};
    read(a[6], s.gravity);
    read(a[7], s.maxTurnRateDegrees);
    s.age      = Duration(std::int64_t(counter(a[8], INT64_MAX)));
    s.lifetime = Duration(std::int64_t(counter(a[9], INT64_MAX)));
    return s;
}
}  // namespace
Result<void> RTSProjectileSystemSnapshot::validate() const {
    if (runtime.capacity == 0 || runtime.capacity > runtime.slots.size() ||
        runtime.slots.size() > weapon::ProjectileRuntime::maximumCapacity())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Projectile pool capacity outside 1..1048576", "projectiles.slots"));
    std::set<std::uint64_t> keys;
    for (std::size_t i = 0; i < runtime.slots.size(); ++i) {
        const auto& slot = runtime.slots[i];
        if (!slot.state) continue;
        const auto& state = *slot.state;
        auto        valid = state.validateTrajectory();
        if (!valid) return valid;
        if (i >= runtime.capacity || state.target || state.handle.slot != i ||
            state.handle.generation != slot.generation)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Portable trajectory contains a process target or wrong slot", "projectiles.slots"));
        keys.insert(state.handle.packedKey());
    }
    if (keys.size() != payloads.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Projectile trajectory/payload counts differ", "projectiles.payloads"));
    for (const auto& value : payloads) {
        if (keys.erase(value.key) != 1 || !value.source.isValid() || !value.faction.isValid() ||
            !std::isfinite(value.targetPoint.x) || !std::isfinite(value.targetPoint.y) ||
            !std::isfinite(value.targetHeight) || value.damageType.empty() || !std::isfinite(value.damage) ||
            value.damage < 0 || !std::isfinite(value.radius) || value.radius < 0 ||
            !std::isfinite(value.splashMinimumDamageFactor) || value.splashMinimumDamageFactor < 0 ||
            value.splashMinimumDamageFactor > 1 || (!value.targetsGround && !value.targetsAir) ||
            value.requiredTargetTags.size() > 1048576 || value.excludedTargetTags.size() > 1048576)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Invalid or duplicated projectile payload", "projectiles.payloads"));
        const auto slot = weapon::ProjectileHandle::fromPackedKey(value.key).slot;
        if (runtime.slots[slot].state->mode == weapon::ProjectileMode::Homing && !value.trajectoryTarget.isValid())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Homing projectile lacks stable target", "projectiles.target"));
    }
    return Result<void>::success();
}
Result<Value> RTSProjectileSystemSnapshot::encodeState() const {
    auto admitted = validate();
    if (!admitted) return Result<Value>::failure(admitted.status());
    Value::Array slots, values;
    slots.reserve(runtime.slots.size());
    values.reserve(payloads.size());
    for (const auto& slot : runtime.slots)
        slots.push_back(Value::Array{std::to_string(slot.generation), slot.state ? trajectory(*slot.state) : Value{}});
    for (const auto& p : payloads) values.push_back(payload(p));
    return Result<Value>::success(Value::object({{"schema", "rts.projectiles"},
                                                 {"version", std::int64_t(1)},
                                                 {"capacity", std::to_string(runtime.capacity)},
                                                 {"slots", std::move(slots)},
                                                 {"payloads", std::move(values)}}));
}
Result<RTSProjectileSystemSnapshot> RTSProjectileSystemSnapshot::prepareState(const Value& v) {
    using R = Result<RTSProjectileSystemSnapshot>;
    try {
        const auto* o = v.getIf<Value::Object>();
        if (!o || o->size() != 5) invalid("Unknown or missing projectile fields");
        for (const auto* key : {"schema", "version", "capacity", "slots", "payloads"})
            if (!o->contains(key)) invalid("Missing projectile field");
        if (text(o->at("schema")) != "rts.projectiles")
            invalid("Unknown projectile schema", DiagnosticCode::Unsupported);
        if (!o->at("version").isInt64() || o->at("version").asInt() != 1)
            invalid("Unknown projectile version", DiagnosticCode::UnknownVersion);
        RTSProjectileSystemSnapshot result;
        result.runtime.capacity =
            std::uint32_t(counter(o->at("capacity"), weapon::ProjectileRuntime::maximumCapacity()));
        for (const auto& row : array(o->at("slots"))) {
            const auto&                    a = tuple(row, 2);
            weapon::ProjectileSlotSnapshot slot;
            slot.generation = std::uint32_t(counter(a[0], UINT32_MAX));
            if (!a[1].isNull()) slot.state = trajectory(a[1]);
            result.runtime.slots.push_back(std::move(slot));
        }
        for (const auto& row : array(o->at("payloads"))) result.payloads.push_back(payload(row));
        auto admitted = result.validate();
        if (!admitted) return R::failure(admitted.status());
        return R::success(std::move(result));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::rts
