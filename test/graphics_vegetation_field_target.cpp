#include "graphics/editing/VegetationFieldTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics_editing;

namespace {
SelectionSnapshot select(const VegetationFieldTarget& target, const StableId& id) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, target.targetId(), id, "vegetation-field-element"});
    return result;
}
}  // namespace

TEST_CASE("graphics.vegetation.field_target_property_and_gizmo_share_atomic_transform") {
    VegetationField   field;
    VegetationElement first;
    first.priority = 10;
    first.center   = {1.f, 2.f, 3.f};
    VegetationElement second;
    second.priority = -2;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{first, second}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto&      target = *created.value();
    const auto ids    = target.elementIds();
    REQUIRE_EQ(ids.size(), 2U);
    const auto selection = select(target, ids[0]);

    auto center = target.makeSet(selection, PropertyPath("element.center"), Value::Array{4.0, 5.0, 6.0},
                                 PropertySetMode::Absolute);
    REQUIRE(center.ok());
    auto staged = target.cloneDomainState();
    REQUIRE(staged->applyDomainOperation(center.value()).ok());
    CHECK_EQ(field.revision(), 1U);
    REQUIRE(target.commitDomainState(std::move(staged)).ok());
    CHECK_EQ(field.revision(), 2U);

    auto transform = target.makeTransform(selection, {7.f, 8.f, 9.f}, {2.f, 3.f, 4.f}, 0.75f);
    REQUIRE(transform.ok());
    REQUIRE(target.applyDomainOperation(transform.value()).ok());
    auto elements = field.snapshotElements();
    REQUIRE(elements.ok());
    REQUIRE_EQ(elements.value().size(), 2U);
    CHECK_EQ(elements.value()[0].center.x, 7.f);
    CHECK_EQ(elements.value()[0].extents.z, 4.f);
    CHECK_EQ(elements.value()[0].yaw, 0.75f);

    DomainOperation undo = transform.value();
    undo.payload         = transform.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    elements = field.snapshotElements();
    REQUIRE(elements.ok());
    CHECK_EQ(elements.value()[0].center.x, 4.f);
}

TEST_CASE("graphics.vegetation.field_target_rejects_stale_runtime_and_invalid_extents") {
    VegetationField field;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto&      target    = *created.value();
    const auto selection = select(target, target.elementIds().front());

    auto invalid = target.makeSet(selection, PropertyPath("element.extents"), Value::Array{1.0, 0.0, 1.0},
                                  PropertySetMode::Absolute);
    CHECK(!invalid.ok());
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto stale = target.makeTransform(selection, {}, {1.f, 1.f, 1.f}, 0.f);
    CHECK(!stale.ok());
    CHECK(!target.currentRevision(selection).ok());
}
