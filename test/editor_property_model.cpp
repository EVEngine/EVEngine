#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorPropertyModel.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace eve::editor;

namespace {

PropertySchema schema() {
    PropertySchema result;
    result.typeId = "test.actor";

    PropertyDescriptor speed;
    speed.path = PropertyPath("movement.speed");
    speed.displayNameKey = "Speed";
    speed.type = PropertyType::Float;
    speed.flags = PropertyFlag::Runtime;
    speed.numeric.minimum = 0.0;
    speed.numeric.maximum = 20.0;
    result.properties.push_back(speed);

    PropertyDescriptor debug;
    debug.path = PropertyPath("debug.name");
    debug.displayNameKey = "Debug name";
    debug.type = PropertyType::String;
    debug.flags = PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
    result.properties.push_back(debug);
    return result;
}

class PropertyProvider final : public IPropertyProvider {
public:
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot &) const override {
        return eve::Result<eve::Revision>::success(eve::Revision{});
    }

    PropertySchema schema(const SelectionSnapshot &) const override { return ::schema(); }

    PropertyReadResult read(const SelectionSnapshot &, const PropertyPath &path) const override {
        ++readCount;
        if (path.value() == "movement.speed") {
            if (speedState != PropertyReadState::Value) return {speedState, {}, {}};
            return {PropertyReadState::Value, EditorValue(speed), {}};
        }
        if (path.value() == "debug.name")
            return {PropertyReadState::Value, EditorValue(debugName), {}};
        return {};
    }

    EditorResult<DomainOperation> makeSet(const SelectionSnapshot &, const PropertyPath &,
                                          const EditorValue &, PropertySetMode) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot &,
                                            const PropertyPath &) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    double speed = 4.0;
    std::string debugName = "Actor #1";
    PropertyReadState speedState = PropertyReadState::Value;
    mutable std::size_t readCount = 0;
};

}  // namespace

TEST_CASE("editor.property_model_routes_runtime_writes_through_command_intent") {
    PropertyProvider provider;
    SelectionSnapshot selection;
    HostProfile runtime = HostProfile::runtimeBuilder();
    EditorPropertyModel model(schema(), selection, &provider, PropertyModelSurface::Runtime,
                              std::move(runtime));

    REQUIRE_EQ(model.schema().properties.size(), static_cast<std::size_t>(1));
    CHECK_EQ(model.schema().properties.front().path, std::string("movement.speed"));
    CHECK(model.read("debug.name") == std::nullopt);

    bool sinkCalled = false;
    model.setEditSink([&](const PropertyEditIntent &intent) {
        sinkCalled = true;
        CHECK_EQ(intent.command.value(), std::string("editor.property.set"));
        const auto *payload = intent.payload.getIf<EditorValue::Object>();
        REQUIRE(payload != nullptr);
        CHECK_EQ(*payload->at("path").getIf<std::string>(), std::string("movement.speed"));
        provider.speed = *payload->at("value").getIf<double>();
        return eve::editing::applied<void>();
    });

    std::string changedPath;
    auto        subscription =
        model.subscribe([&](const eve::property_access::PropertyChange &change) { changedPath = change.path; });
    CHECK(model.write("movement.speed", eve::Value(9.5)).accepted);
    CHECK(sinkCalled);
    CHECK_EQ(provider.speed, 9.5);
    CHECK_EQ(changedPath, std::string("movement.speed"));
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 9.5);
}

TEST_CASE("editor.property_model_value_conversion_preserves_nested_data") {
    EditorValue::Object source;
    source["enabled"] = true;
    source["values"] = EditorValue::Array{EditorValue(3), EditorValue("four")};
    const EditorValue original(std::move(source));
    CHECK(toEditorValue(toPresentationValue(original)) == original);
}

TEST_CASE("editor.property_model_reads_one_atomically_refreshed_snapshot") {
    PropertyProvider provider;
    SelectionSnapshot selection;
    EditorPropertyModel model(schema(), selection, &provider);

    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 4.0);
    provider.speed = 8.0;
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 4.0);

    REQUIRE(model.refresh().ok());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 8.0);
}

TEST_CASE("editor.property_model_notifies_value_availability_transitions") {
    PropertyProvider provider;
    SelectionSnapshot selection;
    EditorPropertyModel model(schema(), selection, &provider);

    std::vector<eve::property_access::PropertyChangeState> states;
    auto subscription = model.subscribe([&](const eve::property_access::PropertyChange &change) {
        if (change.path == "movement.speed") states.push_back(change.state);
    });

    provider.speedState = PropertyReadState::Mixed;
    REQUIRE(model.refresh().ok());
    CHECK(model.read("movement.speed") == std::nullopt);
    provider.speedState = PropertyReadState::Missing;
    REQUIRE(model.refresh().ok());
    CHECK(model.read("movement.speed") == std::nullopt);
    provider.speedState = PropertyReadState::Value;
    provider.speed = 12.0;
    REQUIRE(model.refresh().ok());

    REQUIRE_EQ(states.size(), static_cast<std::size_t>(3));
    CHECK(static_cast<int>(states[0]) == static_cast<int>(eve::property_access::PropertyChangeState::Mixed));
    CHECK(static_cast<int>(states[1]) == static_cast<int>(eve::property_access::PropertyChangeState::Missing));
    CHECK(static_cast<int>(states[2]) == static_cast<int>(eve::property_access::PropertyChangeState::Value));
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 12.0);
}

TEST_CASE("editor.property_model_queues_reentrant_snapshot_notifications") {
    PropertyProvider provider;
    SelectionSnapshot selection;
    EditorPropertyModel model(schema(), selection, &provider);

    std::vector<std::pair<std::string, std::uint64_t>> notifications;
    bool reentered = false;
    auto subscription = model.subscribe([&](const eve::property_access::PropertyChange &change) {
        notifications.emplace_back(change.path, change.revision);
        if (change.path == "movement.speed" && !reentered) {
            reentered = true;
            REQUIRE(model.read("debug.name").has_value());
            CHECK_EQ(*model.read("debug.name")->getIf<std::string>(), std::string("Actor #2"));
            provider.speed = 6.0;
            provider.debugName = "Actor #3";
            REQUIRE(model.refresh().ok());
        }
    });

    provider.speed = 5.0;
    provider.debugName = "Actor #2";
    REQUIRE(model.refresh().ok());

    REQUIRE_EQ(notifications.size(), static_cast<std::size_t>(4));
    CHECK_EQ(notifications[0].first, std::string("debug.name"));
    CHECK_EQ(notifications[1].first, std::string("movement.speed"));
    CHECK_EQ(notifications[2].first, std::string("debug.name"));
    CHECK_EQ(notifications[3].first, std::string("movement.speed"));
    CHECK_EQ(notifications[0].second, notifications[1].second);
    CHECK(notifications[1].second < notifications[2].second);
    CHECK_EQ(notifications[2].second, notifications[3].second);
}

TEST_CASE("editor.property_model_runtime_surface_requires_runtime_world_feature") {
    PropertyProvider provider;
    SelectionSnapshot selection;
    HostProfile denied(HostKind::RuntimeBuilder);
    EditorPropertyModel model(schema(), selection, &provider, PropertyModelSurface::Runtime, std::move(denied));

    CHECK(model.schema().properties.empty());
    CHECK(model.read("movement.speed") == std::nullopt);
    CHECK_EQ(provider.readCount, static_cast<std::size_t>(0));
}
