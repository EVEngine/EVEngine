#include "common/Runtime.h"
#include "property_access/PropertyAccess.h"
#include "scriptmodel/ReflectedPropertyModel.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>
#include <vector>

using namespace eve;
using namespace eve::property_access;
using namespace eve::scriptmodel;

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
}
)SQ";

}  // namespace

TEST_CASE("scriptmodel.reflection_builds_shared_schema_and_structured_values") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "scriptmodel.nut");
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
    CHECK(hasFlag(tags->flags, PropertyFlag::ReadOnly));
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
}

TEST_CASE("scriptmodel.writes_and_refreshes_through_shared_mvvm_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "scriptmodel.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe(
        [&](const PropertyChange &change) { changes.push_back(change); });

    CHECK(model.write("hp", Value(42.0)).accepted);
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);
    CHECK(!changes.empty());
    CHECK_EQ(changes.back().path, std::string("hp"));

    CHECK(!model.write("hp", Value(101.0)).accepted);
    CHECK(!model.write("hp", Value("fast")).accepted);
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);
    CHECK(!model.write("tags", Value(Value::Array{})).accepted);

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

TEST_CASE("scriptmodel.property_validation_matches_presentation_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "scriptmodel.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    auto hpRef = model.schema().find("hp");
    auto jobRef = model.schema().find("job");
    auto tagsRef = model.schema().find("tags");
    REQUIRE(hpRef.has_value());
    REQUIRE(jobRef.has_value());
    REQUIRE(tagsRef.has_value());
    const PropertyDescriptor *hp = &hpRef->get();
    const PropertyDescriptor *job = &jobRef->get();
    const PropertyDescriptor *tags = &tagsRef->get();

    const auto sharedType = validatePropertyValue(*hp, Value("fast"));
    const auto runtimeType = model.write("hp", Value("fast"));
    CHECK(!sharedType.accepted);
    CHECK(!runtimeType.accepted);
    CHECK_EQ(sharedType.code, std::string("property_access.property.type"));
    CHECK_EQ(runtimeType.code, std::string("scriptmodel.property.type"));

    const auto sharedFinite = validatePropertyValue(
        *hp, Value(std::numeric_limits<double>::quiet_NaN()));
    const auto runtimeFinite = model.write(
        "hp", Value(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!sharedFinite.accepted);
    CHECK(!runtimeFinite.accepted);
    CHECK_EQ(sharedFinite.code, std::string("property_access.property.finite"));
    CHECK_EQ(runtimeFinite.code, std::string("scriptmodel.property.finite"));

    const auto sharedMinimum = validatePropertyValue(*hp, Value(-1.0));
    const auto runtimeMinimum = model.write("hp", Value(-1.0));
    CHECK(!sharedMinimum.accepted);
    CHECK(!runtimeMinimum.accepted);
    CHECK_EQ(sharedMinimum.code, std::string("property_access.property.minimum"));
    CHECK_EQ(runtimeMinimum.code, std::string("scriptmodel.property.minimum"));

    const auto sharedMaximum = validatePropertyValue(*hp, Value(101.0));
    const auto runtimeMaximum = model.write("hp", Value(101.0));
    CHECK(!sharedMaximum.accepted);
    CHECK(!runtimeMaximum.accepted);
    CHECK_EQ(sharedMaximum.code, std::string("property_access.property.maximum"));
    CHECK_EQ(runtimeMaximum.code, std::string("scriptmodel.property.maximum"));

    const auto sharedChoice = validatePropertyValue(*job, Value("rogue"));
    const auto runtimeChoice = model.write("job", Value("rogue"));
    CHECK(!sharedChoice.accepted);
    CHECK(!runtimeChoice.accepted);
    CHECK_EQ(sharedChoice.code, std::string("property_access.property.choice"));
    CHECK_EQ(runtimeChoice.code, std::string("scriptmodel.property.choice"));

    const auto sharedReadOnly = validatePropertyValue(*tags, Value(Value::Array{}));
    const auto runtimeReadOnly = model.write("tags", Value(Value::Array{}));
    CHECK(!sharedReadOnly.accepted);
    CHECK(!runtimeReadOnly.accepted);
    CHECK_EQ(sharedReadOnly.code, std::string("property_access.property.read-only"));
    CHECK_EQ(runtimeReadOnly.code, std::string("scriptmodel.property.read-only"));
}
