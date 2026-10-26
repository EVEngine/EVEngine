// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "local-format",
  "origins": [
    "editor_level.cpp",
    "editor_tilemap_formats.cpp",
    "property_validation_parity.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// The three origins are independently selected by test_levels.py profiles.
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_level)
#include "PrivateTestScope.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"

namespace test_editor_level {
using namespace eve::level_editing;

TEST_CASE("editor.level.spec.documentAndBuiltInFormats") {
    LevelDocument source(4, 3, 16.f, 24.f);
    source.setOrientation("hexagonal");
    const int ground = source.addTileLayer("Ground");
    REQUIRE(ground == 0);
    source.getTileLayer(ground)->get().setGid(2, 1, 17);
    const int actors = source.addObjectLayer("Actors");
    const int hero   = source.addObject(actors, "spawn", 32.f, 24.f);
    REQUIRE(hero == 0);
    auto object = source.object(actors, hero);
    REQUIRE(object.has_value());
    object->get().name               = "Player";
    object->get().properties["team"] = "blue";
    source.setProperty("music", "dungeon");

    LevelFormatRegistry formats;
    REQUIRE(formats.getFormatCount() == 2);
    for (const auto* format : {"eve.level", "tiled.json"}) {
        auto text = formats.encode(format, source);
        REQUIRE(text.ok());
        auto reopened = formats.decode(format, text.value());
        REQUIRE(reopened.ok());
        REQUIRE(reopened.value()->getWidth() == 4);
        REQUIRE(reopened.value()->getHeight() == 3);
        REQUIRE(reopened.value()->getOrientation() == "hexagonal");
        REQUIRE(reopened.value()->getTileLayer(ground)->get().getGid(2, 1) == 17);
        REQUIRE(reopened.value()->object(actors, hero)->get().properties.at("team") == "blue");
    }
    auto native = formats.encode("eve.level", source);
    REQUIRE(native.ok());
    REQUIRE(formats.detect("room.level.json", native.value()) == "eve.level");
    auto tiled = formats.encode("tiled.json", source);
    REQUIRE(tiled.ok());
    REQUIRE(formats.detect("room.tmj", tiled.value()) == "tiled.json");
    REQUIRE(!formats.decode("missing", native.value()).ok());
}
}  // namespace test_editor_level
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_tilemap_formats)
#include "PrivateTestScope.h"
#include "common/Json.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"

#include <cstdint>
#include <string>

namespace test_editor_tilemap_formats_unit {
using namespace eve::level_editing;

TEST_CASE("editor.tilemap.spec.losslessFormatBoundary") {
    LevelFormatRegistry formats;
    auto*               formatsView = &formats;
    SUB_CASE("Tiled flags, object identity, animation, and extensions survive Tiled to native to Tiled") {
        auto imported = formatsView->decode("tiled.json", R"({
          "type":"map","width":1,"height":1,"tilewidth":16,"tileheight":16,
          "orientation":"hexagonal","staggeraxis":"x","staggerindex":"even","hexsidelength":8,
          "tilesets":[{"firstgid":1,"source":"tiles/ground.tsj","tiles":[{"id":0,"animation":[{"tileid":0,"duration":100}]}]}],
          "properties":[{"name":"speed","type":"float","value":1.5},{"name":"enabled","type":"bool","value":true}],
          "layers":[{"id":7,"type":"tilelayer","data":[2147483649]},
                    {"id":9,"type":"objectgroup","objects":[{"id":42,"name":"gate","class":"door","x":1,"y":2,
                    "polygon":[{"x":0,"y":0},{"x":4,"y":0},{"x":4,"y":4}]}]}]
        })");
        REQUIRE(imported.ok());
        auto native = formatsView->encode("eve.level", *imported.value());
        REQUIRE(native.ok());
        auto restored = formatsView->decode("eve.level", native.value());
        REQUIRE(restored.ok());
        auto tiled = formatsView->encode("tiled.json", *restored.value());
        REQUIRE(tiled.ok());
        auto parsed = eve::json::Document::parse(tiled.value());
        REQUIRE(parsed.valid());
        auto root = parsed.root();
        REQUIRE(root.getString("orientation") == "hexagonal");
        REQUIRE(root.getString("staggeraxis") == "x");
        REQUIRE(root.get("layers").at(0).get("data").at(0).asDouble() == 2147483649.0);
        REQUIRE(root.get("layers").at(1).get("objects").at(0).getInt("id") == 42);
        REQUIRE(root.get("layers").at(1).get("objects").at(0).get("polygon").size() == 3);
        REQUIRE(root.get("tilesets").at(0).get("tiles").at(0).get("animation").at(0).getInt("duration") == 100);
        REQUIRE(root.get("properties").at(0).get("value").asDouble() == 1.5);
        REQUIRE(root.get("properties").at(1).get("value").asBool());
        auto again = formatsView->encode("tiled.json", *restored.value());
        REQUIRE(again.ok());
        REQUIRE(again.value() == tiled.value());
    };
    SUB_CASE("orientation and tile dimensions cross both codecs") {
        for (const auto* orientation : {"isometric", "staggered", "hexagonal"}) {
            LevelDocument source(3, 2, 64.f, 32.f);
            source.setOrientation(orientation);
            source.addTileLayer("ground");
            auto tiled = formatsView->encode("tiled.json", source);
            REQUIRE(tiled.ok());
            auto imported = formatsView->decode("tiled.json", tiled.value());
            REQUIRE(imported.ok());
            auto native = formatsView->encode("eve.level", *imported.value());
            REQUIRE(native.ok());
            auto reopened = formatsView->decode("eve.level", native.value());
            REQUIRE(reopened.ok());
            REQUIRE(reopened.value()->getOrientation() == orientation);
            REQUIRE(reopened.value()->getTileWidth() == 64.f);
            REQUIRE(reopened.value()->getTileHeight() == 32.f);
        }
    };
    SUB_CASE("native lock and id survive restore while newly allocated ids do not collide") {
        auto restored = formatsView->decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
          "layers":[{"id":"layer-2","type":"objectgroup","locked":true,
                     "objects":[{"id":"object-4","type":"spawn"}]}]})");
        REQUIRE(restored.ok());
        REQUIRE(restored.value()->layer(0)->get().locked);
        REQUIRE(restored.value()->object(0, 0)->get().id == "object-4");
        const int newLayer = restored.value()->addObjectLayer("new");
        REQUIRE(restored.value()->layer(newLayer)->get().id != "layer-2");
        const int newObject = restored.value()->addObject(0, "spawn", 1, 1);
        REQUIRE(restored.value()->object(0, newObject)->get().id != "object-4");
    };
}

TEST_CASE("editor.tilemap.spec.invalidInputsAndMigration") {
    LevelFormatRegistry formats;
    auto*               formatsView = &formats;
    SUB_CASE("unsupported Tiled layer storage fails rather than losing data") {
        for (const auto* layer : {R"({"type":"group","layers":[{"type":"tilelayer","data":[1]}]})",
                                  R"({"type":"imagelayer","image":"background.png"})",
                                  R"({"type":"tilelayer","encoding":"base64","data":"AQAAAA=="})",
                                  R"({"type":"tilelayer","chunks":[{"x":0,"y":0,"width":1,"height":1,"data":[1]}]})"}) {
            const std::string text = std::string(R"({"type":"map","width":1,"height":1,"layers":[)") + layer + "]}";
            REQUIRE(!formatsView->decode("tiled.json", text).ok());
        }
    };
    SUB_CASE("cell arrays reject non-integral, out-of-range, and dimension-mismatched values") {
        for (const auto* cells : {"[]", "[1,2]", "[-1]", "[4294967296]", "[1.5]", "[\"1\"]"}) {
            const std::string text =
                std::string(R"({"type":"map","width":1,"height":1,"layers":[{"type":"tilelayer","data":)") + cells +
                "}]}";
            REQUIRE(!formatsView->decode("tiled.json", text).ok());
        }
        REQUIRE(!formatsView
                     ->decode("eve.level", R"({"format":"eve.level","version":999,"width":1,"height":1,"layers":[]})")
                     .ok());
    };
    SUB_CASE("legacy signed GID is imported as its unsigned bit pattern and written unsigned") {
        auto restored = formatsView->decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
          "layers":[{"id":"layer-1","type":"tilelayer","data":[-2147483647]}]})");
        REQUIRE(restored.ok());
        REQUIRE(std::uint32_t(restored.value()->getTileLayer(0)->get().getGid(0, 0)) == 0x80000001u);
        auto native = formatsView->encode("eve.level", *restored.value());
        REQUIRE(native.ok());
        auto json = eve::json::Document::parse(native.value());
        REQUIRE(json.valid());
        REQUIRE(json.root().get("layers").at(0).get("data").at(0).asDouble() == 2147483649.0);
    };
}

TEST_CASE("editor.tilemap.spec.paintUndoRedoAndSerialization") {
    LevelFormatRegistry formats;
    auto                imported = formats.decode("tiled.json", R"({"type":"map","width":1,"height":1,
      "tilesets":[{"firstgid":1,"source":"ground.tsj"}],
      "layers":[{"id":1,"type":"tilelayer","data":[2147483649]}]})");
    REQUIRE(imported.ok());
    auto& buffer = imported.value()->getTileLayer(0)->get();
    Brush brush;
    brush.setSize(1);
    brush.setTile(2);
    REQUIRE(brush.paintAt(&buffer, 0, 0) == 1);
    REQUIRE(buffer.getGid(0, 0) == 2);
    REQUIRE(brush.getChangeCount() == 1);
    REQUIRE(std::uint32_t(brush.getChangeOldGid(0)) == 0x80000001u);
    EditorHistory history;
    history.beginGroup("paint");
    history.recordTile(brush.getChangeX(0), brush.getChangeY(0), brush.getChangeOldGid(0), brush.getChangeNewGid(0));
    history.endGroup();
    REQUIRE(history.canUndo());
    REQUIRE(history.undoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    REQUIRE(std::uint32_t(buffer.getGid(0, 0)) == 0x80000001u);
    auto saved = formats.encode("tiled.json", *imported.value());
    REQUIRE(saved.ok());
    auto reopened = formats.decode("tiled.json", saved.value());
    REQUIRE(reopened.ok());
    REQUIRE(std::uint32_t(reopened.value()->getTileLayer(0)->get().getGid(0, 0)) == 0x80000001u);
    REQUIRE(history.redoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    REQUIRE(buffer.getGid(0, 0) == 2);
}
}  // namespace test_editor_tilemap_formats_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_property_validation_parity)
#include "PrivateTestScope.h"
#include "TestPropertyAccess.h"
#include "editor/EditorPropertyModel.h"

#include <limits>
#include <utility>

namespace test_property_validation_parity {
using namespace eve::editor;

namespace {
PropertyDescriptor numberProperty() {
    PropertyDescriptor descriptor;
    descriptor.path            = PropertyPath("speed");
    descriptor.type            = PropertyType::Float;
    descriptor.numeric.minimum = 0.0;
    descriptor.numeric.maximum = 10.0;
    return descriptor;
}
PropertyDescriptor enumProperty() {
    PropertyDescriptor descriptor;
    descriptor.path      = PropertyPath("mode");
    descriptor.type      = PropertyType::Enum;
    descriptor.enumItems = {"walk", "run"};
    return descriptor;
}
class Provider final : public IPropertyProvider {
public:
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision{});
    }
    PropertySchema schema(const SelectionSnapshot&) const override {
        PropertySchema result;
        result.typeId = "test.property-validation";
        result.properties.push_back(numberProperty());
        return result;
    }
    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath& path) const override {
        if (path == PropertyPath("speed")) return {PropertyReadState::Value, EditorValue(4.0), {}};
        return {};
    }
    Result<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                    PropertySetMode) const override {
        return eve::editing::applied<DomainOperation>({});
    }
    Result<DomainOperation> makeReset(const SelectionSnapshot&, const PropertyPath&) const override {
        return eve::editing::applied<DomainOperation>({});
    }
};
}  // namespace

TEST_CASE("property.validation.spec.sharedAndEditorRules") {
    SUB_CASE("type, finite, range, and enum inputs select corresponding diagnostics") {
        const auto number = numberProperty();
        const auto shared = toPresentationDescriptor(number);
        auto       type   = eve::property_access::validatePropertyValue(shared, eve::Value("fast"));
        REQUIRE(!type.ok());
        REQUIRE(eve::property_access::writeRule(type) == "property_access.property.type");
        auto finite =
            eve::property_access::validatePropertyValue(shared, eve::Value(std::numeric_limits<double>::quiet_NaN()));
        REQUIRE(!finite.ok());
        REQUIRE(eve::property_access::writeRule(finite) == "property_access.property.finite");
        auto range = eve::property_access::validatePropertyValue(shared, eve::Value(11.0));
        REQUIRE(!range.ok());
        REQUIRE(eve::property_access::writeRule(range) == "property_access.property.maximum");
        auto editorType = validatePropertyValue(number, EditorValue("fast"));
        REQUIRE(!editorType.ok());
        REQUIRE(editorType.diagnostics().size() == 1);
        REQUIRE(eve::editing::diagnosticRule(editorType.diagnostics().front()).value() ==
                "editor.property.type-mismatch");
        auto editorFinite = validatePropertyValue(number, EditorValue(std::numeric_limits<double>::infinity()));
        REQUIRE(!editorFinite.ok());
        REQUIRE(editorFinite.diagnostics().size() == 1);
        REQUIRE(eve::editing::diagnosticRule(editorFinite.diagnostics().front()).value() == "editor.property.finite");
        auto editorRange = validatePropertyValue(number, EditorValue(11.0));
        REQUIRE(!editorRange.ok());
        REQUIRE(editorRange.diagnostics().size() == 1);
        REQUIRE(eve::editing::diagnosticRule(editorRange.diagnostics().front()).value() ==
                "editor.property.above-maximum");
        auto choice       = enumProperty();
        auto editorChoice = validatePropertyValue(choice, EditorValue("sprint"));
        REQUIRE(!editorChoice.ok());
        REQUIRE(editorChoice.diagnostics().size() == 1);
        REQUIRE(eve::editing::diagnosticRule(editorChoice.diagnostics().front()).value() ==
                "editor.property.invalid-enum");
        REQUIRE(eve::property_access::writeRule(eve::property_access::validatePropertyValue(
                    toPresentationDescriptor(choice), eve::Value("sprint"))) == "property_access.property.choice");
        REQUIRE(validatePropertyValue(number, EditorValue(8.0)).ok());
    };
    SUB_CASE("model rejects invalid write before its command sink and sends valid intent") {
        Provider            provider;
        auto                schema = provider.schema({});
        EditorPropertyModel model(schema, {}, &provider);
        int                 calls = 0;
        model.setEditSink([&](const PropertyEditIntent&) {
            ++calls;
            return eve::editing::applied<void>();
        });
        REQUIRE(!model.write("speed", eve::Value(11.0)).ok());
        REQUIRE(calls == 0);
        REQUIRE(model.write("speed", eve::Value(8.0)).ok());
        REQUIRE(calls == 1);
    };
    SUB_CASE("dynamic presentation model uses the same finite rule and transform maps to a struct") {
        eve::property_access::PropertySchema schema;
        schema.properties.push_back(toPresentationDescriptor(numberProperty()));
        TestPropertyAccess model(std::move(schema));
        auto               rejected = model.write("speed", eve::Value(std::numeric_limits<double>::infinity()));
        REQUIRE(!rejected.ok());
        REQUIRE(eve::property_access::writeRule(rejected) == "property_access.property.finite");
        PropertyDescriptor transform;
        transform.path    = PropertyPath("transform");
        transform.type    = PropertyType::Transform;
        const auto mapped = toPresentationDescriptor(transform);
        REQUIRE(mapped.kind == eve::property_access::PropertyKind::Struct);
        REQUIRE(eve::property_access::validatePropertyValue(mapped, eve::Value(eve::Value::Object{})).ok());
    };
}
}  // namespace test_property_validation_parity
#endif
