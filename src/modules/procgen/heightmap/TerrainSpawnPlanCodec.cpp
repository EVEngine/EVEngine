#include "procgen/heightmap/TerrainSpawnPlan.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

#include "common/Value.h"

namespace eve::procgen {
namespace {
constexpr std::size_t kMaximumSnapshotBytes = 128U * 1024U * 1024U;
constexpr std::uint32_t kMaximumRules = 4096;
constexpr std::uint32_t kMaximumInstances = 65536;

struct Writer {
    std::string bytes;
    template <class T>
    void scalar(T value) {
        if constexpr (std::is_enum_v<T>) {
            using Raw = std::underlying_type_t<T>;
            scalar(static_cast<Raw>(value));
        } else if constexpr (std::is_same_v<T, bool>) {
            scalar(static_cast<std::uint8_t>(value));
        } else if constexpr (std::is_same_v<T, float>) {
            scalar(std::bit_cast<std::uint32_t>(value));
        } else if constexpr (std::is_same_v<T, double>) {
            scalar(std::bit_cast<std::uint64_t>(value));
        } else {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                bytes.push_back(static_cast<char>((bits >> (i * 8)) & 0xffU));
        }
    }
    void text(const std::string& value) {
        scalar(static_cast<std::uint32_t>(value.size()));
        bytes.append(value);
    }
};

struct Reader {
    std::string_view bytes;
    std::size_t offset = 0;
    template <class T>
    bool scalar(T& value) {
        if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> raw{};
            if (!scalar(raw)) return false;
            value = static_cast<T>(raw);
            return true;
        } else if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t raw{};
            if (!scalar(raw) || raw > 1) return false;
            value = raw != 0;
            return true;
        } else if constexpr (std::is_same_v<T, float>) {
            std::uint32_t raw{};
            if (!scalar(raw)) return false;
            value = std::bit_cast<float>(raw);
            return std::isfinite(value);
        } else if constexpr (std::is_same_v<T, double>) {
            std::uint64_t raw{};
            if (!scalar(raw)) return false;
            value = std::bit_cast<double>(raw);
            return std::isfinite(value);
        } else {
            if (bytes.size() - offset < sizeof(T)) return false;
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i)
                bits |= static_cast<Unsigned>(static_cast<unsigned char>(bytes[offset++])) << (i * 8);
            value = static_cast<T>(bits);
            return true;
        }
    }
    bool text(std::string& value) {
        std::uint32_t size{};
        if (!scalar(size) || size > bytes.size() - offset) return false;
        value.assign(bytes.substr(offset, size));
        offset += size;
        return true;
    }
};

std::string hexEncode(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

Result<std::string> hexDecode(std::string_view text) {
    if ((text.size() & 1U) != 0 || text.size() / 2 > kMaximumSnapshotBytes)
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: invalid payload size"));
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string result(text.size() / 2, '\0');
    for (std::size_t i = 0; i < result.size(); ++i) {
        const int high = nibble(text[i * 2]), low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: invalid payload hex"));
        result[i] = static_cast<char>((high << 4) | low);
    }
    return Result<std::string>::success(std::move(result));
}

#define STAMP_FIELDS(X) X(originX) X(originZ) X(spacingX) X(spacingZ) X(centerX) X(centerZ) X(width) X(depth) X(rotation) X(amplitude) X(baseHeight) X(blendStrength) X(operation)
#define DETAIL_FIELDS(X) X(minimumFitness) X(fadeStart) X(density) X(namespaceId)
#define TREE_FIELDS(X) X(originX) X(originZ) X(width) X(depth) X(heightScale) X(spacing) X(spawnDensity) X(jitterPercent) X(failureRate) X(minimumFitness) X(snapToTerrain) X(yOffsetMode) X(seaLevel) X(customOffset) X(minimumYOffset) X(maximumYOffset) X(scaleMode) X(minimumWidth) X(maximumWidth) X(minimumHeight) X(maximumHeight) X(widthRandomPercentage) X(heightRandomPercentage) X(healthyR) X(healthyG) X(healthyB) X(healthyA) X(dryR) X(dryG) X(dryB) X(dryA) X(bendFactor) X(boundsRadius) X(seed) X(namespaceId) X(maxPoints)
#define OBJECT_FIELDS(X) X(originX) X(originZ) X(width) X(depth) X(heightScale) X(spacing) X(spawnDensity) X(jitterPercent) X(startOffsetX) X(startOffsetZ) X(failureRate) X(minimumFitness) X(minimumInstanceFitness) X(minimumDirection) X(maximumDirection) X(boundsRadius) X(boundsCheckQuality) X(prototypeScale) X(boundsCollisionCheck) X(seaLevel) X(seed) X(namespaceId) X(maxPoints)
#define PROBE_FIELDS(X) X(originX) X(originZ) X(width) X(depth) X(heightScale) X(spacing) X(jitterPercent) X(minimumFitness) X(seaLevelActive) X(seaLevel) X(reflectionOffset) X(lightOffset) X(reflectionResolution) X(reflectionClipDistance) X(reflectionShadowDistance) X(seed) X(namespaceId) X(maxPoints)
#define INSTANCE_FIELDS(X) X(minimumInstances) X(maximumInstances) X(failureRate) X(minimumOffsetX) X(maximumOffsetX) X(minimumOffsetY) X(maximumOffsetY) X(minimumOffsetZ) X(maximumOffsetZ) X(yOffsetMode) X(customOffset) X(scaleMode) X(commonScale) X(minimumScale) X(maximumScale) X(minimumScaleX) X(maximumScaleX) X(minimumScaleY) X(maximumScaleY) X(minimumScaleZ) X(maximumScaleZ) X(scaleRandomPercentage) X(scaleRandomPercentageX) X(scaleRandomPercentageY) X(scaleRandomPercentageZ) X(minimumRotationX) X(maximumRotationX) X(minimumRotationY) X(maximumRotationY) X(minimumRotationZ) X(maximumRotationZ) X(yOffsetAlongSlope) X(alignForwardToSlope) X(rotateToSlope)

#define WRITE_MEMBER(name) writer.scalar(value.name);
#define READ_MEMBER(name) if (!reader.scalar(value.name)) return false;

void writeStamp(Writer& writer, const TerrainStampSettings& value) { STAMP_FIELDS(WRITE_MEMBER) }
bool readStamp(Reader& reader, TerrainStampSettings& value) { STAMP_FIELDS(READ_MEMBER) return true; }
void writeDetail(Writer& writer, const TerrainDetailSettings& value) { DETAIL_FIELDS(WRITE_MEMBER) }
bool readDetail(Reader& reader, TerrainDetailSettings& value) { DETAIL_FIELDS(READ_MEMBER) return true; }
void writeTree(Writer& writer, const TerrainTreePlacementSettings& value) {
    TREE_FIELDS(WRITE_MEMBER)
    writer.text(value.asset);
}
bool readTree(Reader& reader, TerrainTreePlacementSettings& value) {
    TREE_FIELDS(READ_MEMBER)
    return reader.text(value.asset);
}
void writeInstance(Writer& writer, const TerrainObjectInstanceSettings& value) {
    writer.text(value.asset);
    INSTANCE_FIELDS(WRITE_MEMBER)
}
bool readInstance(Reader& reader, TerrainObjectInstanceSettings& value) {
    if (!reader.text(value.asset)) return false;
    INSTANCE_FIELDS(READ_MEMBER)
    return true;
}
void writeObject(Writer& writer, const TerrainObjectPlacementSettings& value) {
    OBJECT_FIELDS(WRITE_MEMBER)
    writer.text(value.prototype);
    writer.scalar(static_cast<std::uint32_t>(value.instances.size()));
    for (const auto& instance : value.instances) writeInstance(writer, instance);
}
bool readObject(Reader& reader, TerrainObjectPlacementSettings& value) {
    OBJECT_FIELDS(READ_MEMBER)
    if (!reader.text(value.prototype)) return false;
    std::uint32_t count{};
    if (!reader.scalar(count) || count > kMaximumInstances) return false;
    value.instances.resize(count);
    for (auto& instance : value.instances)
        if (!readInstance(reader, instance)) return false;
    return true;
}
void writeProbe(Writer& writer, const TerrainProbePlacementSettings& value) {
    writer.text(value.name);
    writer.scalar(value.type);
    PROBE_FIELDS(WRITE_MEMBER)
}
bool readProbe(Reader& reader, TerrainProbePlacementSettings& value) {
    if (!reader.text(value.name) || !reader.scalar(value.type)) return false;
    PROBE_FIELDS(READ_MEMBER)
    return true;
}
void writeHeightmap(Writer& writer, const Heightmap& value) {
    writer.scalar(value.getWidth());
    writer.scalar(value.getHeight());
    for (float sample : value.data()) writer.scalar(sample);
}
bool readHeightmap(Reader& reader, Heightmap& value) {
    int width{}, height{};
    if (!reader.scalar(width) || !reader.scalar(height) || width <= 0 || height <= 0 || width > INT_MAX / height)
        return false;
    Heightmap candidate(width, height);
    for (float& sample : candidate.data())
        if (!reader.scalar(sample)) return false;
    value = std::move(candidate);
    return true;
}
bool validEnums(const TerrainDetailSpawnRule& rule) {
    return rule.mode >= TerrainDetailMode::Replace && rule.mode <= TerrainDetailMode::Remove &&
           rule.settings.mode >= TerrainDetailMode::Replace && rule.settings.mode <= TerrainDetailMode::Remove;
}
bool validEnums(const TerrainTreeSpawnRule& rule) {
    return rule.mode >= TerrainTreeOperationMode::Add && rule.mode <= TerrainTreeOperationMode::Remove &&
           rule.settings.yOffsetMode >= TerrainTreeYOffsetMode::TerrainHeight &&
           rule.settings.yOffsetMode <= TerrainTreeYOffsetMode::Custom &&
           rule.settings.scaleMode >= TerrainTreeScaleMode::Fixed &&
           rule.settings.scaleMode <= TerrainTreeScaleMode::FitnessRandomized;
}
bool validEnums(const TerrainObjectSpawnRule& rule) {
    if (rule.mode < TerrainObjectOperationMode::Add || rule.mode > TerrainObjectOperationMode::Remove) return false;
    for (const auto& instance : rule.settings.instances)
        if (instance.yOffsetMode < TerrainObjectYOffsetMode::TerrainHeight ||
            instance.yOffsetMode > TerrainObjectYOffsetMode::Custom ||
            instance.scaleMode < TerrainObjectScaleMode::Fixed ||
            instance.scaleMode > TerrainObjectScaleMode::FitnessRandomized)
            return false;
    return true;
}
bool validEnums(const TerrainProbeSpawnRule& rule) {
    return rule.mode >= TerrainProbeOperationMode::Add && rule.mode <= TerrainProbeOperationMode::Remove &&
           rule.settings.type >= TerrainProbeType::Reflection && rule.settings.type <= TerrainProbeType::Light;
}
bool validEnums(const TerrainSplatSpawnRule& rule) { return rule.targetLayer >= 0; }
bool validEnums(const TerrainModifierStampSpawnRule& rule) {
    return rule.operation.operation >= TerrainStampOperation::Raise &&
           rule.operation.operation <= TerrainStampOperation::Subtract;
}
bool stringsFit(const TerrainSpawnRule& variant) {
    return std::visit([](const auto& rule) {
        if (rule.ruleId.size() > std::numeric_limits<std::uint32_t>::max()) return false;
        using Rule = std::decay_t<decltype(rule)>;
        if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule> ||
                      std::is_same_v<Rule, TerrainDetailSpawnRule> ||
                      std::is_same_v<Rule, TerrainModifierStampSpawnRule>) {
            return true;
        } else if constexpr (std::is_same_v<Rule, TerrainTreeSpawnRule>) {
            return rule.settings.asset.size() <= std::numeric_limits<std::uint32_t>::max();
        } else if constexpr (std::is_same_v<Rule, TerrainProbeSpawnRule>) {
            return rule.settings.name.size() <= std::numeric_limits<std::uint32_t>::max();
        } else {
            if (rule.settings.prototype.size() > std::numeric_limits<std::uint32_t>::max() ||
                rule.settings.instances.size() > kMaximumInstances)
                return false;
            return std::all_of(rule.settings.instances.begin(), rule.settings.instances.end(), [](const auto& item) {
                return item.asset.size() <= std::numeric_limits<std::uint32_t>::max();
            });
        }
    }, variant);
}

void writeRule(Writer& writer, const TerrainSpawnRule& variant) {
    std::visit([&](const auto& rule) {
        using Rule = std::decay_t<decltype(rule)>;
        if constexpr (std::is_same_v<Rule, TerrainDetailSpawnRule>) writer.scalar(std::uint8_t{0});
        else if constexpr (std::is_same_v<Rule, TerrainTreeSpawnRule>) writer.scalar(std::uint8_t{1});
        else if constexpr (std::is_same_v<Rule, TerrainObjectSpawnRule>) writer.scalar(std::uint8_t{2});
        else if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule>) writer.scalar(std::uint8_t{3});
        else if constexpr (std::is_same_v<Rule, TerrainModifierStampSpawnRule>) writer.scalar(std::uint8_t{4});
        else writer.scalar(std::uint8_t{5});
        writer.text(rule.ruleId);
        writer.scalar(rule.enabled);
        if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule>) {
            writeHeightmap(writer, rule.paint);
        } else if constexpr (std::is_same_v<Rule, TerrainModifierStampSpawnRule>) {
            writeHeightmap(writer, rule.stamp);
            writeHeightmap(writer, rule.localMask);
            writeHeightmap(writer, rule.globalMask);
        } else {
            writeHeightmap(writer, rule.fitness);
        }
        writeStamp(writer, rule.operation);
        if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule>) {
            writer.scalar(rule.targetLayer);
        } else if constexpr (std::is_same_v<Rule, TerrainDetailSpawnRule>) {
            writer.scalar(rule.mode);
            writeDetail(writer, rule.settings);
            writer.scalar(rule.seed);
        } else if constexpr (std::is_same_v<Rule, TerrainTreeSpawnRule>) {
            writer.scalar(rule.mode);
            writeTree(writer, rule.settings);
        } else if constexpr (std::is_same_v<Rule, TerrainObjectSpawnRule>) {
            writer.scalar(rule.mode);
            writeObject(writer, rule.settings);
        } else if constexpr (std::is_same_v<Rule, TerrainProbeSpawnRule>) {
            writer.scalar(rule.mode);
            writeProbe(writer, rule.settings);
        }
    }, variant);
}

bool readRule(Reader& reader, int version, TerrainSpawnRule& output) {
    std::uint8_t kind{};
    std::string id;
    bool enabled = true;
    Heightmap fitness;
    TerrainStampSettings operation;
    const std::uint8_t maximumKind = version >= 3 ? 5 : (version >= 2 ? 4 : 3);
    if (!reader.scalar(kind) || kind > maximumKind || !reader.text(id) || id.empty() ||
        (version >= 1 && !reader.scalar(enabled)) || !readHeightmap(reader, fitness))
        return false;
    Heightmap localMask, globalMask;
    if (kind == 4 && (!readHeightmap(reader, localMask) || !readHeightmap(reader, globalMask))) return false;
    if (!readStamp(reader, operation)) return false;
    if (kind == 3) {
        TerrainSplatSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.paint = std::move(fitness); rule.operation = operation;
        if (!reader.scalar(rule.targetLayer) || !validEnums(rule)) return false;
        output = std::move(rule);
    } else if (kind == 0) {
        TerrainDetailSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.fitness = std::move(fitness); rule.operation = operation;
        if (!reader.scalar(rule.mode) || !readDetail(reader, rule.settings) || !reader.scalar(rule.seed) || !validEnums(rule)) return false;
        output = std::move(rule);
    } else if (kind == 1) {
        TerrainTreeSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.fitness = std::move(fitness); rule.operation = operation;
        if (!reader.scalar(rule.mode) || !readTree(reader, rule.settings) || !validEnums(rule)) return false;
        output = std::move(rule);
    } else if (kind == 2) {
        TerrainObjectSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.fitness = std::move(fitness); rule.operation = operation;
        if (!reader.scalar(rule.mode) || !readObject(reader, rule.settings) || !validEnums(rule)) return false;
        output = std::move(rule);
    } else if (kind == 4) {
        TerrainModifierStampSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.stamp = std::move(fitness);
        rule.localMask = std::move(localMask); rule.globalMask = std::move(globalMask); rule.operation = operation;
        if (!validEnums(rule)) return false;
        output = std::move(rule);
    } else {
        TerrainProbeSpawnRule rule;
        rule.ruleId = std::move(id); rule.enabled = enabled; rule.fitness = std::move(fitness);
        rule.operation = operation;
        if (!reader.scalar(rule.mode) || !readProbe(reader, rule.settings) || !validEnums(rule)) return false;
        output = std::move(rule);
    }
    return true;
}

#undef WRITE_MEMBER
#undef READ_MEMBER
#undef STAMP_FIELDS
#undef DETAIL_FIELDS
#undef TREE_FIELDS
#undef OBJECT_FIELDS
#undef PROBE_FIELDS
#undef INSTANCE_FIELDS
}  // namespace

Result<std::string> TerrainSpawnPlan::snapshotJson() const {
    if (rules_.size() > kMaximumRules)
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.snapshot: too many rules"));
    if (!std::all_of(rules_.begin(), rules_.end(), stringsFit))
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.spawnPlan.snapshot: string or instance count exceeds limit"));
    Writer writer;
    writer.scalar(static_cast<std::uint32_t>(rules_.size()));
    for (const auto& rule : rules_) writeRule(writer, rule);
    if (writer.bytes.size() > kMaximumSnapshotBytes)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.spawnPlan.snapshot: payload exceeds size limit"));
    Value::Object root{{"payload", hexEncode(writer.bytes)}, {"schema", "eve.procgen.terrain-spawn-plan"},
                       {"version", 3}};
    return Value(std::move(root)).toJson();
}

Result<void> TerrainSpawnPlan::restoreJson(const std::string& json) {
    if (json.size() > kMaximumSnapshotBytes * 2 + 1024)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: JSON exceeds size limit"));
    auto decoded = Value::fromJson(json);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto* root = decoded.value().getIf<Value::Object>();
    if (!root || root->size() != 3 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("payload"))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.spawnPlan.restore: missing or unknown root fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    const auto* version = root->at("version").getIf<std::int64_t>();
    const auto* payload = root->at("payload").getIf<std::string>();
    if (!schema || *schema != "eve.procgen.terrain-spawn-plan" || !version ||
        (*version != 0 && *version != 1 && *version != 2 && *version != 3) ||
        !payload)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.spawnPlan.restore: unsupported schema or version"));
    auto bytes = hexDecode(*payload);
    if (!bytes.ok()) return Result<void>::failure(bytes.status());
    Reader reader{bytes.value()};
    std::uint32_t count{};
    if (!reader.scalar(count) || count > kMaximumRules)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: invalid rule count"));
    TerrainSpawnPlan candidate;
    candidate.rules_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TerrainSpawnRule rule;
        if (!readRule(reader, static_cast<int>(*version), rule))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: invalid rule payload"));
        const auto id = std::visit([](const auto& value) { return value.ruleId; }, rule);
        if (candidate.contains(id))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: duplicate rule ID"));
        candidate.rules_.push_back(std::move(rule));
    }
    if (reader.offset != reader.bytes.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan.restore: trailing payload data"));
    rules_.swap(candidate.rules_);
    return Result<void>::success();
}

}  // namespace eve::procgen
