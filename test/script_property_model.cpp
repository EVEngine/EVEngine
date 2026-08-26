#include "common/Runtime.h"
#include "presentation/PropertyModel.h"
#include "scriptmodel/ReflectedPropertyModel.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

using namespace eve;
using namespace eve::presentation;
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

    const PropertyDescriptor *hp = model.schema().find("hp");
    REQUIRE(hp != nullptr);
    CHECK(static_cast<int>(hp->kind) == static_cast<int>(PropertyKind::Number));
    CHECK_EQ(hp->displayName, std::string("Health"));
    CHECK_EQ(hp->description, std::string("Current health"));
    CHECK_EQ(hp->category, std::string("ModelHero"));
    REQUIRE(hp->numeric.minimum.has_value());
    REQUIRE(hp->numeric.maximum.has_value());
    CHECK_EQ(*hp->numeric.minimum, 0.0);
    CHECK_EQ(*hp->numeric.maximum, 100.0);

    const PropertyDescriptor *base = model.schema().find("baseValue");
    REQUIRE(base != nullptr);
    CHECK_EQ(base->category, std::string("ModelBase"));

    const PropertyDescriptor *job = model.schema().find("job");
    REQUIRE(job != nullptr);
    CHECK(static_cast<int>(job->kind) == static_cast<int>(PropertyKind::Enum));
    CHECK(job->choices == std::vector<std::string>({"warrior", "mage"}));

    const PropertyDescriptor *tags = model.schema().find("tags");
    REQUIRE(tags != nullptr);
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
