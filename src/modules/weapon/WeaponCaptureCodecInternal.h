#pragma once
// Private wire layout for complete typed weapon facts. Each tuple is listed once for both directions.
#include <charconv>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>
#include "weapon/WeaponCapture.h"
namespace eve::weapon::capture_codec {
static_assert(int(WeaponKind::Melee) == 0 && int(WeaponKind::Ranged) == 1 && int(WeaponKind::Magic) == 2 &&
              int(WeaponKind::Missile) == 3 && int(WeaponKind::Custom) == 4);
static_assert(int(ResourceKind::None) == 0 && int(ResourceKind::Ammo) == 1 && int(ResourceKind::Mana) == 2 &&
              int(ResourceKind::Charges) == 3 && int(ResourceKind::Stamina) == 4);
static_assert(int(FireMode::Single) == 0 && int(FireMode::Burst) == 1 && int(FireMode::Auto) == 2);
static_assert(int(definition::ReloadPolicy::KeepInstanceValues) == 0 &&
              int(definition::ReloadPolicy::ReapplyDefaults) == 1 &&
              int(definition::ReloadPolicy::RebuildInstance) == 2 &&
              int(definition::ReloadPolicy::RejectWhileActive) == 3);
struct Invalid {
    Status status;
};
/** @brief Copy a synchronous diagnostic message into a structured codec failure.
 * @ownership Message is borrowed only for this call; the thrown status owns its diagnostic.
 */
[[noreturn]] inline void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "weapon.capture"))};
}
template <class T>
T take(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
/** @brief Read strict text without copying.
 * @ownership Borrows the input's string until the input is changed or destroyed; never retained.
 */
inline const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected weapon text");
    return *s;
}
inline PersistentId id(const Value& v, bool nullable = false) {
    const auto& s      = text(v);
    const auto  result = PersistentId::parse(s);
    if (!result || result->format() != s || (!nullable && result->isNil())) invalid("Invalid canonical weapon UUID");
    return *result;
}
inline std::uint64_t counter(const Value& v) {
    const auto&   s      = text(v);
    std::uint64_t result = 0;
    const auto    parsed = std::from_chars(s.data(), s.data() + s.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size() || !result || std::to_string(result) != s)
        invalid("Invalid canonical weapon generation");
    return result;
}
template <class T>
auto fields(T& v) {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, WeaponDefinition>)
        return std::tie(v.id, v.kind, v.logic, v.damage, v.penetration, v.range, v.falloffStart, v.minimumDamageFactor,
                        v.splashMinimumDamageFactor, v.accuracy, v.scatterRadius, v.spread, v.spreadMin, v.spreadMax,
                        v.spreadPerShot, v.spreadRecover, v.recoilPitch, v.recoilYaw, v.recoilRecover, v.damageType,
                        v.element, v.targetsGround, v.targetsAir, v.friendlyFire, v.blockedByObstacles,
                        v.requiredTargetTags, v.excludedTargetTags, v.preferredTargetTags, v.preferredTargetBonus,
                        v.selectableModes, v.zoomFov, v.fireMode, v.cooldown, v.burstSize, v.burstInterval, v.resource,
                        v.stages, v.arc, v.magSize, v.reserveSize, v.reloadTime, v.projectile, v.effectMuzzle,
                        v.effectSound);
    else if constexpr (std::is_same_v<U, AttackResource>)
        return std::tie(v.kind, v.max, v.regen, v.cost, v.infinite);
    else if constexpr (std::is_same_v<U, AttackStageSpec>)
        return std::tie(v.windupTime, v.activeTime, v.recoverTime);
    else if constexpr (std::is_same_v<U, ProjectileSpec>)
        return std::tie(v.type, v.speed, v.gravity, v.aoe, v.pelletCount, v.pelletSpread);
    else if constexpr (std::is_same_v<U, AttackRequest>)
        return std::tie(v.targetX, v.targetY, v.targetZ, v.hasTarget, v.muzzleX, v.muzzleY, v.muzzleZ, v.yaw, v.pitch,
                        v.shooterId, v.arcAngle, v.aoeRadius, v.spread, v.pelletIndex, v.pelletCount);
    else if constexpr (std::is_same_v<U, WeaponEntity::Identity>)
        return std::tie(v.id, v.defId, v.ownerId);
    else if constexpr (std::is_same_v<U, WeaponEntity::Aim>)
        return std::tie(v.yaw, v.pitch, v.desiredYaw, v.desiredPitch, v.turnSpeed);
}
template <class T>
constexpr int enumMax() {
    if constexpr (std::is_same_v<T, WeaponKind>)
        return 4;
    else if constexpr (std::is_same_v<T, ResourceKind>)
        return 4;
    else if constexpr (std::is_same_v<T, FireMode>)
        return 2;
    else if constexpr (std::is_same_v<T, definition::ReloadPolicy>)
        return 3;
    else
        static_assert(!sizeof(T), "Undeclared weapon wire enum");
}
template <class T>
struct Vector : std::false_type {};
template <class T, class A>
struct Vector<std::vector<T, A>> : std::true_type {};
template <class T>
Value encode(const T& v) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_same_v<T, int>)
        return Value(v);
    else if constexpr (std::is_same_v<T, float>) {
        if (!std::isfinite(v)) invalid("Nonfinite weapon scalar");
        return Value(v);
    } else if constexpr (std::is_enum_v<T>) {
        const auto n = static_cast<int>(v);
        if (n < 0 || n > enumMax<T>()) invalid("Invalid weapon enum");
        return Value(n);
    } else {
        Value::Array result;
        if constexpr (Vector<T>::value) {
            result.reserve(v.size());
            for (const auto& entry : v) result.push_back(encode(entry));
        } else {
            auto tuple = fields(v);
            result.reserve(std::tuple_size_v<decltype(tuple)>);
            std::apply([&](const auto&... item) { (result.push_back(encode(item)), ...); }, tuple);
        }
        return Value(std::move(result));
    }
}
/** @brief Copy strict wire fields into detached typed storage.
 * @ownership Input and its child views are borrowed synchronously; output owns copied strings and arrays.
 */
template <class T>
void decode(const Value& v, T& output) {
    if constexpr (std::is_same_v<T, std::string>)
        output = text(v);
    else if constexpr (std::is_same_v<T, bool>) {
        const auto* b = v.getIf<bool>();
        if (!b) invalid("Expected weapon boolean");
        output = *b;
    } else if constexpr (std::is_same_v<T, int> || std::is_enum_v<T>) {
        if (!v.isInt64()) invalid("Expected weapon integer");
        const auto n = v.asInt();
        if constexpr (std::is_enum_v<T>) {
            if (n < 0 || n > enumMax<T>()) invalid("Unknown weapon enum");
        } else if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
            invalid("Weapon integer overflow");
        output = static_cast<T>(n);
    } else if constexpr (std::is_same_v<T, float>) {
        if (!v.isDouble() && !v.isInt64()) invalid("Expected weapon number");
        const auto n = v.isDouble() ? v.asDouble() : static_cast<double>(v.asInt());
        if (!std::isfinite(n) || !std::isfinite(static_cast<float>(n))) invalid("Nonfinite weapon number");
        output = static_cast<float>(n);
    } else {
        // @ownership Synchronous view into v, used only to copy owning output members below.
        const auto* a = v.getIf<Value::Array>();
        if (!a) invalid("Expected weapon tuple/array");
        if constexpr (Vector<T>::value) {
            output.reserve(a->size());
            for (const auto& entry : *a) {
                typename T::value_type item;
                decode(entry, item);
                output.push_back(std::move(item));
            }
        } else {
            auto tuple = fields(output);
            if (a->size() != std::tuple_size_v<decltype(tuple)>) invalid("Unknown or missing weapon tuple fields");
            std::size_t index = 0;
            std::apply([&](auto&... item) { (decode((*a)[index++], item), ...); }, tuple);
        }
    }
}
// Shared implementation in WeaponDefinitionRuntime.cpp; its legacy snapshot format remains unchanged.
Value                      encodeRuntime(const WeaponRuntimeState& state);
Result<WeaponRuntimeState> decodeRuntime(const Value& state);
}  // namespace eve::weapon::capture_codec
