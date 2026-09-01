#include "procgen/AttributeTable.h"
#include "procgen/PointSet.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::procgen;

TEST_CASE("procgen.attributeTable.storesSparseTypedColumns") {
    AttributeTable table;
    const size_t   firstRow  = table.appendRow();
    const size_t   secondRow = table.appendRow();
    CHECK_EQ(firstRow, size_t(0));
    CHECK_EQ(secondRow, size_t(1));

    auto height = table.setFloat(1, "height", 12.5f);
    REQUIRE(height.ok());
    auto biome = table.setString(0, "biome", "forest");
    REQUIRE(biome.ok());

    CHECK_EQ(table.rowCount(), size_t(2));
    CHECK_EQ(table.columnCount(), size_t(2));
    CHECK_EQ(table.columnName(0), "height");
    CHECK_EQ(table.columnName(1), "biome");
    CHECK(!table.getFloat(0, "height").has_value());
    REQUIRE(table.getFloat(1, "height").has_value());
    CHECK_EQ(*table.getFloat(1, "height"), 12.5f);
    REQUIRE(table.getString(0, "biome").has_value());
    CHECK_EQ(*table.getString(0, "biome"), "forest");
}

TEST_CASE("procgen.attributeTable.rejectsTypeConflictWithoutMutation") {
    AttributeTable table;
    const size_t   row = table.appendRow();
    CHECK_EQ(row, size_t(0));
    auto initial = table.setFloat(0, "weight", 0.75f);
    REQUIRE(initial.ok());

    auto conflict = table.setInt(0, "weight", 4);
    REQUIRE(!conflict.ok());
    REQUIRE(conflict.error() != nullptr);
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::TypeMismatch);
    REQUIRE(table.typeOf("weight").has_value());
    CHECK_EQ(int(*table.typeOf("weight")), int(ProcgenAttributeType::Float));
    REQUIRE(table.getFloat(0, "weight").has_value());
    CHECK_EQ(*table.getFloat(0, "weight"), 0.75f);
}

TEST_CASE("procgen.attributeTable.appendsRowsTransactionally") {
    AttributeTable source;
    source.resize(2);
    auto sourceValue = source.setInt(1, "layer", 7);
    REQUIRE(sourceValue.ok());

    AttributeTable target;
    const size_t   targetRow = target.appendRow();
    CHECK_EQ(targetRow, size_t(0));
    auto targetValue = target.setString(0, "name", "root");
    REQUIRE(targetValue.ok());
    auto copied = target.appendRowFrom(source, 1);
    REQUIRE(copied.ok());
    CHECK_EQ(copied.value(), size_t(1));
    CHECK_EQ(target.rowCount(), size_t(2));
    REQUIRE(target.getString(0, "name").has_value());
    CHECK_EQ(*target.getString(0, "name"), "root");
    REQUIRE(target.getInt(1, "layer").has_value());
    CHECK_EQ(*target.getInt(1, "layer"), std::int64_t(7));

    AttributeTable incompatible;
    const size_t   incompatibleRow = incompatible.appendRow();
    CHECK_EQ(incompatibleRow, size_t(0));
    auto incompatibleValue = incompatible.setFloat(0, "layer", 1.f);
    REQUIRE(incompatibleValue.ok());
    const size_t rowsBefore = target.rowCount();
    auto         rejected   = target.appendRowFrom(incompatible, 0);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::TypeMismatch);
    CHECK_EQ(target.rowCount(), rowsBefore);
    REQUIRE(target.typeOf("layer").has_value());
    CHECK_EQ(int(*target.typeOf("layer")), int(ProcgenAttributeType::Int));
}

TEST_CASE("procgen.attributeTable.supportsSelfCopy") {
    AttributeTable table;
    const size_t   row = table.appendRow();
    CHECK_EQ(row, size_t(0));
    auto value = table.setVector(0, "normal", {1.f, 2.f, 3.f});
    REQUIRE(value.ok());

    auto copied = table.appendRowFrom(table, 0);
    REQUIRE(copied.ok());
    REQUIRE(table.getVector(copied.value(), "normal").has_value());
    const auto normal = *table.getVector(copied.value(), "normal");
    CHECK_EQ(normal.x, 1.f);
    CHECK_EQ(normal.y, 2.f);
    CHECK_EQ(normal.z, 3.f);
}

TEST_CASE("procgen.pointSet.preservesColumnarAttributesAcrossOperations") {
    PointSet  points;
    const int first  = points.add(0.f, 1.f, 0.f);
    const int second = points.add(2.f, 3.f, 0.f);
    REQUIRE(points.trySetFloatAttribute(first, "weight", 0.25f).ok());
    REQUIRE(points.trySetFloatAttribute(second, "weight", 0.75f).ok());
    REQUIRE(points.trySetStringAttribute(second, "biome", "forest").ok());

    const PointSet filtered = filterPointHeight(points, 2.f, 4.f);
    CHECK_EQ(filtered.getCount(), 1);
    CHECK_EQ(filtered.attributes().rowCount(), size_t(1));
    CHECK_EQ(filtered.getFloatAttribute(0, "weight", -1.f), 0.75f);
    CHECK_EQ(filtered.getStringAttribute(0, "biome", ""), std::string("forest"));

    const PointSet transformed = transformPointSet3D(filtered, 1.f, 2.f, 3.f, 10.f, 20.f, 30.f, 2.f, 2.f, 2.f);
    CHECK_EQ(transformed.attributes().rowCount(), size_t(1));
    CHECK_EQ(transformed.getFloatAttribute(0, "weight", -1.f), 0.75f);
    CHECK_EQ(transformed.getStringAttribute(0, "biome", ""), std::string("forest"));
}

TEST_CASE("procgen.pointSet.rejectsConflictingAttributeSchema") {
    PointSet  points;
    const int row = points.add(0.f, 0.f, 0.f);
    REQUIRE(points.trySetFloatAttribute(row, "weight", 0.5f).ok());

    const auto conflict = points.trySetIntAttribute(row, "weight", 2);
    REQUIRE(!conflict.ok());
    REQUIRE(conflict.error() != nullptr);
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::TypeMismatch);
    CHECK_EQ(points.getAttributeType(row, "weight"), std::string("float"));
    CHECK_EQ(points.getFloatAttribute(row, "weight", -1.f), 0.5f);
    CHECK_EQ(points.attributes().rowCount(), size_t(points.getCount()));
}
