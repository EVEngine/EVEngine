#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_level)
#include "PrivateTestScope.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_level.cpp
namespace test_editor_level {
using namespace eve::level_editing;

TEST_CASE("editor.level.document_and_formats") {
    LevelDocument level(4, 3, 16.f, 24.f);
    level.setOrientation("hexagonal");
    const int ground = level.addTileLayer("Ground");
    level.getTileLayer(ground)->get().setGid(2, 1, 17);
    const int actors = level.addObjectLayer("Actors");
    const int hero   = level.addObject(actors, "spawn", 32.f, 24.f);
    REQUIRE(hero >= 0);
    auto heroRef = level.object(actors, hero);
    REQUIRE(heroRef.has_value());
    heroRef->get().name               = "Player";
    heroRef->get().properties["team"] = "blue";
    level.setProperty("music", "dungeon");

    LevelFormatRegistry formats;
    CHECK_EQ(formats.getFormatCount(), 2);
    auto nativeResult = formats.encode("eve.level", level);
    REQUIRE(nativeResult.ok());
    std::string native = std::move(nativeResult).takeValue();
    CHECK_EQ(formats.detect("room.level.json", native), std::string("eve.level"));

    auto roundtripResult = formats.decode("eve.level", native);
    REQUIRE(roundtripResult.ok());
    auto roundtrip = std::move(roundtripResult).takeValue();
    CHECK_EQ(roundtrip->getOrientation(), std::string("hexagonal"));
    CHECK_EQ(roundtrip->getTileLayer(0)->get().getGid(2, 1), 17);
    CHECK_EQ(roundtrip->object(1, 0)->get().properties.at("team"), std::string("blue"));

    auto tiledResult = formats.encode("tiled.json", level);
    REQUIRE(tiledResult.ok());
    std::string tiled = std::move(tiledResult).takeValue();
    CHECK_EQ(formats.detect("room.tmj", tiled), std::string("tiled.json"));
    auto importedResult = formats.decode("tiled.json", tiled);
    REQUIRE(importedResult.ok());
    auto imported = std::move(importedResult).takeValue();
    CHECK_EQ(imported->getLayerCount(), 2);
    CHECK_EQ(imported->getTileLayer(0)->get().getGid(2, 1), 17);
}
}  // namespace test_editor_level
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_tilemap_formats)
#include "PrivateTestScope.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include "common/Json.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_tilemap_formats.cpp
namespace test_editor_tilemap_formats_unit {
using namespace eve::level_editing;

TEST_CASE("editor.tilemap.formats.preserveAssetsFlagsAndObjectIdentity") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({
      "type":"map","width":1,"height":1,"tilewidth":16,"tileheight":16,
      "orientation":"hexagonal",
      "tilesets":[{"firstgid":1,"source":"tiles/ground.tsj"}],
      "staggeraxis":"x","staggerindex":"even","hexsidelength":8,
      "layers":[{"id":7,"type":"tilelayer","data":[2147483649]},
                {"id":9,"type":"objectgroup","objects":[
                  {"id":42,"name":"gate","class":"door","x":1,"y":2,
                   "polygon":[{"x":0,"y":0},{"x":4,"y":0},{"x":4,"y":4}]}]}]
    })");
    REQUIRE(decoded.ok());
    auto encoded = formats.encode("tiled.json", *decoded.value());
    REQUIRE(encoded.ok());
    auto json = eve::json::Document::parse(encoded.value());
    REQUIRE(json.valid());
    auto root = json.root();
    CHECK_EQ(root.getString("orientation"), std::string("hexagonal"));
    CHECK_EQ(root.get("tilesets").at(0).getString("source"), std::string("tiles/ground.tsj"));
    CHECK_EQ(root.getString("staggeraxis"), std::string("x"));
    CHECK_EQ(root.get("layers").at(0).getInt("id"), 7);
    CHECK_EQ(root.get("layers").at(0).get("data").at(0).asDouble(), 2147483649.0);
    auto object = root.get("layers").at(1).get("objects").at(0);
    CHECK_EQ(object.getInt("id"), 42);
    CHECK_EQ(object.get("polygon").size(), size_t(3));
    CHECK_EQ(object.getString("class"), std::string("door"));
}

TEST_CASE("editor.tilemap.formats.roundTripProjectedOrientations") {
    LevelFormatRegistry formats;
    for (const char *orientation : {"isometric", "staggered", "hexagonal"}) {
        LevelDocument level(3, 2, 64.f, 32.f);
        level.setOrientation(orientation);
        level.addTileLayer("ground");

        auto tiled = formats.encode("tiled.json", level);
        REQUIRE(tiled.ok());
        auto restored = formats.decode("tiled.json", tiled.value());
        REQUIRE(restored.ok());
        CHECK_EQ(restored.value()->getOrientation(), std::string(orientation));

        auto native = formats.encode("eve.level", *restored.value());
        REQUIRE(native.ok());
        auto reopened = formats.decode("eve.level", native.value());
        REQUIRE(reopened.ok());
        CHECK_EQ(reopened.value()->getOrientation(), std::string(orientation));
        CHECK_EQ(reopened.value()->getTileWidth(), 64.f);
        CHECK_EQ(reopened.value()->getTileHeight(), 32.f);
    }
}

TEST_CASE("editor.tilemap.formats.rejectUnsupportedWithoutBlankMaps") {
    LevelFormatRegistry formats;
    for (const char *layer : {R"({"type":"group","layers":[{"type":"tilelayer","data":[1]}]})",
                              R"({"type":"imagelayer","image":"background.png"})",
                              R"({"type":"tilelayer","encoding":"base64","data":"AQAAAA=="})",
                              R"({"type":"tilelayer","chunks":[{"x":0,"y":0,"width":1,"height":1,"data":[1]}]})"}) {
        auto decoded = formats.decode("tiled.json",
                                      std::string(R"({"type":"map","width":1,"height":1,"layers":[)") + layer + "]}");
        CHECK(!decoded.ok());
    }
}

TEST_CASE("editor.tilemap.formats.rejectMalformedCellsAndFutureSchema") {
    LevelFormatRegistry formats;
    for (const char *data : {"[]", "[1,2]", "[-1]", "[4294967296]", "[1.5]", "[\"1\"]"}) {
        auto decoded = formats.decode(
            "tiled.json",
            std::string(R"({"type":"map","width":1,"height":1,"layers":[{"type":"tilelayer","data":)") + data + "}]}");
        CHECK(!decoded.ok());
    }
    auto future =
        formats.decode("eve.level", R"({"format":"eve.level","version":999,"width":1,"height":1,"layers":[]})");
    CHECK(!future.ok());
}

TEST_CASE("editor.tilemap.formats.nativePreservesLocksAndStringIds") {
    LevelFormatRegistry formats;
    LevelDocument       level(1, 1);
    int                 li        = level.addObjectLayer("objects");
    level.layer(li)->get().locked = true;
    int               oi          = level.addObject(li, "spawn", 0, 0);
    const std::string id          = level.object(li, oi)->get().id;
    auto              encoded     = formats.encode("eve.level", level);
    REQUIRE(encoded.ok());
    auto decoded = formats.decode("eve.level", encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value()->layer(0)->get().locked);
    CHECK_EQ(decoded.value()->object(0, 0)->get().id, id);
}

TEST_CASE("editor.tilemap.formats.typedPropertiesSurviveNativeConversion") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({"type":"map","width":1,"height":1,
      "tilesets":[{"firstgid":1,"tiles":[{"id":0,"animation":[{"tileid":0,"duration":100}]}]}],
      "properties":[{"name":"speed","type":"float","value":1.5},
                    {"name":"enabled","type":"bool","value":true},
                    {"name":"label","type":"string","value":"old"}],
      "layers":[{"id":10,"type":"tilelayer","data":[1]}]})");
    REQUIRE(decoded.ok());
    decoded.value()->setProperty("label", "new");
    auto native = formats.encode("eve.level", *decoded.value());
    REQUIRE(native.ok());
    auto restored = formats.decode("eve.level", native.value());
    REQUIRE(restored.ok());
    auto tiled = formats.encode("tiled.json", *restored.value());
    REQUIRE(tiled.ok());
    auto json = eve::json::Document::parse(tiled.value());
    REQUIRE(json.valid());
    auto properties = json.root().get("properties");
    REQUIRE(properties.size() == 3);
    CHECK_EQ(properties.at(0).getString("type"), std::string("float"));
    CHECK_EQ(properties.at(0).get("value").asDouble(), 1.5);
    CHECK(properties.at(1).get("value").asBool());
    CHECK_EQ(properties.at(2).getString("value"), std::string("new"));
    CHECK_EQ(json.root().get("tilesets").at(0).get("tiles").at(0).get("animation").at(0).getInt("duration"), 100);
    auto again = formats.encode("tiled.json", *restored.value());
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), tiled.value());
}


TEST_CASE("editor.tilemap.formats.newIdsDoNotCollideAfterRestore") {
    LevelFormatRegistry formats;
    auto decoded = formats.decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
      "layers":[{"id":"layer-2","type":"objectgroup","objects":[{"id":"object-4","type":"spawn"}]}]})");
    REQUIRE(decoded.ok());
    const int added = decoded.value()->addObjectLayer("new");
    CHECK_NE(decoded.value()->layer(added)->get().id, std::string("layer-2"));
    const int object = decoded.value()->addObject(0, "spawn", 1, 1);
    CHECK_NE(decoded.value()->object(0, object)->get().id, std::string("object-4"));
}

TEST_CASE("editor.tilemap.formats.paintUndoRedoSaveJourney") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({"type":"map","width":1,"height":1,
      "tilesets":[{"firstgid":1,"source":"ground.tsj"}],
      "layers":[{"id":1,"type":"tilelayer","data":[2147483649]}]})");
    REQUIRE(decoded.ok());
    auto &buffer = decoded.value()->getTileLayer(0)->get();
    Brush brush;
    brush.setSize(1);
    brush.setTile(2);
    REQUIRE(brush.paintAt(&buffer, 0, 0) == 1);
    EditorHistory history;
    history.beginGroup("paint");
    for (int i = 0; i < brush.getChangeCount(); ++i)
        history.recordTile(brush.getChangeX(i), brush.getChangeY(i), brush.getChangeOldGid(i),
                           brush.getChangeNewGid(i));
    history.endGroup();
    REQUIRE(history.undoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    CHECK_EQ(uint32_t(buffer.getGid(0, 0)), uint32_t{0x80000001});
    auto undone = formats.encode("tiled.json", *decoded.value());
    REQUIRE(undone.ok());
    auto reopened = formats.decode("tiled.json", undone.value());
    REQUIRE(reopened.ok());
    CHECK_EQ(uint32_t(reopened.value()->getTileLayer(0)->get().getGid(0, 0)), uint32_t{0x80000001});
    REQUIRE(history.redoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    CHECK_EQ(buffer.getGid(0, 0), 2);
}

TEST_CASE("editor.tilemap.formats.migrateNativeSignedGid") {
    LevelFormatRegistry formats;
    auto decoded = formats.decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
      "layers":[{"id":"layer-1","type":"tilelayer","data":[-2147483647]}]})");
    REQUIRE(decoded.ok());
    CHECK_EQ(uint32_t(decoded.value()->getTileLayer(0)->get().getGid(0, 0)), uint32_t{0x80000001});
    auto encoded = formats.encode("eve.level", *decoded.value());
    REQUIRE(encoded.ok());
    auto json = eve::json::Document::parse(encoded.value());
    REQUIRE(json.valid());
    CHECK_EQ(json.root().get("layers").at(0).get("data").at(0).asDouble(), 2147483649.0);
}
}  // namespace test_editor_tilemap_formats_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_property_validation_parity)
#include "PrivateTestScope.h"
#include <limits>
#include <utility>
#include "TestPropertyAccess.h"
#include "editor/EditorPropertyModel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// property_validation_parity.cpp
namespace test_property_validation_parity {
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

    Result<DomainOperation> makeSet(const SelectionSnapshot &, const PropertyPath &, const EditorValue &,
                                    PropertySetMode) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    Result<DomainOperation> makeReset(const SelectionSnapshot &, const PropertyPath &) const override {
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
    CHECK(!sharedType.ok());
    CHECK(!editorType.ok());
    CHECK_EQ(eve::property_access::writeRule(sharedType), std::string("property_access.property.type"));
    REQUIRE_EQ(editorType.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorType.diagnostics().front()).value(),
             std::string("editor.property.type-mismatch"));

    const double nonFinite    = std::numeric_limits<double>::quiet_NaN();
    const auto   sharedFinite = eve::property_access::validatePropertyValue(presentationNumber, eve::Value(nonFinite));
    const auto   editorFinite = validatePropertyValue(editorNumber, EditorValue(nonFinite));
    CHECK(!sharedFinite.ok());
    CHECK(!editorFinite.ok());
    CHECK_EQ(eve::property_access::writeRule(sharedFinite), std::string("property_access.property.finite"));
    REQUIRE_EQ(editorFinite.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorFinite.diagnostics().front()).value(),
             std::string("editor.property.finite"));

    const auto sharedRange = eve::property_access::validatePropertyValue(presentationNumber, eve::Value(11.0));
    const auto editorRange = validatePropertyValue(editorNumber, EditorValue(11.0));
    CHECK(!sharedRange.ok());
    CHECK(!editorRange.ok());
    CHECK_EQ(eve::property_access::writeRule(sharedRange), std::string("property_access.property.maximum"));
    REQUIRE_EQ(editorRange.diagnostics().size(), static_cast<std::size_t>(1));
    CHECK_EQ(eve::editing::diagnosticRule(editorRange.diagnostics().front()).value(),
             std::string("editor.property.above-maximum"));

    const PropertyDescriptor                       editorEnum       = editorEnumProperty();
    const eve::property_access::PropertyDescriptor presentationEnum = toPresentationDescriptor(editorEnum);
    const auto sharedEnum       = eve::property_access::validatePropertyValue(presentationEnum, eve::Value("sprint"));
    const auto editorEnumResult = validatePropertyValue(editorEnum, EditorValue("sprint"));
    CHECK(!sharedEnum.ok());
    CHECK(!editorEnumResult.ok());
    CHECK_EQ(eve::property_access::writeRule(sharedEnum), std::string("property_access.property.choice"));
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
    CHECK(!rejected.ok());
    CHECK_EQ(eve::property_access::writeRule(rejected), std::string("editor.property.intent"));
    CHECK(!sinkCalled);

    const auto accepted = model.write("speed", eve::Value(8.0));
    CHECK(accepted.ok());
    CHECK(sinkCalled);
}

TEST_CASE("property.validation_dynamic_model_uses_shared_finite_rule") {
    const PropertyDescriptor             editorNumber = editorNumberProperty();
    eve::property_access::PropertySchema schema;
    schema.properties.push_back(toPresentationDescriptor(editorNumber));
    TestPropertyAccess model(std::move(schema));

    const auto rejected = model.write("speed", eve::Value(std::numeric_limits<double>::infinity()));
    CHECK(!rejected.ok());
    CHECK_EQ(eve::property_access::writeRule(rejected), std::string("property_access.property.finite"));
}

TEST_CASE("property.validation_transform_descriptor_preserves_editor_object_semantics") {
    PropertyDescriptor transform;
    transform.path = PropertyPath("transform");
    transform.type = PropertyType::Transform;

    const eve::property_access::PropertyDescriptor shared = toPresentationDescriptor(transform);
    CHECK_EQ(static_cast<int>(shared.kind), static_cast<int>(eve::property_access::PropertyKind::Struct));
    CHECK(eve::property_access::validatePropertyValue(shared, eve::Value(eve::Value::Object{})).ok());
}
}  // namespace test_property_validation_parity
#endif
