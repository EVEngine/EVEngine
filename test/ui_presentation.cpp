#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "property_access/PropertyAccess.h"
#include "TestPropertyAccess.h"
#include "ui/PropertyView.h"
#include "ui/UIHost.h"

#include <cstdint>
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

}  // namespace

TEST_CASE("ui.presentation.dynamic_model_validates_and_notifies") {
    TestPropertyAccess model(playerSchema());
    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe(
        [&changes](const PropertyChange &change) { changes.push_back(change); });

    CHECK(model.write("movement.speed", Value(8.0)).accepted);
    REQUIRE_EQ(changes.size(), static_cast<std::size_t>(1));
    CHECK_EQ(changes.front().path, std::string("movement.speed"));
    CHECK_EQ(model.revision(), std::uint64_t(1));
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 8.0);

    CHECK(!model.write("movement.speed", Value(20.0)).accepted);
    CHECK(!model.write("movement.mode", Value("swim")).accepted);
    CHECK(!model.write("debug.label", Value("changed")).accepted);
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));

    subscription.dispose();
    CHECK(model.write("state.alive", Value(false)).accepted);
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));
}

TEST_CASE("ui.presentation.generated_view_binds_two_way") {
    TestPropertyAccess model(playerSchema());
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

    CHECK(model.write("movement.speed", Value(3.0)).accepted);
    syncPropertyView(*host, model, options);
    CHECK_EQ(node(*host, "player/movement_speed")->value, 3.0f);
}

TEST_CASE("ui.presentation.component_tracks_model_revision") {
    TestPropertyAccess model(playerSchema());
    PropertyComponent component(&model);
    component.build();
    component.attach(UIHost::createHost("presentation-component"));
    component.rebuild();
    CHECK(!component.isDirty());

    CHECK(model.write("state.alive", Value(false)).accepted);
    CHECK(component.isDirty());
    CHECK(component.updateIfDirty());
    CHECK(!component.isDirty());
}
