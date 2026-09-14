#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "TestPropertyAccess.h"
#include "editor/EditorPropertyModel.h"

#include <limits>
#include <utility>

using namespace eve::editor;

namespace {

PropertyDescriptor editorNumberProperty() {
    PropertyDescriptor property;
    property.path            = PropertyPath("speed");
    property.type            = PropertyType::Float;
    property.numeric.minimum = 0.0;
    property.numeric.maximum = 10.0;
    return property;
}

PropertyDescriptor editorEnumProperty() {
    PropertyDescriptor property;
    property.path      = PropertyPath("mode");
    property.type      = PropertyType::Enum;
    property.enumItems = {"walk", "run"};
    return property;
}

class PropertyProvider final : public IPropertyProvider {
public:
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot &) const override {
        return eve::Result<eve::Revision>::success(eve::Revision{});
    }

    explicit PropertyProvider(PropertySchema schema) : schema_(std::move(schema)) {}

    PropertySchema schema(const SelectionSnapshot &) const override { return schema_; }

    PropertyReadResult read(const SelectionSnapshot &, const PropertyPath &path) const override {
        if (path == PropertyPath("speed")) return {PropertyReadState::Value, EditorValue(speed), {}};
        return {};
    }

    EditorResult<DomainOperation> makeSet(const SelectionSnapshot &, const PropertyPath &, const EditorValue &,
                                          PropertySetMode) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot &, const PropertyPath &) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    double speed = 4.0;

private:
    PropertySchema schema_;
};

PropertySchema editorSchema() {
    PropertySchema schema;
    schema.typeId = "test.property-validation";
    schema.properties.push_back(editorNumberProperty());
    return schema;
}

}  // namespace

TEST_CASE("property.validation_presentation_and_editor_share_type_enum_range_and_finite_rules") {
    const PropertyDescriptor                       editorNumber       = editorNumberProperty();
    const eve::property_access::PropertyDescriptor presentationNumber = toPresentationDescriptor(editorNumber);

    const auto sharedType = eve::property_access::validatePropertyValue(presentationNumber, eve::Value("fast"));
    const auto editorType = validatePropertyValue(editorNumber, EditorValue("fast"));
    CHECK(!sharedType.accepted);
    CHECK(!editorType.ok());
    CHECK_EQ(sharedType.code, std::string("property_access.property.type"));
    REQUIRE_EQ(editorType.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorType.diagnostics().front()).value(),
             std::string("editor.property.type-mismatch"));

    const double nonFinite    = std::numeric_limits<double>::quiet_NaN();
    const auto   sharedFinite = eve::property_access::validatePropertyValue(presentationNumber, eve::Value(nonFinite));
    const auto   editorFinite = validatePropertyValue(editorNumber, EditorValue(nonFinite));
    CHECK(!sharedFinite.accepted);
    CHECK(!editorFinite.ok());
    CHECK_EQ(sharedFinite.code, std::string("property_access.property.finite"));
    REQUIRE_EQ(editorFinite.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorFinite.diagnostics().front()).value(),
             std::string("editor.property.finite"));

    const auto sharedRange = eve::property_access::validatePropertyValue(presentationNumber, eve::Value(11.0));
    const auto editorRange = validatePropertyValue(editorNumber, EditorValue(11.0));
    CHECK(!sharedRange.accepted);
    CHECK(!editorRange.ok());
    CHECK_EQ(sharedRange.code, std::string("property_access.property.maximum"));
    REQUIRE_EQ(editorRange.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorRange.diagnostics().front()).value(),
             std::string("editor.property.above-maximum"));

    const PropertyDescriptor                       editorEnum       = editorEnumProperty();
    const eve::property_access::PropertyDescriptor presentationEnum = toPresentationDescriptor(editorEnum);
    const auto sharedEnum       = eve::property_access::validatePropertyValue(presentationEnum, eve::Value("sprint"));
    const auto editorEnumResult = validatePropertyValue(editorEnum, EditorValue("sprint"));
    CHECK(!sharedEnum.accepted);
    CHECK(!editorEnumResult.ok());
    CHECK_EQ(sharedEnum.code, std::string("property_access.property.choice"));
    REQUIRE_EQ(editorEnumResult.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorEnumResult.diagnostics().front()).value(),
             std::string("editor.property.invalid-enum"));
}

TEST_CASE("property.validation_editor_model_rejects_before_command_sink") {
    PropertyProvider    provider(editorSchema());
    EditorPropertyModel model(editorSchema(), {}, &provider);
    bool                sinkCalled = false;
    model.setEditSink([&](const PropertyEditIntent &) {
        sinkCalled = true;
        return eve::editing::applied<void>();
    });

    const auto rejected = model.write("speed", eve::Value(11.0));
    CHECK(!rejected.accepted);
    CHECK_EQ(rejected.code, std::string("editor.property.intent"));
    CHECK(!sinkCalled);

    const auto accepted = model.write("speed", eve::Value(8.0));
    CHECK(accepted.accepted);
    CHECK(sinkCalled);
}

TEST_CASE("property.validation_dynamic_model_uses_shared_finite_rule") {
    const PropertyDescriptor             editorNumber = editorNumberProperty();
    eve::property_access::PropertySchema schema;
    schema.properties.push_back(toPresentationDescriptor(editorNumber));
    TestPropertyAccess model(std::move(schema));

    const auto rejected = model.write("speed", eve::Value(std::numeric_limits<double>::infinity()));
    CHECK(!rejected.accepted);
    CHECK_EQ(rejected.code, std::string("property_access.property.finite"));
}

TEST_CASE("property.validation_transform_descriptor_preserves_editor_object_semantics") {
    PropertyDescriptor transform;
    transform.path = PropertyPath("transform");
    transform.type = PropertyType::Transform;

    const eve::property_access::PropertyDescriptor shared = toPresentationDescriptor(transform);
    CHECK_EQ(static_cast<int>(shared.kind), static_cast<int>(eve::property_access::PropertyKind::Struct));
    CHECK(eve::property_access::validatePropertyValue(shared, eve::Value(eve::Value::Object{})).accepted);
}
