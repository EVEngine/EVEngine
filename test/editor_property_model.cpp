#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorPropertyModel.h"

#include <cstdint>
#include <string>
#include <utility>

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
    PropertySchema schema(const SelectionSnapshot &) const override { return ::schema(); }

    PropertyReadResult read(const SelectionSnapshot &, const PropertyPath &path) const override {
        if (path.value() == "movement.speed")
            return {PropertyReadState::Value, EditorValue(speed), {}};
        if (path.value() == "debug.name")
            return {PropertyReadState::Value, EditorValue("Actor #1"), {}};
        return {};
    }

    EditorResult<DomainOperation> makeSet(const SelectionSnapshot &, const PropertyPath &,
                                          const EditorValue &, PropertySetMode) const override {
        return EditorResult<DomainOperation>::applied({});
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot &,
                                            const PropertyPath &) const override {
        return EditorResult<DomainOperation>::applied({});
    }

    double speed = 4.0;
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
        return EditorResult<void>::applied();
    });

    std::string changedPath;
    auto subscription = model.subscribe(
        [&](const eve::presentation::PropertyChange &change) { changedPath = change.path; });
    CHECK(model.write("movement.speed", eve::presentation::Value(9.5)).accepted);
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
