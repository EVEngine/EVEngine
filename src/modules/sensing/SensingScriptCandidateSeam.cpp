#include "sensing/SensingScriptCandidateSeam.h"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/ScriptProvider.h"
#include "common/Value.h"
#include "sensing/Targeting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::sensing {
namespace {

/**
 * @brief 脚本词表的一条：枚举值 ↔ 脚本看到的字符串。
 *
 * 每张表只有这一份，**两个方向都用它**（这是 §9.71.2 的教训：同一份映射写两遍必然漂移）。
 * 表长用 `static_assert` 钉死，加枚举值忘了改表会编译失败。
 */
template <class T>
struct NameEntry {
    T                value;
    std::string_view name;
};

constexpr std::array<NameEntry<CoordinateSpace>, 4> kCoordinateSpaces = {{
    {CoordinateSpace::World2D, "world2d"},
    {CoordinateSpace::World3D, "world3d"},
    {CoordinateSpace::Grid2D, "grid2d"},
    {CoordinateSpace::Grid3D, "grid3d"},
}};
static_assert(kCoordinateSpaces.size() == 4, "坐标空间表与枚举不同步");

constexpr std::array<NameEntry<TargetDomain>, 5> kTargetDomains = {{
    {TargetDomain::Any, "any"},
    {TargetDomain::Self, "self"},
    {TargetDomain::Ally, "ally"},
    {TargetDomain::Enemy, "enemy"},
    {TargetDomain::Neutral, "neutral"},
}};
static_assert(kTargetDomains.size() == 5, "关系类表与枚举不同步");

constexpr std::array<NameEntry<WorldArea::Shape>, 5> kWorldAreaShapes = {{
    {WorldArea::Shape::Circle2D, "circle2d"},
    {WorldArea::Shape::Box2D, "box2d"},
    {WorldArea::Shape::Sphere3D, "sphere3d"},
    {WorldArea::Shape::Box3D, "box3d"},
    {WorldArea::Shape::Cone2D, "cone2d"},
}};
static_assert(kWorldAreaShapes.size() == 5, "世界区域形状表与枚举不同步");

constexpr std::array<NameEntry<GridArea::Shape>, 2> kGridAreaShapes = {{
    {GridArea::Shape::Box2D, "gridBox2d"},
    {GridArea::Shape::Box3D, "gridBox3d"},
}};
static_assert(kGridAreaShapes.size() == 2, "格子区域形状表与枚举不同步");

template <class T, std::size_t N>
std::string_view nameOf(const std::array<NameEntry<T>, N>& table, T value) noexcept {
    for (const NameEntry<T>& entry : table) {
        if (entry.value == value) return entry.name;
    }
    return {};
}

template <class T, std::size_t N>
bool parseName(const std::array<NameEntry<T>, N>& table, std::string_view text, T* out) noexcept {
    for (const NameEntry<T>& entry : table) {
        if (entry.name != text) continue;
        if (out != nullptr) *out = entry.value;
        return true;
    }
    return false;
}

/** @brief 访问一个对象字段；不是对象或没有该字段时返回 nullptr。 */
const Value* field(const Value& object, std::string_view key) {
    const auto* fields = object.getIf<Value::Object>();
    if (fields == nullptr) return nullptr;
    const auto found = fields->find(std::string(key));
    return found == fields->end() ? nullptr : &found->second;
}

bool isNull(const Value* value) { return value == nullptr || value->type() == Value::Type::Null; }

/** @brief 读一个有限数值；整数与浮点都收。 */
bool readNumber(const Value& value, double& out) {
    if (const auto* integer = value.getIf<std::int64_t>()) {
        out = static_cast<double>(*integer);
        return true;
    }
    if (const auto* number = value.getIf<double>()) {
        out = *number;
        return std::isfinite(*number);
    }
    return false;
}

/** @brief 读一个整数：整数直收，浮点必须精确（不做截断）。 */
bool readInteger(const Value& value, std::int64_t& out) {
    if (const auto* integer = value.getIf<std::int64_t>()) {
        out = *integer;
        return true;
    }
    if (const auto* number = value.getIf<double>()) {
        if (!std::isfinite(*number) || std::floor(*number) != *number) return false;
        out = static_cast<std::int64_t>(*number);
        return true;
    }
    return false;
}

/** @brief 读一个字符串数组；缺失算空数组，出现非字符串就失败。 */
bool readStrings(const Value* value, std::vector<std::string>& out) {
    if (isNull(value)) return true;
    const auto* entries = value->getIf<Value::Array>();
    if (entries == nullptr) return false;
    out.reserve(entries->size());
    for (const Value& entry : *entries) {
        const auto* text = entry.getIf<std::string>();
        if (text == nullptr) return false;
        out.push_back(*text);
    }
    return true;
}

template <class T>
Result<T> scriptFailure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "sensing.script-candidates"));
}

// ---------------------------------------------------------------------------
// 下行：C++ 记录 → 脚本表
// ---------------------------------------------------------------------------

Value worldPointValue(const WorldPoint& point) {
    return Value::object({{"space", Value::string(std::string(nameOf(kCoordinateSpaces, point.space())))},
                          {"x", Value::number(double(point.x()))},
                          {"y", Value::number(double(point.y()))},
                          {"z", Value::number(double(point.z()))}});
}

Value gridPointValue(const GridPoint& point) {
    return Value::object({{"space", Value::string(std::string(nameOf(kCoordinateSpaces, point.space())))},
                          {"x", Value::integer(point.x())},
                          {"y", Value::integer(point.y())},
                          {"z", Value::integer(point.z())}});
}

Value locationValue(const TargetLocation& location) {
    if (const auto* world = std::get_if<WorldPoint>(&location)) return worldPointValue(*world);
    return gridPointValue(std::get<GridPoint>(location));
}

Value stringsValue(const std::vector<std::string>& values) {
    Value::Array out;
    out.reserve(values.size());
    for (const std::string& value : values) out.push_back(Value::string(value));
    return Value::array(std::move(out));
}

/** @brief 区域 → 脚本表；没有区域约束时给 `null`。 */
Value areaValue(const TargetingSpec& spec) {
    if (spec.worldArea) {
        const WorldArea&  area  = *spec.worldArea;
        const std::string shape = std::string(nameOf(kWorldAreaShapes, area.shape()));
        Value::Object     out{{"shape", Value::string(shape)}};
        switch (area.shape()) {
            case WorldArea::Shape::Circle2D:
            case WorldArea::Shape::Sphere3D:
                out.emplace("center", worldPointValue(area.first()));
                out.emplace("radius", Value::number(double(area.radius())));
                break;
            case WorldArea::Shape::Box2D:
            case WorldArea::Shape::Box3D:
                out.emplace("min", worldPointValue(area.first()));
                out.emplace("max", worldPointValue(area.second()));
                break;
            case WorldArea::Shape::Cone2D:
                // 方向存在 second()、范围存在 radius()、半角是这一片才补上的出口。
                out.emplace("apex", worldPointValue(area.first()));
                out.emplace("dirX", Value::number(double(area.second().x())));
                out.emplace("dirY", Value::number(double(area.second().y())));
                out.emplace("halfAngle", Value::number(double(area.halfAngle())));
                out.emplace("range", Value::number(double(area.radius())));
                break;
        }
        return Value::object(std::move(out));
    }
    if (spec.gridArea) {
        const GridArea& gridArea = *spec.gridArea;
        Value::Object   out{{"shape", Value::string(std::string(nameOf(kGridAreaShapes, gridArea.shape())))},
                            {"min", gridPointValue(gridArea.minimum())},
                            {"max", gridPointValue(gridArea.maximum())}};
        return Value::object(std::move(out));
    }
    return {};
}

/**
 * @brief 一次查询 → 脚本表。
 *
 * 只投影 **provider 该满足的约束**（坐标空间、原点、距离、区域、三类筛选）；可见性与数量策略是
 * resolver 的活，故意不下发（见头文件里的说明）。
 */
Value::Object queryValue(const TargetingQuery& query) {
    const TargetingSpec& spec = query.spec;
    Value::Object        out;
    out.emplace("origin", Value::string(query.origin.format()));
    out.emplace("space", Value::string(std::string(nameOf(kCoordinateSpaces, spec.space))));
    out.emplace("originLocation", locationValue(query.originLocation));
    out.emplace("domain", Value::string(std::string(nameOf(kTargetDomains, spec.domain))));
    out.emplace("minRange", Value::number(double(spec.minRange)));
    // +inf 过不去（编组拒绝非有限数）：无界用 null 表示，这也是脚本侧唯一能表达"无界"的值。
    out.emplace("maxRange", std::isfinite(spec.maxRange) ? Value::number(double(spec.maxRange)) : Value{});
    out.emplace("requiredTags", stringsValue(spec.requiredTags));
    out.emplace("excludedTags", stringsValue(spec.excludedTags));
    out.emplace("zone", spec.zone ? Value::string(spec.zone->logicalId().format()) : Value{});
    out.emplace("area", areaValue(spec));
    return out;
}

// ---------------------------------------------------------------------------
// 上行：脚本表 → C++ 记录
// ---------------------------------------------------------------------------

/**
 * @brief 脚本给的位置 → `TargetLocation`。
 *
 * `space` 必须显式给出且与查询空间一致：格子与世界不可互换，静默换算只会得到悄悄错的距离。
 */
Result<TargetLocation> locationFromScript(const Value& value, CoordinateSpace querySpace) {
    const Value* spaceField = field(value, "space");
    const auto*  spaceText  = spaceField != nullptr ? spaceField->getIf<std::string>() : nullptr;
    if (spaceText == nullptr)
        return scriptFailure<TargetLocation>(DiagnosticCode::InvalidArgument,
                                             "candidate location must state its 'space'", "candidate.location.space");
    CoordinateSpace space = CoordinateSpace::World2D;
    if (!parseName(kCoordinateSpaces, *spaceText, &space))
        return scriptFailure<TargetLocation>(DiagnosticCode::InvalidArgument,
                                             "candidate location has an unknown space: " + *spaceText,
                                             "candidate.location.space");
    if (space != querySpace)
        return scriptFailure<TargetLocation>(
            DiagnosticCode::Unsupported,
            "candidate location space '" + *spaceText + "' does not match the query space '" +
                std::string(nameOf(kCoordinateSpaces, querySpace)) + "'; convert explicitly instead of mixing spaces",
            "candidate.location.space");

    const Value* xField = field(value, "x");
    const Value* yField = field(value, "y");
    const Value* zField = field(value, "z");
    const bool   isGrid = space == CoordinateSpace::Grid2D || space == CoordinateSpace::Grid3D;
    if (isGrid) {
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;
        if (xField == nullptr || yField == nullptr || !readInteger(*xField, x) || !readInteger(*yField, y) ||
            (zField != nullptr && !isNull(zField) && !readInteger(*zField, z)))
            return scriptFailure<TargetLocation>(DiagnosticCode::InvalidArgument,
                                                 "grid candidate location requires integral x/y", "candidate.location");
        if (space == CoordinateSpace::Grid2D)
            return Result<TargetLocation>::success(TargetLocation(GridPoint::grid2D(int32_t(x), int32_t(y))));
        return Result<TargetLocation>::success(TargetLocation(GridPoint::grid3D(int32_t(x), int32_t(y), int32_t(z))));
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (xField == nullptr || yField == nullptr || !readNumber(*xField, x) || !readNumber(*yField, y) ||
        (zField != nullptr && !isNull(zField) && !readNumber(*zField, z)))
        return scriptFailure<TargetLocation>(DiagnosticCode::InvalidArgument,
                                             "world candidate location requires finite x/y", "candidate.location");
    if (space == CoordinateSpace::World2D) {
        auto point = WorldPoint::world2D(float(x), float(y));
        if (!point.ok()) return Result<TargetLocation>::failure(point.status());
        return Result<TargetLocation>::success(TargetLocation(std::move(point).takeValue()));
    }
    auto point = WorldPoint::world3D(float(x), float(y), float(z));
    if (!point.ok()) return Result<TargetLocation>::failure(point.status());
    return Result<TargetLocation>::success(TargetLocation(std::move(point).takeValue()));
}

Result<TargetCandidate> candidateFromScript(const Value& entry, CoordinateSpace querySpace) {
    if (entry.type() != Value::Type::Object)
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument, "every candidate must be a table",
                                              "candidate");

    const Value* subjectField = field(entry, "subject");
    const auto*  subjectText  = subjectField != nullptr ? subjectField->getIf<std::string>() : nullptr;
    if (subjectText == nullptr)
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument, "candidate requires a 'subject' string",
                                              "candidate.subject");
    const auto subjectId = PersistentId::parse(*subjectText);
    if (!subjectId)
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument,
                                              "candidate subject is not a canonical UUID: " + *subjectText,
                                              "candidate.subject");

    const Value* locationField = field(entry, "location");
    if (locationField == nullptr)
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument, "candidate requires a 'location' table",
                                              "candidate.location");
    auto location = locationFromScript(*locationField, querySpace);
    if (!location.ok()) return Result<TargetCandidate>::failure(location.status());

    TargetCandidate candidate;
    candidate.subject  = SubjectRef::fromPersistentId(*subjectId);
    candidate.location = std::move(location).takeValue();

    if (const Value* domainField = field(entry, "domain"); !isNull(domainField)) {
        const auto*  domainText = domainField->getIf<std::string>();
        TargetDomain domain     = TargetDomain::Neutral;
        if (domainText == nullptr || !parseName(kTargetDomains, *domainText, &domain))
            return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument, "candidate has an unknown domain",
                                                  "candidate.domain");
        candidate.domain = domain;
    }
    if (!readStrings(field(entry, "tags"), candidate.tags))
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument,
                                              "candidate 'tags' must be an array of strings", "candidate.tags");

    std::vector<std::string> zoneIds;
    if (!readStrings(field(entry, "zones"), zoneIds))
        return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument,
                                              "candidate 'zones' must be an array of strings", "candidate.zones");
    candidate.zones.reserve(zoneIds.size());
    for (const std::string& zoneId : zoneIds) {
        const auto parsed = LogicalId::parse(zoneId);
        if (!parsed)
            return scriptFailure<TargetCandidate>(DiagnosticCode::InvalidArgument,
                                                  "candidate zone is not a valid logical id: " + zoneId,
                                                  "candidate.zones");
        const auto zone = ZoneRef::fromLogicalId(*parsed);
        if (!zone)
            return scriptFailure<TargetCandidate>(
                DiagnosticCode::InvalidArgument, "candidate zone is not a valid ZoneRef: " + zoneId, "candidate.zones");
        candidate.zones.push_back(*zone);
    }
    return Result<TargetCandidate>::success(std::move(candidate));
}

// ---------------------------------------------------------------------------
// 适配器
// ---------------------------------------------------------------------------

/** @brief 脚本提供方实现的 `ISensingCandidateProvider`。 */
class ScriptSensingCandidateProvider final : public ISensingCandidateProvider {
public:
    explicit ScriptSensingCandidateProvider(ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const ScriptProviderHandle& handle() const noexcept { return handle_; }

    Result<std::vector<TargetCandidate>> query(const TargetingQuery& query) const override {
        const ScriptArg args[] = {ScriptArg::fromTable(queryValue(query))};
        auto            answer = ScriptProviderRegistry::instance().callValue(handle_, "queryCandidates", args);
        if (!answer.ok()) return Result<std::vector<TargetCandidate>>::failure(answer.status());

        const auto* entries = answer.value().getIf<Value::Array>();
        if (entries == nullptr)
            return scriptFailure<std::vector<TargetCandidate>>(
                DiagnosticCode::InvalidArgument, "queryCandidates must return an array of candidate tables",
                "sensing.script-candidates");

        std::vector<TargetCandidate> candidates;
        candidates.reserve(entries->size());
        for (const Value& entry : *entries) {
            auto candidate = candidateFromScript(entry, query.spec.space);
            if (!candidate.ok()) return Result<std::vector<TargetCandidate>>::failure(candidate.status());
            candidates.push_back(std::move(candidate).takeValue());
        }
        return Result<std::vector<TargetCandidate>>::success(std::move(candidates));
    }

private:
    ScriptProviderHandle handle_;
};

/** @brief 进程级持有脚本适配器（理由同其它接缝：能力表是进程级的）。 */
struct ScriptSensingCandidateProviders {
    std::vector<std::unique_ptr<ScriptSensingCandidateProvider>> live;
    std::vector<std::unique_ptr<ScriptSensingCandidateProvider>> retired;
};

ScriptSensingCandidateProviders& scriptSensingCandidateProviders() {
    static ScriptSensingCandidateProviders state;
    return state;
}

}  // namespace

EVENGINE_API_PLATFORM void declareScriptSensingCandidateSeam() {
    ScriptSensingCandidateProviders& owned = scriptSensingCandidateProviders();
    ScriptProviderRegistry::instance().declareCapability(
        ISensingCandidateProvider::capabilityName,
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            // 单实现槽位：`provide` 会顶掉当前实现，顶掉必须可见（否则原生 provider 会"悄悄失效"，
            // 而症状只是候选变少）。
            if (cap::query<ISensingCandidateProvider>() != nullptr) {
                recordLogEvent("warn", "script sensing candidate provider '" + handle.name +
                                           "' replaced the provider that owned ISensingCandidateProvider");
            }
            auto                            adapter = std::make_unique<ScriptSensingCandidateProvider>(handle);
            ScriptSensingCandidateProvider* raw     = adapter.get();
            // 先注册再交所有权：从这一刻起能力槽位持有裸指针，`owned.live` 是让它有效的唯一原因。
            cap::provide<ISensingCandidateProvider>(raw);
            owned.live.push_back(std::move(adapter));
            return Result<void>::success();
        },
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            const auto match = std::find_if(owned.live.begin(), owned.live.end(),
                                            [&handle](const std::unique_ptr<ScriptSensingCandidateProvider>& adapter) {
                                                return adapter->handle() == handle;
                                            });
            if (match == owned.live.end()) {
                // 幂等：后置条件是"这一代没有适配器"，模块退出时可能已经撤过了。
                return Result<void>::success();
            }
            cap::revoke<ISensingCandidateProvider>(match->get());
            owned.retired.push_back(std::move(*match));
            owned.live.erase(match);
            return Result<void>::success();
        });
}

EVENGINE_API_PLATFORM void withdrawScriptSensingCandidateSeam() {
    ScriptSensingCandidateProviders& owned = scriptSensingCandidateProviders();
    for (auto& adapter : owned.live) {
        cap::revoke<ISensingCandidateProvider>(adapter.get());
        owned.retired.push_back(std::move(adapter));
    }
    owned.live.clear();

    // 连声明一起撤：模块不在了还留着接缝，只会让脚本发布出一个永远不会被调用的适配器。
    ScriptProviderRegistry::instance()
        .removeCapability(ISensingCandidateProvider::capabilityName)
        .ignore("a module may be destroyed without ever having declared the seam");
}

}  // namespace eve::sensing
