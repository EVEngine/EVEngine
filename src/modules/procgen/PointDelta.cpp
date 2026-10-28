#include "procgen/PointDelta.h"

#include <algorithm>
#include <bit>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace eve::procgen {
namespace {

template <class T>
void hashValue(std::uint64_t& hash, const T& value) {
    if constexpr (std::is_enum_v<T>) {
        hashValue(hash, static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        hashValue(hash, std::uint8_t(value ? 1 : 0));
    } else if constexpr (std::is_floating_point_v<T>) {
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        hashValue(hash, std::bit_cast<Bits>(value));
    } else {
        using Bits      = std::make_unsigned_t<T>;
        const Bits bits = Bits(value);
        for (std::size_t index = 0; index < sizeof(Bits); ++index) {
            hash ^= static_cast<unsigned char>(bits >> (index * 8u));
            hash *= 1099511628211ull;
        }
    }
}

void hashText(std::uint64_t& hash, std::string_view value) {
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    const unsigned char terminator = 0;
    hashValue(hash, terminator);
}

std::vector<std::string> sortedColumns(const AttributeTable& attributes) {
    std::vector<std::string> names;
    names.reserve(attributes.columnCount());
    for (std::size_t column = 0; column < attributes.columnCount(); ++column)
        names.emplace_back(attributes.columnName(column));
    std::sort(names.begin(), names.end());
    return names;
}

bool schemasEqual(const AttributeTable& left, const AttributeTable& right) {
    const auto leftNames  = sortedColumns(left);
    const auto rightNames = sortedColumns(right);
    if (leftNames != rightNames) return false;
    return std::all_of(leftNames.begin(), leftNames.end(), [&](const std::string& name) {
        return left.typeOf(name) == right.typeOf(name);
    });
}

bool attributeRowsEqual(const AttributeTable& left, std::size_t leftRow, const AttributeTable& right,
                        std::size_t rightRow) {
    if (!schemasEqual(left, right)) return false;
    for (const auto& name : sortedColumns(left)) {
        if (left.has(leftRow, name) != right.has(rightRow, name)) return false;
        if (!left.has(leftRow, name)) continue;
        switch (*left.typeOf(name)) {
            case ProcgenAttributeType::Float:
                if (left.getFloat(leftRow, name) != right.getFloat(rightRow, name)) return false;
                break;
            case ProcgenAttributeType::Int:
                if (left.getInt(leftRow, name) != right.getInt(rightRow, name)) return false;
                break;
            case ProcgenAttributeType::Bool:
                if (left.getBool(leftRow, name) != right.getBool(rightRow, name)) return false;
                break;
            case ProcgenAttributeType::Vector: {
                const auto a = left.getVector(leftRow, name);
                const auto b = right.getVector(rightRow, name);
                if (!a || !b || a->x != b->x || a->y != b->y || a->z != b->z) return false;
                break;
            }
            case ProcgenAttributeType::String:
                if (left.getString(leftRow, name) != right.getString(rightRow, name)) return false;
                break;
        }
    }
    return true;
}

bool pointsEqual(const ProcgenPoint& a, const ProcgenPoint& b) {
    return a.id == b.id && a.x == b.x && a.y == b.y && a.z == b.z && a.normalX == b.normalX &&
           a.normalY == b.normalY && a.normalZ == b.normalZ && a.pitch == b.pitch && a.yaw == b.yaw &&
           a.roll == b.roll && a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.scaleZ == b.scaleZ &&
           a.density == b.density && a.seed == b.seed && a.boundsMinX == b.boundsMinX &&
           a.boundsMinY == b.boundsMinY && a.boundsMinZ == b.boundsMinZ && a.boundsMaxX == b.boundsMaxX &&
           a.boundsMaxY == b.boundsMaxY && a.boundsMaxZ == b.boundsMaxZ && a.colorR == b.colorR &&
           a.colorG == b.colorG && a.colorB == b.colorB && a.colorA == b.colorA && a.steepness == b.steepness;
}

Result<std::unordered_map<std::uint64_t, std::size_t>> indexPoints(const PointSet& points) {
    std::unordered_map<std::uint64_t, std::size_t> index;
    index.reserve(points.points().size());
    for (std::size_t row = 0; row < points.points().size(); ++row) {
        const auto id = points.points()[row].id;
        if (id == 0)
            return Result<std::unordered_map<std::uint64_t, std::size_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "point delta requires non-zero ids", "points.id"));
        if (!index.emplace(id, row).second)
            return Result<std::unordered_map<std::uint64_t, std::size_t>>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point delta requires unique ids", "points.id"));
    }
    return Result<std::unordered_map<std::uint64_t, std::size_t>>::success(std::move(index));
}

void hashAttributeRow(std::uint64_t& hash, const AttributeTable& attributes, std::size_t row) {
    for (const auto& name : sortedColumns(attributes)) {
        hashText(hash, name);
        const auto type = *attributes.typeOf(name);
        hashValue(hash, type);
        const bool present = attributes.has(row, name);
        hashValue(hash, present);
        if (!present) continue;
        switch (type) {
            case ProcgenAttributeType::Float: hashValue(hash, *attributes.getFloat(row, name)); break;
            case ProcgenAttributeType::Int: hashValue(hash, *attributes.getInt(row, name)); break;
            case ProcgenAttributeType::Bool: hashValue(hash, *attributes.getBool(row, name)); break;
            case ProcgenAttributeType::Vector: {
                const auto value = *attributes.getVector(row, name);
                hashValue(hash, value.x);
                hashValue(hash, value.y);
                hashValue(hash, value.z);
                break;
            }
            case ProcgenAttributeType::String: hashText(hash, *attributes.getString(row, name)); break;
        }
    }
}

}  // namespace

Result<std::uint64_t> fingerprintPointSet(const PointSet& points) {
    auto indexed = indexPoints(points);
    if (!indexed.ok()) return Result<std::uint64_t>::failure(indexed.status());
    std::uint64_t hash = 14695981039346656037ull;
    const auto count = points.points().size();
    hashValue(hash, count);
    for (std::size_t row = 0; row < count; ++row) {
        const auto& point = points.points()[row];
        hashValue(hash, point.id);
        hashValue(hash, point.x);
        hashValue(hash, point.y);
        hashValue(hash, point.z);
        hashValue(hash, point.normalX);
        hashValue(hash, point.normalY);
        hashValue(hash, point.normalZ);
        hashValue(hash, point.pitch);
        hashValue(hash, point.yaw);
        hashValue(hash, point.roll);
        hashValue(hash, point.scaleX);
        hashValue(hash, point.scaleY);
        hashValue(hash, point.scaleZ);
        hashValue(hash, point.density);
        hashValue(hash, point.seed);
        hashValue(hash, point.boundsMinX);
        hashValue(hash, point.boundsMinY);
        hashValue(hash, point.boundsMinZ);
        hashValue(hash, point.boundsMaxX);
        hashValue(hash, point.boundsMaxY);
        hashValue(hash, point.boundsMaxZ);
        hashValue(hash, point.colorR);
        hashValue(hash, point.colorG);
        hashValue(hash, point.colorB);
        hashValue(hash, point.colorA);
        hashValue(hash, point.steepness);
        hashAttributeRow(hash, points.attributes(), row);
    }
    return Result<std::uint64_t>::success(hash == 0 ? 1 : hash);
}

Result<PointDelta> diffPointSets(const PointSet& before, const PointSet& after) {
    auto beforeIndex = indexPoints(before);
    if (!beforeIndex.ok()) return Result<PointDelta>::failure(beforeIndex.status());
    auto afterIndex = indexPoints(after);
    if (!afterIndex.ok()) return Result<PointDelta>::failure(afterIndex.status());
    auto baseFingerprint = fingerprintPointSet(before);
    if (!baseFingerprint.ok()) return Result<PointDelta>::failure(baseFingerprint.status());
    auto targetFingerprint = fingerprintPointSet(after);
    if (!targetFingerprint.ok()) return Result<PointDelta>::failure(targetFingerprint.status());

    PointDelta delta;
    delta.baseFingerprint   = baseFingerprint.value();
    delta.targetFingerprint = targetFingerprint.value();
    delta.targetOrder.reserve(after.points().size());
    for (const auto& point : before.points())
        if (!afterIndex.value().contains(point.id)) delta.removed.push_back(point.id);
    const bool sameSchema = schemasEqual(before.attributes(), after.attributes());
    for (std::size_t row = 0; row < after.points().size(); ++row) {
        const auto id = after.points()[row].id;
        delta.targetOrder.push_back(id);
        const auto found = beforeIndex.value().find(id);
        if (found == beforeIndex.value().end()) {
            auto appended = delta.added.appendPointFrom(after, row);
            if (!appended.ok()) return Result<PointDelta>::failure(appended.status());
        } else if (!sameSchema || !pointsEqual(before.points()[found->second], after.points()[row]) ||
                   !attributeRowsEqual(before.attributes(), found->second, after.attributes(), row)) {
            auto appended = delta.updated.appendPointFrom(after, row);
            if (!appended.ok()) return Result<PointDelta>::failure(appended.status());
        }
    }
    return Result<PointDelta>::success(std::move(delta));
}

Result<PointSet> applyPointDelta(const PointSet& base, const PointDelta& delta) {
    auto baseFingerprint = fingerprintPointSet(base);
    if (!baseFingerprint.ok()) return Result<PointSet>::failure(baseFingerprint.status());
    if (baseFingerprint.value() != delta.baseFingerprint)
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "point delta base fingerprint is stale", "base"));
    auto baseIndex = indexPoints(base);
    auto addedIndex = indexPoints(delta.added);
    auto updatedIndex = indexPoints(delta.updated);
    if (!baseIndex.ok()) return Result<PointSet>::failure(baseIndex.status());
    if (!addedIndex.ok()) return Result<PointSet>::failure(addedIndex.status());
    if (!updatedIndex.ok()) return Result<PointSet>::failure(updatedIndex.status());
    for (const auto& [id, row] : addedIndex.value()) {
        (void)row;
        if (baseIndex.value().contains(id) || updatedIndex.value().contains(id))
            return Result<PointSet>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point delta added identity is inconsistent", "added"));
    }
    for (const auto& [id, row] : updatedIndex.value()) {
        (void)row;
        if (!baseIndex.value().contains(id))
            return Result<PointSet>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "point delta updated identity is not in the base", "updated"));
    }

    std::unordered_set<std::uint64_t> removed(delta.removed.begin(), delta.removed.end());
    if (removed.size() != delta.removed.size())
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "point delta contains duplicate removals", "removed"));
    for (const auto id : removed)
        if (!baseIndex.value().contains(id) || updatedIndex.value().contains(id))
            return Result<PointSet>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point delta removal is inconsistent", "removed"));

    PointSet staged;
    staged.reserve(delta.targetOrder.size());
    std::unordered_set<std::uint64_t> emitted;
    emitted.reserve(delta.targetOrder.size());
    for (const auto id : delta.targetOrder) {
        if (id == 0 || !emitted.insert(id).second || removed.contains(id))
            return Result<PointSet>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point delta target order is inconsistent", "targetOrder"));
        const PointSet* source = nullptr;
        std::size_t row = 0;
        if (const auto found = addedIndex.value().find(id); found != addedIndex.value().end()) {
            source = &delta.added;
            row    = found->second;
        } else if (const auto found = updatedIndex.value().find(id); found != updatedIndex.value().end()) {
            source = &delta.updated;
            row    = found->second;
        } else if (const auto found = baseIndex.value().find(id); found != baseIndex.value().end()) {
            source = &base;
            row    = found->second;
        } else {
            return Result<PointSet>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point delta references an unknown id", "targetOrder"));
        }
        auto appended = staged.appendPointFrom(*source, row);
        if (!appended.ok()) return Result<PointSet>::failure(appended.status());
    }
    if (emitted.size() != base.points().size() - removed.size() + delta.added.points().size())
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "point delta omits or duplicates points", "targetOrder"));
    auto fingerprint = fingerprintPointSet(staged);
    if (!fingerprint.ok()) return Result<PointSet>::failure(fingerprint.status());
    if (fingerprint.value() != delta.targetFingerprint)
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "point delta target fingerprint does not match", "delta"));
    return Result<PointSet>::success(std::move(staged));
}

}  // namespace eve::procgen
