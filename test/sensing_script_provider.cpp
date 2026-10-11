#include "ScriptTest.h"

#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "sensing/Sensing.h"
#include "sensing/SensingScriptCandidateSeam.h"
#include "sensing/Targeting.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 决定③ 的第四个落地插座：`ISensingCandidateProvider` 可以由脚本提供。
//
// 与地块碰撞那一类不同，这条插座的载荷是**带坐标空间与区域几何的记录**：下行是一张查询表，上行是
// 一串候选表。编组的两条纪律在用例里钉住：①位置一律自带 `space`；②候选的空间必须与查询空间一致
// （不一致返回 `Unsupported`，不静默换算）。可见性与数量策略不下发——那是 resolver 的活。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::sensing;

const char* kScriptSensingProvider = R"SQ(
lastOrigin <- ""
lastSpace <- ""
lastDomain <- ""
lastMaxRange <- -1.0
lastMinRange <- -1.0
lastRequiredTagCount <- -1
lastExcludedTagCount <- -1
lastZone <- ""
lastAreaShape <- ""
lastAreaRadius <- -1.0
lastOriginX <- -1.0

class CandidateProvider {
    function queryCandidates(query) {
        lastOrigin = query.origin
        lastSpace = query.space
        lastDomain = query.domain
        lastMaxRange = query.maxRange == null ? -1.0 : query.maxRange
        lastMinRange = query.minRange
        lastRequiredTagCount = query.requiredTags.len()
        lastExcludedTagCount = query.excludedTags.len()
        lastZone = query.zone == null ? "" : query.zone
        lastAreaShape = query.area == null ? "none" : query.area.shape
        lastAreaRadius = query.area == null ? -1.0 : query.area.radius
        lastOriginX = query.originLocation.x
        return [
            { subject = "00000000-0000-7000-8000-000000000002",
              location = { space = "world2d", x = 3.0, y = 4.0 },
              domain = "enemy", tags = ["unit", "visible"], zones = ["arena:central"] },
            { subject = "00000000-0000-7000-8000-000000000004",
              location = { space = "world2d", x = 2.0, y = 0.0 } },
        ]
    }
}

class GridCandidateProvider {
    function queryCandidates(query) {
        // 查询是 world2d，却回了 grid2d：适配器必须拒绝，而不是当成同一套单位算距离。
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 3, y = 4 } }]
    }
}

class NotAnArrayProvider {
    function queryCandidates(query) { return { subject = "not-an-array" } }
}

class BadSubjectProvider {
    function queryCandidates(query) {
        return [{ subject = "not-a-uuid", location = { space = "world2d", x = 1.0, y = 1.0 } }]
    }
}

class FractionalGridProvider {
    function queryCandidates(query) {
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 1.5, y = 2.0 } }]
    }
}

// 格子查询配格子候选是合法的：适配器不是"只认世界坐标"。
class GridOkProvider {
    function queryCandidates(query) {
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 3, y = 4 }, domain = "enemy", tags = ["unit"] }]
    }
}

class BrokenProvider {
    function queryCandidates(query) { throw "sensing provider is broken" }
}

candidateProvider <- CandidateProvider()
gridCandidateProvider <- GridCandidateProvider()
notAnArrayProvider <- NotAnArrayProvider()
badSubjectProvider <- BadSubjectProvider()
fractionalGridProvider <- FractionalGridProvider()
gridOkProvider <- GridOkProvider()
brokenProvider <- BrokenProvider()
)SQ";

constexpr const char* kOriginId = "00000000-0000-7000-8000-000000000001";
constexpr const char* kEnemyId  = "00000000-0000-7000-8000-000000000002";
constexpr const char* kAllyId   = "00000000-0000-7000-8000-000000000004";

SubjectRef subject(const char* text) {
    const auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

WorldPoint world3D(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    return std::move(point).expect("test World3D point must be valid");
}

WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

/** 一个带齐各类约束的 World2D 查询：用来检查下行编组有没有把字段丢掉。 */
TargetingQuery worldQuery() {
    TargetingSpec spec;
    spec.space        = CoordinateSpace::World2D;
    spec.domain       = TargetDomain::Enemy;
    spec.minRange     = 1.5f;
    spec.maxRange     = 10.f;
    spec.requiredTags = {"unit"};
    spec.excludedTags = {"hidden"};
    const auto zoneId = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    auto zone = ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());
    spec.zone      = *zone;
    spec.worldArea = std::move(WorldArea::circle2D(world2D(0.f, 0.f), 10.f)).expect("circle area");
    return TargetingQuery{subject(kOriginId), world2D(1.f, 2.f), spec};
}

/** 只给空间与原点的最小查询：坐标空间用例用它，避免被其它约束掩盖。 */
TargetingQuery minimalWorldQuery() {
    TargetingSpec spec;
    spec.space = CoordinateSpace::World2D;
    return TargetingQuery{subject(kOriginId), world2D(0.f, 0.f), spec};
}

class ScriptSensingProviderTest : public ScriptTest {
public:
    ScriptSensingProviderTest() : ScriptTest(kScriptSensingProvider) {
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        cap::detail::clearAllRaw();
    }

    ~ScriptSensingProviderTest() {
        withdrawScriptSensingCandidateSeam();
        cap::detail::clearAllRaw();
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    /** 发布一个脚本候选提供方；返回结果表，测试据此断言接线成没成。 */
    ssq::Object publish(const char* name, const char* instance) {
        const std::string source = std::string("published <- eve.provider.publishFor(\"") +
                                   ISensingCandidateProvider::capabilityName + "\", \"" + name + "\", " + instance +
                                   ")";
        vm.run(vm.compileSource(source.c_str()));
        return vm.find("published");
    }

    /** 按名字退订（脚本侧 `publishFor` 拿不到 C++ 句柄，这里用名字 + 当前代际还原一个）。 */
    void unload(const char* name) {
        auto& registry = ScriptProviderRegistry::instance();
        REQUIRE(registry.unload(ScriptProviderHandle{name, registry.generationOf(name)}).ok());
    }

private:
    std::vector<std::string> declaredBefore_;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderAnswersTheResolver") {
    declareScriptSensingCandidateSeam();
    ssq::Object published = publish("test.sensing.candidates", "candidateProvider");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    REQUIRE(cap::query<ISensingCandidateProvider>() != nullptr);

    auto resolved = TargetingResolver{}.resolve(worldQuery());
    REQUIRE(resolved.ok());
    // 脚本回了两条：一条 enemy（被 domain 约束保留），一条缺省 neutral（被挡掉）。
    REQUIRE_EQ(resolved.value().subjects().size(), std::size_t{1});
    REQUIRE_EQ(resolved.value().subjects().front(), subject(kEnemyId));

    // 下行编组：脚本看到的字段与查询里的约束逐个对上（包括区域与无界/有界距离）。
    REQUIRE_EQ(vm.find("lastOrigin").toString(), std::string(kOriginId));
    REQUIRE_EQ(vm.find("lastSpace").toString(), std::string("world2d"));
    REQUIRE_EQ(vm.find("lastDomain").toString(), std::string("enemy"));
    REQUIRE_EQ(vm.find("lastMinRange").toFloat(), 1.5);
    REQUIRE_EQ(vm.find("lastMaxRange").toFloat(), 10.0);
    REQUIRE_EQ(vm.find("lastRequiredTagCount").toInt(), 1);
    REQUIRE_EQ(vm.find("lastExcludedTagCount").toInt(), 1);
    REQUIRE_EQ(vm.find("lastZone").toString(), std::string("arena:central"));
    REQUIRE_EQ(vm.find("lastAreaShape").toString(), std::string("circle2d"));
    REQUIRE_EQ(vm.find("lastAreaRadius").toFloat(), 10.0);
    REQUIRE_EQ(vm.find("lastOriginX").toFloat(), 1.0);

    // 退订之后能力槽位重新为空，resolver 回到"没有提供方"。
    unload("test.sensing.candidates");
    REQUIRE(cap::query<ISensingCandidateProvider>() == nullptr);
    const auto absent = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!absent.ok());
    REQUIRE_EQ(static_cast<int>(absent.code()), static_cast<int>(StatusCode::Unsupported));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderSeamIsAbsentWithoutTheModule") {
    // 没有声明接缝时，发布被明确拒绝；resolver 报 Unsupported，而不是"绑了但不响"。
    REQUIRE(!ScriptProviderRegistry::instance().hasCapabilitySeam(ISensingCandidateProvider::capabilityName));
    ssq::Object refused = publish("test.sensing.orphan", "candidateProvider");
    REQUIRE(!refused.find("ok").toBool());
    REQUIRE_EQ(refused.find("code").toString(), std::string("not_found"));
    const auto absent = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!absent.ok());
    REQUIRE_EQ(static_cast<int>(absent.code()), static_cast<int>(StatusCode::Unsupported));

    // `Sensing` 模块自己构造时会声明接缝：这就是生产通路。
    Sensing sensing;
    REQUIRE(ScriptProviderRegistry::instance().hasCapabilitySeam(ISensingCandidateProvider::capabilityName));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateAnswerMustStayInTheQuerySpace") {
    declareScriptSensingCandidateSeam();
    REQUIRE(publish("test.sensing.grid", "gridCandidateProvider").find("ok").toBool());

    // 查询是 world2d、候选自称 grid2d：明确拒绝（`Unsupported`），而不是把格子当世界单位算距离。
    const auto resolved = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!resolved.ok());
    REQUIRE_EQ(static_cast<int>(resolved.code()), static_cast<int>(StatusCode::Unsupported));
    REQUIRE(resolved.error() != nullptr);
    REQUIRE(resolved.error()->message().find("does not match the query space") != std::string::npos);
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.malformedCandidateAnswersAreRejected") {
    declareScriptSensingCandidateSeam();

    // 返回的不是数组：拒绝（`Rejected`），而不是"当成零个候选"。
    REQUIRE(publish("test.sensing.not-array", "notAnArrayProvider").find("ok").toBool());
    auto notArray = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!notArray.ok());
    REQUIRE_EQ(static_cast<int>(notArray.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.not-array");

    // 主体不是规范 UUID：拒绝。
    REQUIRE(publish("test.sensing.bad-subject", "badSubjectProvider").find("ok").toBool());
    auto badSubject = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!badSubject.ok());
    REQUIRE_EQ(static_cast<int>(badSubject.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.bad-subject");

    // 格子坐标不是整数：拒绝，而不是截断成整数后继续。
    REQUIRE(publish("test.sensing.fractional", "fractionalGridProvider").find("ok").toBool());
    TargetingSpec gridSpec;
    gridSpec.space = CoordinateSpace::Grid2D;
    TargetingQuery gridQuery{subject(kOriginId), TargetLocation(GridPoint::grid2D(0, 0)), gridSpec};
    auto           fractional = TargetingResolver{}.resolve(gridQuery);
    REQUIRE(!fractional.ok());
    REQUIRE_EQ(static_cast<int>(fractional.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.fractional");

    // 同一个空间换成整数的格子候选就是合法答案：适配器不是"只认世界坐标"。
    REQUIRE(publish("test.sensing.grid-ok", "gridOkProvider").find("ok").toBool());
    auto gridAnswer = TargetingResolver{}.resolve(gridQuery);
    REQUIRE(gridAnswer.ok());
    REQUIRE_EQ(gridAnswer.value().subjects().size(), std::size_t{1});
    REQUIRE_EQ(gridAnswer.value().subjects().front(), subject(kEnemyId));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderFailureIsReportedNotSwallowed") {
    declareScriptSensingCandidateSeam();
    REQUIRE(publish("test.sensing.broken", "brokenProvider").find("ok").toBool());

    // 脚本抛错 → 解析失败（`Failed`），不吞：吞掉会让上层把"没候选"当成"真的没有敌人"。
    const auto failed = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!failed.ok());
    REQUIRE_EQ(static_cast<int>(failed.code()), static_cast<int>(StatusCode::Failed));

    // 提供方在失败后还活着：换回能用的脚本对象，同一条通路恢复。
    unload("test.sensing.broken");
    REQUIRE(publish("test.sensing.good", "candidateProvider").find("ok").toBool());
    REQUIRE(TargetingResolver{}.resolve(minimalWorldQuery()).ok());
}

TEST_CASE("sensing.script.coordinateSpaceSpellingMatchesTheModuleName") {
    // 脚本词表只有一份，但它必须与模块既有的 `coordinateSpaceName` 拼写一致；这条用例把两者钉在
    // 一起：任何一边改名字，这里就红。
    const SubjectRef origin   = subject(kOriginId);
    const auto       queryFor = [&origin](CoordinateSpace space) {
        TargetingSpec spec;
        spec.space = space;
        TargetLocation location;
        switch (space) {
            case CoordinateSpace::World2D: location = TargetLocation(world2D(0.f, 0.f)); break;
            case CoordinateSpace::World3D: location = TargetLocation(world3D(0.f, 0.f, 0.f)); break;
            case CoordinateSpace::Grid2D: location = TargetLocation(GridPoint::grid2D(0, 0)); break;
            case CoordinateSpace::Grid3D: location = TargetLocation(GridPoint::grid3D(0, 0, 0)); break;
        }
        return TargetingQuery{origin, location, spec};
    };
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::World2D), std::string_view("world2d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::World3D), std::string_view("world3d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::Grid2D), std::string_view("grid2d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::Grid3D), std::string_view("grid3d"));
    // 四个空间都能构造出通过校验的查询：词表与校验规则对齐。
    for (const CoordinateSpace space :
         {CoordinateSpace::World2D, CoordinateSpace::World3D, CoordinateSpace::Grid2D, CoordinateSpace::Grid3D}) {
        REQUIRE(queryFor(space).validate().ok());
    }
}
