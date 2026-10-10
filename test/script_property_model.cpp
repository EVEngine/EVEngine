#include "common/Runtime.h"
#include "property_access/PropertyAccess.h"
#include "property_access/squirrel/ReflectedPropertyModel.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace eve;
using namespace eve::property_access;

namespace {

const char *kModelScript = R"SQ(
class ModelBase {
    baseValue = 2
}
class ModelHero extends ModelBase {
    </ editor = "slider", min = 0, max = 100, step = 1,
       label = "Health", tooltip = "Current health" />
    hp = 80.0
    </ editor = "combo", options = "warrior,mage" />
    job = "warrior"
    alive = true
    tags = ["player", "hero"]
    stats = { armor = 3 }
    </ editor = "color" />
    tint = [1.0, 0.5, 0.25, 1.0]
    </ editor = "vec2" />
    size = [2.0, 3.0]
}
)SQ";

}  // namespace

TEST_CASE("property_access.reflection_builds_shared_schema_and_structured_values") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");

    ReflectedPropertyModel model(runtime, hero);
    CHECK_EQ(model.schema().typeId, std::string("ModelHero"));

    auto hpRef = model.schema().find("hp");
    REQUIRE(hpRef.has_value());
    const PropertyDescriptor *hp = &hpRef->get();
    CHECK(static_cast<int>(hp->kind) == static_cast<int>(PropertyKind::Number));
    CHECK_EQ(hp->displayName, std::string("Health"));
    CHECK_EQ(hp->description, std::string("Current health"));
    CHECK_EQ(hp->category, std::string("ModelHero"));
    REQUIRE(hp->numeric.minimum.has_value());
    REQUIRE(hp->numeric.maximum.has_value());
    CHECK_EQ(*hp->numeric.minimum, 0.0);
    CHECK_EQ(*hp->numeric.maximum, 100.0);

    auto baseRef = model.schema().find("baseValue");
    REQUIRE(baseRef.has_value());
    const PropertyDescriptor *base = &baseRef->get();
    CHECK_EQ(base->category, std::string("ModelBase"));

    auto jobRef = model.schema().find("job");
    REQUIRE(jobRef.has_value());
    const PropertyDescriptor *job = &jobRef->get();
    CHECK(static_cast<int>(job->kind) == static_cast<int>(PropertyKind::Enum));
    CHECK(job->choices == std::vector<std::string>({"warrior", "mage"}));

    auto tagsRef = model.schema().find("tags");
    REQUIRE(tagsRef.has_value());
    const PropertyDescriptor *tags = &tagsRef->get();
    CHECK(!hasFlag(tags->flags, PropertyFlag::ReadOnly));
    CHECK(static_cast<int>(tags->kind) == static_cast<int>(PropertyKind::Array));
    const std::optional<Value> tagValue = model.read("tags");
    REQUIRE(tagValue.has_value());
    const Value::Array *tagArray = tagValue->getIf<Value::Array>();
    REQUIRE(tagArray != nullptr);
    CHECK_EQ(tagArray->size(), static_cast<std::size_t>(2));

    const std::optional<Value> statsValue = model.read("stats");
    REQUIRE(statsValue.has_value());
    const Value::Object *stats = statsValue->getIf<Value::Object>();
    REQUIRE(stats != nullptr);
    CHECK(stats->contains("armor"));

    auto tintRef = model.schema().find("tint");
    REQUIRE(tintRef.has_value());
    CHECK(static_cast<int>(tintRef->get().kind) == static_cast<int>(PropertyKind::Color));
    CHECK(!hasFlag(tintRef->get().flags, PropertyFlag::ReadOnly));

    auto sizeRef = model.schema().find("size");
    REQUIRE(sizeRef.has_value());
    CHECK(static_cast<int>(sizeRef->get().kind) == static_cast<int>(PropertyKind::Vec2));
}

TEST_CASE("property_access.writes_and_refreshes_through_shared_mvvm_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe(
        [&](const PropertyChange &change) { changes.push_back(change); });

    CHECK(model.write("hp", Value(42.0)).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);
    CHECK(!changes.empty());
    CHECK_EQ(changes.back().path, std::string("hp"));

    CHECK(!model.write("hp", Value(101.0)).ok());
    CHECK(!model.write("hp", Value("fast")).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);

    Value::Array tags = {Value(std::string("player")), Value(std::string("elite"))};
    CHECK(model.write("tags", Value(tags)).ok());
    CHECK_EQ(runtime.arraySize(hero, "tags"), static_cast<std::size_t>(2));
    CHECK_EQ(runtime.arrayGet(hero, "tags", 1).asString(), std::string("elite"));

    Value::Array tint = {Value(0.1), Value(0.2), Value(0.3), Value(0.4)};
    CHECK(model.write("tint", Value(tint)).ok());
    CHECK(std::fabs(runtime.arrayGet(hero, "tint", 2).asFloat() - 0.3f) < 1e-5f);

    Value::Array badTint = {Value(0.1), Value(0.2)};
    CHECK(!model.write("tint", Value(badTint)).ok());

    Value::Object stats;
    stats.emplace("armor", Value(std::int64_t(9)));
    stats.emplace("resist", Value(1.5));
    CHECK(model.write("stats", Value(stats)).ok());
    CHECK_EQ(runtime.tableGet(hero, "stats", "armor").asInt(), static_cast<std::int64_t>(9));
    CHECK_EQ(runtime.tableGet(hero, "stats", "resist").asFloat(), 1.5);

    ReflectedValue external;
    external.kind = ReflectedValueKind::Bool;
    external.boolean = false;
    REQUIRE(runtime.writeProperty(hero, "alive", external));
    const std::size_t before = changes.size();
    model.refresh();
    REQUIRE(changes.size() > before);
    CHECK_EQ(changes.back().path, std::string("alive"));
    const bool *alive = changes.back().value.getIf<bool>();
    REQUIRE(alive != nullptr);
    CHECK(!*alive);
}

TEST_CASE("property_access.script_validation_matches_shared_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object            hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    auto hpRef   = model.schema().find("hp");
    auto jobRef  = model.schema().find("job");
    auto tintRef = model.schema().find("tint");
    REQUIRE(hpRef.has_value());
    REQUIRE(jobRef.has_value());
    REQUIRE(tintRef.has_value());
    const PropertyDescriptor *hp   = &hpRef->get();
    const PropertyDescriptor *job  = &jobRef->get();
    const PropertyDescriptor *tint = &tintRef->get();

    const auto sharedType  = validatePropertyValue(*hp, Value("fast"));
    const auto runtimeType = model.write("hp", Value("fast"));
    CHECK(!sharedType.ok());
    CHECK(!runtimeType.ok());
    CHECK_EQ(writeRule(sharedType), std::string("property_access.property.type"));
    CHECK_EQ(writeRule(runtimeType), std::string("property_access.script.type"));

    const auto sharedFinite  = validatePropertyValue(*hp, Value(std::numeric_limits<double>::quiet_NaN()));
    const auto runtimeFinite = model.write("hp", Value(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!sharedFinite.ok());
    CHECK(!runtimeFinite.ok());
    CHECK_EQ(writeRule(sharedFinite), std::string("property_access.property.finite"));
    CHECK_EQ(writeRule(runtimeFinite), std::string("property_access.script.finite"));

    const auto sharedMinimum  = validatePropertyValue(*hp, Value(-1.0));
    const auto runtimeMinimum = model.write("hp", Value(-1.0));
    CHECK(!sharedMinimum.ok());
    CHECK(!runtimeMinimum.ok());
    CHECK_EQ(writeRule(sharedMinimum), std::string("property_access.property.minimum"));
    CHECK_EQ(writeRule(runtimeMinimum), std::string("property_access.script.minimum"));

    const auto sharedMaximum  = validatePropertyValue(*hp, Value(101.0));
    const auto runtimeMaximum = model.write("hp", Value(101.0));
    CHECK(!sharedMaximum.ok());
    CHECK(!runtimeMaximum.ok());
    CHECK_EQ(writeRule(sharedMaximum), std::string("property_access.property.maximum"));
    CHECK_EQ(writeRule(runtimeMaximum), std::string("property_access.script.maximum"));

    const auto sharedChoice  = validatePropertyValue(*job, Value("rogue"));
    const auto runtimeChoice = model.write("job", Value("rogue"));
    CHECK(!sharedChoice.ok());
    CHECK(!runtimeChoice.ok());
    CHECK_EQ(writeRule(sharedChoice), std::string("property_access.property.choice"));
    CHECK_EQ(writeRule(runtimeChoice), std::string("property_access.script.choice"));

    Value::Array badArity = {Value(1.0), Value(2.0)};
    const auto sharedArity  = validatePropertyValue(*tint, Value(badArity));
    const auto runtimeArity = model.write("tint", Value(badArity));
    CHECK(!sharedArity.ok());
    CHECK(!runtimeArity.ok());
    CHECK_EQ(writeRule(sharedArity), std::string("property_access.property.arity"));
    CHECK_EQ(writeRule(runtimeArity), std::string("property_access.script.arity"));
}
