#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "TestPropertyAccess.h"
#include "property_access/PropertyAccess.h"
#include "ui/PropertyView.h"
#include "ui/UIHost.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace eve::property_access;
using namespace eve::ui;
using eve::Value;

namespace {

PropertySchema playerSchema() {
    PropertySchema schema;
    schema.typeId = "game.player";

    PropertyDescriptor alive;
    alive.path = "state.alive";
    alive.displayName = "Alive";
    alive.category = "State";
    alive.kind = PropertyKind::Bool;
    alive.flags = PropertyFlag::Runtime;
    alive.defaultValue = true;
    schema.properties.push_back(alive);

    PropertyDescriptor speed;
    speed.path = "movement.speed";
    speed.displayName = "Speed";
    speed.description = "Maximum movement speed";
    speed.category = "Movement";
    speed.kind = PropertyKind::Number;
    speed.flags = PropertyFlag::Runtime;
    speed.defaultValue = 4.0;
    speed.numeric.minimum = 0.0;
    speed.numeric.maximum = 12.0;
    schema.properties.push_back(speed);

    PropertyDescriptor mode;
    mode.path = "movement.mode";
    mode.displayName = "Mode";
    mode.category = "Movement";
    mode.kind = PropertyKind::Enum;
    mode.flags = PropertyFlag::Runtime;
    mode.defaultValue = "walk";
    mode.choices = {"walk", "run", "fly"};
    schema.properties.push_back(mode);

    PropertyDescriptor debug;
    debug.path = "debug.label";
    debug.displayName = "Debug Label";
    debug.kind = PropertyKind::ReadOnlyText;
    debug.flags = PropertyFlag::Advanced | PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
    debug.defaultValue = "Player #1";
    schema.properties.push_back(debug);

    PropertyDescriptor tint;
    tint.path = "visual.tint";
    tint.displayName = "Tint";
    tint.category = "Visual";
    tint.kind = PropertyKind::Color;
    tint.flags = PropertyFlag::Runtime;
    tint.defaultValue = Value(Value::Array{Value(1.0), Value(0.0), Value(0.0), Value(1.0)});
    schema.properties.push_back(tint);

    PropertyDescriptor size;
    size.path = "visual.size";
    size.displayName = "Size";
    size.category = "Visual";
    size.kind = PropertyKind::Vec2;
    size.flags = PropertyFlag::Runtime;
    size.defaultValue = Value(Value::Array{Value(2.0), Value(4.0)});
    schema.properties.push_back(size);

    PropertyDescriptor tags;
    tags.path = "state.tags";
    tags.displayName = "Tags";
    tags.category = "State";
    tags.kind = PropertyKind::Array;
    tags.flags = PropertyFlag::Runtime;
    tags.defaultValue = Value(Value::Array{Value(std::string("player"))});
    schema.properties.push_back(tags);
    return schema;
}

UINode *node(UIHost &host, const std::string &id) {
    auto found = host.findById(id);
    return found ? &found->get() : nullptr;
}

UIHost *createHost(const std::string &name) {
    auto host = UIHost::resolve(UIHost::createHost(name));
    return host ? &host->get() : nullptr;
}

double numericValueApprox(const Value &value) {
    if (const auto *number = value.getIf<double>()) return *number;
    if (const auto *integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return 0.0;
}

}  // namespace

TEST_CASE("ui.presentation.dynamic_model_validates_and_notifies") {
    TestPropertyAccess          model(playerSchema());
    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe(
        [&changes](const PropertyChange &change) { changes.push_back(change); });

    CHECK(model.write("movement.speed", Value(8.0)).ok());
    REQUIRE_EQ(changes.size(), static_cast<std::size_t>(1));
    CHECK_EQ(changes.front().path, std::string("movement.speed"));
    CHECK_EQ(model.revision(), std::uint64_t(1));
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 8.0);

    CHECK(!model.write("movement.speed", Value(20.0)).ok());
    CHECK(!model.write("movement.mode", Value("swim")).ok());
    CHECK(!model.write("debug.label", Value("changed")).ok());
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));

    subscription.dispose();
    CHECK(model.write("state.alive", Value(false)).ok());
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));
}

TEST_CASE("ui.presentation.generated_view_binds_two_way") {
    TestPropertyAccess  model(playerSchema());
    PropertyViewOptions options;
    options.idPrefix = "player/";
    options.title = "Player";
    options.showEditorOnly = false;

    UIHost *host = createHost("presentation-test");
    REQUIRE(host != nullptr);
    host->setTree(buildPropertyView(model, options));

    UINode *alive = node(*host, "player/state_alive");
    UINode *speed = node(*host, "player/movement_speed");
    UINode *mode = node(*host, "player/movement_mode");
    REQUIRE(alive != nullptr);
    REQUIRE(speed != nullptr);
    REQUIRE(mode != nullptr);
    CHECK(node(*host, "player/debug_label") == nullptr);
    CHECK_EQ(speed->tooltip, std::string("Maximum movement speed"));
    CHECK_EQ(static_cast<int>(speed->accessibilityRole),
             static_cast<int>(AccessibilityRole::Slider));
    CHECK_EQ(speed->accessibilityName, std::string("Speed"));
    CHECK_EQ(speed->accessibilityDescription, std::string("Maximum movement speed"));

    REQUIRE_GE(alive->handlerToggle, 1u);
    host->tree()->toggleHandlers[alive->handlerToggle - 1](false);
    REQUIRE(model.read("state.alive").has_value());
    CHECK(!*model.read("state.alive")->getIf<bool>());

    REQUIRE_GE(speed->handlerValue, 1u);
    host->tree()->valueHandlers[speed->handlerValue - 1](9.5f);
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 9.5);

    REQUIRE_GE(mode->handlerValue, 1u);
    host->tree()->valueHandlers[mode->handlerValue - 1](2.0f);
    REQUIRE(model.read("movement.mode").has_value());
    CHECK_EQ(*model.read("movement.mode")->getIf<std::string>(), std::string("fly"));

    CHECK(model.write("movement.speed", Value(3.0)).ok());
    syncPropertyView(*host, model, options);
    CHECK_EQ(node(*host, "player/movement_speed")->value, 3.0f);

    UINode *tintR = node(*host, "player/visual_tint_r");
    UINode *sizeX = node(*host, "player/visual_size_x");
    UINode *tag0 = node(*host, "player/state_tags_0");
    REQUIRE(tintR != nullptr);
    REQUIRE(sizeX != nullptr);
    REQUIRE(tag0 != nullptr);
    REQUIRE_GE(tintR->handlerText, 1u);
    host->tree()->textHandlers[tintR->handlerText - 1]("0.25");
    // Keep optional Values alive: getIf returns a pointer into the optional storage.
    const std::optional<Value> tintValue = model.read("visual.tint");
    REQUIRE(tintValue.has_value());
    const Value::Array *tint = tintValue->getIf<Value::Array>();
    REQUIRE(tint != nullptr);
    CHECK_EQ(numericValueApprox((*tint)[0]), 0.25);

    REQUIRE_GE(sizeX->handlerText, 1u);
    host->tree()->textHandlers[sizeX->handlerText - 1]("8");
    const std::optional<Value> sizeValue = model.read("visual.size");
    REQUIRE(sizeValue.has_value());
    const Value::Array *size = sizeValue->getIf<Value::Array>();
    REQUIRE(size != nullptr);
    CHECK_EQ(numericValueApprox((*size)[0]), 8.0);

    REQUIRE_GE(tag0->handlerText, 1u);
    host->tree()->textHandlers[tag0->handlerText - 1]("hero");
    const std::optional<Value> tagsValue = model.read("state.tags");
    REQUIRE(tagsValue.has_value());
    const Value::Array *tags = tagsValue->getIf<Value::Array>();
    REQUIRE(tags != nullptr);
    const std::string *tag0Text = (*tags)[0].getIf<std::string>();
    REQUIRE(tag0Text != nullptr);
    CHECK_EQ(*tag0Text, std::string("hero"));
}

TEST_CASE("ui.presentation.component_tracks_model_revision") {
    TestPropertyAccess model(playerSchema());
    PropertyComponent component(&model);
    component.build();
    component.attach(UIHost::createHost("presentation-component"));
    component.rebuild();
    CHECK(!component.isDirty());

    CHECK(model.write("state.alive", Value(false)).ok());
    CHECK(component.isDirty());
    CHECK(component.updateIfDirty());
    CHECK(!component.isDirty());
}
