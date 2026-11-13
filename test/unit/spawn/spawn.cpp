#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include "common/Capability.h"
#include "spawn/SpawnDefinition.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnMap.h"
#include "spawn/SpawnRandom.h"
#include "spawn/SpawnRegion.h"
#include "spawn/SpawnRestoreState.h"
#include "spawn/SpawnSavedState.h"
#include "spawn/SpawnWorld.h"

namespace test_spawn {
namespace {
class Units final : public eve::spawn::IUnitDefinitionResolver {
public:
    eve::Result<void> validate(const eve::DefinitionRef& reference) const override {
        if (reference.format() == "rpg.unit:missing")
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Missing test unit"));
        return eve::Result<void>::success();
    }
};
eve::Value templateSource() {
    return eve::Value::fromJson(R"({
      "schema":"eve.spawn-template", "version":1, "id":"wolves",
      "content":{"kind":"weighted","selection":"per_unit","entries":[
        {"unit":{"type":"rpg.unit","id":"wolf"},"weight":80},
        {"unit":{"type":"rpg.unit","id":"alpha"},"weight":20}]},
      "schedule":{"kind":"maintain_population","target_alive":4,"initial_population":4,
        "replacement":{"after":"death","delay_seconds":[20,35]}},
      "limits":{"alive":4,"total":{"kind":"unlimited"},"budget_group":"forest"},
      "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":12},
      "batch_policy":"atomic",
      "failure":{"placement_blocked":{"kind":"retry","delay_seconds":2}}
    })")
        .expect("valid test template JSON");
}
eve::Value mapSource() {
    return eve::Value::fromJson(R"({
      "schema":"eve.map-spawns","version":1,"map":"forest",
      "budgets":[{"id":"forest","alive_limit":10}],
      "instances":[{"id":"north","template":{"type":"spawn.template","id":"wolves"},
        "region":{"kind":"circle","space":"world2d","center":[-120,80],"radius":18},
        "activation":{"kind":"player_proximity","enter_distance":60,"exit_distance":80},
        "inactive_policy":"pause","unload_policy":"persist"}]
    })")
        .expect("valid test map JSON");
}
class Templates final : public eve::spawn::ITemplateResolver {
public:
    eve::Value                                content = templateSource();
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef& reference) const override {
        if (reference.format() != "spawn.template:wolves")
            return eve::Result<eve::spawn::CompiledTemplate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Unknown template"));
        return eve::spawn::compileTemplate(content, Units{});
    }
};
}  // namespace

TEST_CASE("Spawn template compilation owns validated typed data and exact duration") {
    Units units;
    auto  source   = templateSource();
    auto  compiled = eve::spawn::compileTemplate(source, units);
    REQUIRE(compiled.ok());
    source.set("id", eve::Value("changed"));
    CHECK_EQ(compiled.value().definition().id, "wolves");
    const auto& schedule = std::get<eve::spawn::PopulationSchedule>(compiled.value().definition().schedule);
    CHECK_EQ(schedule.minimumDelay.nanoseconds(), std::int64_t(20000000000));
    CHECK_EQ(schedule.maximumDelay.nanoseconds(), std::int64_t(35000000000));
    CHECK_EQ(std::get<eve::spawn::WeightedContent>(compiled.value().definition().content).entries[0].unit.format(),
             "rpg.unit:wolf");
}

TEST_CASE("Spawn template compilation rejects schema and semantic failures with field paths") {
    Units units;
    SUB_CASE("Unknown field") {
        auto source = templateSource();
        source.set("typo", eve::Value(1));
        auto result = eve::spawn::compileTemplate(source, units);
        REQUIRE(!result.ok());
        REQUIRE(result.status().primaryDiagnostic() != nullptr);
        CHECK_EQ(result.status().primaryDiagnostic()->path(), "$.typo");
    };
    SUB_CASE("Unknown version") {
        auto source = templateSource();
        source.set("version", eve::Value(2));
        auto result = eve::spawn::compileTemplate(source, units);
        REQUIRE(!result.ok());
        CHECK(result.status().primaryDiagnostic()->code() == eve::DiagnosticCode::UnknownVersion);
    };
    SUB_CASE("Cannot fit target population") {
        auto source = templateSource();
        source.find("limits")->set("alive", eve::Value(3));
        CHECK(!eve::spawn::compileTemplate(source, units).ok());
    };
    SUB_CASE("All zero weights") {
        auto  source  = templateSource();
        auto* entries = source.find("content")->find("entries");
        entries->at(0).set("weight", eve::Value(0));
        entries->at(1).set("weight", eve::Value(0));
        CHECK(!eve::spawn::compileTemplate(source, units).ok());
    };
    SUB_CASE("Unknown unit") {
        auto source = templateSource();
        source.find("content")->find("entries")->at(0).find("unit")->set("id", eve::Value("missing"));
        auto result = eve::spawn::compileTemplate(source, units);
        REQUIRE(!result.ok());
        CHECK(result.status().primaryDiagnostic()->code() == eve::DiagnosticCode::NotFound);
        CHECK_EQ(result.status().primaryDiagnostic()->path(), "$.content.entries[0].unit");
    };
    SUB_CASE("Negative or inverted replacement duration") {
        auto source = templateSource();
        source.find("schedule")->find("replacement")->set("delay_seconds", eve::Value::array({35, 20}));
        CHECK(!eve::spawn::compileTemplate(source, units).ok());
        source.find("schedule")->find("replacement")->set("delay_seconds", eve::Value::array({-1, 20}));
        CHECK(!eve::spawn::compileTemplate(source, units).ok());
    };
}

TEST_CASE("Spawn finite waves cannot demand more than lifetime quota") {
    Units units;
    auto  source = templateSource();
    auto  object = std::get<eve::Value::Object>(source.storage());
    object.erase("content");
    source        = eve::Value(std::move(object));
    auto schedule = eve::Value::fromJson(R"({"kind":"waves","waves":[
      {"id":"first","delay_seconds":0,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":3}],"complete_when":"all_members_dead"},
      {"id":"second","delay_seconds":5,"members":[{"id":"boss","unit":{"type":"rpg.unit","id":"alpha"},"count":2}],"complete_when":"all_members_dead"}]})")
                        .expect("valid waves");
    source.set("schedule", schedule);
    source.find("limits")->set("total", eve::Value::object({{"kind", "finite"}, {"count", 4}}));
    CHECK(!eve::spawn::compileTemplate(source, units).ok());
    source.find("limits")->set("total", eve::Value::object({{"kind", "finite"}, {"count", 5}}));
    auto result = eve::spawn::compileTemplate(source, units);
    REQUIRE(result.ok());
    CHECK_EQ(std::get<eve::spawn::WaveSchedule>(result.value().definition().schedule).waves.size(), std::size_t(2));
}

TEST_CASE("Spawn interval skip rejects retry and rounds subnanosecond period to invalid zero") {
    auto source = templateSource();
    source.set("schedule", eve::Value::object({{"kind", "interval"},
                                               {"period_seconds", 30},
                                               {"initial_delay_seconds", 0},
                                               {"count_per_batch", 4},
                                               {"on_missed", "skip"}}));
    CHECK(!eve::spawn::compileTemplate(source, Units{}).ok());
    auto object = std::get<eve::Value::Object>(source.storage());
    object.erase("failure");
    source = eve::Value(std::move(object));
    REQUIRE(eve::spawn::compileTemplate(source, Units{}).ok());
    source.find("schedule")->set("period_seconds", eve::Value(0.0000000001));
    CHECK(!eve::spawn::compileTemplate(source, Units{}).ok());
}

TEST_CASE("Spawn map constrained overrides validate the merged template and budget") {
    auto source = mapSource();
    source.find("instances")
        ->at(0)
        .set("overrides",
             eve::Value::object(
                 {{"population",
                   eve::Value::object({{"target_alive", 3}, {"initial_population", 3}, {"alive_limit", 3}})}}));
    auto result = eve::spawn::compileMap(source, Templates{}, Units{});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().instances().size(), std::size_t(1));
    CHECK_EQ(result.value().instances()[0].revision->definition().limits.alive, std::uint32_t(3));
    CHECK_EQ(std::get<eve::spawn::CircleRegion>(result.value().instances()[0].region).center.x, -120.0);
    source.find("budgets")->at(0).set("alive_limit", eve::Value(2));
    CHECK(!eve::spawn::compileMap(source, Templates{}, Units{}).ok());
    source.find("budgets")->at(0).set("alive_limit", eve::Value(10));
    source.find("instances")->at(0).find("overrides")->find("population")->set("initial_population", eve::Value(4));
    CHECK(!eve::spawn::compileMap(source, Templates{}, Units{}).ok());
}

TEST_CASE("Spawn map polygon validation rejects crossing and duplicate vertices") {
    auto source  = mapSource();
    auto polygon = eve::Value::fromJson(R"({"kind":"polygon","space":"world2d","vertices":[[0,0],[4,0],[4,4],[0,4]]})")
                       .expect("polygon");
    source.find("instances")->at(0).set("region", polygon);
    REQUIRE(eve::spawn::compileMap(source, Templates{}, Units{}).ok());
    polygon.set("vertices", eve::Value::array({eve::Value::array({0, 0}), eve::Value::array({4, 4}),
                                               eve::Value::array({0, 4}), eve::Value::array({4, 0})}));
    source.find("instances")->at(0).set("region", polygon);
    CHECK(!eve::spawn::compileMap(source, Templates{}, Units{}).ok());
    polygon.find("vertices")->at(3) = eve::Value::array({0, 0});
    source.find("instances")->at(0).set("region", polygon);
    CHECK(!eve::spawn::compileMap(source, Templates{}, Units{}).ok());
}

TEST_CASE("Spawn map rejects invalid proximity and unsupported coordinate spaces") {
    auto source = mapSource();
    source.find("instances")->at(0).find("activation")->set("exit_distance", eve::Value(60));
    CHECK(!eve::spawn::compileMap(source, Templates{}, Units{}).ok());
    source.find("instances")->at(0).find("activation")->set("exit_distance", eve::Value(80));
    source.find("instances")->at(0).find("region")->set("space", eve::Value("world3d"));
    auto result = eve::spawn::compileMap(source, Templates{}, Units{});
    REQUIRE(!result.ok());
    CHECK(result.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Unsupported);
}
}  // namespace test_spawn

namespace test_spawn {
namespace {
class Placement final : public eve::spawn::ISpawnPlacement {
public:
    bool                         blocked = false;
    std::vector<eve::SubjectRef> attempts;
    eve::Result<void>            validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
        return eve::Result<void>::success();
    }
    eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest& request,
                                                   const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                   std::uint64_t&) override {
        for (const auto& unit : request.units) attempts.push_back(unit.subject);
        if (blocked) return eve::Result<eve::spawn::PlacementOutcome>::success(eve::spawn::PlacementBlocked{});
        return eve::Result<eve::spawn::PlacementOutcome>::success(
            std::vector<eve::spawn::Point>(request.units.size(), {1, 2}));
    }
};
class Factory final : public eve::spawn::ISpawnFactory {
public:
    std::function<void()>                                onPrepare;
    std::function<void(const eve::spawn::BatchRequest&)> onRequest;
    bool                                                 failCommit     = false;
    bool                                                 blockedPrepare = false;
    bool                                                 blockedCommit  = false;
    std::vector<eve::SubjectRef>                         live;
    bool                                                 failRemoval = false;
    eve::Result<eve::spawn::RemovalReceipt>              remove(const eve::spawn::RemovalRequest& request) override {
        if (failRemoval)
            return eve::Result<eve::spawn::RemovalReceipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Injected removal failure"));
        if (request.policy == eve::spawn::DetachPolicy::RemoveUnits)
            for (const auto& subject : request.subjects) std::erase(live, subject);
        return eve::Result<eve::spawn::RemovalReceipt>::success({request.subjects.size()});
    }
    struct Prepared final : eve::spawn::IPreparedSpawn {
        Factory&                     owner;
        std::vector<eve::SubjectRef> subjects;
        Prepared(Factory& factory, std::vector<eve::SubjectRef> values) : owner(factory), subjects(std::move(values)) {}
        bool                                   published = false;
        bool                                   isPublished() const noexcept override { return published; }
        eve::Result<eve::spawn::CommitOutcome> commit() override {
            if (owner.blockedCommit)
                return eve::Result<eve::spawn::CommitOutcome>::success(eve::spawn::CommitOutcome::PlacementBlocked);
            if (owner.failCommit)
                return eve::Result<eve::spawn::CommitOutcome>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Injected commit failure"));
            if (published) return eve::Result<eve::spawn::CommitOutcome>::success(eve::spawn::CommitOutcome::Committed);
            owner.live.insert(owner.live.end(), subjects.begin(), subjects.end());
            published = true;
            return eve::Result<eve::spawn::CommitOutcome>::success(eve::spawn::CommitOutcome::Committed);
        }
    };
    eve::Result<eve::spawn::PrepareOutcome> prepare(const eve::spawn::BatchRequest& request,
                                                    std::span<const eve::spawn::Point>) override {
        if (onPrepare) onPrepare();
        if (onRequest) onRequest(request);
        if (blockedPrepare) return eve::Result<eve::spawn::PrepareOutcome>::success(eve::spawn::PlacementBlocked{});
        std::vector<eve::SubjectRef> subjects;
        for (const auto& unit : request.units) subjects.push_back(unit.subject);
        live.reserve(live.size() + subjects.size());
        std::unique_ptr<eve::spawn::IPreparedSpawn> token = std::make_unique<Prepared>(*this, std::move(subjects));
        return eve::Result<eve::spawn::PrepareOutcome>::success(eve::spawn::PrepareOutcome(std::move(token)));
    }
};
struct RuntimeFixture {
    RuntimeFixture() = default;
    explicit RuntimeFixture(eve::spawn::WorkLimits limits)
        : world(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000001"), 42, limits) {}

    std::shared_ptr<Placement>                  placement = std::make_shared<Placement>();
    std::shared_ptr<Factory>                    factory   = std::make_shared<Factory>();
    std::shared_ptr<eve::spawn::SpawnProviders> providers =
        std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{placement, factory});
    eve::spawn::SpawnWorld    world{*eve::PersistentId::parse("01900000-0000-7000-8000-000000000001"), 42};
    eve::spawn::SpawnerHandle attach(Templates templates = {}) {
        auto source = mapSource();
        source.find("instances")->at(0).set("activation", eve::Value::object({{"kind", "map_loaded"}}));
        auto map = eve::spawn::compileMap(source, templates, Units{}).expect("compiled fixture map");
        return world.attach(map, providers).expect("attached world")[0];
    }
    eve::spawn::AdvanceReport step(std::uint64_t tick, std::int64_t seconds,
                                   std::span<const eve::spawn::LifecycleInput> events = {}) {
        return world.advance({eve::SimulationTick(tick), eve::Duration(seconds * 1000000000)}, events)
            .expect("runtime step");
    }
};
}  // namespace
TEST_CASE("Spawn population replaces each death once using simulation cooldown") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    CHECK_EQ(fixture.step(1, 0).units, std::uint32_t(4));
    auto initial = fixture.world.inspect(handle).expect("initial snapshot");
    REQUIRE(initial.subjects.size() == 4);
    eve::spawn::LifecycleInput death{initial.subjects[0], 1, eve::spawn::RemovalReason::Death, handle};
    CHECK_EQ(fixture.step(2, 0, std::span(&death, 1)).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(3, 0, std::span(&death, 1)).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(4, 19).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(5, 16).units, std::uint32_t(1));
    auto replaced = fixture.world.inspect(handle).expect("replacement snapshot");
    CHECK_EQ(replaced.alive, std::uint32_t(4));
    CHECK_EQ(replaced.total, std::uint64_t(5));
}
TEST_CASE("Spawn blocked placement preserves selected operation and does not consume quota") {
    RuntimeFixture fixture;
    auto           handle      = fixture.attach();
    fixture.placement->blocked = true;
    CHECK_EQ(fixture.step(1, 0).units, std::uint32_t(0));
    auto blocked = fixture.world.inspect(handle).expect("blocked snapshot");
    CHECK(blocked.blocked == eve::spawn::BlockReason::PlacementBlocked);
    CHECK_EQ(blocked.total, std::uint64_t(0));
    fixture.placement->blocked = false;
    CHECK_EQ(fixture.step(2, 1).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(3, 1).units, std::uint32_t(4));
    REQUIRE(fixture.placement->attempts.size() == 8);
    CHECK(fixture.placement->attempts[0] == fixture.placement->attempts[4]);
}
TEST_CASE("Spawn paused instance freezes its remaining cooldown") {
    RuntimeFixture fixture;
    auto           handle      = fixture.attach();
    fixture.placement->blocked = true;
    fixture.step(1, 0);
    REQUIRE(fixture.world.setActive(handle, false).ok());
    fixture.placement->blocked = false;
    CHECK_EQ(fixture.step(2, 100).units, std::uint32_t(0));
    REQUIRE(fixture.world.setActive(handle, true).ok());
    CHECK_EQ(fixture.step(3, 1).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(4, 1).units, std::uint32_t(4));
}
TEST_CASE("Spawn commit failure leaves domain publication and budget unchanged") {
    RuntimeFixture fixture;
    auto           handle       = fixture.attach();
    fixture.factory->failCommit = true;
    auto report                 = fixture.step(1, 0);
    CHECK_EQ(report.units, std::uint32_t(0));
    CHECK_EQ(report.diagnostics.size(), std::size_t(1));
    CHECK(fixture.factory->live.empty());
    auto state = fixture.world.inspect(handle).expect("failed snapshot");
    REQUIRE(state.phase == eve::spawn::Phase::Faulted);
    CHECK_EQ(state.alive, std::uint32_t(0));
    CHECK_EQ(state.total, std::uint64_t(0));
}
TEST_CASE("Spawn repeated tick and foreign owner reject without mutation") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.step(1, 0);
    auto rejected = fixture.world.advance({eve::SimulationTick(1), eve::Duration(1000000000)});
    REQUIRE(!rejected.ok());
    handle.owner = handle.owner.child("foreign");
    CHECK(!fixture.world.inspect(handle).ok());
}
TEST_CASE("Spawn provider session expiry is observable and creates no units") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.providers.reset();
    CHECK_EQ(fixture.step(1, 0).units, std::uint32_t(0));
    auto state = fixture.world.inspect(handle).expect("provider absent snapshot");
    REQUIRE(state.blocked == eve::spawn::BlockReason::ProviderUnavailable);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn interval skips missed periods without accumulating deferred units") {
    Templates templates;
    auto      fields = std::get<eve::Value::Object>(templates.content.storage());
    fields.erase("failure");
    templates.content = eve::Value(std::move(fields));
    templates.content.set("schedule", eve::Value::object({{"kind", "interval"},
                                                          {"period_seconds", 10},
                                                          {"initial_delay_seconds", 0},
                                                          {"count_per_batch", 1},
                                                          {"on_missed", "skip"}}));
    RuntimeFixture fixture;
    auto           handle      = fixture.attach(templates);
    fixture.placement->blocked = true;
    REQUIRE(fixture.step(1, 35).units == 0);
    fixture.placement->blocked = false;
    CHECK_EQ(fixture.step(2, 4).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(3, 1).units, std::uint32_t(1));
    CHECK_EQ(fixture.world.inspect(handle).expect("interval snapshot").total, std::uint64_t(1));
}
TEST_CASE("Spawn once publishes a fixed group only once") {
    Templates templates;
    templates.content.set("schedule", eve::Value::object({{"kind", "once"}}));
    templates.content.set(
        "content",
        eve::Value::object({{"kind", "fixed"},
                            {"members", eve::Value::array({eve::Value::object(
                                            {{"id", "guards"},
                                             {"unit", eve::Value::object({{"type", "rpg.unit"}, {"id", "wolf"}})},
                                             {"count", 2}})})}}));
    RuntimeFixture fixture;
    auto           handle = fixture.attach(templates);
    REQUIRE(fixture.step(1, 0).units == 2);
    CHECK_EQ(fixture.step(2, 100).units, std::uint32_t(0));
    CHECK(fixture.world.inspect(handle).expect("once snapshot").phase == eve::spawn::Phase::Completed);
}
TEST_CASE("Spawn waves require confirmed deaths and preserve wave delay") {
    Templates templates;
    auto      fields = std::get<eve::Value::Object>(templates.content.storage());
    fields.erase("content");
    templates.content = eve::Value(std::move(fields));
    templates.content.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
      {"id":"first","delay_seconds":0,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"},
      {"id":"second","delay_seconds":5,"members":[{"id":"boss","unit":{"type":"rpg.unit","id":"alpha"},"count":1}],"complete_when":"all_members_dead"}]})")
                                          .expect("wave schedule"));
    RuntimeFixture fixture;
    auto           handle = fixture.attach(templates);
    REQUIRE(fixture.step(1, 0).units == 1);
    auto                       state = fixture.world.inspect(handle).expect("wave snapshot");
    eve::spawn::LifecycleInput death{state.subjects[0], 1, eve::spawn::RemovalReason::Death, handle};
    CHECK_EQ(fixture.step(2, 0, std::span(&death, 1)).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(3, 4).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(4, 1).units, std::uint32_t(1));
    state = fixture.world.inspect(handle).expect("second wave snapshot");
    eve::spawn::LifecycleInput removed{state.subjects[0], 2, eve::spawn::RemovalReason::Administrative, handle};
    fixture.step(5, 0, std::span(&removed, 1));
    CHECK(fixture.world.inspect(handle).expect("removed wave snapshot").phase == eve::spawn::Phase::Faulted);
}
TEST_CASE("Spawn budget arbitration rotates after successful service") {
    RuntimeFixture fixture;
    auto           source    = mapSource();
    auto*          instances = source.find("instances");
    instances->at(0).set("activation", eve::Value::object({{"kind", "map_loaded"}}));
    auto second = instances->at(0);
    second.set("id", eve::Value("south"));
    instances->pushBack(second);
    source.find("budgets")->at(0).set("alive_limit", eve::Value(4));
    auto map     = eve::spawn::compileMap(source, Templates{}, Units{}).expect("shared budget map");
    auto handles = fixture.world.attach(map, fixture.providers).expect("shared budget handles");
    REQUIRE(fixture.step(1, 0).units == 4);
    auto                                    state = fixture.world.inspect(handles[0]).expect("north population");
    std::vector<eve::spawn::LifecycleInput> events;
    for (std::size_t i = 0; i < state.subjects.size(); ++i)
        events.push_back({state.subjects[i], i + 1, eve::spawn::RemovalReason::Death, handles[0]});
    REQUIRE(fixture.step(2, 35, events).units == 4);
    CHECK_EQ(fixture.world.inspect(handles[1]).expect("south population").alive, std::uint32_t(4));
    CHECK_EQ(fixture.world.inspect(handles[0]).expect("north population").alive, std::uint32_t(0));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn lifecycle rejects stale epochs before changing tick or live population") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    auto state = fixture.world.inspect(handle).expect("population snapshot");
    auto stale = handle;
    ++stale.epoch;
    eve::spawn::LifecycleInput event{state.subjects[0], 1, eve::spawn::RemovalReason::Death, stale};
    auto rejected = fixture.world.advance({eve::SimulationTick(2), eve::Duration(0)}, std::span(&event, 1));
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    event.source = handle;
    CHECK_EQ(fixture.step(2, 0, std::span(&event, 1)).units, std::uint32_t(0));
    CHECK_EQ(fixture.world.inspect(handle).expect("population snapshot").alive, std::uint32_t(3));
}
TEST_CASE("Spawn fixed seed replays unit identities and replacement deadlines") {
    RuntimeFixture a, b;
    auto           ah = a.attach(), bh = b.attach();
    REQUIRE(a.step(1, 0).units == 4);
    REQUIRE(b.step(1, 0).units == 4);
    auto as = a.world.inspect(ah).expect("a snapshot");
    auto bs = b.world.inspect(bh).expect("b snapshot");
    CHECK(as.subjects == bs.subjects);
    eve::spawn::LifecycleInput ae{as.subjects[0], 1, eve::spawn::RemovalReason::Death, ah};
    eve::spawn::LifecycleInput be{bs.subjects[0], 1, eve::spawn::RemovalReason::Death, bh};
    a.step(2, 0, std::span(&ae, 1));
    b.step(2, 0, std::span(&be, 1));
    CHECK(a.world.inspect(ah).expect("a replacement").remaining ==
          b.world.inspect(bh).expect("b replacement").remaining);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn final placement contention retries without faulting or publishing") {
    RuntimeFixture fixture;
    auto           handle           = fixture.attach();
    fixture.factory->blockedPrepare = true;
    REQUIRE(fixture.step(1, 0).units == 0);
    auto state = fixture.world.inspect(handle).expect("blocked preparation");
    CHECK(state.phase == eve::spawn::Phase::Running);
    CHECK(state.blocked == eve::spawn::BlockReason::PlacementBlocked);
    CHECK(fixture.factory->live.empty());
    fixture.factory->blockedPrepare = false;
    CHECK_EQ(fixture.step(2, 2).units, std::uint32_t(4));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn detach failure retains budget and identity until explicit cleanup succeeds") {
    RuntimeFixture fixture;
    const auto     handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    REQUIRE(fixture.world.enqueue({handle, eve::spawn::ControlKind::Pause}).ok());
    fixture.factory->failRemoval = true;
    REQUIRE(!fixture.world.detach(handle, eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK_EQ(fixture.world.inspect(handle).expect("retained tracking").alive, std::uint32_t(4));
    CHECK_EQ(fixture.factory->live.size(), std::size_t(4));
    fixture.factory->failRemoval = false;
    auto detached                = fixture.world.detach(handle, eve::spawn::DetachPolicy::RemoveUnits);
    REQUIRE(detached.ok());
    CHECK_EQ(detached.value().cancelledCommands, std::size_t(1));
    CHECK_EQ(detached.value().released, std::size_t(4));
    CHECK(detached.value().events.back().kind == eve::spawn::SpawnEventKind::Detached);
    CHECK(fixture.factory->live.empty());
    CHECK(!fixture.world.inspect(handle).ok());
}
TEST_CASE("Spawn keep detach preserves domain units and reattachment uses new identities") {
    RuntimeFixture fixture;
    const auto     oldHandle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    const auto original = fixture.factory->live;
    REQUIRE(fixture.world.detach(oldHandle, eve::spawn::DetachPolicy::KeepUnits).ok());
    CHECK_EQ(fixture.factory->live.size(), std::size_t(4));
    const auto newHandle = fixture.attach();
    CHECK(newHandle.epoch > oldHandle.epoch);
    REQUIRE(fixture.step(2, 0).units == 4);
    const auto next = fixture.world.inspect(newHandle).expect("new source");
    for (const auto& subject : next.subjects)
        CHECK(std::find(original.begin(), original.end(), subject) == original.end());
}
TEST_CASE("Spawn expired cleanup provider keeps a live source observable") {
    RuntimeFixture fixture;
    const auto     handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    fixture.providers.reset();
    REQUIRE(!fixture.world.detach(handle, eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK_EQ(fixture.world.inspect(handle).expect("cleanup still owed").alive, std::uint32_t(4));
}
TEST_CASE("Spawn observers see committed state and defer controls until next tick") {
    RuntimeFixture fixture;
    const auto     handle    = fixture.attach();
    bool           inspected = false, directRejected = false, queued = false;
    auto           throws = fixture.world
                      .subscribe([](const eve::spawn::SpawnEvent& event) {
                          if (event.kind == eve::spawn::SpawnEventKind::BatchCommitted)
                              throw std::runtime_error("listener failure");
                      })
                      .expect("throwing subscription");
    auto observer = fixture.world
                        .subscribe([&](const eve::spawn::SpawnEvent& event) {
                            if (event.kind != eve::spawn::SpawnEventKind::BatchCommitted) return;
                            inspected      = fixture.world.inspect(handle).expect("committed state").alive == 4;
                            directRejected = !fixture.world.setActive(handle, false).ok();
                            queued         = fixture.world.enqueue({handle, eve::spawn::ControlKind::Pause}).ok();
                        })
                        .expect("control subscription");
    auto report = fixture.step(1, 0);
    REQUIRE(inspected);
    CHECK(directRejected);
    CHECK(queued);
    CHECK_EQ(report.diagnostics.size(), std::size_t(1));
    CHECK(fixture.world.inspect(handle).expect("before queued pause").phase == eve::spawn::Phase::Running);
    auto paused = fixture.step(2, 0);
    CHECK(fixture.world.inspect(handle).expect("after queued pause").phase == eve::spawn::Phase::Dormant);
    REQUIRE(!paused.events.empty());
    CHECK(paused.events[0].sequence.value() > report.events.back().sequence.value());
}
TEST_CASE("Spawn disposed subscriptions do not receive later committed events") {
    RuntimeFixture fixture;
    fixture.attach();
    std::uint32_t observed = 0;
    auto token = fixture.world.subscribe([&](const eve::spawn::SpawnEvent&) { ++observed; }).expect("subscription");
    token.dispose();
    REQUIRE(fixture.step(1, 0).units == 4);
    CHECK_EQ(observed, std::uint32_t(0));
}
TEST_CASE("Spawn retry resumes a failed hidden transaction without consuming a new identity") {
    RuntimeFixture fixture;
    const auto     handle       = fixture.attach();
    fixture.factory->failCommit = true;
    REQUIRE(fixture.step(1, 0).units == 0);
    const auto attempts         = fixture.placement->attempts;
    fixture.factory->failCommit = false;
    REQUIRE(fixture.world.enqueue({handle, eve::spawn::ControlKind::RetryFault}).ok());
    REQUIRE(fixture.step(2, 0).units == 4);
    CHECK(fixture.placement->attempts[0] == fixture.placement->attempts[attempts.size()]);
    CHECK_EQ(fixture.world.inspect(handle).expect("repaired source").total, std::uint64_t(4));
}
TEST_CASE("Spawn administrative removals require explicit population reconciliation") {
    RuntimeFixture fixture;
    const auto     handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    const auto                 state = fixture.world.inspect(handle).expect("initial population");
    eve::spawn::LifecycleInput removed{state.subjects[0], 1, eve::spawn::RemovalReason::Administrative, handle};
    CHECK_EQ(fixture.step(2, 0, std::span(&removed, 1)).units, std::uint32_t(0));
    CHECK_EQ(fixture.step(3, 100).units, std::uint32_t(0));
    REQUIRE(fixture.world.enqueue({handle, eve::spawn::ControlKind::ReconcilePopulation}).ok());
    auto reconciled = fixture.step(4, 0);
    CHECK_EQ(reconciled.units, std::uint32_t(1));
    CHECK(std::any_of(reconciled.events.begin(), reconciled.events.end(),
                      [](const auto& event) { return event.kind == eve::spawn::SpawnEventKind::Reconciled; }));
    CHECK_EQ(fixture.world.inspect(handle).expect("reconciled population").alive, std::uint32_t(4));
}
TEST_CASE("Spawn provider callbacks cannot reenter publication or inspect provisional state") {
    RuntimeFixture fixture;
    const auto     handle          = fixture.attach();
    bool           inspectRejected = false, advanceRejected = false;
    fixture.factory->onPrepare = [&] {
        inspectRejected = !fixture.world.inspect(handle).ok();
        advanceRejected = !fixture.world.advance({eve::SimulationTick(2), eve::Duration(0)}).ok();
    };
    REQUIRE(fixture.step(1, 0).units == 4);
    CHECK(inspectRejected);
    CHECK(advanceRejected);
}
TEST_CASE("Spawn provider exceptions become faults without publishing units") {
    RuntimeFixture fixture;
    const auto     handle      = fixture.attach();
    fixture.factory->onPrepare = [] { throw std::runtime_error("prepare failure"); };
    const auto report          = fixture.step(1, 0);
    REQUIRE(report.units == 0);
    REQUIRE(report.diagnostics.size() == 1);
    CHECK(report.diagnostics[0].code() == eve::DiagnosticCode::CallbackFailure);
    CHECK(fixture.world.inspect(handle).expect("failed source").phase == eve::spawn::Phase::Faulted);
    CHECK(fixture.factory->live.empty());
}
TEST_CASE("Spawn composition capability binds once and reports missing providers") {
    struct Source final : eve::spawn::ISpawnProviderSource {
        std::shared_ptr<eve::spawn::SpawnProviders>              providers;
        eve::Result<std::shared_ptr<eve::spawn::SpawnProviders>> acquire() override {
            return eve::Result<std::shared_ptr<eve::spawn::SpawnProviders>>::success(providers);
        }
    } source;
    REQUIRE(!eve::spawn::bindSpawnProviders().ok());
    RuntimeFixture fixture;
    source.providers = fixture.providers;
    eve::cap::provide<eve::spawn::ISpawnProviderSource>(&source);
    struct Revoke {
        Source& source;
        ~Revoke() { eve::cap::revoke<eve::spawn::ISpawnProviderSource>(&source); }
    } revoke{source};
    const auto bound = eve::spawn::bindSpawnProviders().expect("bound providers");
    CHECK(bound == fixture.providers);
    eve::cap::revoke<eve::spawn::ISpawnProviderSource>(&source);
    CHECK(!eve::spawn::bindSpawnProviders().ok());
    CHECK(bound->factory == fixture.factory);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn operation paths never reuse subjects over repeated population cycles") {
    RuntimeFixture fixture;
    const auto     handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    std::set<eve::PersistentId> seen;
    std::uint64_t               tick = 1, sequence = 0;
    for (int cycle = 0; cycle < 16; ++cycle) {
        const auto state = fixture.world.inspect(handle).expect("cycle population");
        REQUIRE(state.alive == 4);
        std::vector<eve::spawn::LifecycleInput> deaths;
        for (const auto& subject : state.subjects) {
            REQUIRE(seen.insert(subject.persistentId()).second);
            deaths.push_back({subject, ++sequence, eve::spawn::RemovalReason::Death, handle});
        }
        fixture.step(++tick, 0, deaths);
        REQUIRE(fixture.step(++tick, 35).units == 1);
        for (int i = 0; i < 3; ++i) REQUIRE(fixture.step(++tick, 0).units == 1);
    }
    CHECK_EQ(seen.size(), std::size_t(64));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn initial population warms up to the maintain target with injected cooldown") {
    Templates templates;
    templates.content.find("schedule")->set("initial_population", eve::Value(2));
    RuntimeFixture fixture;
    const auto     handle = fixture.attach(templates);
    REQUIRE(fixture.step(1, 0).units == 2);
    CHECK_EQ(fixture.step(2, 19).units, std::uint32_t(0));
    REQUIRE(fixture.step(3, 16).units == 1);
    REQUIRE(fixture.step(4, 0).units == 1);
    CHECK_EQ(fixture.world.inspect(handle).expect("target population").alive, std::uint32_t(4));
}
TEST_CASE("Spawn zero initial population still reaches its configured maintain target") {
    Templates templates;
    templates.content.find("schedule")->set("initial_population", eve::Value(0));
    RuntimeFixture fixture;
    const auto     handle = fixture.attach(templates);
    REQUIRE(fixture.step(1, 0).units == 0);
    REQUIRE(fixture.step(2, 35).units == 1);
    for (std::uint64_t tick = 3; tick <= 5; ++tick) REQUIRE(fixture.step(tick, 0).units == 1);
    CHECK_EQ(fixture.world.inspect(handle).expect("warmed population").alive, std::uint32_t(4));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn bounded controls preserve equal setters and reject overflow before mutation") {
    eve::spawn::WorkLimits limits;
    limits.queuedCommands = 2;
    RuntimeFixture fixture(limits);
    const auto     handle = fixture.attach();
    REQUIRE(fixture.world.setActive(handle, false).ok());
    REQUIRE(fixture.world.setActive(handle, true).ok());
    REQUIRE(fixture.world.setActive(handle, true).ok());
    CHECK(!fixture.world.setActive(handle, false).ok());
    REQUIRE(fixture.world.enqueue({handle, eve::spawn::ControlKind::Pause}).ok());
    REQUIRE(fixture.world.enqueue({handle, eve::spawn::ControlKind::Activate}).ok());
    CHECK(!fixture.world.enqueue({handle, eve::spawn::ControlKind::Pause}).ok());
    auto report = fixture.step(1, 0);
    CHECK_EQ(report.units, std::uint32_t(4));
    CHECK(fixture.world.inspect(handle).expect("bounded controls state").phase == eve::spawn::Phase::Running);
}
TEST_CASE("Spawn missing providers fail attachment without retaining partial state") {
    RuntimeFixture fixture;
    auto           map      = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("map admission");
    auto           rejected = fixture.world.attach(map, {});
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().primaryDiagnostic()->code() == eve::DiagnosticCode::NotFound);
    const auto handle = fixture.attach();
    REQUIRE(fixture.step(1, 0).units == 4);
    CHECK_EQ(fixture.world.inspect(handle).expect("retry attachment").alive, std::uint32_t(4));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn disk sampling is area uniform and distance includes the interior") {
    auto map = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("compiled disk");
    REQUIRE(eve::spawn::distanceToRegion(map, 0, {-120, 80}).expect("center") == 0);
    CHECK_EQ(eve::spawn::distanceToRegion(map, 0, {-100, 80}).expect("outside"), 2.0);
    double squared = 0;
    for (int i = 0; i < 100; ++i) {
        auto p = eve::spawn::sampleRegion(map, 0, (i + 0.5) / 100, (i * 37 % 100 + 0.5) / 100).expect("disk candidate");
        REQUIRE(p.has_value());
        squared += (p->x + 120) * (p->x + 120) + (p->y - 80) * (p->y - 80);
        REQUIRE(eve::spawn::distanceToRegion(map, 0, *p).expect("inside disk") < 1e-10);
    }
    CHECK(std::abs(squared / 100 - 162) < 1e-10);
}
TEST_CASE("Spawn concave region rejects bounding box candidates and measures edges") {
    auto source  = mapSource();
    auto polygon = eve::Value::fromJson(R"({"kind":"polygon","space":"world2d",
        "vertices":[[0,0],[4,0],[4,1],[1,1],[1,4],[0,4]]})")
                       .expect("concave region");
    source.find("instances")->at(0).set("region", polygon);
    auto map = eve::spawn::compileMap(source, Templates{}, Units{}).expect("compiled concave region");
    REQUIRE(!eve::spawn::sampleRegion(map, 0, .75, .75).expect("normal rejection").has_value());
    REQUIRE(eve::spawn::sampleRegion(map, 0, .125, .75).expect("accepted arm").has_value());
    CHECK_EQ(eve::spawn::distanceToRegion(map, 0, {3, 3}).expect("notch"), 2.0);
    CHECK_EQ(eve::spawn::distanceToRegion(map, 0, {1, 3}).expect("boundary"), 0.0);
    CHECK_EQ(eve::spawn::distanceToRegion(map, 0, {.5, 3}).expect("interior"), 0.0);
}
TEST_CASE("Spawn point regions sample discrete candidates and measure nearest point") {
    auto source = mapSource();
    source.find("instances")
        ->at(0)
        .set("region",
             eve::Value::fromJson(R"({"kind":"points","space":"world2d","points":[[0,0],[4,0]]})").expect("points"));
    auto map = eve::spawn::compileMap(source, Templates{}, Units{}).expect("compiled points");
    REQUIRE(eve::spawn::sampleRegion(map, 0, 0, 0).expect("first")->x == 0);
    CHECK_EQ(eve::spawn::sampleRegion(map, 0, .999, 0).expect("last")->x, 4.0);
    CHECK_EQ(eve::spawn::distanceToRegion(map, 0, {2, 0}).expect("nearest"), 2.0);
}
TEST_CASE("Spawn region queries reject invalid inputs without mutating compiled content") {
    auto map = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("compiled region");
    REQUIRE(!eve::spawn::sampleRegion(map, 1, .5, .5).ok());
    CHECK(!eve::spawn::sampleRegion(map, 0, 1, .5).ok());
    CHECK(!eve::spawn::sampleRegion(map, 0, -.1, .5).ok());
    CHECK(!eve::spawn::sampleRegion(map, 0, .5, std::numeric_limits<double>::quiet_NaN()).ok());
    CHECK(!eve::spawn::distanceToRegion(map, 0, {std::numeric_limits<double>::infinity(), 0}).ok());
    CHECK_EQ(map.id(), "forest");
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn player activation uses region distance and multiplayer hysteresis") {
    RuntimeFixture fixture;
    auto           map    = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("proximity map");
    auto           source = fixture.world.attach(map, fixture.providers).expect("attached proximity")[0];
    std::vector<eve::spawn::Point> players{{1000, 1000}, {-50, 80}};
    auto                           advance = [&](std::uint64_t tick) {
        return fixture.world
            .advance({eve::SimulationTick(tick), eve::Duration(0)}, {},
                                               {std::span<const eve::spawn::Point>(players), {}})
            .expect("activation step");
    };
    REQUIRE(advance(1).units == 4);
    players = {{-30, 80}};
    advance(2);
    CHECK(fixture.world.inspect(source).expect("inside exit threshold").phase == eve::spawn::Phase::Running);
    players = {{-10, 80}};
    advance(3);
    CHECK(fixture.world.inspect(source).expect("outside exit threshold").phase == eve::spawn::Phase::Dormant);
    players = {{-30, 80}};
    advance(4);
    CHECK(fixture.world.inspect(source).expect("outside enter threshold").phase == eve::spawn::Phase::Dormant);
    players = {{-120, 80}};
    advance(5);
    CHECK(fixture.world.inspect(source).expect("inside region").phase == eve::spawn::Phase::Running);
    players.clear();
    advance(6);
    CHECK(fixture.world.inspect(source).expect("no players").phase == eve::spawn::Phase::Dormant);
}
TEST_CASE("Spawn invalid activation facts reject the whole tick before mutation") {
    RuntimeFixture fixture;
    auto           map    = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("proximity map");
    auto           source = fixture.world.attach(map, fixture.providers).expect("attached proximity")[0];
    std::vector<eve::spawn::Point> players{{-120, 80}, {std::numeric_limits<double>::quiet_NaN(), 0}};
    REQUIRE(
        !fixture.world
             .advance({eve::SimulationTick(1), eve::Duration(0)}, {}, {std::span<const eve::spawn::Point>(players), {}})
             .ok());
    CHECK(fixture.world.inspect(source).expect("unchanged activation").phase == eve::spawn::Phase::Dormant);
    players.pop_back();
    auto valid = fixture.world.advance({eve::SimulationTick(1), eve::Duration(0)}, {},
                                       {std::span<const eve::spawn::Point>(players), {}});
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value().units, std::uint32_t(4));
}
TEST_CASE("Spawn resolved event activation latches and rejects duplicate or stale inputs") {
    RuntimeFixture fixture;
    auto           value = mapSource();
    value.find("instances")->at(0).set("activation", eve::Value::object({{"kind", "event"}, {"event", "gate.open"}}));
    auto map    = eve::spawn::compileMap(value, Templates{}, Units{}).expect("event map");
    auto source = fixture.world.attach(map, fixture.providers).expect("event source")[0];
    std::vector<eve::spawn::SpawnerHandle> events{source, source};
    REQUIRE(!fixture.world.advance({eve::SimulationTick(1), eve::Duration(0)}, {}, {{}, events}).ok());
    events.pop_back();
    auto activated = fixture.world.advance({eve::SimulationTick(1), eve::Duration(0)}, {}, {{}, events});
    REQUIRE(activated.ok());
    CHECK_EQ(activated.value().units, std::uint32_t(4));
    fixture.step(2, 0);
    CHECK(fixture.world.inspect(source).expect("latched event").phase == eve::spawn::Phase::Running);
    fixture.world.detach(source, eve::spawn::DetachPolicy::KeepUnits).expect("detach source");
    CHECK(!fixture.world.advance({eve::SimulationTick(3), eve::Duration(0)}, {}, {{}, events}).ok());
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn queued pause precedes proximity hysteresis in the same activation frame") {
    RuntimeFixture fixture;
    auto           map    = eve::spawn::compileMap(mapSource(), Templates{}, Units{}).expect("proximity map");
    auto           source = fixture.world.attach(map, fixture.providers).expect("source")[0];
    fixture.world.setActive(source, true).expect("manual activation");
    fixture.world.enqueue({source, eve::spawn::ControlKind::Pause}).expect("pause at boundary");
    std::vector<eve::spawn::Point> players{{-30, 80}};
    auto                           report = fixture.world.advance({eve::SimulationTick(1), eve::Duration(0)}, {},
                                                                  {std::span<const eve::spawn::Point>(players), {}});
    REQUIRE(report.ok());
    CHECK_EQ(report.value().units, std::uint32_t(0));
    CHECK(fixture.world.inspect(source).expect("paused outside enter threshold").phase == eve::spawn::Phase::Dormant);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn late spatial blocking retries without fault or budget consumption") {
    RuntimeFixture f;
    auto           source    = f.attach();
    f.factory->blockedCommit = true;
    REQUIRE(f.step(1, 0).units == 0);
    auto blocked = f.world.inspect(source).expect("blocked ledger");
    CHECK(blocked.phase == eve::spawn::Phase::Running);
    CHECK(blocked.blocked == eve::spawn::BlockReason::PlacementBlocked);
    CHECK_EQ(blocked.alive, std::uint32_t(0));
    CHECK_EQ(blocked.total, std::uint64_t(0));
    f.factory->blockedCommit = false;
    REQUIRE(f.step(2, 2).units == 4);
    CHECK_EQ(f.world.inspect(source).expect("retried ledger").total, std::uint64_t(4));
}
}  // namespace test_spawn

namespace test_spawn {
namespace {
eve::Result<eve::SnapshotEnvelope> checkpoint(eve::spawn::SpawnWorld& world, eve::PersistentId epoch,
                                              eve::Revision revision, const eve::SnapshotHashProvider& hash) {
    auto captured = world.capture(epoch, revision);
    if (!captured) return eve::Result<eve::SnapshotEnvelope>::failure(captured.status());
    return captured.value().seal(hash);
}
eve::Result<eve::ContentId> checkpointTestHash(std::string_view text) {
    eve::ContentId::Bytes bytes{};
    std::uint64_t         state = 14695981039346656037ULL;
    for (unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        state    = eve::spawn::nextRandomWord(state);
        bytes[i] = static_cast<std::uint8_t>(state >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
}  // namespace
TEST_CASE("Spawn checkpoint seals complete blocked intent and deferred work without advancing simulation") {
    RuntimeFixture fixture;
    auto           handle      = fixture.attach();
    fixture.placement->blocked = true;
    fixture.step(1, 0);
    fixture.world.setActive(handle, false).expect("pause");
    fixture.world.enqueue({handle, eve::spawn::ControlKind::Activate}).expect("deferred activation");
    const auto epoch    = handle.owner.child("checkpoint");
    auto       snapshot = checkpoint(fixture.world, epoch, eve::Revision(7), checkpointTestHash).expect("capture");
    REQUIRE(snapshot.type == "eve.spawn-runtime");
    REQUIRE(snapshot.schema.format() == "spawn:runtime");
    REQUIRE_EQ(snapshot.schemaVersion.value(), 1u);
    REQUIRE_EQ(snapshot.tick.value(), 1u);
    REQUIRE(snapshot.instanceId == handle.owner);
    REQUIRE_EQ(snapshot.revision.value(), 7u);
    REQUIRE(snapshot.payload.find("save_epoch")->asString() == epoch.format());
    const auto& slot = snapshot.payload.find("slots")->at(0);
    REQUIRE(slot.find("phase")->asString() == "dormant");
    REQUIRE(slot.find("pending")->find("units")->getIf<eve::Value::Array>()->size() == 4);
    REQUIRE(slot.find("pending")->find("ordinal")->asString() == "1");
    REQUIRE(slot.find("pending")->keys().size() == 3);
    REQUIRE(slot.find("content_rng")->isString());
    REQUIRE(slot.find("placement_rng")->isString());
    REQUIRE(slot.find("cooldown_rng")->isString());
    REQUIRE(snapshot.payload.find("budgets")->at(0).find("alive")->asString() == "0");
    REQUIRE(snapshot.payload.find("commands")->at(0).find("kind")->asString() == "activate");
    REQUIRE(snapshot.payload.find("events")->at(0).find("kind")->asString() == "paused");
    REQUIRE(snapshot.payload.find("providers") == nullptr);
    REQUIRE(snapshot.payload.find("observers") == nullptr);
    auto repeated = checkpoint(fixture.world, epoch, eve::Revision(7), checkpointTestHash).expect("repeat capture");
    REQUIRE(snapshot.contentHash == repeated.contentHash);
    REQUIRE(snapshot.payload.toJson().expect("snapshot JSON") == repeated.payload.toJson().expect("repeated JSON"));
    const auto json    = eve::serializeSnapshotEnvelope(snapshot).expect("serialize");
    auto       decoded = eve::parseSnapshotEnvelope(json, checkpointTestHash).expect("envelope roundtrip");
    REQUIRE(decoded.payload.toJson().expect("decoded JSON") == snapshot.payload.toJson().expect("snapshot JSON"));
    auto tampered = snapshot;
    tampered.payload.set("seed", "999");
    REQUIRE(!eve::verifySnapshotEnvelope(tampered, checkpointTestHash).ok());
    REQUIRE(fixture.placement->attempts.size() == 4);
    REQUIRE(fixture.factory->live.empty());
    REQUIRE(fixture.world.sourceLease(handle).expect("unchanged lease").isCurrent());
}
TEST_CASE("Spawn checkpoint preserves authoritative population replacement clock and pinned content") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.step(1, 0);
    const auto epoch = handle.owner.child("checkpoint");
    auto first = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("initial checkpoint");
    const auto                       dead = fixture.factory->live[0];
    const eve::spawn::LifecycleInput event{dead, 1, eve::spawn::RemovalReason::Death, handle};
    fixture.step(2, 0, std::span(&event, 1));
    fixture.world.setActive(handle, false).expect("pause replacement clock");
    fixture.step(3, 50);
    auto later =
        checkpoint(fixture.world, epoch, eve::Revision(2), checkpointTestHash).expect("replacement checkpoint");
    const auto& slot = later.payload.find("slots")->at(0);
    REQUIRE(slot.find("alive")->asString() == "3");
    REQUIRE(slot.find("total")->asString() == "4");
    REQUIRE(slot.find("tracked")->getIf<eve::Value::Array>()->size() == 3);
    REQUIRE(slot.find("replacements")->getIf<eve::Value::Array>()->size() == 1);
    REQUIRE(slot.find("elapsed_ns")->asString() == "0");
    REQUIRE(later.payload.find("now_ns")->asString() == "50000000000");
    REQUIRE(later.payload.find("lifecycle_sequence")->asString() == "1");
    REQUIRE(later.payload.find("budgets")->at(0).find("alive")->asString() == "3");
    REQUIRE(first.payload.find("slots")->at(0).find("tracked")->getIf<eve::Value::Array>()->size() == 4);
    const auto& revision = later.payload.find("revisions")->at(0);
    REQUIRE(revision.find("source")->asString() == templateSource().toJson().expect("template JSON"));
    auto digest =
        checkpointTestHash("eve.spawn-template/v1\n" + revision.find("source")->asString()).expect("revision digest");
    REQUIRE(revision.find("hash")->asString() == digest.format());
}
TEST_CASE("Spawn checkpoint sealing is independent of live world mutation and rejects same token reentry") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    const auto     epoch  = handle.owner.child("checkpoint");
    REQUIRE(!fixture.world.capture({}, eve::Revision(1)).ok());
    REQUIRE(!fixture.world.capture(epoch, eve::Revision(0)).ok());
    auto frozen = fixture.world.capture(epoch, eve::Revision(1)).expect("freeze");
    REQUIRE(!frozen.seal({}).ok());
    bool                      callback = false;
    eve::SnapshotHashProvider guarded  = [&](std::string_view text) {
        REQUIRE(!frozen.seal(checkpointTestHash).ok());
        if (!callback) {
            callback = true;
            REQUIRE_EQ(fixture.step(1, 0).units, 4u);
        }
        return checkpointTestHash(text);
    };
    auto first = frozen.seal(guarded).expect("independent sealing");
    REQUIRE(callback);
    REQUIRE_EQ(first.tick.value(), 0u);
    REQUIRE(first.payload.find("slots")->at(0).find("alive")->asString() == "0");
    REQUIRE_EQ(fixture.world.inspect(handle).expect("live population").alive, 4u);
    eve::SnapshotHashProvider throwing = [](std::string_view) -> eve::Result<eve::ContentId> {
        throw std::runtime_error("digest fault");
    };
    REQUIRE(!frozen.seal(throwing).ok());
    auto retry = frozen.seal(checkpointTestHash).expect("digest retry");
    REQUIRE(retry.contentHash == first.contentHash);
    REQUIRE(fixture.world.setActive(handle, false).ok());
    fixture.world.setActive(handle, true).expect("resume");
    REQUIRE_EQ(fixture.step(2, 0).units, 0u);
}
TEST_CASE("Spawn checkpoint retains retired slot generations and releases no domain units") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.step(1, 0);
    fixture.world.detach(handle, eve::spawn::DetachPolicy::KeepUnits).expect("detach");
    auto snapshot = checkpoint(fixture.world, handle.owner.child("checkpoint"), eve::Revision(1), checkpointTestHash)
                        .expect("retired capture");
    const auto& slot = snapshot.payload.find("slots")->at(0);
    REQUIRE(!slot.find("occupied")->asBool());
    REQUIRE(slot.find("generation")->asString() == "2");
    REQUIRE(slot.find("tracked")->getIf<eve::Value::Array>()->empty());
    REQUIRE_EQ(fixture.factory->live.size(), 4u);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn checkpoint preserves every region and activation variant with shared revision deduplication") {
    RuntimeFixture fixture;
    auto           source   = mapSource();
    auto           original = source.find("instances")->at(0);
    auto           points   = original;
    points.set("id", "outpost");
    points.set("region",
               eve::Value::fromJson(R"({"kind":"points","space":"world2d","points":[[1,2],[4,5]]})").expect("points"));
    points.set("activation", eve::Value::object({{"kind", "event"}, {"event", "alarm"}}));
    auto polygon = original;
    polygon.set("id", "guard");
    polygon.set("region",
                eve::Value::fromJson(R"({"kind":"polygon","space":"world2d","vertices":[[-1,-1],[2,-1],[1,2]]})")
                    .expect("polygon"));
    polygon.set("activation", eve::Value::object({{"kind", "map_loaded"}}));
    auto* instances = source.find("instances")->getIf<eve::Value::Array>();
    instances->push_back(std::move(points));
    instances->push_back(std::move(polygon));
    auto        map     = eve::spawn::compileMap(source, Templates{}, Units{}).expect("variant map");
    auto        handles = fixture.world.attach(map, fixture.providers).expect("variants attached");
    std::size_t hashes  = 0;
    auto        snapshot =
        checkpoint(fixture.world, handles[0].owner.child("checkpoint"), eve::Revision(1), [&](std::string_view input) {
            ++hashes;
            return checkpointTestHash(input);
        }).expect("variant capture");
    REQUIRE_EQ(hashes, 2u);
    REQUIRE_EQ(snapshot.payload.find("revisions")->getIf<eve::Value::Array>()->size(), 1u);
    const auto& slots = *snapshot.payload.find("slots")->getIf<eve::Value::Array>();
    REQUIRE_EQ(slots.size(), 3u);
    for (const auto& slot : slots) {
        REQUIRE(slot.find("template")->asString() == "0");
        auto        id         = slot.find("id")->asString();
        const auto& region     = *slot.find("region");
        const auto& activation = *slot.find("activation");
        if (id == "north") {
            REQUIRE(region.find("kind")->asString() == "circle");
            REQUIRE(activation.find("kind")->asString() == "player_proximity");
        } else if (id == "outpost") {
            REQUIRE_EQ(region.find("points")->getIf<eve::Value::Array>()->size(), 2u);
            REQUIRE(activation.find("event")->asString() == "alarm");
        } else {
            REQUIRE(id == "guard");
            REQUIRE_EQ(region.find("vertices")->getIf<eve::Value::Array>()->size(), 3u);
            REQUIRE(activation.find("kind")->asString() == "map_loaded");
        }
    }
}
TEST_CASE("Spawn checkpoint rejects colliding pinned revision digests before publication and remains usable") {
    RuntimeFixture fixture;
    auto           source = mapSource();
    auto           second = source.find("instances")->at(0);
    second.set("id", "south");
    second.set("overrides",
               eve::Value::fromJson(R"({"population":{"target_alive":2,"initial_population":2,"alive_limit":2}})")
                   .expect("override"));
    source.find("instances")->getIf<eve::Value::Array>()->push_back(std::move(second));
    auto                      map       = eve::spawn::compileMap(source, Templates{}, Units{}).expect("revision map");
    auto                      handles   = fixture.world.attach(map, fixture.providers).expect("revision attach");
    eve::SnapshotHashProvider collision = [](std::string_view) {
        eve::ContentId::Bytes bytes{};
        bytes[0] = 1;
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    auto rejected = checkpoint(fixture.world, handles[0].owner.child("checkpoint"), eve::Revision(1), collision);
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
    auto valid = checkpoint(fixture.world, handles[0].owner.child("checkpoint"), eve::Revision(1), checkpointTestHash)
                     .expect("capture after digest rejection");
    REQUIRE_EQ(valid.payload.find("revisions")->getIf<eve::Value::Array>()->size(), 2u);
    REQUIRE(fixture.world.setActive(handles[0], true).ok());
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn frozen checkpoint outlives its source world without retaining providers") {
    std::optional<eve::spawn::SpawnCheckpoint> frozen;
    std::weak_ptr<eve::spawn::SpawnProviders>  providers;
    eve::spawn::SpawnerHandle                  original;
    {
        RuntimeFixture fixture;
        auto           handle = fixture.attach();
        fixture.step(1, 0);
        providers = fixture.providers;
        original  = handle;
        frozen.emplace(
            fixture.world.capture(handle.owner.child("checkpoint"), eve::Revision(1)).expect("frozen capture"));
    }
    REQUIRE(providers.expired());
    REQUIRE_EQ(frozen->membership().size(), std::size_t(4));
    for (const auto& binding : frozen->membership()) {
        CHECK(binding.subject.isValid());
        CHECK(binding.source == original);
    }

    auto snapshot = frozen->seal(checkpointTestHash).expect("seal without source world");
    REQUIRE(snapshot.payload.find("slots")->at(0).find("alive")->asString() == "4");
    REQUIRE_EQ(snapshot.payload.find("slots")->at(0).find("tracked")->getIf<eve::Value::Array>()->size(), 4u);
}
}  // namespace test_spawn

namespace test_spawn {
namespace {
eve::SnapshotEnvelope resealCheckpoint(eve::SnapshotEnvelope snapshot) {
    return eve::makeSnapshotEnvelope(snapshot.type, snapshot.schema, snapshot.schemaVersion, snapshot.instanceId,
                                     snapshot.revision, snapshot.tick, std::move(snapshot.payload), checkpointTestHash)
        .expect("reseal test checkpoint");
}
}  // namespace
TEST_CASE("Spawn checkpoint typed admission owns live blocked and retired scheduler facts without publication") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    const auto     epoch  = handle.owner.child("checkpoint");
    fixture.step(1, 0);
    auto snapshot = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("live capture");
    auto admitted =
        eve::spawn::SpawnRestoreState::decode(snapshot, epoch, checkpointTestHash, Units{}).expect("typed live data");
    REQUIRE(admitted.saveEpoch() == epoch);
    REQUIRE_EQ(admitted.slotCount(), 1u);
    REQUIRE_EQ(admitted.trackedCount(), 4u);
    snapshot.payload.set("seed", "999");
    REQUIRE_EQ(admitted.trackedCount(), 4u);
    fixture.world.detach(handle, eve::spawn::DetachPolicy::KeepUnits).expect("retire");
    auto retired = checkpoint(fixture.world, epoch, eve::Revision(2), checkpointTestHash).expect("retired capture");
    auto tombstone =
        eve::spawn::SpawnRestoreState::decode(retired, epoch, checkpointTestHash, Units{}).expect("typed tombstone");
    REQUIRE_EQ(tombstone.slotCount(), 1u);
    REQUIRE_EQ(tombstone.trackedCount(), 0u);
    REQUIRE_EQ(fixture.factory->live.size(), 4u);
    RuntimeFixture blocked;
    auto           blockedHandle = blocked.attach();
    blocked.placement->blocked   = true;
    blocked.step(1, 0);
    blocked.world.setActive(blockedHandle, false).expect("pause");
    blocked.world.enqueue({blockedHandle, eve::spawn::ControlKind::Activate}).expect("queued control");
    auto pending = checkpoint(blocked.world, epoch, eve::Revision(1), checkpointTestHash).expect("blocked capture");
    auto intent  = eve::spawn::SpawnRestoreState::decode(pending, epoch, checkpointTestHash, Units{})
                      .expect("typed pending intent");
    REQUIRE_EQ(intent.trackedCount(), 0u);
    REQUIRE(blocked.factory->live.empty());
}
TEST_CASE("Spawn checkpoint admission rejects sealed semantic corruption before touching live data") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.step(1, 0);
    const auto epoch  = handle.owner.child("checkpoint");
    auto       valid  = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("valid capture");
    auto       reject = [&](eve::SnapshotEnvelope changed) {
        auto sealed = resealCheckpoint(std::move(changed));
        auto result = eve::spawn::SpawnRestoreState::decode(sealed, epoch, checkpointTestHash, Units{});
        REQUIRE(!result.ok());
        REQUIRE_EQ(fixture.world.inspect(handle).expect("old world intact").alive, 4u);
    };
    auto bad = valid;
    bad.payload.set("unknown", true);
    reject(std::move(bad));
    bad = valid;
    bad.payload.set("seed", "042");
    reject(std::move(bad));
    bad = valid;
    bad.payload.set("random_algorithm", "future/v2");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).set("phase", "alien");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).set("alive", "3");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("budgets")->at(0).set("alive", "3");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).set("anchor_ns", "1");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).find("tracked")->at(1).set(
        "subject", bad.payload.find("slots")->at(0).find("tracked")->at(0).find("subject")->asString());
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("revisions")->at(0).set("source", "{}");
    reject(std::move(bad));
    bad               = valid;
    bad.schemaVersion = eve::SchemaVersion(2);
    reject(std::move(bad));
    REQUIRE(!eve::spawn::SpawnRestoreState::decode(valid, epoch.child("other"), checkpointTestHash, Units{}).ok());
}
TEST_CASE("Spawn checkpoint admission preserves pending canonical identity and enforces queued source references") {
    RuntimeFixture fixture;
    auto           handle      = fixture.attach();
    fixture.placement->blocked = true;
    fixture.step(1, 0);
    fixture.world.enqueue({handle, eve::spawn::ControlKind::Pause}).expect("queue");
    auto epoch  = handle.owner.child("checkpoint");
    auto valid  = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("pending capture");
    auto reject = [&](eve::SnapshotEnvelope value) {
        auto sealed = resealCheckpoint(std::move(value));
        REQUIRE(!eve::spawn::SpawnRestoreState::decode(sealed, epoch, checkpointTestHash, Units{}).ok());
    };
    auto bad = valid;
    bad.payload.find("slots")->at(0).find("pending")->set("ordinal", "2");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).find("pending")->find("units")->at(0).set("subject", handle.owner.format());
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("slots")->at(0).find("pending")->find("units")->at(0).set("definition", "rpg.unit:other");
    reject(std::move(bad));
    bad = valid;
    bad.payload.find("commands")->at(0).find("source")->set("generation", "2");
    reject(std::move(bad));
    REQUIRE_EQ(fixture.world.inspect(handle).expect("unchanged").alive, 0u);
}
TEST_CASE("Spawn checkpoint admission copies input before hash callbacks can alter the caller envelope") {
    RuntimeFixture fixture;
    auto           handle = fixture.attach();
    fixture.step(1, 0);
    auto epoch    = handle.owner.child("checkpoint");
    auto snapshot = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("capture");
    eve::SnapshotHashProvider hash = [&](std::string_view bytes) {
        snapshot.payload.find("slots")->at(0).set("alive", "999");
        return checkpointTestHash(bytes);
    };
    auto data = eve::spawn::SpawnRestoreState::decode(snapshot, epoch, hash, Units{}).expect("stable admission copy");
    REQUIRE_EQ(data.trackedCount(), 4u);
    REQUIRE_EQ(fixture.world.inspect(handle).expect("world unchanged").alive, 4u);
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn checkpoint admission supports every schedule at blocked live and completed boundaries") {
    for (int kind = 0; kind < 4; ++kind) {
        Templates templates;
        if (kind == 0) {
            templates.content.set("schedule", eve::Value::object({{"kind", "once"}}));
            templates.content.set(
                "content",
                eve::Value::fromJson(
                    R"({"kind":"fixed","members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":2}]})")
                    .expect("fixed content"));
        } else if (kind == 1) {
            templates.content.getIf<eve::Value::Object>()->erase("failure");
            templates.content.set(
                "schedule",
                eve::Value::fromJson(
                    R"({"kind":"interval","period_seconds":10,"initial_delay_seconds":0,"count_per_batch":2,"on_missed":"skip"})")
                    .expect("interval"));
        } else if (kind == 2) {
            templates.content.getIf<eve::Value::Object>()->erase("content");
            templates.content.set(
                "schedule",
                eve::Value::fromJson(
                    R"({"kind":"waves","waves":[{"id":"guard","delay_seconds":0,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"}]})")
                    .expect("wave"));
        }
        RuntimeFixture fixture;
        auto           handle = fixture.attach(templates);
        auto           epoch  = handle.owner.child("checkpoint");
        auto           admit  = [&] {
            auto data =
                checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("schedule capture");
            auto candidate = eve::spawn::SpawnRestoreState::decode(data, epoch, checkpointTestHash, Units{})
                                 .expect("schedule admission");
            REQUIRE_EQ(candidate.trackedCount(), fixture.world.inspect(handle).expect("current state").subjects.size());
        };
        fixture.factory->failCommit = true;
        fixture.step(1, 0);
        admit();
        fixture.factory->failCommit = false;
        fixture.world.enqueue({handle, eve::spawn::ControlKind::RetryFault}).expect("retry fault");
        fixture.step(2, 0);
        admit();
        if (kind == 2) {
            const auto subjects = fixture.world.inspect(handle).expect("wave actors").subjects;
            REQUIRE_EQ(subjects.size(), 1u);
            const eve::spawn::LifecycleInput death{subjects[0], 1, eve::spawn::RemovalReason::Death, handle};
            fixture.step(3, 0, std::span(&death, 1));
            admit();
        }
    }
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn runtime incarnation rejects old handles while preserving durable unit identities") {
    eve::spawn::SpawnerHandle old;
    eve::spawn::SourceLease   oldLease;
    eve::PersistentId         operation;
    eve::SubjectRef           subject;
    eve::SnapshotEnvelope     saved;
    {
        RuntimeFixture first;
        old       = first.attach();
        oldLease  = first.world.sourceLease(old).expect("old lease");
        operation = oldLease.operationIdentity(eve::spawn::OperationOrdinal(1)).expect("old operation");
        subject   = oldLease.unitIdentity(eve::spawn::OperationOrdinal(1), 0).expect("old member");
        first.world.setActive(old, false).expect("pause first");
        first.world.enqueue({old, eve::spawn::ControlKind::Activate}).expect("queue first activation");
        saved = checkpoint(first.world, old.owner.child("save"), eve::Revision(1), checkpointTestHash)
                    .expect("first capture");
    }
    REQUIRE(!oldLease.isCurrent());
    RuntimeFixture replacement;
    auto           fresh = replacement.attach();
    REQUIRE(fresh.owner == old.owner);
    REQUIRE_EQ(fresh.epoch, old.epoch);
    REQUIRE(fresh.slot == old.slot);
    REQUIRE(fresh.incarnation != old.incarnation);
    REQUIRE(!replacement.world.inspect(old).ok());
    REQUIRE(!replacement.world.setActive(old, false).ok());
    REQUIRE(!replacement.world.enqueue({old, eve::spawn::ControlKind::Pause}).ok());
    REQUIRE(!replacement.world.sourceLease(old).ok());
    REQUIRE(!replacement.world.detach(old, eve::spawn::DetachPolicy::KeepUnits).ok());
    const eve::spawn::LifecycleInput staleDeath{subject, 1, eve::spawn::RemovalReason::Death, old};
    REQUIRE(!replacement.world
                 .advance({eve::SimulationTick(1), eve::Duration(0)},
                          std::span<const eve::spawn::LifecycleInput>(&staleDeath, 1))
                 .ok());
    auto lease = replacement.world.sourceLease(fresh).expect("fresh lease");
    REQUIRE(lease.operationIdentity(eve::spawn::OperationOrdinal(1)).expect("same operation") == operation);
    REQUIRE(lease.unitIdentity(eve::spawn::OperationOrdinal(1), 0).expect("same member") == subject);
    replacement.world.setActive(fresh, false).expect("pause replacement");
    replacement.world.enqueue({fresh, eve::spawn::ControlKind::Activate}).expect("queue replacement activation");
    auto again = checkpoint(replacement.world, fresh.owner.child("save"), eve::Revision(1), checkpointTestHash)
                     .expect("replacement capture");
    REQUIRE(again.payload.toJson().expect("replacement JSON") == saved.payload.toJson().expect("saved JSON"));
}
TEST_CASE("Spawn world move retains its incarnation and rejects the displaced world handles") {
    RuntimeFixture first;
    RuntimeFixture second;
    auto           firstHandle  = first.attach();
    auto           secondHandle = second.attach();
    auto           firstLease   = first.world.sourceLease(firstHandle).expect("first lease");
    auto           secondLease  = second.world.sourceLease(secondHandle).expect("second lease");
    REQUIRE(!second.world.inspect(firstHandle).ok());
    second.world = std::move(first.world);
    REQUIRE(second.world.inspect(firstHandle).ok());
    REQUIRE(firstLease.isCurrent());
    REQUIRE(!secondLease.isCurrent());
    REQUIRE(!second.world.inspect(secondHandle).ok());
    auto moved = std::move(second.world);
    REQUIRE(moved.inspect(firstHandle).ok());
    REQUIRE(firstLease.isCurrent());
}
TEST_CASE("Spawn checkpoint lists unique future references from every wave before publication and during retry") {
    Templates templates;
    auto      fields = std::get<eve::Value::Object>(templates.content.storage());
    fields.erase("content");
    templates.content = eve::Value(std::move(fields));
    templates.content.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
      {"id":"first","delay_seconds":0,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1},{"id":"elite","unit":{"type":"rpg.unit","id":"alpha"},"count":1}],"complete_when":"all_members_dead"},
      {"id":"second","delay_seconds":5,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1},{"id":"boss","unit":{"type":"rpg.unit","id":"omega"},"count":1}],"complete_when":"all_members_dead"}]})")
                                          .expect("all waves"));
    RuntimeFixture fixture;
    auto           source  = fixture.attach(templates);
    auto           epoch   = source.owner.child("future-ref-save");
    auto           initial = fixture.world.capture(epoch, eve::Revision(1)).expect("before first wave");
    REQUIRE_EQ(initial.unitReferences().size(), std::size_t(3));
    REQUIRE_EQ(initial.unitReferences()[0].format(), "rpg.unit:alpha");
    REQUIRE_EQ(initial.unitReferences()[1].format(), "rpg.unit:omega");
    REQUIRE_EQ(initial.unitReferences()[2].format(), "rpg.unit:wolf");
    fixture.placement->blocked = true;
    REQUIRE_EQ(fixture.step(1, 0).units, std::uint32_t(0));
    auto pending = fixture.world.capture(epoch, eve::Revision(2)).expect("saved retry intent");
    REQUIRE_EQ(pending.unitReferences().size(), std::size_t(3));
    auto moved = std::move(pending);
    REQUIRE(pending.unitReferences().empty());
    REQUIRE_EQ(moved.unitReferences()[1].format(), "rpg.unit:omega");
    auto envelope = moved.seal(checkpointTestHash).expect("all future waves in file");
    auto parsed   = eve::spawn::SpawnCheckpoint::parse(envelope, epoch, checkpointTestHash, Units{})
                      .expect("file future references");
    REQUIRE_EQ(parsed.unitReferences().size(), std::size_t(3));
    REQUIRE_EQ(parsed.unitReferences()[1].format(), "rpg.unit:omega");
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Spawn restored core rebuilds leases and pending handles while preserving exact scheduler bytes") {
    RuntimeFixture fixture;
    auto           oldSource   = fixture.attach();
    auto           oldLease    = fixture.world.sourceLease(oldSource).expect("old lease");
    fixture.placement->blocked = true;
    fixture.step(1, 0);
    fixture.world.setActive(oldSource, false).expect("queued pause event");
    fixture.world.setActive(oldSource, true).expect("queued resume event");
    fixture.world.enqueue({oldSource, eve::spawn::ControlKind::Activate}).expect("saved command");
    auto epoch    = oldSource.owner.child("restore-core");
    auto envelope = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("saved core");
    auto state =
        eve::spawn::SpawnRestoreState::decode(envelope, epoch, checkpointTestHash, Units{}).expect("admitted core");
    REQUIRE_EQ(state.sources().size(), std::size_t(1));
    auto       retained  = state.clone().expect("retain CPU facts for commit retries");
    auto       alternate = retained.clone().expect("independent attempt");
    const auto saved     = state.sources()[0];
    auto       placement = std::make_shared<Placement>();
    auto       factory   = std::make_shared<Factory>();
    auto       providers = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{placement, factory});
    auto       restored  = eve::spawn::SpawnWorld::restore(state, providers).expect("bind actual runtime providers");
    REQUIRE(state.saveEpoch().isNil());
    REQUIRE(state.sources().empty());
    REQUIRE(!state.clone().ok());
    REQUIRE(!eve::spawn::SpawnWorld::restore(state, providers).ok());
    auto       lease  = restored.resolveSavedSource(saved).expect("new process source authority");
    const auto source = lease.source();
    REQUIRE(lease.isCurrent());
    REQUIRE(oldLease.isCurrent());
    REQUIRE(source.incarnation != oldSource.incarnation);
    bool requested     = false;
    factory->onRequest = [&](const eve::spawn::BatchRequest& request) {
        requested = true;
        REQUIRE(request.source == source);
        REQUIRE(request.lifetime.isCurrent());
        REQUIRE(request.lifetime.source() == source);
        REQUIRE(request.lifetime.operationIdentity(request.ordinal).expect("restored deterministic operation") ==
                request.operation);
        for (std::size_t i = 0; i < request.units.size(); ++i)
            REQUIRE(request.lifetime.unitIdentity(request.ordinal, static_cast<std::uint32_t>(i))
                        .expect("restored member identity") == request.units[i].subject);
    };

    REQUIRE(eve::spawn::savedSpawnerKey(source) == eve::spawn::savedSpawnerKey(oldSource));
    REQUIRE(!restored.inspect(oldSource).ok());
    REQUIRE(!fixture.world.inspect(source).ok());
    REQUIRE_EQ(restored.inspect(source).expect("restored phase").alive, std::uint32_t(0));
    auto roundtrip = checkpoint(restored, epoch, eve::Revision(1), checkpointTestHash).expect("rebound checkpoint");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(roundtrip).expect("restored bytes"),
               eve::serializeSnapshotEnvelope(envelope).expect("original bytes"));
    auto otherPlacement = std::make_shared<Placement>();
    auto otherFactory   = std::make_shared<Factory>();
    auto otherProviders =
        std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{otherPlacement, otherFactory});
    auto other      = eve::spawn::SpawnWorld::restore(alternate, otherProviders).expect("bind independent attempt");
    auto otherLease = other.resolveSavedSource(saved).expect("independent authority");
    REQUIRE(otherLease.source().incarnation != source.incarnation);
    REQUIRE(retained.saveEpoch() == epoch);
    REQUIRE_EQ(retained.placementBindings().size(), std::size_t(1));
    auto otherEnvelope =
        checkpoint(other, epoch, eve::Revision(1), checkpointTestHash).expect("copy preserves every scheduler fact");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(otherEnvelope).expect("cloned bytes"),
               eve::serializeSnapshotEnvelope(envelope).expect("original bytes"));
    other.retireSources().expect("cancel failed joint attempt");
    REQUIRE(!otherLease.isCurrent());
    REQUIRE(lease.isCurrent());
    std::vector<eve::spawn::SpawnEvent> seen;
    auto subscription = restored.subscribe([&](const auto& event) { seen.push_back(event); }).expect("new observer");
    auto report       = restored.advance({eve::SimulationTick(2), eve::Duration(2000000000)}).expect("pending retry");
    REQUIRE_EQ(report.units, std::uint32_t(4));
    REQUIRE(requested);
    REQUIRE_EQ(factory->live, restored.inspect(source).expect("actual membership").subjects);
    REQUIRE(!seen.empty());
    for (const auto& event : seen) REQUIRE(event.source == source);
    auto invalid = saved;
    ++invalid.generation;
    REQUIRE(!restored.resolveSavedSource(invalid).ok());
    restored.detach(source, eve::spawn::DetachPolicy::KeepUnits).expect("revoke new source");
    REQUIRE(!lease.isCurrent());
    REQUIRE(oldLease.isCurrent());
}
TEST_CASE("Spawn core restore preserves admitted facts on absent throwing reentrant or replaced providers") {
    RuntimeFixture fixture;
    auto           source   = fixture.attach();
    auto           epoch    = source.owner.child("failed-restore");
    auto           envelope = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("core");
    auto state = eve::spawn::SpawnRestoreState::decode(envelope, epoch, checkpointTestHash, Units{}).expect("admit");
    struct RestorePlacement final : eve::spawn::ISpawnPlacement {
        std::function<eve::Result<void>()> callback;
        eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
            return callback();
        }
        eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest&,
                                                       const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                       std::uint64_t&) override {
            return eve::Result<eve::spawn::PlacementOutcome>::success(eve::spawn::PlacementBlocked{});
        }
    };
    auto placement = std::make_shared<RestorePlacement>();
    auto factory   = std::make_shared<Factory>();
    auto providers = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{placement, factory});
    REQUIRE(!eve::spawn::SpawnWorld::restore(state, {}).ok());
    placement->callback = []() -> eve::Result<void> { throw std::runtime_error("injected placement admission"); };
    REQUIRE(!eve::spawn::SpawnWorld::restore(state, providers).ok());
    bool reentryRejected = false;
    placement->callback  = [&]() {
        reentryRejected = !eve::spawn::SpawnWorld::restore(state, providers).ok();
        REQUIRE(!state.clone().ok());
        providers->factory = std::make_shared<Factory>();
        return eve::Result<void>::success();
    };
    REQUIRE(!eve::spawn::SpawnWorld::restore(state, providers).ok());
    REQUIRE(reentryRejected);
    REQUIRE(state.saveEpoch() == epoch);
    REQUIRE_EQ(state.sources().size(), std::size_t(1));
    placement->callback = [&] {
        providers.reset();
        return eve::Result<void>::success();
    };
    REQUIRE(!eve::spawn::SpawnWorld::restore(state, providers).ok());
    REQUIRE(state.saveEpoch() == epoch);
    providers           = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{placement, factory});
    placement->callback = [] { return eve::Result<void>::success(); };
    auto restored =
        eve::spawn::SpawnWorld::restore(state, providers).expect("same candidate retries after provider repair");
    auto lease = restored.resolveSavedSource(eve::spawn::savedSpawnerKey(source)).expect("source");
    REQUIRE_EQ(restored.inspect(lease.source()).expect("state preserved").alive, std::uint32_t(0));
    providers.reset();
    REQUIRE(restored.advance({eve::SimulationTick(1), eve::Duration(0)}).ok());
}
TEST_CASE("Spawn diagnostic builders expose actual budgets without mutating checkpoints or publication") {
    RuntimeFixture fixture;
    REQUIRE(!fixture.world.captureDiagnostics().ok());
    const auto source  = fixture.attach();
    auto       initial = fixture.world.captureDiagnostics().expect("initial facts");
    REQUIRE_EQ(initial.budgets[0].alive, std::uint32_t(0));
    bool rejected = false, observed = false;
    fixture.factory->onPrepare = [&] { rejected = !fixture.world.captureDiagnostics().ok(); };
    auto subscription          = fixture.world
                            .subscribe([&](const auto&) {
                                auto facts = fixture.world.captureDiagnostics().expect("published observer boundary");
                                REQUIRE_EQ(facts.budgets[0].alive, std::uint32_t(4));
                                observed = true;
                            })
                            .expect("subscribe");
    fixture.step(1, 0);
    REQUIRE(rejected);
    REQUIRE(observed);
    const auto epoch    = source.owner.child("diagnostic-proof");
    const auto before   = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("before");
    auto       snapshot = fixture.world.captureDiagnostics().expect("actual ledger");
    REQUIRE_EQ(snapshot.sources[0].state.alive, std::uint32_t(4));
    REQUIRE_EQ(snapshot.sources[0].aliveLimit, std::uint32_t(4));
    REQUIRE_EQ(snapshot.budgets[0].alive, std::uint32_t(4));
    REQUIRE_EQ(snapshot.budgets[0].limit, std::uint32_t(10));
    REQUIRE(snapshot.sources[0].source == source);
    REQUIRE(snapshot.sources[0].templateRevision != nullptr);
    snapshot.budgets[0].alive = 999;
    snapshot.sources.clear();
    const auto after = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("after");
    REQUIRE_EQ(before.payload.toJson().expect("before bytes"), after.payload.toJson().expect("after bytes"));
    REQUIRE_EQ(fixture.world.captureDiagnostics().expect("unchanged ledger").budgets[0].alive, std::uint32_t(4));
}
TEST_CASE("Spawn empty map instances compile advance and restore without placeholder sources") {
    auto source = mapSource();
    source.set("instances", eve::Value::Array{});
    auto map = eve::spawn::compileMap(source, Templates{}, Units{}).expect("empty map is a real compiled map");
    RuntimeFixture fixture;
    REQUIRE(fixture.world.attach(map, fixture.providers).expect("empty attach").empty());
    REQUIRE_EQ(fixture.step(1, 1).units, std::uint32_t(0));
    auto captured = fixture.world.captureDiagnostics().expect("empty published facts");
    REQUIRE(captured.sources.empty());
    REQUIRE_EQ(captured.budgets[0].alive, std::uint32_t(0));
    const auto epoch = captured.owner.child("empty-map-save");
    auto       saved = checkpoint(fixture.world, epoch, eve::Revision(1), checkpointTestHash).expect("empty save");
    auto       state =
        eve::spawn::SpawnRestoreState::decode(saved, epoch, checkpointTestHash, Units{}).expect("empty admission");
    auto reject = [&](eve::SnapshotEnvelope candidate) {
        auto sealed = resealCheckpoint(std::move(candidate));
        REQUIRE(!eve::spawn::SpawnRestoreState::decode(sealed, epoch, checkpointTestHash, Units{}).ok());
    };
    auto badCursor = saved;
    badCursor.payload.set("cursor", "1");
    reject(std::move(badCursor));
    RuntimeFixture populated;
    auto           handle = populated.attach();
    populated.world.enqueue({handle, eve::spawn::ControlKind::Activate}).expect("real queued reference");
    auto nonempty = checkpoint(populated.world, epoch, eve::Revision(1), checkpointTestHash).expect("reference source");
    auto badCommands = saved;
    badCommands.payload.set("commands", *nonempty.payload.find("commands"));
    reject(std::move(badCommands));
    auto orphanRevision = saved;
    orphanRevision.payload.set("revisions", *nonempty.payload.find("revisions"));
    reject(std::move(orphanRevision));
    REQUIRE_EQ(state.slotCount(), std::size_t(0));
    REQUIRE(!state.owner().isNil());
    auto restored  = eve::spawn::SpawnWorld::restore(state, fixture.providers).expect("empty restore");
    auto roundtrip = checkpoint(restored, epoch, eve::Revision(1), checkpointTestHash).expect("empty reseal");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(saved).expect("before"),
               eve::serializeSnapshotEnvelope(roundtrip).expect("after"));
    REQUIRE_EQ(restored.advance({eve::SimulationTick(2), eve::Duration(0)}).expect("empty restored advance").units,
               std::uint32_t(0));
}
}  // namespace test_spawn

namespace test_spawn {
TEST_CASE("Player activation frames share canonical World2D decoding and bound owning input") {
    using eve::spawn::decodePlayerFrame;
    auto source  = eve::Value::fromJson("[[-5.5,2],[-5.5,2]]").expect("player positions");
    auto decoded = decodePlayerFrame(source, 2).expect("duplicates and negative coordinates are valid");
    REQUIRE_EQ(decoded.size(), std::size_t(2));
    REQUIRE_EQ(decoded[0].x, -5.5);
    source.at(0).at(0) = eve::Value(99);
    REQUIRE_EQ(decoded[0].x, -5.5);
    REQUIRE(decodePlayerFrame(eve::Value::array({}), 0).expect("known empty frame").empty());
    REQUIRE(!decodePlayerFrame(source, 1).ok());
    for (const auto* json : {"null", "{}", "[[]]", "[[1,2,3]]", "[[true,2]]", "[[1,\"bad\"]]"}) {
        auto value  = eve::Value::fromJson(json).expect("syntactically valid input");
        auto result = decodePlayerFrame(value, 1024);
        REQUIRE(!result.ok());
        REQUIRE_EQ(result.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);
    }
    auto invalid = eve::Value::array({eve::Value::array({std::numeric_limits<double>::infinity(), 0})});
    REQUIRE(!decodePlayerFrame(invalid, 1024).ok());
    invalid.at(0).at(0) = eve::Value(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!decodePlayerFrame(invalid, 1024).ok());
}
}  // namespace test_spawn

namespace test_spawn_saved_lifecycle {
using namespace eve;
TEST_CASE("spawn.savedLifecycle.canonicalRowsPreserveOrderDuplicatesAndAllReasons") {
    std::vector<spawn::SavedLifecycleInput> inputs;
    const auto                              owner = *PersistentId::parse("01900000-0000-7000-8000-000000000daa");
    for (auto reason : {spawn::RemovalReason::Death, spawn::RemovalReason::Administrative,
                        spawn::RemovalReason::Unloaded, spawn::RemovalReason::Detached})
        inputs.push_back({SubjectRef::fromPersistentId(owner.child("unit")), 9, reason, {owner, 3, 2, 7}});
    inputs.push_back(inputs.front());
    auto source = spawn::encodeSavedSpawnerKey(inputs.front().source).expect("source codec");
    CHECK(spawn::prepareSavedSpawnerKey(source).expect("source round trip") == inputs.front().source);
    source.set("extra", 0);
    CHECK(!spawn::prepareSavedSpawnerKey(source).ok());
    const auto value  = spawn::encodeSavedLifecycleInputs(inputs).expect("encode durable rows");
    const auto text   = value.toJson().expect("write durable rows");
    const auto parsed = spawn::prepareSavedLifecycleInputs(Value::fromJson(text).expect("read durable rows"))
                            .expect("admit durable rows");
    REQUIRE_EQ(parsed.size(), inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        CHECK(parsed[i].subject == inputs[i].subject);
        CHECK_EQ(parsed[i].sequence, 9u);
        CHECK(parsed[i].source == inputs[i].source);
        CHECK(parsed[i].reason == inputs[i].reason);
    }
    CHECK_EQ(spawn::encodeSavedLifecycleInputs(parsed).expect("repeat rows").toJson().expect("repeat wire"), text);
}
TEST_CASE("spawn.savedLifecycle.rejectsUnknownFieldsReasonsAndInvalidDurableSources") {
    const auto                              owner = *PersistentId::parse("01900000-0000-7000-8000-000000000dab");
    std::vector<spawn::SavedLifecycleInput> inputs{
        {SubjectRef::fromPersistentId(owner.child("unit")), 1, spawn::RemovalReason::Death, {owner, 1, 0, 1}}};
    const auto original = spawn::encodeSavedLifecycleInputs(inputs).expect("baseline rows");
    for (int mode = 0; mode < 5; ++mode) {
        auto  value = original;
        auto& row   = *value.getIf<Value::Array>()->front().getIf<Value::Object>();
        if (mode == 0) row.emplace("unknown", true);
        if (mode == 1) row.at("reason") = "vanished";
        if (mode == 2) row.at("sequence") = "01";
        if (mode == 3) row.at("source").getIf<Value::Object>()->at("epoch") = "0";
        if (mode == 4) row.at("source").getIf<Value::Object>()->at("generation") = "4294967296";
        REQUIRE(!spawn::prepareSavedLifecycleInputs(value).ok());
    }
    inputs[0].sequence = 0;
    REQUIRE(!spawn::encodeSavedLifecycleInputs(inputs).ok());
}
}  // namespace test_spawn_saved_lifecycle

namespace test_spawn {
TEST_CASE("Spawn file checkpoint remains resealable after source retirement without issuing source authority") {
    const auto epoch    = eve::PersistentId::parse("01900000-0000-7000-8000-000000000001")->child("file-checkpoint");
    auto       envelope = [&] {
        RuntimeFixture fixture;
        auto           source = fixture.attach();
        fixture.step(1, 0);
        fixture.world.setActive(source, false).expect("queued pause");
        return checkpoint(fixture.world, epoch, eve::Revision(19), checkpointTestHash).expect("freeze file");
    }();
    const auto bytes  = eve::serializeSnapshotEnvelope(envelope).expect("original bytes");
    auto       file   = eve::parseSnapshotEnvelope(bytes, checkpointTestHash).expect("independent file read");
    auto       frozen = eve::spawn::SpawnCheckpoint::parse(std::move(file), epoch, checkpointTestHash, Units{})
                      .expect("admit detached checkpoint");
    REQUIRE_EQ(frozen.membership().size(), std::size_t(4));
    REQUIRE_EQ(frozen.unitReferences().size(), std::size_t(2));
    for (const auto& member : frozen.membership()) REQUIRE(member.source.incarnation.isZero());
    auto resealed = frozen.seal(checkpointTestHash).expect("reseal detached facts");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(resealed).expect("resealed bytes"), bytes);
    auto admitted = eve::spawn::SpawnRestoreState::decode(resealed, epoch, checkpointTestHash, Units{})
                        .expect("existing restore decoder");
    REQUIRE_EQ(admitted.trackedCount(), std::size_t(4));
    REQUIRE_EQ(admitted.tick().value(), std::uint64_t(1));
    REQUIRE_EQ(frozen.unitReferences()[0].format(), "rpg.unit:alpha");
    REQUIRE_EQ(frozen.unitReferences()[1].format(), "rpg.unit:wolf");
    REQUIRE(!eve::spawn::SpawnCheckpoint::parse(envelope, epoch.child("wrong"), checkpointTestHash, Units{}).ok());
    auto corrupt = envelope;
    corrupt.payload.set("seed", "999");
    REQUIRE(!eve::spawn::SpawnCheckpoint::parse(std::move(corrupt), epoch, checkpointTestHash, Units{}).ok());
    struct Missing final : eve::spawn::IUnitDefinitionResolver {
        eve::Result<void> validate(const eve::DefinitionRef&) const override {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Saved recipe missing"));
        }
    } missing;
    REQUIRE(!eve::spawn::SpawnCheckpoint::parse(envelope, epoch, checkpointTestHash, missing).ok());
    REQUIRE(eve::spawn::SpawnCheckpoint::parse(envelope, epoch, checkpointTestHash, Units{}).ok());
}
}  // namespace test_spawn
