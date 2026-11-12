// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"spawn_editor","default":"module-composition","cases":{"platform-file":[
"Spawn asset disk save failure preserves canonical source and previous file",
"Spawn runtime export writes canonical JSON through the actual atomic filesystem"],"render-offscreen":[
"Spawn host GPU background clips real tiles and restores the prior canvas"],"render-window":[
"Spawn native window presents real panels background and SDL editing events"]}}
*/
// clang-format on
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <typeindex>
#include "Fixtures.h"
#include "common/ServiceInterfaces.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorDocumentService.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "filesystem/physfs/Filesystem.h"
#include "font/FontData.h"
#include "graphics/Canvas.h"
#include "graphics/Font.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/RenderSystem.h"
#include "map/Map.h"
#include "map/TileRenderViewport.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editing/SpawnTemplateDocument.h"
#include "map/spawn/editor/PreparedSpawnExport.h"
#include "map/spawn/editor/SpawnAssetDocument.h"
#include "map/spawn/editor/SpawnAssetSavePanel.h"
#include "map/spawn/editor/SpawnDiagnosticsPanel.h"
#include "map/spawn/editor/SpawnEditorApplication.h"
#include "map/spawn/editor/SpawnEditorHost.h"
#include "map/spawn/editor/SpawnEditorWorkspace.h"
#include "map/spawn/editor/SpawnExportPanel.h"
#include "map/spawn/editor/SpawnForm.h"
#include "map/spawn/editor/SpawnFormPanel.h"
#include "map/spawn/editor/SpawnMapViewport.h"
#include "map/spawn/editor/SpawnRegionTool.h"
#include "map/spawn/editor/TiledSpawnImport.h"
#include "property_access/PropertyAccess.h"
#include "spawn/SpawnDefinition.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnMap.h"
#include "ui/Component.h"
#include "ui/UI.h"
#include "window/sdl/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_spawn_editor {
namespace {
// Fixed fixture geometry: center (40,40), radius 32 logical pixels. Each octant
// must contain an opaque stroke sample within one pixel of the expected circle.
// Center/exterior probes also exclude a filled patch masquerading as a contour.
struct CirclePreviewCoverage {
    float r;
    float offContourRed;
};
template <class ReadRed>
CirclePreviewCoverage circlePreviewCoverage(ReadRed readRed) {
    std::array<float, 8> octants{};
    for (int y = 7; y <= 73; ++y)
        for (int x = 7; x <= 73; ++x) {
            const double dx = x + 0.5 - 40.0, dy = y + 0.5 - 40.0;
            if (std::abs(std::hypot(dx, dy) - 32.0) > 1.0) continue;
            const double angle = std::atan2(dy, dx) + 3.141592653589793;
            const auto sector = std::min(std::size_t(7), static_cast<std::size_t>(angle * 8 / (2 * 3.141592653589793)));
            octants[sector]   = std::max(octants[sector], readRed(x, y));
        }
    float off = 0;
    for (const auto& p : std::array<std::array<int, 2>, 5>{{{40, 40}, {4, 40}, {76, 40}, {40, 4}, {40, 76}}})
        off = std::max(off, readRed(p[0], p[1]));
    return {*std::min_element(octants.begin(), octants.end()), off};
}
struct Content final : eve::spawn::ITemplateResolver, eve::spawn::IUnitDefinitionResolver {
    std::function<void()> onValidate;
    eve::Result<void>     validate(const eve::DefinitionRef&) const override {
        if (onValidate) onValidate();
        return eve::Result<void>::success();
    }
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
        return eve::spawn::compileTemplate(source(), *this);
    }
    static eve::Value source() {
        return eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"camp",
          "content":{"kind":"fixed","members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}]},
          "schedule":{"kind":"once"},"limits":{"alive":1,"total":{"kind":"unlimited"},"budget_group":"hostiles"},
          "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":2},
          "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
            .expect("template");
    }
};
eve::Value mapSource() {
    return eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
      "budgets":[{"id":"hostiles","alive_limit":10}],"instances":[{"id":"north",
      "template":{"type":"spawn.template","id":"camp"},"region":{"kind":"circle","space":"world2d","center":[0,0],"radius":3},
      "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})")
        .expect("sidecar");
}
eve::Value tiledDefaults() {
    auto source = mapSource();
    auto value  = source.find("instances")->at(0);
    value.getIf<eve::Value::Object>()->erase("id");
    value.getIf<eve::Value::Object>()->erase("region");
    return value;
}
struct EditorAssetFiles final : eve::service::IFileSystem {
    int         reads   = 0;
    std::string sidecar = mapSource().toJson().expect("sidecar");
    bool        readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        ++reads;
        std::string text;
        if (path == "forest.json")
            text = R"({"width":1,"height":1,"tilewidth":32,"tileheight":32,"data":[1]})";
        else if (path == "forest.spawns.json")
            text = sidecar;
        else
            return false;
        out.assign(text.begin(), text.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string&) override { return false; }
};
}  // namespace
TEST_CASE("Spawn form owns actual model backend authority and document across writes undo and redo") {
    auto                   content = std::make_shared<Content>();
    std::weak_ptr<Content> witness = content;
    auto                   doc =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("forest"), mapSource(), content, content)
            .expect("document");
    auto form = eve::spawn_editor::SpawnForm::create(std::move(doc)).expect("form");
    REQUIRE(!doc);
    content.reset();
    REQUIRE(!witness.expired());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(1));
    auto original = form->view().read("$source");
    REQUIRE(original.has_value());
    auto changed = *original;
    changed.find("instances")->at(0).find("region")->set("radius", 9);
    form->view().write("$source", changed).expect("real model write and commit");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(2));
    auto authoritative = form->captureSource().expect("authoritative document source");
    REQUIRE_EQ(authoritative.revision, form->sourceRevision());
    REQUIRE_EQ(authoritative.source.toJson().expect("document snapshot"), changed.toJson().expect("committed value"));
    authoritative.source.set("map", "detached-only");
    REQUIRE_EQ(form->captureSource().expect("new independent snapshot").source.toJson().expect("source"),
               changed.toJson().expect("original"));

    REQUIRE_EQ(form->view().read("$source")->toJson().expect("new snapshot"), changed.toJson().expect("candidate"));
    form->undo().expect("actual model undo");
    REQUIRE_EQ(form->view().read("$source")->toJson().expect("undo snapshot"), original->toJson().expect("original"));
    form->redo().expect("actual model redo");
    REQUIRE_EQ(form->view().read("$source")->toJson().expect("redo snapshot"), changed.toJson().expect("candidate"));
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    changed.find("instances")->at(0).find("region")->set("radius", -1);
    REQUIRE(!form->view().write("$source", changed).ok());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    REQUIRE(!form->view().write("version", eve::Value(2)).ok());
    form.reset();
    REQUIRE(witness.expired());
}
TEST_CASE("Tiled spawn import preserves source identities offsets and canonical undo history") {
    using namespace eve;
    using namespace eve::spawn_editor;
    auto content = std::make_shared<Content>();
    auto document =
        spawn_editing::SpawnDocumentTarget::create(editing::TargetId("forest"), mapSource(), content, content)
            .expect("document");
    std::shared_ptr<SpawnForm> form  = SpawnForm::create(std::move(document)).expect("form");
    const auto                 asset = *AssetGuid::parse("01900000-0000-7000-8000-000000000401");
    auto tiled = Value::fromJson(R"({"type":"map","version":"1.10","orientation":"orthogonal","infinite":false,
      "layers":[{"type":"group","offsetx":10,"offsety":20,"layers":[{"type":"objectgroup","offsetx":3,"offsety":4,
      "objects":[{"id":7,"type":"eve.spawn","point":true,"x":2,"y":3,
      "properties":[]},
      {"id":8,"class":"eve.spawn","rotation":90,"x":4,"y":5,"width":2,"height":4,
      "properties":[]},
      {"id":9,"type":"eve.spawn","ellipse":true,"x":0,"y":0,"width":6,"height":6,
      "properties":[]}]}]}]})")
                     .expect("Tiled");
    auto unrelated = Value::fromJson(R"({"type":"objectgroup","parallaxx":0.5,
        "objects":[{"id":77,"type":"npc","gid":1,"x":0,"y":0}]})")
                         .expect("unrelated layer");
    tiled.find("layers")->getIf<Value::Array>()->push_back(std::move(unrelated));
    const map_spawn::PixelTransform transform{{100, -20}, 2};
    auto plan = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content)
                    .expect("import plan");
    REQUIRE_EQ(form->sourceRevision().value(), 1);
    REQUIRE(!form->history().canUndo);
    REQUIRE_EQ(plan.importedIds().size(), 3);
    const auto stable = std::string("tiled.") + asset.format() + ".7";
    CHECK_EQ(plan.importedIds()[0], stable);
    const auto& instances = *plan.source().find("instances")->getIf<Value::Array>();
    REQUIRE_EQ(instances.size(), 4);
    CHECK_EQ(instances[0].find("id")->asString(), "north");
    const auto& point =
        *instances[1].find("region")->find("points")->getIf<Value::Array>()->front().getIf<Value::Array>();
    CHECK_EQ(point[0].asDouble(), 130.);
    CHECK_EQ(point[1].asDouble(), 34.);
    const auto& rectangle = *instances[2].find("region")->find("vertices")->getIf<Value::Array>();
    CHECK(std::abs(rectangle[2].getIf<Value::Array>()->at(0).asDouble() - 126.) < 1e-12);
    CHECK(std::abs(rectangle[2].getIf<Value::Array>()->at(1).asDouble() - 42.) < 1e-12);
    CHECK_EQ(instances[3].find("region")->find("radius")->asDouble(), 6.);
    struct PreviewOverlay final : editor::IEditorOverlay {
        bool                  amber   = true;
        unsigned              circles = 0, lines = 0;
        editor::OverlayPoint  first{};
        std::function<void()> callback;
        void                  line(const editor::OverlayPoint&, const editor::OverlayPoint&,
                                   const editor::OverlayStyle& style) override {
            amber = amber && style.color == 0xffffb040U;
            ++lines;
        }
        void circle(const editor::OverlayPoint& at, float, const editor::OverlayStyle& style) override {
            amber = amber && style.color == 0xffffb040U;
            if (!circles++) first = at;
            if (callback) callback();
        }
        void rectangle(const editor::OverlayPoint&, const editor::OverlayPoint&, const editor::OverlayStyle&) override {
        }
        void text(const editor::OverlayPoint&, const std::string&, const editor::OverlayStyle&) override {}
    } overlay;
    const MapViewportFrame frame{10, 20, 1, 2, 2};
    auto                   invalidFrame = frame;
    invalidFrame.zoom                   = 0;
    REQUIRE(!plan.drawPreview(invalidFrame, overlay).ok());
    CHECK_EQ(overlay.circles, 0u);
    overlay.callback = [&] { CHECK(!plan.commit().ok()); };
    plan.drawPreview(frame, overlay).expect("detached imported geometry");
    CHECK_EQ(overlay.circles, 2u);
    CHECK_EQ(overlay.lines, 4u);
    CHECK(overlay.amber);
    CHECK_EQ(overlay.first.x, 38.f);
    CHECK_EQ(overlay.first.y, 70.f);
    CHECK_EQ(form->sourceRevision().value(), 1);
    CHECK(!form->history().canUndo);
    overlay.callback = {};
    plan.commit().expect("canonical transaction");
    REQUIRE(!plan.commit().ok());
    REQUIRE(form->history().canUndo);
    CHECK_EQ(form->captureMap().expect("compiled source").map->instances().size(), 4);
    form->undo().expect("undo import");
    CHECK(form->captureSource().expect("original").source == mapSource());
    form->redo().expect("redo import");
    auto configured = form->captureSource().expect("sidecar settings").source;
    configured.find("instances")->at(1).set("activation", Value::object({{"kind", "event"}, {"event", "retained"}}));
    form->view().write("$source", configured).expect("explicit sidecar activation edit");
    auto* objects = tiled.find("layers")->at(0).find("layers")->at(0).find("objects")->getIf<Value::Array>();
    (*objects)[0].set("name", "renamed without new identity");
    (*objects)[0].set("x", 3);
    std::reverse(objects->begin(), objects->end());
    auto update = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content)
                      .expect("reimport");
    CHECK_EQ(update.importedIds()[0], stable);
    REQUIRE_EQ(update.source().find("instances")->arraySize(), 4);
    update.commit().expect("upsert");
    auto updated = form->captureSource().expect("updated");
    CHECK_EQ(updated.source.find("instances")->at(1).find("id")->asString(), stable);
    CHECK_EQ(updated.source.find("instances")->at(1).find("activation")->find("event")->asString(), "retained");
    CHECK_EQ(updated.source.find("instances")->at(1).find("region")->find("points")->at(0).at(0).asDouble(), 132.);
    auto stale = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content)
                     .expect("stale proposal");
    form->undo().expect("advance form revision");
    REQUIRE(!stale.commit().ok());
    auto expired = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content)
                       .expect("weak proposal");
    form.reset();
    REQUIRE(!expired.commit().ok());
    REQUIRE_EQ(expired.importedIds().size(), 3);
    overlay.circles = 0;
    expired.drawPreview(frame, overlay).expect("preview survives form retirement");
    CHECK_EQ(overlay.circles, 2u);
    std::optional<PreparedTiledSpawnImport> retiring(std::move(expired));
    overlay.callback = [&] { retiring.reset(); };
    retiring->drawPreview(frame, overlay).expect("preview pins facts across outer retirement");
    CHECK(!retiring);
}

TEST_CASE("Tiled import rejects invalid geometry atomically and pins plans across observer retirement") {
    using namespace eve;
    using namespace eve::spawn_editor;
    auto                       content = std::make_shared<Content>();
    std::shared_ptr<SpawnForm> form    = SpawnForm::create(spawn_editing::SpawnDocumentTarget::create(
                                                            editing::TargetId("forest"), mapSource(), content, content)
                                                               .expect("document"))
                                          .expect("form");
    const auto                      asset = *AssetGuid::parse("01900000-0000-7000-8000-000000000402");
    const map_spawn::PixelTransform transform{{0, 0}, 1};
    auto tiled = Value::fromJson(R"({"type":"map","version":"1.10","orientation":"orthogonal",
      "layers":[{"type":"objectgroup","objects":[{"id":1,"type":"eve.spawn","point":true,"x":1,"y":2,
      "properties":[]}]}]})")
                     .expect("Tiled");
    for (int invalid = 0; invalid < 12; ++invalid) {
        auto  input  = tiled;
        auto* object = &input.find("layers")->at(0).find("objects")->at(0);
        if (invalid == 0) input.set("version", "2.0");
        if (invalid == 1) object->set("id", 0);
        if (invalid == 2) object->set("x", std::numeric_limits<double>::infinity());
        if (invalid == 3) object->set("capsule", true);
        if (invalid == 4) input.find("layers")->at(0).find("objects")->getIf<Value::Array>()->push_back(*object);
        if (invalid == 5)
            object->find("properties")
                ->getIf<Value::Array>()
                ->push_back(Value::object({{"name", "eve.spawn.unknown"}, {"type", "string"}, {"value", "x"}}));
        if (invalid == 6) {
            object->set("point", false);
            object->set("ellipse", true);
            object->set("width", 2);
            object->set("height", 3);
        }
        if (invalid == 7) {
            object->set("point", false);
            object->set("polygon",
                        Value::Array{Value::object({{"x", 0}, {"y", 0}}), Value::object({{"x", 2}, {"y", 2}}),
                                     Value::object({{"x", 0}, {"y", 2}}), Value::object({{"x", 2}, {"y", 0}})});
        }
        if (invalid == 8) object->set("unknown_shape", true);
        if (invalid == 9)
            input.find("layers")->at(0).find("objects")->getIf<Value::Array>()->push_back(
                Value::object({{"id", 1}, {"type", "npc"}}));
        if (invalid == 10 || invalid == 11)
            object->find("properties")
                ->getIf<Value::Array>()
                ->push_back(Value::object({{"name", invalid == 10 ? "eve.spawn.template" : "eve.spawn.activation"},
                                           {"type", "string"},
                                           {"value", invalid == 10 ? "camp" : "{\"kind\":\"map_loaded\"}"}}));
        REQUIRE(!PreparedTiledSpawnImport::prepare(form, std::move(input), asset, transform, tiledDefaults(), content,
                                                   content)
                     .ok());
        CHECK_EQ(form->sourceRevision().value(), 1);
        CHECK(!form->history().canUndo);
    }
    REQUIRE(
        !PreparedTiledSpawnImport::prepare(form, tiled, asset, {{0, 0}, 0}, tiledDefaults(), content, content).ok());
    REQUIRE(
        !PreparedTiledSpawnImport::prepare(form, tiled, AssetGuid::nil(), transform, tiledDefaults(), content, content)
             .ok());
    REQUIRE(!PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), {}, content).ok());
    REQUIRE(!PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, Value{}, content, content).ok());
    content->onValidate = [] { throw std::runtime_error("resolver unavailable"); };
    REQUIRE(!PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content).ok());
    content->onValidate = {};
    std::optional<PreparedTiledSpawnImport> active;
    active.emplace(PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, tiledDefaults(), content, content)
                       .expect("proposal"));
    bool rejectedReentry = false;
    auto subscription    = form->view().subscribe([&](const auto&) {
        if (!active) return;
        rejectedReentry = !active->commit().ok();
        active.reset();
    });
    active->commit().expect("state survives outer plan retirement");
    CHECK(!active.has_value());
    CHECK(rejectedReentry);
    CHECK_EQ(form->captureMap().expect("published").map->instances().size(), 2);
    form->undo().expect("canonical history retained");
    CHECK(form->captureSource().expect("undo result").source == mapSource());
}

TEST_CASE("Tiled instance references update geometry while sidecar configuration remains authoritative") {
    using namespace eve;
    using namespace eve::spawn_editor;
    auto                       content = std::make_shared<Content>();
    std::shared_ptr<SpawnForm> form    = SpawnForm::create(spawn_editing::SpawnDocumentTarget::create(
                                                            editing::TargetId("forest"), mapSource(), content, content)
                                                               .expect("document"))
                                          .expect("form");
    const auto                      asset = *AssetGuid::parse("01900000-0000-7000-8000-000000000403");
    const map_spawn::PixelTransform transform{{0, 0}, 1};
    auto tiled = Value::fromJson(R"({"type":"map","version":"1.10","orientation":"orthogonal",
      "layers":[{"type":"objectgroup","objects":[{"id":1,"type":"eve.spawn","point":true,"x":8,"y":9,
      "properties":[{"name":"eve.spawn.instance","type":"string","value":"north"}]}]}]})")
                     .expect("Tiled reference");
    auto defaults = tiledDefaults();
    defaults.set("activation", Value::object({{"kind", "event"}, {"event", "new-only"}}));
    auto plan = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, defaults, content, content)
                    .expect("existing target");
    REQUIRE_EQ(plan.importedIds().size(), 1);
    CHECK_EQ(plan.importedIds()[0], "north");
    const auto& row     = plan.source().find("instances")->at(0);
    auto        initial = mapSource();
    CHECK(*row.find("template") == *initial.find("instances")->at(0).find("template"));
    CHECK_EQ(row.find("activation")->find("kind")->asString(), "map_loaded");
    plan.commit().expect("geometry-only update");
    CHECK_EQ(form->captureMap().expect("one source").map->instances().size(), 1);
    auto again = PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, Value{}, content, content)
                     .expect("reference needs no creation defaults");
    again.commit().expect("same reference");
    auto missing = tiled;
    missing.find("layers")->at(0).find("objects")->at(0).find("properties")->at(0).set("value", "missing");
    REQUIRE(!PreparedTiledSpawnImport::prepare(form, missing, asset, transform, Value{}, content, content).ok());
    auto* objects   = tiled.find("layers")->at(0).find("objects")->getIf<Value::Array>();
    auto  duplicate = objects->front();
    duplicate.set("id", 2);
    objects->push_back(std::move(duplicate));
    REQUIRE(!PreparedTiledSpawnImport::prepare(form, tiled, asset, transform, Value{}, content, content).ok());
    CHECK_EQ(form->captureSource().expect("failed import did not publish").source.find("instances")->arraySize(), 1);
}

TEST_CASE("Spawn template form uses structured field writes and preserves the last valid snapshot") {
    auto content = std::make_shared<Content>();
    auto doc     = eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("camp"),
                                                                           Content::source(), content)
                   .expect("document");
    auto form           = eve::spawn_editor::SpawnForm::create(std::move(doc)).expect("form");
    auto unsupportedMap = form->captureMap();
    REQUIRE(!unsupportedMap.ok());
    REQUIRE_EQ(unsupportedMap.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);

    auto placement = form->view().read("placement");
    REQUIRE(placement.has_value());
    placement->set("attempts_per_unit", 5);
    form->view().write("placement", *placement).expect("template field write");
    REQUIRE_EQ(*form->view().read("placement")->find("attempts_per_unit")->getIf<std::int64_t>(), std::int64_t(5));
    placement->set("attempts_per_unit", 0);
    REQUIRE(!form->view().write("placement", *placement).ok());
    REQUIRE(form->captureSource().ok());

    REQUIRE_EQ(*form->view().read("placement")->find("attempts_per_unit")->getIf<std::int64_t>(), std::int64_t(5));
    form->undo().expect("template undo");
    REQUIRE_EQ(*form->view().read("placement")->find("attempts_per_unit")->getIf<std::int64_t>(), std::int64_t(2));
    unsigned calls      = 0;
    content->onValidate = [&] {
        if (++calls > 1) throw std::runtime_error("commit validator unavailable");
    };
    placement->set("attempts_per_unit", 6);
    REQUIRE(!form->view().write("placement", *placement).ok());
    REQUIRE(calls > 1);
    REQUIRE(!form->captureSource().ok());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(3));
    REQUIRE_EQ(*form->view().read("placement")->find("attempts_per_unit")->getIf<std::int64_t>(), std::int64_t(2));
    content->onValidate = {};
    form->retry().expect("retry actual retained commit");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    REQUIRE_EQ(*form->view().read("placement")->find("attempts_per_unit")->getIf<std::int64_t>(), std::int64_t(6));
    calls               = 0;
    content->onValidate = [&] {
        if (++calls > 1) throw std::runtime_error("second commit failure");
    };
    placement->set("attempts_per_unit", 7);
    REQUIRE(!form->view().write("placement", *placement).ok());
    content->onValidate = {};
    form->discard().expect("discard retained unpublished operation");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    REQUIRE(!form->retry().ok());
    REQUIRE(
        !eve::spawn_editor::SpawnForm::create(std::unique_ptr<eve::spawn_editing::SpawnTemplateDocumentTarget>()).ok());
}
TEST_CASE("Spawn UI controls preserve form ownership diagnose errors and reject stale trees") {
    auto content = std::make_shared<Content>();
    auto doc =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("forest"), mapSource(), content, content)
            .expect("document");
    std::shared_ptr<eve::spawn_editor::SpawnForm> form =
        eve::spawn_editor::SpawnForm::create(std::move(doc)).expect("form");
    std::weak_ptr<eve::spawn_editor::SpawnForm> witness = form;
    auto                                        panel   = eve::spawn_editor::createSpawnFormPanel(form).expect("panel");
    panel->mountAs("spawn-form-panel-test");
    const auto mountedHost = panel->host();
    REQUIRE(bool(eve::ui::UIHost::resolve(mountedHost)));
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& id) -> eve::ui::WidgetDesc* {
        if (node.id == id) return &node;
        for (auto& child : node.children)
            if (auto* result = self(self, child, id)) return result;
        return nullptr;
    };
    auto  tree = panel->build();
    auto* map  = find(find, tree, "spawn/field/map");
    REQUIRE(map != nullptr);
    REQUIRE(bool(map->onTextChange));
    REQUIRE(!find(find, tree, "spawn/undo")->enabled);
    auto oldCallback = map->onTextChange;
    map->onTextChange("forest-renamed");
    REQUIRE_EQ(*form->view().read("map")->getIf<std::string>(), std::string("forest-renamed"));
    eve::ui::detail::flushPendingComponents();
    REQUIRE_EQ(eve::ui::UIHost::resolve(mountedHost)->get().findById("spawn/field/map")->get().valueText,
               std::string("forest-renamed"));
    oldCallback("stale-overwrite");
    REQUIRE_EQ(*form->view().read("map")->getIf<std::string>(), std::string("forest-renamed"));
    auto refreshed = panel->build();
    REQUIRE(find(find, refreshed, "spawn/diagnostic/0") != nullptr);
    REQUIRE(find(find, refreshed, "spawn/undo")->enabled);
    find(find, refreshed, "spawn/undo")->onClick();
    REQUIRE_EQ(*form->view().read("map")->getIf<std::string>(), std::string("forest"));
    auto invalidTree = panel->build();
    find(find, invalidTree, "spawn/source-json")->onTextChange("invalid JSON");
    auto errorTree = panel->build();
    REQUIRE(find(find, errorTree, "spawn/diagnostic/0") != nullptr);
    auto invalidSource = mapSource();
    invalidSource.find("instances")->at(0).find("region")->set("radius", -1);
    find(find, errorTree, "spawn/source-json")->onTextChange(invalidSource.toJson().expect("invalid candidate"));
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(3));
    auto  retainedTree = panel->build();
    auto* json         = find(find, retainedTree, "spawn/source-json");
    REQUIRE(json != nullptr);
    auto callback = json->onTextChange;
    panel.reset();
    form.reset();
    REQUIRE(!witness.expired());
    callback(mapSource().toJson().expect("safe retained callback"));
    oldCallback  = {};
    callback     = {};
    tree         = {};
    refreshed    = {};
    invalidTree  = {};
    errorTree    = {};
    retainedTree = {};
    eve::ui::UIHost::resolve(mountedHost)->get().setTree({});
    REQUIRE(witness.expired());
    REQUIRE(!eve::spawn_editor::createSpawnFormPanel({}).ok());
}
TEST_CASE("Spawn deep controls edit fractional geometry weighted members and finite waves canonically") {
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& label) -> eve::ui::WidgetDesc* {
        if (node.text == label && node.onTextChange) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, label)) return found;
        return nullptr;
    };
    auto content = std::make_shared<Content>();
    auto mapDoc =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("map"), mapSource(), content, content)
            .expect("map");
    std::shared_ptr<eve::spawn_editor::SpawnForm> mapForm =
        eve::spawn_editor::SpawnForm::create(std::move(mapDoc)).expect("form");
    auto  mapPanel = eve::spawn_editor::createSpawnFormPanel(mapForm).expect("panel");
    auto  tree     = mapPanel->build();
    auto* radius   = find(find, tree, "instances[0].region.radius");
    REQUIRE(radius != nullptr);
    auto stale = radius->onTextChange;
    radius->onTextChange("11.5");
    REQUIRE_EQ(
        *mapForm->view().read("$source")->find("instances")->at(0).find("region")->find("radius")->getIf<double>(),
        11.5);
    stale("20");
    REQUIRE_EQ(mapForm->targetRevision().value(), std::uint64_t(2));
    auto updated = mapPanel->build();
    find(find, updated, "instances[0].region.radius")->onTextChange("-1");
    REQUIRE_EQ(mapForm->targetRevision().value(), std::uint64_t(2));
    find(find, updated, "instances[0].region.radius")->onTextChange("not a number");
    REQUIRE_EQ(mapForm->targetRevision().value(), std::uint64_t(2));
    mapForm->undo().expect("deep field undo");
    auto source = Content::source();
    source.getIf<eve::Value::Object>()->erase("failure");
    source.set("content", eve::Value::fromJson(R"({"kind":"weighted","selection":"per_unit","entries":[
        {"unit":{"type":"rpg.unit","id":"wolf"},"weight":3}]})")
                              .expect("weighted"));
    source.set(
        "schedule",
        eve::Value::fromJson(
            R"({"kind":"interval","period_seconds":2,"initial_delay_seconds":0,"count_per_batch":1,"on_missed":"skip"})")
            .expect("interval"));
    source.set("extensions",
               eve::Value::fromJson(R"({"game:editor":{"a.b":1,"a_b":2,"path/~":3}})").expect("extensions"));
    auto templateDoc =
        eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("template"), source, content)
            .expect("weighted document");
    std::shared_ptr<eve::spawn_editor::SpawnForm> templateForm =
        eve::spawn_editor::SpawnForm::create(std::move(templateDoc)).expect("form");
    auto  templatePanel = eve::spawn_editor::createSpawnFormPanel(templateForm).expect("panel");
    auto  weightedTree  = templatePanel->build();
    auto* weight        = find(find, weightedTree, "content.entries[0].weight");
    REQUIRE(weight != nullptr);
    weight->onTextChange("8");
    REQUIRE_EQ(*templateForm->view()
                    .read("$source")
                    ->find("content")
                    ->find("entries")
                    ->at(0)
                    .find("weight")
                    ->getIf<std::int64_t>(),
               std::int64_t(8));
    auto  specialTree = templatePanel->build();
    auto* dot         = find(find, specialTree, "extensions.game:editor.a.b");
    auto* underscore  = find(find, specialTree, "extensions.game:editor.a_b");
    REQUIRE(dot != nullptr);
    REQUIRE(underscore != nullptr);
    REQUIRE(dot->id != underscore->id);
    dot->onTextChange("7");
    auto specialSource = templateForm->view().read("$source");
    REQUIRE_EQ(*specialSource->find("extensions")->find("game:editor")->find("a.b")->getIf<std::int64_t>(),
               std::int64_t(7));
    REQUIRE_EQ(*specialSource->find("extensions")->find("game:editor")->find("a_b")->getIf<std::int64_t>(),
               std::int64_t(2));
    auto waves = Content::source();
    waves.getIf<eve::Value::Object>()->erase("content");
    waves.find("limits")->set("alive", 5);
    waves.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
        {"id":"first","delay_seconds":0,"members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"}]})")
                              .expect("wave"));
    templateForm->view().write("$source", waves).expect("atomic wave transition");
    auto  waveTree = templatePanel->build();
    auto* count    = find(find, waveTree, "schedule.waves[0].members[0].count");
    REQUIRE(find(find, waveTree, "failure.placement_blocked.delay_seconds") != nullptr);
    REQUIRE(count != nullptr);
    count->onTextChange("2.5");
    REQUIRE_EQ(*templateForm->view()
                    .read("$source")
                    ->find("schedule")
                    ->find("waves")
                    ->at(0)
                    .find("members")
                    ->at(0)
                    .find("count")
                    ->getIf<std::int64_t>(),
               std::int64_t(1));
    count->onTextChange("2");
    REQUIRE_EQ(*templateForm->view()
                    .read("$source")
                    ->find("schedule")
                    ->find("waves")
                    ->at(0)
                    .find("members")
                    ->at(0)
                    .find("count")
                    ->getIf<std::int64_t>(),
               std::int64_t(2));
    templateForm->undo().expect("wave count undo");
    templateForm->redo().expect("wave count redo");
}
TEST_CASE("Spawn structure drafts add remove repair cancel and reject changed authoritative bases") {
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& key) -> eve::ui::WidgetDesc* {
        if (node.id == key || node.text == key) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, key)) return found;
        return nullptr;
    };
    auto units  = std::make_shared<Content>();
    auto source = Content::source();
    source.find("limits")->set("alive", 5);
    auto doc = eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("draft"), source, units)
                   .expect("document");
    std::shared_ptr<eve::spawn_editor::SpawnForm> form =
        eve::spawn_editor::SpawnForm::create(std::move(doc)).expect("form");
    auto panel = eve::spawn_editor::createSpawnFormPanel(form).expect("panel");
    auto live  = panel->build();
    find(find, live, "spawn/draft/begin")->onClick();
    auto draft = panel->build();
    REQUIRE(!find(find, draft, "spawn/undo")->enabled);
    find(find, draft, "New item JSON for content.members")
        ->onTextChange(R"({"id":"second","unit":{"type":"rpg.unit","id":"wolf"},"count":1})");
    auto staleAdd = find(find, draft, "Duplicate content.members[0]")->onClick;
    staleAdd();
    staleAdd();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(1));
    REQUIRE_EQ(form->view().read("$source")->find("content")->find("members")->getIf<eve::Value::Array>()->size(),
               std::size_t(1));
    auto rename = panel->build();
    find(find, rename, "content.members[1].id")->onTextChange("second");
    auto members = panel->build();
    find(find, members, "content.members[1].count")->onTextChange("2");
    auto ready = panel->build();
    find(find, ready, "spawn/draft/apply")->onClick();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(2));
    REQUIRE_EQ(form->view().read("$source")->find("content")->find("members")->getIf<eve::Value::Array>()->size(),
               std::size_t(2));
    form->undo().expect("whole structure undo");
    form->redo().expect("whole structure redo");
    auto second = panel->build();
    find(find, second, "spawn/draft/begin")->onClick();
    auto removeFirst = panel->build();
    find(find, removeFirst, "Remove content.members[0]")->onClick();
    auto removeLast = panel->build();
    find(find, removeLast, "Remove content.members[0]")->onClick();
    auto empty = panel->build();
    find(find, empty, "spawn/draft/apply")->onClick();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    auto repair = panel->build();
    REQUIRE(find(find, repair, "spawn/draft/status") != nullptr);
    find(find, repair, "New item JSON for content.members")
        ->onTextChange(R"({"id":"repaired","unit":{"type":"rpg.unit","id":"wolf"},"count":1})");
    find(find, repair, "Add to content.members")->onClick();
    auto repaired = panel->build();
    find(find, repaired, "spawn/draft/apply")->onClick();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(5));
    auto third = panel->build();
    find(find, third, "spawn/draft/begin")->onClick();
    auto invalidRoot = panel->build();
    find(find, invalidRoot, "spawn/source-json")->onTextChange("null");
    auto nullDraft = panel->build();
    find(find, nullDraft, "spawn/draft/apply")->onClick();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(5));
    auto cancel = panel->build();
    find(find, cancel, "spawn/draft/cancel")->onClick();
    auto fourth = panel->build();
    find(find, fourth, "spawn/draft/begin")->onClick();
    auto external = *form->view().read("$source");
    external.set("id", "external-change");
    form->view().write("$source", external).expect("external authoring edit");
    auto conflicted = panel->build();
    find(find, conflicted, "spawn/draft/apply")->onClick();
    REQUIRE_EQ(*form->view().read("$source")->find("id")->getIf<std::string>(), std::string("external-change"));
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(6));
    find(find, conflicted, "spawn/draft/cancel")->onClick();
    auto retryBegin = panel->build();
    find(find, retryBegin, "spawn/draft/begin")->onClick();
    auto retryEdit = panel->build();
    find(find, retryEdit, "content.members[0].count")->onTextChange("2");
    auto     retryReady    = panel->build();
    auto     reenterCancel = find(find, retryReady, "spawn/draft/cancel")->onClick;
    unsigned calls         = 0;
    bool     reentered     = false;
    units->onValidate      = [&] {
        if (++calls > 1) {
            reentered = true;
            reenterCancel();
            throw std::runtime_error("draft commit unavailable");
        }
    };
    find(find, retryReady, "spawn/draft/apply")->onClick();
    REQUIRE(reentered);
    REQUIRE(form->history().pending);
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(6));
    auto failed = panel->build();
    REQUIRE(find(find, failed, "spawn/draft/status") != nullptr);
    find(find, failed, "content.members[0].count")->onTextChange("7");
    units->onValidate = {};
    find(find, failed, "spawn/retry")->onClick();
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(7));
    REQUIRE_EQ(
        *form->view().read("$source")->find("content")->find("members")->at(0).find("count")->getIf<std::int64_t>(),
        std::int64_t(2));
    auto finished = panel->build();
    REQUIRE(find(find, finished, "spawn/draft/status") == nullptr);
}
TEST_CASE("Spawn World2D region tools use real session capture overlays and canonical transactions") {
    using namespace eve::spawn_editor;
    using Event   = eve::editor::EditorPointerEvent;
    auto content  = std::make_shared<Content>();
    auto document = eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("regions"), mapSource(),
                                                                    content, content)
                        .expect("map document");
    std::shared_ptr<SpawnForm> form = SpawnForm::create(std::move(document)).expect("form");
    auto circle                     = SpawnRegionTool::create(form, "north", RegionShape::Circle).expect("circle tool");
    auto points                     = SpawnRegionTool::create(form, "north", RegionShape::Points).expect("points tool");
    auto polygon = SpawnRegionTool::create(form, "north", RegionShape::Polygon).expect("polygon tool");
    eve::editor::EditorSession session;
    REQUIRE(session.addTool(circle.get()));
    REQUIRE(session.addTool(points.get()));
    REQUIRE(session.addTool(polygon.get()));
    REQUIRE(session.activateTool(circle->descriptor().id));
    auto pointer = [&](Event::Phase phase, float x, float y, int id = 3) {
        Event event;
        event.phase     = phase;
        event.x         = x;
        event.y         = y;
        event.pointerId = id;
        return session.dispatchPointer(event);
    };
    auto key = [&](std::string name) {
        eve::editor::EditorKeyEvent event;
        event.key     = std::move(name);
        event.pressed = true;
        return session.dispatchKey(event);
    };
    REQUIRE(pointer(Event::Phase::Down, 1, 2).capturePointer);
    REQUIRE(session.hasPointerCapture());
    pointer(Event::Phase::Move, 4, 6);
    struct Overlay final : eve::editor::IEditorOverlay {
        unsigned circles = 0, lines = 0;
        float    radius = 0;
        void     line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                      const eve::editor::OverlayStyle&) override {
            ++lines;
        }
        void circle(const eve::editor::OverlayPoint&, float r, const eve::editor::OverlayStyle&) override {
            ++circles;
            radius = r;
        }
        void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                       const eve::editor::OverlayStyle&) override {}
        void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}
    } overlay;
    session.drawOverlay(overlay);
    REQUIRE_EQ(overlay.circles, 1u);
    REQUIRE_EQ(overlay.radius, 5.f);
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(1));
    REQUIRE(pointer(Event::Phase::Up, 4, 6).releasePointer);
    REQUIRE(!session.hasPointerCapture());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(2));
    REQUIRE_EQ(*form->view().read("$source")->find("instances")->at(0).find("region")->find("radius")->getIf<double>(),
               5.0);
    form->undo().expect("region undo");
    pointer(Event::Phase::Down, 2, 2);
    key("Escape");
    REQUIRE(!session.hasPointerCapture());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(3));
    pointer(Event::Phase::Down, 0, 0);
    pointer(Event::Phase::Up, std::numeric_limits<float>::quiet_NaN(), 0);
    REQUIRE(!session.hasPointerCapture());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(3));
    key("Escape");
    REQUIRE(session.activateTool(points->descriptor().id));
    pointer(Event::Phase::Down, -2, 1);
    pointer(Event::Phase::Down, 3, 4);
    key("Enter");
    auto pointSource = form->view().read("$source");
    REQUIRE_EQ(pointSource->find("instances")->at(0).find("region")->find("points")->getIf<eve::Value::Array>()->size(),
               std::size_t(2));
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    REQUIRE(session.activateTool(polygon->descriptor().id));
    pointer(Event::Phase::Down, 0, 0);
    pointer(Event::Phase::Down, 2, 0);
    key("Enter");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    REQUIRE(!polygon->diagnostics().empty());
    pointer(Event::Phase::Down, 0, 2);
    key("Enter");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(5));
    REQUIRE_EQ(form->view()
                   .read("$source")
                   ->find("instances")
                   ->at(0)
                   .find("region")
                   ->find("vertices")
                   ->getIf<eve::Value::Array>()
                   ->size(),
               std::size_t(3));
    pointer(Event::Phase::Down, 0, 0);
    pointer(Event::Phase::Down, 2, 0);
    pointer(Event::Phase::Down, 0, 2);
    auto external = *form->view().read("$source");
    external.set("map", "external-map");
    form->view().write("$source", external).expect("external authoring update");
    key("Enter");
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(6));
    REQUIRE(!polygon->diagnostics().empty());
    key("Escape");
    REQUIRE(!SpawnRegionTool::create(form, "missing", RegionShape::Circle).ok());
    session.clearTools();
}
TEST_CASE("Spawn map viewport shares canonical coordinates preserves capture and rejects renderer reentry") {
    using namespace eve::spawn_editor;
    using Event = eve::editor::EditorPointerEvent;
    auto units  = std::make_shared<Content>();
    auto document =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("viewport"), mapSource(), units, units)
            .expect("document");
    std::shared_ptr<SpawnForm>           form = SpawnForm::create(std::move(document)).expect("form");
    const eve::map_spawn::PixelTransform transform{{100, -50}, 0.5};
    MapViewportFrame                     frame{20, 30, 10, 20, 2};
    auto viewport = SpawnMapViewport::create(form, "north", transform, frame).expect("viewport");
    auto pointer  = [&](Event::Phase phase, float x, float y, int id = 3) {
        Event e;
        e.phase     = phase;
        e.x         = x;
        e.y         = y;
        e.pointerId = id;
        return viewport->pointerEvent(e);
    };
    REQUIRE(pointer(Event::Phase::Down, 20, 30).expect("down").capturePointer);
    pointer(Event::Phase::Move, 40, 30).expect("move");
    struct Overlay final : eve::editor::IEditorOverlay {
        float                 x = 0, y = 0, radius = 0;
        std::function<void()> callback;
        void                  line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                   const eve::editor::OverlayStyle&) override {}
        void                  rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                        const eve::editor::OverlayStyle&) override {}
        void circle(const eve::editor::OverlayPoint& point, float r, const eve::editor::OverlayStyle&) override {
            x      = point.x;
            y      = point.y;
            radius = r;
            if (callback) callback();
        }
        void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}
    } overlay;
    bool reentered   = false;
    overlay.callback = [&] {
        reentered    = true;
        auto changed = frame;
        changed.zoom = 4;
        REQUIRE(!viewport->setFrame(changed).ok());
    };
    viewport->drawOverlay(overlay).expect("screen projection");
    REQUIRE(reentered);
    REQUIRE_EQ(overlay.x, 20.f);
    REQUIRE_EQ(overlay.y, 30.f);
    REQUIRE_EQ(overlay.radius, 20.f);
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(1));
    overlay.callback = [] { throw std::runtime_error("renderer rejected feedback"); };
    REQUIRE(!viewport->drawOverlay(overlay).ok());
    overlay.callback = {};
    pointer(Event::Phase::Up, 40, 30).expect("up");
    auto  source = form->view().read("$source");
    auto* region = source->find("instances")->at(0).find("region");
    REQUIRE_EQ(*region->find("center")->at(0).getIf<double>(), 105.0);
    REQUIRE_EQ(*region->find("center")->at(1).getIf<double>(), -40.0);
    REQUIRE_EQ(*region->find("radius")->getIf<double>(), 5.0);
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(2));
    pointer(Event::Phase::Down, 20, 30).expect("new gesture");
    auto invalidFrame = frame;
    invalidFrame.zoom = 0;
    REQUIRE(!viewport->setFrame(invalidFrame).ok());
    REQUIRE(viewport->hasPointerCapture());
    frame.zoom = 4;
    viewport->setFrame(frame).expect("pan zoom cancels gesture");
    REQUIRE(!viewport->hasPointerCapture());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(2));
    pointer(Event::Phase::Down, 20, 30).expect("captured gesture");
    auto ignored =
        pointer(Event::Phase::Up, std::numeric_limits<float>::quiet_NaN(), 0, 9).expect("other pointer ignored");
    REQUIRE(!ignored.handled);
    REQUIRE(viewport->hasPointerCapture());
    REQUIRE(!pointer(Event::Phase::Up, std::numeric_limits<float>::quiet_NaN(), 0).ok());
    REQUIRE(!viewport->hasPointerCapture());
    pointer(Event::Phase::Down, 20, 30).expect("selection gesture");
    viewport->selectShape(RegionShape::Circle).expect("same shape cancels old gesture");
    REQUIRE(!viewport->hasPointerCapture());
    REQUIRE(!viewport->selectShape(static_cast<RegionShape>(99)).ok());
    REQUIRE(!SpawnMapViewport::create(form, "north", {{0, 0}, 0}, frame).ok());
    REQUIRE(!eve::map_spawn::worldToPixel({{0, 0}, 0}, {1, 1}).ok());
    auto world = eve::map_spawn::pixelToWorld(transform, {10, 20}).expect("canonical map transform");
    REQUIRE_EQ(world.x, 105.0);
    REQUIRE_EQ(world.y, -40.0);
    auto changedMap = *form->view().read("$source");
    changedMap.set("map", "different-map");
    form->view().write("$source", changedMap).expect("map identity edit");
    REQUIRE(!pointer(Event::Phase::Down, 20, 30).ok());
    REQUIRE(!viewport->hasPointerCapture());
    REQUIRE(!viewport->selectShape(RegionShape::Points).ok());
}
TEST_CASE("Spawn diagnostic panels consume actual frozen ledgers and visibly retain failed refresh data") {
    struct Placement final : eve::spawn::ISpawnPlacement {
        eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
            return eve::Result<void>::success();
        }
        eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest&,
                                                       const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                       std::uint64_t&) override {
            return eve::Result<eve::spawn::PlacementOutcome>::success(eve::spawn::PlacementBlocked{});
        }
    };
    struct Factory final : eve::spawn::ISpawnFactory {
        eve::Result<eve::spawn::PrepareOutcome> prepare(const eve::spawn::BatchRequest&,
                                                        std::span<const eve::spawn::Point>) override {
            return eve::Result<eve::spawn::PrepareOutcome>::success(eve::spawn::PlacementBlocked{});
        }
        eve::Result<eve::spawn::RemovalReceipt> remove(const eve::spawn::RemovalRequest& request) override {
            return eve::Result<eve::spawn::RemovalReceipt>::success({request.subjects.size()});
        }
    };
    auto providers = std::make_shared<eve::spawn::SpawnProviders>(
        eve::spawn::SpawnProviders{std::make_shared<Placement>(), std::make_shared<Factory>()});
    Content content;
    auto    map =
        std::make_unique<eve::spawn::CompiledMap>(eve::spawn::compileMap(mapSource(), content, content).expect("map"));
    const auto owner = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000091");
    auto       world = std::make_unique<eve::spawn::SpawnWorld>(owner, 17);
    world->attach(*map, providers).expect("attached world");
    auto initial = world->captureDiagnostics().expect("initial published facts");
    auto stale   = initial;
    auto panel   = eve::spawn_editor::SpawnDiagnosticsPanel::create(std::move(initial)).expect("panel");
    auto old     = panel->snapshot();
    panel->component().mountAs("spawn-runtime-diagnostics-test");
    const auto host = panel->component().host();
    world->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("blocked actual scheduler");
    panel->refresh(world->captureDiagnostics()).expect("explicit refresh");
    REQUIRE(panel->component().updateIfDirty());
    REQUIRE_EQ(panel->snapshot()->tick.value(), std::uint64_t(1));
    REQUIRE_EQ(old->tick.value(), std::uint64_t(0));
    REQUIRE_EQ(panel->snapshot()->sources[0].state.blocked, eve::spawn::BlockReason::PlacementBlocked);
    REQUIRE(eve::ui::UIHost::resolve(host)
                ->get()
                .findById("spawn/runtime/source/0-1/blocked")
                ->get()
                .text.find("Placement blocked") != std::string::npos);
    REQUIRE(eve::ui::UIHost::resolve(host)->get().findById("spawn/runtime/source/0-1/template-source")->get().readOnly);
    auto current = panel->snapshot();
    REQUIRE(!panel->refresh(eve::Result<eve::spawn::WorldDiagnostics>::success(std::move(stale))).ok());
    REQUIRE(panel->snapshot() == current);
    REQUIRE(panel->component().updateIfDirty());
    REQUIRE(bool(eve::ui::UIHost::resolve(host)->get().findById("spawn/runtime/stale")));
    panel->refresh(world->captureDiagnostics()).expect("same boundary recovers display");
    REQUIRE(panel->component().updateIfDirty());
    REQUIRE(!eve::ui::UIHost::resolve(host)->get().findById("spawn/runtime/stale"));
    {
        eve::spawn::SpawnWorld foreign(owner, 17);
        foreign.attach(*map, providers).expect("same-id different incarnation");
        REQUIRE(!panel->refresh(foreign.captureDiagnostics()).ok());
    }
    world.reset();
    map.reset();
    REQUIRE_EQ(panel->snapshot()->sources[0].templateRevision->definition().id, std::string("camp"));
    REQUIRE(!panel
                 ->refresh(eve::Result<eve::spawn::WorldDiagnostics>::failure(
                     eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Runtime world closed")))
                 .ok());
    REQUIRE(panel->component().updateIfDirty());
    REQUIRE(bool(eve::ui::UIHost::resolve(host)->get().findById("spawn/runtime/stale")));
    REQUIRE(!eve::spawn_editor::SpawnDiagnosticsPanel::create({}).ok());
    std::weak_ptr<const eve::spawn::CompiledTemplate> pinned = panel->snapshot()->sources[0].templateRevision;
    panel.reset();
    old.reset();
    current.reset();
    REQUIRE(pinned.expired());
}
TEST_CASE("Spawn owning workspace binds real source shape and frame controls and supports empty maps") {
    using namespace eve::spawn_editor;
    using Event = eve::editor::EditorPointerEvent;
    auto find   = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& key) -> eve::ui::WidgetDesc* {
        if (node.id == key || node.text == key) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, key)) return found;
        return nullptr;
    };
    auto units  = std::make_shared<Content>();
    auto source = mapSource();
    auto east   = source.find("instances")->at(0);
    east.set("id", "east");
    source.find("instances")->getIf<eve::Value::Array>()->push_back(std::move(east));
    auto doc =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("workspace"), source, units, units)
            .expect("document");
    std::shared_ptr<SpawnForm> form = SpawnForm::create(std::move(doc)).expect("form");
    auto workspace = SpawnEditorWorkspace::create(form, {{100, -50}, 0.5}, {20, 30, 10, 20, 2}).expect("workspace");
    REQUIRE(workspace->form() == form);
    workspace->controlsPanel().mountAs("spawn-workspace-controls-test");
    workspace->formPanel().mountAs("spawn-workspace-form-test");
    auto pointer = [&](Event::Phase phase, float x, float y) {
        Event event;
        event.phase     = phase;
        event.x         = x;
        event.y         = y;
        event.pointerId = 3;
        return workspace->pointerEvent(event);
    };
    const auto initialRevision = form->sourceRevision();
    const auto hover           = pointer(Event::Phase::Move, 20, 30).expect("unselected native hover");
    REQUIRE(!hover.handled);
    REQUIRE(!hover.capturePointer);
    REQUIRE(!workspace->hasPointerCapture());
    REQUIRE_EQ(form->sourceRevision().value(), initialRevision.value());
    REQUIRE(workspace->diagnostics().empty());
    REQUIRE(!pointer(Event::Phase::Down, 20, 30).ok());
    auto initial = workspace->controlsPanel().build();
    find(find, initial, "Edit region: north")->onClick();
    pointer(Event::Phase::Down, 20, 30).expect("selected source down");
    pointer(Event::Phase::Move, 40, 30).expect("selected source preview");
    auto           selected       = workspace->controlsPanel().build();
    auto           eastDuringDraw = find(find, selected, "Edit region: east")->onClick;
    struct Overlay final : eve::editor::IEditorOverlay {
        std::function<void()> callback;
        void                  line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                   const eve::editor::OverlayStyle&) override {}
        void                  rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                        const eve::editor::OverlayStyle&) override {}
        void circle(const eve::editor::OverlayPoint&, float, const eve::editor::OverlayStyle&) override {
            if (callback) callback();
        }
        void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}
    } overlay;
    overlay.callback = [&] { eastDuringDraw(); };
    workspace->drawOverlay(overlay).expect("selection reentry rejected");
    REQUIRE(workspace->hasPointerCapture());
    pointer(Event::Phase::Up, 40, 30).expect("selected source commit");
    REQUIRE_EQ(*form->view().read("$source")->find("instances")->at(0).find("region")->find("radius")->getIf<double>(),
               5.0);
    REQUIRE_EQ(
        *form->view().read("$source")->find("instances")->at(1).find("region")->find("radius")->getIf<std::int64_t>(),
        std::int64_t(3));
    eastDuringDraw();
    REQUIRE(!workspace->diagnostics().empty());
    auto current = workspace->controlsPanel().build();
    find(find, current, "Edit region: east")->onClick();
    pointer(Event::Phase::Down, 20, 30).expect("east down");
    pointer(Event::Phase::Up, 60, 30).expect("east commit");
    REQUIRE_EQ(*form->view().read("$source")->find("instances")->at(1).find("region")->find("radius")->getIf<double>(),
               10.0);
    form->undo().expect("same actual form history");
    auto frameControls = workspace->controlsPanel().build();
    pointer(Event::Phase::Down, 20, 30).expect("frame gesture");
    find(find, frameControls, "spawn/workspace/frame/Zoom")->onTextChange("4");
    REQUIRE_EQ(workspace->frame().zoom, 4.0);
    REQUIRE(!workspace->hasPointerCapture());
    REQUIRE_EQ(form->targetRevision().value(), std::uint64_t(4));
    auto clear = workspace->controlsPanel().build();
    find(find, clear, "spawn/workspace/clear")->onClick();
    REQUIRE(!pointer(Event::Phase::Down, 20, 30).ok());
    auto retained = workspace->controlsPanel().build();
    auto callback = find(find, retained, "Edit region: north")->onClick;
    workspace.reset();
    const auto owners = form.use_count();
    callback();
    REQUIRE_EQ(form.use_count(), owners);
    auto emptySource = mapSource();
    emptySource.set("instances", eve::Value::Array{});
    auto emptyDoc = eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("empty-workspace"),
                                                                    emptySource, units, units)
                        .expect("empty map");
    std::shared_ptr<SpawnForm> emptyForm = SpawnForm::create(std::move(emptyDoc)).expect("empty form");
    auto empty     = SpawnEditorWorkspace::create(emptyForm, {{0, 0}, 1}, {}).expect("empty workspace");
    auto noSources = empty->controlsPanel().build();
    REQUIRE(find(find, noSources, "Edit region: north") == nullptr);
    auto fields = empty->formPanel().build();
    find(find, fields, "spawn/draft/begin")->onClick();
    auto       draft = empty->formPanel().build();
    const auto item  = mapSource().find("instances")->at(0).toJson().expect("new instance");
    find(find, draft, "New item JSON for instances")->onTextChange(item);
    find(find, draft, "Add to instances")->onClick();
    auto ready = empty->formPanel().build();
    find(find, ready, "spawn/draft/apply")->onClick();
    auto populated = empty->controlsPanel().build();
    find(find, populated, "Edit region: north")->onClick();
    auto modes = empty->controlsPanel().build();
    find(find, modes, "spawn/workspace/shape/Points")->onClick();
    Event click;
    click.phase = Event::Phase::Down;
    click.x     = 1;
    click.y     = 2;
    empty->pointerEvent(click).expect("point edit");
    eve::editor::EditorKeyEvent enter;
    enter.key     = "Enter";
    enter.pressed = true;
    empty->keyEvent(enter).expect("point commit");
    REQUIRE_EQ(
        *emptyForm->view().read("$source")->find("instances")->at(0).find("region")->find("kind")->getIf<std::string>(),
        std::string("points"));
}
TEST_CASE("Spawn asset document saves exact committed source and preserves edits failures and history") {
    struct Store final : eve::editor::IAtomicDocumentStore {
        eve::editor::MemoryAtomicDocumentStore   memory;
        std::function<void()>                    callback;
        bool                                     reject = false;
        eve::Result<eve::editor::StoredDocument> read(const std::string& uri) const override {
            return memory.read(uri);
        }
        eve::Result<eve::editor::StoredDocument> compareAndSwap(const std::string& uri, eve::editor::Revision revision,
                                                                const std::string&              hash,
                                                                const eve::editor::EditorValue& value) override {
            if (callback) callback();
            if (reject)
                return eve::Result<eve::editor::StoredDocument>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "injected save failure"));
            return memory.compareAndSwap(uri, revision, hash, value);
        }
    };
    auto                     store   = std::make_shared<Store>();
    auto                     content = std::make_shared<Content>();
    eve::editor::DocumentKey key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("spawn-forest")};
    auto asset = eve::spawn_editor::SpawnAssetDocument::openSidecar(store, key, "content://forest.spawns", mapSource(),
                                                                    content, content)
                     .expect("new asset");
    REQUIRE(asset->dirty());
    auto form      = asset->form();
    auto setRadius = [&](int radius) {
        auto candidate = *form->view().read("$source");
        candidate.find("instances")->at(0).find("region")->set("radius", radius);
        form->view().write("$source", candidate).expect("real edit");
    };
    store->callback = [&] {
        REQUIRE(!asset->save().ok());
        setRadius(8);
    };
    asset->save().expect("captured old revision saved");
    REQUIRE(asset->dirty());
    auto reopened = eve::spawn_editor::SpawnAssetDocument::openSidecar(store, key, "content://forest.spawns",
                                                                       eve::Value{}, content, content)
                        .expect("read actual persisted source instead of fallback");
    REQUIRE(!reopened->dirty());
    REQUIRE_EQ(reopened->form()->view().read("$source")->toJson().expect("disk"),
               mapSource().toJson().expect("original"));
    store->callback = {};
    store->reject   = true;
    REQUIRE(!asset->save().ok());
    REQUIRE(asset->dirty());
    REQUIRE(form->history().canUndo);
    form->undo().expect("save failure keeps original history");
    form->redo().expect("redo remains available");
    store->reject = false;
    asset->save().expect("retry current source");
    REQUIRE(!asset->dirty());
    setRadius(9);
    REQUIRE(!reopened->save().ok());
    REQUIRE(!reopened->dirty());
    asset->save().expect("valid current CAS");
    eve::editor::DocumentKey templateKey{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("spawn-template")};
    auto templ = eve::spawn_editor::SpawnAssetDocument::openTemplate(store, templateKey, "content://camp.template",
                                                                     Content::source(), content)
                     .expect("template asset");
    templ->save().expect("canonical template save");
    auto loaded = eve::spawn_editor::SpawnAssetDocument::openTemplate(store, templateKey, "content://camp.template",
                                                                      eve::Value{}, content)
                      .expect("template persisted admission");
    REQUIRE(!loaded->dirty());
    auto invalid = store->memory.compareAndSwap("content://invalid", 0, "", eve::editor::EditorValue{})
                       .expect("invalid disk source");
    REQUIRE(!eve::spawn_editor::SpawnAssetDocument::openTemplate(store, templateKey, "content://invalid",
                                                                 Content::source(), content)
                 .ok());
    asset.reset();
    setRadius(10);
    REQUIRE(form->history().canUndo);
}
TEST_CASE("Spawn asset disk save failure preserves canonical source and previous file") {
    struct Directory {
        std::filesystem::path path;
        Directory() {
            for (unsigned attempt = 0; attempt < 32; ++attempt) {
                auto candidate = std::filesystem::temp_directory_path() /
                                 ("eve-spawn-asset-" + std::to_string(std::random_device{}()));
                if (std::filesystem::create_directory(candidate)) {
                    path = std::move(candidate);
                    return;
                }
            }
            throw std::runtime_error("Cannot create unique asset directory");
        }
        ~Directory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } directory;
    auto                     store   = std::make_shared<eve::editor::DiskAtomicDocumentStore>(directory.path);
    auto                     content = std::make_shared<Content>();
    eve::editor::DocumentKey key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("disk-forest")};
    const std::string        uri = "content://forest.spawns";
    auto asset = eve::spawn_editor::SpawnAssetDocument::openSidecar(store, key, uri, mapSource(), content, content)
                     .expect("disk asset");
    asset->save().expect("first atomic disk write");
    auto before    = store->read(uri).expect("persisted original");
    auto candidate = *asset->form()->view().read("$source");
    candidate.find("instances")->at(0).find("region")->set("radius", 12);
    asset->form()->view().write("$source", candidate).expect("actual edit");
    store->setBeforeReplaceHook([](const auto&) { return false; });
    REQUIRE(!asset->save().ok());
    REQUIRE(asset->dirty());
    REQUIRE(asset->form()->history().canUndo);
    auto unchanged = store->read(uri).expect("old disk envelope survives");
    REQUIRE_EQ(unchanged.contentHash, before.contentHash);
    REQUIRE_EQ(unchanged.revision, before.revision);
    store->setBeforeReplaceHook({});
    asset->save().expect("retry file replacement");
    asset.reset();
    store.reset();
    auto freshStore = std::make_shared<eve::editor::DiskAtomicDocumentStore>(directory.path);
    auto loaded =
        eve::spawn_editor::SpawnAssetDocument::openSidecar(freshStore, key, uri, eve::Value{}, content, content)
            .expect("new owner reads disk");
    REQUIRE(!loaded->dirty());
    REQUIRE_EQ(loaded->form()->view().read("$source")->toJson().expect("reopened source"),
               candidate.toJson().expect("expected source"));
}
TEST_CASE("Spawn save controls show actual failures and unsaved revisions and retire stale callbacks") {
    struct Store final : eve::editor::IAtomicDocumentStore {
        eve::editor::MemoryAtomicDocumentStore   memory;
        std::function<void()>                    callback;
        bool                                     reject = false;
        unsigned                                 writes = 0;
        eve::Result<eve::editor::StoredDocument> read(const std::string& uri) const override {
            return memory.read(uri);
        }
        eve::Result<eve::editor::StoredDocument> compareAndSwap(const std::string& uri, eve::editor::Revision revision,
                                                                const std::string&              hash,
                                                                const eve::editor::EditorValue& value) override {
            ++writes;
            if (callback) callback();
            if (reject)
                return eve::Result<eve::editor::StoredDocument>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "disk unavailable"));
            return memory.compareAndSwap(uri, revision, hash, value);
        }
    };
    auto                     store   = std::make_shared<Store>();
    auto                     content = std::make_shared<Content>();
    eve::editor::DocumentKey key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("save-panel")};
    std::shared_ptr<eve::spawn_editor::SpawnAssetDocument> asset =
        eve::spawn_editor::SpawnAssetDocument::openSidecar(store, key, "content://panel", mapSource(), content, content)
            .expect("asset");
    auto panel = eve::spawn_editor::createSpawnAssetSavePanel(asset).expect("save controls");
    panel->mountAs("spawn-asset-save-test");
    auto host = panel->host();
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& id) -> eve::ui::WidgetDesc* {
        if (node.id == id) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, id)) return found;
        return nullptr;
    };
    auto tree   = panel->build();
    auto old    = find(find, tree, "spawn/asset/save")->onClick;
    auto source = *asset->form()->view().read("$source");
    source.find("instances")->at(0).find("region")->set("radius", 7);
    asset->form()->view().write("$source", source).expect("new committed source");
    old();
    REQUIRE_EQ(store->writes, 0u);
    auto stale = panel->build();
    REQUIRE(find(find, stale, "spawn/asset/error/0") != nullptr);
    store->reject = true;
    find(find, stale, "spawn/asset/save")->onClick();
    REQUIRE(asset->dirty());
    auto failed = panel->build();
    REQUIRE_EQ(find(find, failed, "spawn/asset/error/0")->text, std::string("disk unavailable"));
    store->reject   = false;
    auto save       = find(find, failed, "spawn/asset/save")->onClick;
    store->callback = [&] { save(); };
    save();
    REQUIRE_EQ(store->writes, 2u);
    REQUIRE(!asset->dirty());
    store->callback = {};
    eve::ui::detail::flushPendingComponents();
    REQUIRE_EQ(eve::ui::UIHost::resolve(host)->get().findById("spawn/asset/status")->get().text,
               std::string("Configuration saved"));
    auto                                                 live    = panel->build();
    auto                                                 retired = find(find, live, "spawn/asset/save")->onClick;
    std::weak_ptr<eve::spawn_editor::SpawnAssetDocument> witness = asset;
    asset.reset();
    REQUIRE(!witness.expired());
    panel.reset();
    retired();
    REQUIRE_EQ(store->writes, 2u);
    REQUIRE(!eve::spawn_editor::createSpawnAssetSavePanel({}).ok());
}
TEST_CASE("Spawn runtime export writes canonical JSON through the actual atomic filesystem") {
    struct Directory {
        std::filesystem::path path;
        ~Directory() {
            if (!path.empty()) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup;
    auto  filesystemOwner = std::make_shared<eve::filesystem::physfs::Filesystem>();
    auto& filesystem      = *filesystemOwner;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        REQUIRE(filesystem.setIdentity(
            "eve-spawn-export-" + std::to_string(std::random_device{}()) + "-" + std::to_string(std::random_device{}()),
            true));
        auto path = std::filesystem::path(filesystem.getSaveDirectory());
        if (!std::filesystem::exists(path)) {
            cleanup.path = std::move(path);
            break;
        }
    }
    REQUIRE(!cleanup.path.empty());
    REQUIRE(filesystem.setupWriteDirectory());
    auto content  = std::make_shared<Content>();
    auto document = eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("export-map"), mapSource(),
                                                                    content, content)
                        .expect("map document");
    std::shared_ptr<eve::spawn_editor::SpawnForm> form =
        eve::spawn_editor::SpawnForm::create(std::move(document)).expect("map form");
    auto exported = eve::spawn_editor::PreparedSpawnExport::prepare(form).expect("freeze admitted source");
    REQUIRE_EQ(exported.sourceRevision().value(), std::uint64_t(1));
    auto next = *form->view().read("$source");
    next.find("instances")->at(0).find("region")->set("radius", 12);
    form->view().write("$source", next).expect("later edit");
    exported.write(filesystem.observeAtomicWrites(), "spawns.json").expect("write frozen source");
    REQUIRE_EQ(filesystem.readText("spawns.json"), exported.json());
    auto decoded  = eve::Value::fromJson(filesystem.readText("spawns.json")).expect("bare runtime JSON");
    auto compiled = eve::spawn::compileMap(decoded, *content, *content).expect("actual runtime compileMap");
    REQUIRE_EQ(compiled.canonicalJson(), mapSource().toJson().expect("original config"));
    REQUIRE(!exported.write(filesystem.observeAtomicWrites(), "../outside.json").ok());
    REQUIRE(!exported.write(filesystem.observeAtomicWrites(), "missing/target.json").ok());
    REQUIRE_EQ(filesystem.readText("spawns.json"), exported.json());
    auto current = eve::spawn_editor::PreparedSpawnExport::prepare(form).expect("capture new revision");
    REQUIRE_EQ(current.sourceRevision().value(), std::uint64_t(2));
    std::weak_ptr<eve::spawn_editor::SpawnForm> witness = form;
    form.reset();
    REQUIRE(witness.expired());
    current.write(filesystem.observeAtomicWrites(), "spawns.json").expect("retry independent frozen source");
    REQUIRE_EQ(filesystem.readText("spawns.json"), next.toJson().expect("later bytes"));
    auto templDoc = eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("export-template"),
                                                                            Content::source(), content)
                        .expect("template document");
    std::shared_ptr<eve::spawn_editor::SpawnForm> templateForm =
        eve::spawn_editor::SpawnForm::create(std::move(templDoc)).expect("template form");
    auto templateExport = eve::spawn_editor::PreparedSpawnExport::prepare(templateForm).expect("freeze template");
    templateExport.write(filesystem.observeAtomicWrites(), "template.json").expect("template write");
    auto templateSource = eve::Value::fromJson(filesystem.readText("template.json")).expect("template JSON");
    REQUIRE(eve::spawn::compileTemplate(templateSource, *content).ok());
    REQUIRE(!eve::spawn_editor::PreparedSpawnExport::prepare({}).ok());
    auto panel = eve::spawn_editor::createSpawnExportPanel(templateForm, filesystemOwner->observeAtomicWrites(),
                                                           "missing/panel.json")
                     .expect("export controls");
    panel->mountAs("spawn-export-controls-test");
    auto host = panel->host();
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& id) -> eve::ui::WidgetDesc* {
        if (node.id == id) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, id)) return found;
        return nullptr;
    };
    auto tree = panel->build();
    REQUIRE(!find(find, tree, "spawn/export/write")->enabled);
    find(find, tree, "spawn/export/prepare")->onClick();
    auto ready    = panel->build();
    auto oldWrite = find(find, ready, "spawn/export/write")->onClick;
    auto changed  = *templateForm->view().read("$source");
    changed.set("id", "edited-camp");
    templateForm->view().write("$source", changed).expect("edit after preparation");
    oldWrite();
    REQUIRE(!filesystem.fileExists("panel.json"));
    auto stale = panel->build();
    REQUIRE(find(find, stale, "spawn/export/error/0") != nullptr);
    REQUIRE(find(find, stale, "spawn/export/status")->text.find("older snapshot") != std::string::npos);
    find(find, stale, "spawn/export/write")->onClick();
    auto failed = panel->build();
    REQUIRE(find(find, failed, "spawn/export/error/0") != nullptr);
    REQUIRE(find(find, failed, "spawn/export/write")->enabled);
    REQUIRE(filesystem.createDirectory("missing"));
    find(find, failed, "spawn/export/write")->onClick();
    REQUIRE_EQ(filesystem.readText("missing/panel.json"), templateExport.json());
    eve::ui::detail::flushPendingComponents();
    REQUIRE(eve::ui::UIHost::resolve(host)->get().findById("spawn/export/written"));
    auto written = panel->build();
    find(find, written, "spawn/export/path")->onTextChange("new-panel.json");
    auto newPath = panel->build();
    REQUIRE(!find(find, newPath, "spawn/export/write")->enabled);
    find(find, newPath, "spawn/export/prepare")->onClick();
    auto finalTree = panel->build();
    auto retired   = find(find, finalTree, "spawn/export/write")->onClick;
    panel.reset();
    retired();
    REQUIRE(!filesystem.fileExists("new-panel.json"));
    eve::ui::UIHost::resolve(host)->get().setTree({});
    REQUIRE(!eve::spawn_editor::createSpawnExportPanel({}, filesystemOwner->observeAtomicWrites(), "unused").ok());

    for (const auto& item : filesystem.getDirectoryItems("")) REQUIRE(item.find(".tmp.") == std::string::npos);
}
TEST_CASE("Spawn native host owns panel mounts and routes canvas focus and captured releases") {
    using namespace eve::spawn_editor;
    using Event                               = eve::editor::EditorPointerEvent;
    auto                                table = std::make_shared<ecs::Table>();
    ecs::ScopedTable                    scope(*table);
    auto                                filesystem = std::make_shared<eve::filesystem::physfs::Filesystem>();
    auto                                content    = std::make_shared<Content>();
    auto                                store      = std::make_shared<eve::editor::MemoryAtomicDocumentStore>();
    eve::editor::DocumentKey            key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("native-host")};
    std::shared_ptr<SpawnAssetDocument> asset =
        SpawnAssetDocument::openSidecar(store, key, "content://host", mapSource(), content, content).expect("asset");
    SpawnHostLayout layout{
        {0, 0, 200, 200}, {200, 0, 200, 200}, {400, 0, 200, 200}, {200, 200, 200, 80}, {400, 200, 200, 80}};
    auto host =
        SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {}, layout, "spawns.json")
            .expect("mounted host");
    std::array<eve::ui::UIHostHandle, 4> handles{host->workspace().controlsPanel().host(),
                                                 host->workspace().formPanel().host(), host->savePanel().host(),
                                                 host->exportPanel().host()};
    eve::ui::detail::flushPendingComponents();
    for (auto handle : handles) {
        auto mounted = eve::ui::UIHost::resolve(handle);
        REQUIRE(bool(mounted));
        auto tree = mounted->get().tree();
        REQUIRE(tree->nodes[tree->root].type == eve::ui::NodeType::Window);
    }
    REQUIRE_EQ(eve::ui::UIHost::resolve(handles[0])->get().meta()->posX, 200.f);
    auto background = host->backgroundViewport(400, 400).expect("high DPI background projection");
    REQUIRE_EQ(background.centerX, 100.f);
    REQUIRE_EQ(background.centerY, 100.f);
    REQUIRE_EQ(background.zoom, 2.f);
    REQUIRE(!host->backgroundViewport(400, 200).ok());
    host->workspace().setFrame({20, 30, 5, 7, 2}).expect("shared background and overlay frame");
    auto moved = host->backgroundViewport(400, 400).expect("offset pan zoom projection");
    REQUIRE_EQ(moved.centerX, 45.f);
    REQUIRE_EQ(moved.centerY, 42.f);
    REQUIRE_EQ(moved.zoom, 4.f);
    host->workspace().setFrame({}).expect("restore test input frame");

    auto tree = host->workspace().controlsPanel().build();
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& text) -> eve::ui::WidgetDesc* {
        if (node.text == text || node.id == text) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, text)) return found;
        return nullptr;
    };
    find(find, tree, "Edit region: north")->onClick();
    auto pointer = [&](Event::Phase phase, float x, float y, bool blocked = false) {
        Event event;
        event.phase  = phase;
        event.x      = x;
        event.y      = y;
        event.button = 0;
        return host->pointerEvent(event, blocked);
    };
    REQUIRE(!pointer(Event::Phase::Down, 250, 20).expect("panel input ignored").handled);
    REQUIRE(!pointer(Event::Phase::Down, 10, 10, true).expect("occluded canvas ignored").handled);
    REQUIRE(!host->keyEvent({"Enter", true}).expect("no canvas focus").handled);
    pointer(Event::Phase::Down, 10, 10).expect("canvas capture");
    REQUIRE(host->workspace().hasPointerCapture());
    struct Overlay final : eve::editor::IEditorOverlay {
        std::function<void()> callback;
        void                  line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                   const eve::editor::OverlayStyle&) override {}
        void circle(const eve::editor::OverlayPoint&, float, const eve::editor::OverlayStyle&) override { callback(); }
        void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                       const eve::editor::OverlayStyle&) override {}
        void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}
    } overlay;
    bool reentryRejected = false;
    overlay.callback     = [&] { reentryRejected = !pointer(Event::Phase::Down, 20, 20).ok(); };
    host->drawOverlay(overlay).expect("native host screen-space feedback");
    REQUIRE(reentryRejected);

    pointer(Event::Phase::Up, 250, 10, true).expect("release outside canvas still reaches capture");
    REQUIRE(!host->workspace().hasPointerCapture());
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(2));
    auto saveTree = host->savePanel().build();
    find(find, saveTree, "spawn/asset/save")->onClick();
    REQUIRE(!asset->dirty());
    auto retiredSave = host->savePanel().build();
    auto retired     = find(find, retiredSave, "spawn/asset/save")->onClick;
    ecs::DestroyEntity(&eve::ui::UIHost::resolve(handles[0])->get());
    REQUIRE(!pointer(Event::Phase::Down, 10, 10).ok());
    host.reset();
    for (auto handle : handles) REQUIRE(!eve::ui::UIHost::resolve(handle));
    auto persisted = store->read("content://host").expect("saved value");
    retired();
    REQUIRE_EQ(store->read("content://host").expect("unchanged store").revision, persisted.revision);
    auto invalid     = layout;
    invalid.controls = layout.canvas;
    REQUIRE(!SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {}, invalid,
                                     "spawns.json")
                 .ok());
    struct RejectAdmission final : ecs::IComponentSlotBuffer {
        unsigned              calls = 0;
        std::uint32_t         size() const override { return 0; }
        const std::type_info& getType() const override { return typeid(RejectAdmission); }
        void                  ensure_space(std::uint32_t) override {
            if (++calls == 2) throw std::runtime_error("second panel admission failed");
        }
        void                       transfer_slot(std::uint32_t, std::uint32_t, ecs::IComponentSlotBuffer*) override {}
        ecs::IComponentSlotBuffer* ensure_equivalent(ecs::IComponentManager*) override { return nullptr; }
        void                       reset_slot(std::uint32_t) override {}
    };
    table->getOrCreateManager<eve::ui::UIHost>().components.emplace(std::type_index(typeid(RejectAdmission)),
                                                                    new RejectAdmission);
    bool failed = false;
    try {
        auto attempt = SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {}, layout,
                                               "spawns.json");
        (void)attempt;
    } catch (const std::runtime_error&) {
        failed = true;
    }
    REQUIRE(failed);
    std::size_t count = 0;
    {
        auto view = ecs::View<eve::ui::UIHost>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
    }
    REQUIRE_EQ(count, std::size_t(0));
}
TEST_CASE("Spawn host GPU background clips real tiles and restores the prior canvas") {
    using namespace eve::spawn_editor;
    GfxFixture graphics(128, 128, true);
    auto*      gfx = graphics.gfx;
    REQUIRE(gfx != nullptr);
    auto                     table = std::make_shared<ecs::Table>();
    ecs::ScopedTable         scope(*table);
    auto                     filesystem = std::make_shared<eve::filesystem::physfs::Filesystem>();
    auto                     content    = std::make_shared<Content>();
    auto                     store      = std::make_shared<eve::editor::MemoryAtomicDocumentStore>();
    eve::editor::DocumentKey key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("gpu-editor-host")};
    std::shared_ptr<SpawnAssetDocument> asset =
        SpawnAssetDocument::openSidecar(store, key, "content://gpu", mapSource(), content, content).expect("asset");
    SpawnHostLayout layout{
        {0, 0, 64, 64}, {64, 0, 200, 200}, {264, 0, 200, 200}, {64, 200, 200, 80}, {264, 200, 200, 80}};
    auto host =
        SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {}, layout, "spawns.json")
            .expect("host");
    eve::map::Map map;
    auto*         layer = map.newLayer(4, 4, 16, 16);
    layer->setTile(1, 1, 1);
    layer->setTint(1, 0, 0, 1);
    auto* unrelated = map.newLayer(4, 4, 16, 16);
    unrelated->fill(1);
    unrelated->setTint(0, 1, 0, 1);
    auto* inherited = eve::graphics::Camera2D::createCamera(*table);
    inherited->setPosition(10000, 10000);
    layer->setCamera(inherited);
    const std::array selected{ecs::handle_of(layer)};
    auto*            target   = gfx->newCanvas(64, 64);
    auto*            previous = gfx->newCanvas(32, 32);
    REQUIRE(target != nullptr);
    REQUIRE(previous != nullptr);
    gfx->setCanvas(previous);
    previous->clear(eve::graphics::Color(0, 0, 1, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 32, 32, eve::graphics::Color(0, 0, 1, 1));
    gfx->setBackgroundColorRGBA(0, 0, 0, 1);
    host->renderBackground(selected, *gfx, *target).expect("real tile GPU draw");
    REQUIRE(gfx->getCanvas() == previous);
    auto red = target->getPixel(24, 24);
    REQUIRE(red.r > 0.4f);
    REQUIRE(red.g < 0.05f);
    REQUIRE(red.b < 0.05f);
    auto outside = target->getPixel(4, 4);
    REQUIRE(outside.r < 0.05f);
    REQUIRE(outside.g < 0.05f);
    auto preserved = previous->getPixel(4, 4);
    REQUIRE(preserved.b > 0.9f);
    REQUIRE(preserved.r < 0.05f);
    host->workspace().setFrame({0, 0, 24, 24, 2}).expect("pan and zoom into tile corner");
    host->renderBackground(selected, *gfx, *target).expect("clipped corner render");
    REQUIRE(target->getPixel(8, 8).r > 0.4f);
    REQUIRE(target->getPixel(20, 20).r < 0.05f);
    REQUIRE(layer->draw()->camera == inherited);
    auto* dpi = gfx->newCanvas(128, 128);
    REQUIRE(dpi != nullptr);
    host->workspace().setFrame({}).expect("default frame for DPI");
    host->renderBackground(selected, *gfx, *dpi).expect("uniform high DPI target");
    REQUIRE(dpi->getPixel(48, 48).r > 0.4f);
    REQUIRE(dpi->getPixel(8, 8).r < 0.05f);
    auto controls = host->workspace().controlsPanel().build();
    auto find     = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& label) -> eve::ui::WidgetDesc* {
        if (node.text == label) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, label)) return found;
        return nullptr;
    };
    auto* select = find(find, controls, "Edit region: north");
    REQUIRE(select != nullptr);
    select->onClick();
    using Event = eve::editor::EditorPointerEvent;
    Event gesture;
    gesture.phase = Event::Phase::Down;
    gesture.x     = 32;
    gesture.y     = 32;
    host->pointerEvent(gesture, false).expect("start actual circle preview");
    gesture.phase = Event::Phase::Move;
    gesture.x     = 52;
    host->pointerEvent(gesture, false).expect("extend preview");
    host->renderBackground(selected, *gfx, *target).expect("map underneath feedback");
    host->renderOverlay(*gfx, *target).expect("canonical primitive GPU feedback");
    REQUIRE(gfx->getCanvas() == previous);
    auto peakGreen = [](eve::graphics::Canvas& canvas, int x, int y) {
        float peak = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) peak = std::max(peak, canvas.getPixel(x + dx, y + dy).g);
        return peak;
    };
    REQUIRE(peakGreen(*target, 52, 32) > 0.4f);
    REQUIRE(target->getPixel(24, 24).r > 0.4f);
    host->renderOverlay(*gfx, *dpi).expect("high DPI primitive feedback");
    REQUIRE(peakGreen(*dpi, 104, 64) > 0.4f);
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(1));
    gesture.phase = Event::Phase::Cancel;
    host->pointerEvent(gesture, false).expect("cancel preview without source mutation");
    REQUIRE(!host->workspace().hasPointerCapture());
    host->renderBackground({}, *gfx, *target).expect("empty selection clears old target contents");
    REQUIRE(gfx->getCanvas() == previous);
    REQUIRE(target->getPixel(8, 8).r < 0.05f);
    REQUIRE(target->getPixel(24, 24).r < 0.05f);
    const std::array duplicate{selected[0], selected[0]};
    REQUIRE(!host->renderBackground(duplicate, *gfx, *target).ok());
    REQUIRE(gfx->getCanvas() == previous);
    REQUIRE(previous->getPixel(4, 4).b > 0.9f);
    host.reset();
    host = SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 2}, {0, 0, 1e38, 0, 1},
                                   layout, "spawns.json")
               .expect("large world text preview host");
    auto hugeControls = host->workspace().controlsPanel().build();
    find(find, hugeControls, "Edit region: north")->onClick();
    gesture.phase = Event::Phase::Down;
    gesture.x     = 32;
    gesture.y     = 32;
    host->pointerEvent(gesture, false).expect("large world gesture start");
    gesture.phase = Event::Phase::Move;
    gesture.x     = -2e38f;
    host->pointerEvent(gesture, false).expect("large world radius emits annotation");
    REQUIRE(gfx->getFont() == nullptr);
    auto missingFont = host->renderOverlay(*gfx, *target);
    REQUIRE(!missingFont.ok());
    REQUIRE(missingFont.diagnostics()[0].message().find("font") != std::string::npos);
    REQUIRE(gfx->getCanvas() == previous);
    const auto fontPath =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "fonts/FontAwesome.ttf";
    std::ifstream fontFile(fontPath, std::ios::binary);
    REQUIRE(bool(fontFile));
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(fontFile), std::istreambuf_iterator<char>()};
    eve::font::FontData       fontData(std::move(bytes), 12);
    std::unique_ptr<eve::graphics::Font> font(gfx->newFont(&fontData));
    REQUIRE(bool(font));
    gfx->setFont(font.get());
    host->renderOverlay(*gfx, *target).expect("explicit text provider admitted");
    gfx->setFont(nullptr);
    REQUIRE(gfx->getCanvas() == previous);
    gesture.phase = Event::Phase::Cancel;
    host->pointerEvent(gesture, false).expect("cancel annotation gesture");
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(1));
    // The same spatial oracle runs against an actual host overlay without requiring OS focus.
    host.reset();
    SpawnHostLayout spatialLayout{
        {0, 0, 96, 96}, {96, 0, 200, 200}, {296, 0, 200, 200}, {96, 200, 200, 80}, {296, 200, 200, 80}};
    auto* spatialTarget = gfx->newCanvas(96, 96);
    REQUIRE(spatialTarget != nullptr);
    host = SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {0, 0, 0, 0, 1},
                                   spatialLayout, "spawns.json")
               .expect("spatial preview host");
    auto spatialControls = host->workspace().controlsPanel().build();
    find(find, spatialControls, "Edit region: north")->onClick();
    host->renderBackground({}, *gfx, *spatialTarget).expect("no-preview background");
    const auto absent = circlePreviewCoverage([&](int x, int y) { return spatialTarget->getPixel(x, y).r; });
    REQUIRE(absent.r < 0.9f);
    gesture.phase = Event::Phase::Down;
    gesture.x     = 40;
    gesture.y     = 40;
    host->pointerEvent(gesture, false).expect("spatial preview down");
    gesture.phase = Event::Phase::Move;
    gesture.x     = 72;
    host->pointerEvent(gesture, false).expect("spatial preview drag");
    host->renderOverlay(*gfx, *spatialTarget).expect("spatial preview pixels");
    const auto appPreview = circlePreviewCoverage([&](int x, int y) { return spatialTarget->getPixel(x, y).r; });
    REQUIRE(appPreview.r > 0.9f);
    REQUIRE(appPreview.offContourRed < 0.05f);
    gesture.phase = Event::Phase::Cancel;
    host->pointerEvent(gesture, false).expect("spatial preview cancel");
    gfx->setCanvas();
}
TEST_CASE("Spawn preview spatial oracle rejects missing misplaced and unrelated red pixels") {
    auto contour = [](int x, int y, double cx, double cy, double radius) {
        return std::abs(std::hypot(x + 0.5 - cx, y + 0.5 - cy) - radius) < 0.5 ? 1.f : 0.f;
    };
    const auto valid = circlePreviewCoverage([&](int x, int y) { return contour(x, y, 40, 40, 32); });
    REQUIRE(valid.r > 0.9f);
    REQUIRE(valid.offContourRed < 0.05f);
    REQUIRE(circlePreviewCoverage([](int, int) { return 0.f; }).r < 0.9f);
    REQUIRE(circlePreviewCoverage([&](int x, int y) { return contour(x, y, 48, 40, 32); }).r < 0.9f);
    REQUIRE(circlePreviewCoverage([&](int x, int y) { return contour(x, y, 40, 40, 24); }).r < 0.9f);
    REQUIRE(circlePreviewCoverage([](int x, int y) { return x == 40 && y == 40 ? 1.f : 0.f; }).r < 0.9f);
    REQUIRE(circlePreviewCoverage([](int x, int y) { return x == 71 && y == 40 ? 1.f : 0.f; }).r < 0.9f);
    const auto filled = circlePreviewCoverage([](int, int) { return 1.f; });
    REQUIRE(filled.offContourRed > 0.9f);
}
TEST_CASE("Spawn native window presents real panels background and SDL editing events") {
    using namespace eve::spawn_editor;
    GfxFixture graphics(640, 400);
    auto*      gfx = graphics.gfx;
    REQUIRE(gfx != nullptr);
    auto* ui = eve::ui::UI::create();
    REQUIRE(ui != nullptr);
    REQUIRE(ui->initBackend());
    struct Shutdown {
        eve::ui::UI* ui;
        ~Shutdown() { ui->shutdownBackend(); }
    } shutdown{ui};
    auto                                table = std::make_shared<ecs::Table>();
    ecs::ScopedTable                    scope(*table);
    auto                                filesystem = eve::filesystem::Filesystem::create();
    auto                                content    = std::make_shared<Content>();
    auto                                store      = std::make_shared<eve::editor::MemoryAtomicDocumentStore>();
    eve::editor::DocumentKey            key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("window-host")};
    std::shared_ptr<SpawnAssetDocument> asset =
        SpawnAssetDocument::openSidecar(store, key, "content://window-host", mapSource(), content, content)
            .expect("asset");
    SpawnHostLayout layout{
        {0, 0, 240, 240}, {240, 0, 200, 200}, {440, 0, 200, 200}, {240, 200, 200, 200}, {440, 200, 200, 200}};
    auto host = SpawnEditorHost::create(table, asset, filesystem->observeAtomicWrites(), {{0, 0}, 1}, {0, 0, 0, 0, 4},
                                        layout, "spawns.json")
                    .expect("native window host");
    eve::map::Map map;
    auto*         layer = map.newLayer(4, 4, 16, 16);
    layer->setTile(1, 1, 1);
    layer->setTint(1, 0, 0, 1);
    const std::array layers{ecs::handle_of(layer)};
    auto*            canvas = gfx->newCanvas(240, 240);
    REQUIRE(canvas != nullptr);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColorRGBA(0.01f, 0.01f, 0.01f, 1);
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& label) -> eve::ui::WidgetDesc* {
        if (node.text == label) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, label)) return found;
        return nullptr;
    };
    auto  controls = host->workspace().controlsPanel().build();
    auto* select   = find(find, controls, "Edit region: north");
    REQUIRE(select != nullptr);
    select->onClick();
    auto frame = [&] {
        gfx->clearScreen();
        host->renderBackground(layers, *gfx, *canvas).expect("window map background");
        host->renderOverlay(*gfx, *canvas).expect("window gesture overlay");
        gfx->drawTexturedRectRGBA(canvas->getTexture(), 0, 0, 240, 240, 1, 1, 1, 1);
        ui->beginFrameAndRender();
        gfx->present();
        ui->dispatchEvents();
    };
    for (int i = 0; i < 3; ++i) frame();
    auto mapPixel = gfx->getPixel(96, 96);
    REQUIRE(mapPixel.r > 0.4f);
    REQUIRE(mapPixel.g < 0.05f);
    auto panelPixel = gfx->getPixel(250, 150);
    REQUIRE(panelPixel.r + panelPixel.g + panelPixel.b > 0.05f);
    for (const auto& point : std::array<std::array<int, 2>, 3>{{{450, 150}, {250, 300}, {450, 300}}}) {
        const auto pixel = gfx->getPixel(point[0], point[1]);
        REQUIRE(pixel.r + pixel.g + pixel.b > 0.05f);
    }

    auto* native = dynamic_cast<eve::window::sdl::Window*>(graphics.win);
    REQUIRE(native != nullptr);
    const Uint32 windowId = SDL_GetWindowID(static_cast<SDL_Window*>(native->getHandle()));
    auto         dispatch = [&](SDL_Event event, bool blockedMouse = false, bool blockedKeyboard = false) {
        REQUIRE_EQ(SDL_PushEvent(&event), 1);
        eve::editor::ToolResponse response;
        SDL_Event                 queued;
        while (SDL_PollEvent(&queued)) {
            ui->processEvent(&queued);
            // This test feeds the selected host's tagged events; application routes other windows separately.
            const bool tagged =
                (queued.type == SDL_MOUSEBUTTONDOWN || queued.type == SDL_MOUSEBUTTONUP) ? queued.button.which == 23
                        : queued.type == SDL_MOUSEMOTION ? queued.motion.which == 23
                        : queued.type == SDL_KEYDOWN     ? queued.key.windowID == windowId
                        : queued.type == SDL_WINDOWEVENT
                            ? queued.window.windowID == windowId && queued.window.event == SDL_WINDOWEVENT_FOCUS_LOST
                            : false;
            if (tagged)
                response = host->processSdlEvent(queued, blockedMouse, blockedKeyboard).expect("native SDL routing");
        }
        return response;
    };
    SDL_Event event{};
    event.type            = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = windowId;
    event.button.which    = 23;
    event.button.button   = SDL_BUTTON_LEFT;
    event.button.x        = 80;
    event.button.y        = 80;
    REQUIRE(dispatch(event).capturePointer);
    frame();
    event                 = {};
    event.type            = SDL_MOUSEMOTION;
    event.motion.windowID = windowId;
    event.motion.which    = 23;
    event.motion.x        = 120;
    event.motion.y        = 80;
    dispatch(event);
    frame();
    float previewGreen = 0;
    for (int y = 79; y <= 81; ++y)
        for (int x = 119; x <= 121; ++x) previewGreen = std::max(previewGreen, gfx->getPixel(x, y).g);
    REQUIRE(previewGreen > 0.3f);
    event                 = {};
    event.type            = SDL_MOUSEBUTTONUP;
    event.button.windowID = windowId;
    event.button.which    = 23;
    event.button.button   = SDL_BUTTON_LEFT;
    event.button.x        = 700;
    event.button.y        = 80;
    REQUIRE(dispatch(event, true).releasePointer);
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(2));
    frame();
    auto pointControls = host->workspace().controlsPanel().build();
    find(find, pointControls, "Points")->onClick();
    for (int x : {40, 80}) {
        SDL_Event point{};
        point.type            = SDL_MOUSEBUTTONDOWN;
        point.button.windowID = windowId;
        point.button.which    = 23;
        point.button.button   = SDL_BUTTON_LEFT;
        point.button.x        = x;
        point.button.y        = 40;
        dispatch(point);
        point.type = SDL_MOUSEBUTTONUP;
        dispatch(point);
    }
    SDL_Event keyEvent{};
    keyEvent.type           = SDL_KEYDOWN;
    keyEvent.key.windowID   = windowId;
    keyEvent.key.keysym.sym = SDLK_RETURN;
    REQUIRE(!dispatch(keyEvent, false, true).handled);
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(2));
    keyEvent.key.keysym.sym = SDLK_BACKSPACE;
    REQUIRE(dispatch(keyEvent).handled);
    keyEvent.key.keysym.sym = SDLK_RETURN;
    keyEvent.key.repeat     = 1;
    REQUIRE(!dispatch(keyEvent).handled);
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(2));
    keyEvent.key.repeat = 0;
    REQUIRE(dispatch(keyEvent).releasePointer);
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(3));
    auto points = asset->form()->captureSource().expect("native keyboard commit");
    REQUIRE_EQ(
        points.source.find("instances")->at(0).find("region")->find("points")->getIf<eve::Value::Array>()->size(),
        std::size_t(1));
    auto circleControls = host->workspace().controlsPanel().build();
    find(find, circleControls, "Circle")->onClick();
    SDL_Event escapeGesture{};
    escapeGesture.type            = SDL_MOUSEBUTTONDOWN;
    escapeGesture.button.windowID = windowId;
    escapeGesture.button.which    = 23;
    escapeGesture.button.button   = SDL_BUTTON_LEFT;
    escapeGesture.button.x        = 40;
    escapeGesture.button.y        = 40;
    REQUIRE(dispatch(escapeGesture).capturePointer);
    SDL_Event escape{};
    escape.type           = SDL_KEYDOWN;
    escape.key.windowID   = windowId;
    escape.key.keysym.sym = SDLK_ESCAPE;
    REQUIRE(dispatch(escape, false, true).releasePointer);
    REQUIRE(!host->workspace().hasPointerCapture());
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(3));


    event.type     = SDL_MOUSEBUTTONDOWN;
    event.button.x = 300;
    event.button.y = 100;
    REQUIRE(!dispatch(event, true).handled);
    REQUIRE(!host->workspace().hasPointerCapture());
    event.button.x = 40;
    event.button.y = 40;
    REQUIRE(dispatch(event).capturePointer);
    event                 = {};
    event.type            = SDL_WINDOWEVENT;
    event.window.windowID = windowId;
    event.window.event    = SDL_WINDOWEVENT_FOCUS_LOST;
    REQUIRE(dispatch(event).releasePointer);
    REQUIRE(!host->workspace().hasPointerCapture());
    REQUIRE_EQ(asset->form()->sourceRevision().value(), std::uint64_t(3));
    event                = {};
    event.type           = SDL_KEYDOWN;
    event.key.windowID   = windowId;
    event.key.keysym.sym = SDLK_RETURN;
    REQUIRE(!dispatch(event).handled);
    frame();
    REQUIRE(gfx->getPresentOverlay() != nullptr);
    REQUIRE(eve::ui::UIHost::resolve(host->savePanel().host())->get().findById("spawn/asset/save"));
    REQUIRE(eve::ui::UIHost::resolve(host->exportPanel().host())->get().findById("spawn/export/prepare"));

    host.reset();
    EditorAssetFiles         appFiles;
    eve::editor::DocumentKey appKey{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("window-app")};
    auto appBinding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
      "map_uri":"project://forest.json","spawns_uri":"project://forest.spawns.json",
      "pixel_to_world":{"origin":[0,0],"scale":1}})")
                          .expect("application binding");
    auto app = SpawnEditorApplication::open(
                   appBinding, R"({"schema":"eve.map.object-contract","version":1,"types":[]})", appFiles, store,
                   appKey, "content://window-app", content, content, filesystem->observeAtomicWrites(), {0, 0, 0, 0, 4},
                   layout, "application.spawns.json")
                   .expect("real window application");
    auto appControls = app->host().workspace().controlsPanel().build();
    find(find, appControls, "Edit region: north")->onClick();
    auto* sdlWindow = static_cast<SDL_Window*>(native->getHandle());
    SDL_ShowWindow(sdlWindow);
    SDL_RaiseWindow(sdlWindow);
    const auto focusStarted = SDL_GetTicks();
    while (SDL_GetKeyboardFocus() != sdlWindow && Uint32(SDL_GetTicks() - focusStarted) < 1500) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    const bool foreground = SDL_GetKeyboardFocus() == sdlWindow;
    SDL_Event  appEvent{};
    appEvent.type            = SDL_MOUSEBUTTONDOWN;
    appEvent.button.windowID = windowId + 1;
    appEvent.button.which    = 24;
    appEvent.button.button   = SDL_BUTTON_LEFT;
    appEvent.button.x        = 40;
    appEvent.button.y        = 40;
    REQUIRE(!app->processWindowEvent(appEvent, *sdlWindow, false, false).expect("foreign window").handled);
    REQUIRE(!app->closeState().pointerCapture);
    appEvent.button.windowID = windowId;
    REQUIRE(!app->processWindowEvent(appEvent, *sdlWindow, true, false).expect("UI blocked").handled);
    ui->processEvent(&appEvent);
    auto capture = app->processWindowEvent(appEvent, *sdlWindow, false, false);
    if (foreground) {
        REQUIRE(std::move(capture).expect("native capture").capturePointer);
        REQUIRE((SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MOUSE_CAPTURE) != 0);
        REQUIRE(app->closeState().pointerCapture);
        // Exercise a real circle drag: a down-only gesture has a zero-radius contour.
        SDL_Event motion{};
        motion.type            = SDL_MOUSEMOTION;
        motion.motion.windowID = windowId;
        motion.motion.which    = appEvent.button.which;
        motion.motion.x        = 72;
        motion.motion.y        = 40;
        ui->processEvent(&motion);
        app->processWindowEvent(motion, *sdlWindow, false, false).expect("native circle preview drag");
    } else {
        REQUIRE(!capture.ok());
        REQUIRE_EQ(capture.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);
        REQUIRE(!app->closeState().pointerCapture);
        REQUIRE(!(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MOUSE_CAPTURE));
        REQUIRE_EQ(app->asset()->form()->sourceRevision().value(), std::uint64_t(1));
        std::fprintf(stdout,
                     "spawn editor: foreground unavailable; verified capture rejection and cancellation; positive "
                     "native capture is unverified\n");
    }
    {
        ecs::ScopedTable appScope(*app->world());
        auto             layers = ecs::View<eve::map::TileLayer, eve::map::TileLayer::Config>();
        for (auto it = layers.begin(); it != layers.end(); ++it) it.entity()->setTint(0, 0.5f, 0, 1);
    }
    eve::graphics::PrimitiveCanvas2D scene;
    auto                             appFrame = [&] {
        gfx->clearScreen();
        app->render(*gfx, *canvas, scene).expect("actual application map scene and overlay");
        gfx->drawTexturedRectRGBA(canvas->getTexture(), 0, 0, 240, 240, 1, 1, 1, 1);
        ecs::ScopedTable appScope(*app->world());
        ui->beginFrameAndRender();
        gfx->present();
        ui->dispatchEvents();
    };
    appFrame();
    auto appBackground = gfx->getPixel(96, 96);
    REQUIRE(appBackground.r < 0.05f);
    REQUIRE(appBackground.g > 0.1f);
    REQUIRE(gfx->getPixel(200, 96).g < 0.05f);
    if (foreground) {
        const auto appPreview = circlePreviewCoverage([&](int x, int y) { return gfx->getPixel(x, y).r; });
        REQUIRE(appPreview.r > 0.9f);
        REQUIRE(appPreview.offContourRed < 0.05f);
        appEvent.type     = SDL_MOUSEBUTTONUP;
        appEvent.button.x = 700;
        appEvent.button.y = 80;
        ui->processEvent(&appEvent);
        REQUIRE(
            app->processWindowEvent(appEvent, *sdlWindow, true, false).expect("outside native release").releasePointer);
        REQUIRE(!(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MOUSE_CAPTURE));
        REQUIRE(!app->closeState().pointerCapture);
        REQUIRE_EQ(app->asset()->form()->sourceRevision().value(), std::uint64_t(2));
        appEvent.type     = SDL_MOUSEBUTTONDOWN;
        appEvent.button.x = 40;
        appEvent.button.y = 40;
        REQUIRE(app->processWindowEvent(appEvent, *sdlWindow, false, false).expect("capture again").capturePointer);
        app->cancelWindowInteraction(*sdlWindow).expect("explicit cancel and native release");
        REQUIRE(!(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MOUSE_CAPTURE));
        REQUIRE_EQ(app->asset()->form()->sourceRevision().value(), std::uint64_t(2));
    } else {
        app->cancelWindowInteraction(*sdlWindow).expect("unfocused cancellation is idempotent");
        REQUIRE_EQ(app->asset()->form()->sourceRevision().value(), std::uint64_t(1));
    }
    // This application uses zoom 4: World2D (24,24) projects onto target pixel (96,96).
    auto pointSource = app->asset()->form()->captureSource().expect("source for layer order").source;
    pointSource.find("instances")
        ->at(0)
        .set("region",
             eve::Value::fromJson(R"({"kind":"points","space":"world2d","points":[[24,24]]})").expect("point region"));
    app->asset()->form()->view().write("$source", pointSource).expect("actual point source edit");
    eve::graphics::PrimitivePaint marker;
    marker.mode  = eve::graphics::PaintMode::Fill;
    marker.color = {0.2f, 1.f, 0.4f, 1.f};
    scene.drawRect({84, 84}, {108, 108}, marker);
    appFrame();
    const auto markerCenter = gfx->getPixel(96, 96);
    REQUIRE((markerCenter.g > 0.8f && markerCenter.r < 0.4f && markerCenter.b < 0.6f));
    bool pointAboveScene = false;
    for (int y = 92; y <= 100 && !pointAboveScene; ++y)
        for (int x = 92; x <= 100 && !pointAboveScene; ++x) {
            const auto pixel = gfx->getPixel(x, y);
            pointAboveScene  = pixel.b > 0.6f && pixel.g > 0.6f && pixel.r < 0.6f;
        }
    REQUIRE(pointAboveScene);
    app->asset()->save().expect("save application source");
    app->checkClose().expect("clean normal close");
    app.reset();
    gfx->setScreenReadbackEnabled(false);
}
TEST_CASE("Spawn export writer leases reject retired providers without retaining or rebinding modules") {
    using namespace eve::spawn_editor;
    eve::filesystem::AtomicTextWriteLease absent;
    REQUIRE(!absent.available());
    REQUIRE(!absent.write("unused.json", "{}").ok());
    auto filesystem = std::make_shared<eve::filesystem::physfs::Filesystem>();
    std::weak_ptr<eve::filesystem::physfs::Filesystem> witness = filesystem;
    auto                                               lease   = filesystem->observeAtomicWrites();
    REQUIRE(lease.available());
    auto units = std::make_shared<Content>();
    auto document =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("lease-test"), mapSource(), units, units)
            .expect("document");
    std::shared_ptr<SpawnForm> form   = SpawnForm::create(std::move(document)).expect("form");
    auto                       frozen = PreparedSpawnExport::prepare(form).expect("independent source snapshot");
    auto                       panel  = createSpawnExportPanel(form, lease, "unused.json").expect("weak writer panel");
    panel->mountAs("weak-spawn-export");
    auto find = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& id) -> eve::ui::WidgetDesc* {
        if (node.id == id) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, id)) return found;
        return nullptr;
    };
    auto initial = panel->build();
    find(find, initial, "spawn/export/prepare")->onClick();
    auto prepared = panel->build();
    filesystem.reset();
    REQUIRE(witness.expired());
    REQUIRE(!lease.available());
    REQUIRE(eve::cap::query<eve::service::IFileSystem>() == nullptr);
    REQUIRE(!frozen.write(lease, "unused.json").ok());
    find(find, prepared, "spawn/export/write")->onClick();
    auto failed = panel->build();
    REQUIRE(find(find, failed, "spawn/export/error/0") != nullptr);
    REQUIRE(find(find, failed, "spawn/export/status")->text.find("Prepared source revision") != std::string::npos);
    REQUIRE_EQ(form->sourceRevision().value(), std::uint64_t(1));
    auto replacement = std::make_shared<eve::filesystem::physfs::Filesystem>();
    REQUIRE(replacement->observeAtomicWrites().available());
    REQUIRE(!lease.available());
    REQUIRE(!frozen.write(lease, "unused.json").ok());
    REQUIRE(!createSpawnExportPanel(form, lease, "unused.json").ok());
    replacement.reset();
    panel.reset();
    auto* managed = eve::filesystem::Filesystem::create();
    REQUIRE(managed != nullptr);
    auto managedLease = managed->observeAtomicWrites();
    REQUIRE(managedLease.available());
    auto rejected = frozen.write(managedLease, "../outside.json");
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.diagnostics()[0].message().find("relative") != std::string::npos);
}
}  // namespace test_spawn_editor

namespace test_spawn_editor {
TEST_CASE(
    "Spawn editor application loads canonical assets into a private world and rejects mismatched persisted identity") {
    using namespace eve::spawn_editor;
    EditorAssetFiles         files;
    auto                     outer = std::make_shared<ecs::Table>();
    ecs::ScopedTable         outerScope(*outer);
    eve::map::Map            callerMap;
    auto                     callerLayer = ecs::handle_of(callerMap.newLayer(2, 2, 8, 8));
    auto                     filesystem  = eve::filesystem::Filesystem::create();
    auto                     content     = std::make_shared<Content>();
    auto                     store       = std::make_shared<eve::editor::MemoryAtomicDocumentStore>();
    eve::editor::DocumentKey key{eve::editor::DocumentKind::Generic, eve::editor::AssetGuid("application")};
    auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
      "map_uri":"project://forest.json","spawns_uri":"project://forest.spawns.json",
      "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("binding");
    SpawnHostLayout layout{
        {0, 0, 200, 200}, {200, 0, 200, 200}, {400, 0, 200, 200}, {200, 200, 200, 80}, {400, 200, 200, 80}};
    auto open = [&] {
        return SpawnEditorApplication::open(binding, R"({"schema":"eve.map.object-contract","version":1,"types":[]})",
                                            files, store, key, "content://application", content, content,
                                            filesystem->observeAtomicWrites(), {}, layout, "forest.spawns.json");
    };
    binding.set("navigation_layer", 8);
    REQUIRE(!open().ok());
    REQUIRE_EQ(callerMap.getLayerCount(), 1);
    binding.set("navigation_layer", 0);
    auto originalLayout = layout;
    layout.save         = layout.canvas;
    REQUIRE(!open().ok());
    REQUIRE_EQ(callerMap.getLayerCount(), 1);
    layout   = originalLayout;
    auto app = open().expect("private editor");
    REQUIRE(app->closeState().unsavedSource);
    REQUIRE(!app->checkClose().ok());
    app->asset()->save().expect("save initial source");
    app->checkClose().expect("clean initial source");
    auto findById = [](auto&& self, eve::ui::WidgetDesc& node, const std::string& id) -> eve::ui::WidgetDesc* {
        if (node.id == id) return &node;
        for (auto& child : node.children)
            if (auto* found = self(self, child, id)) return found;
        return nullptr;
    };
    auto formTree = app->host().workspace().formPanel().build();
    findById(findById, formTree, "spawn/draft/begin")->onClick();
    REQUIRE(app->closeState().structureDraft);
    REQUIRE(!app->closeState().unsavedSource);
    app->asset()->save().expect("saving committed source preserves UI draft");
    REQUIRE(!app->checkClose().ok());
    auto draftedTree = app->host().workspace().formPanel().build();
    findById(findById, draftedTree, "spawn/draft/cancel")->onClick();
    REQUIRE(!app->closeState().structureDraft);
    app->checkClose().expect("draft explicitly resolved");

    REQUIRE(app->world().get() != outer.get());
    REQUIRE_EQ(callerMap.getLayerCount(), 1);
    REQUIRE(ecs::try_get(callerLayer) != nullptr);
    REQUIRE_EQ(app->asset()->form()->captureSource().expect("actual source").source.find("map")->asString(), "forest");
    auto changed = app->asset()->form()->captureSource().expect("source").source;
    changed.set("map", "other-map");
    app->asset()->form()->view().write("$source", changed).expect("edit identity");
    app->asset()->save().expect("persist edited source");
    app.reset();
    auto mismatch = open();
    REQUIRE(!mismatch.ok());
    REQUIRE_EQ(mismatch.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    REQUIRE_EQ(callerMap.getLayerCount(), 1);
    binding.set("version", 999);
    auto reads = files.reads;
    REQUIRE(!open().ok());
    REQUIRE_EQ(files.reads, reads);
    REQUIRE(ecs::try_get(callerLayer) != nullptr);
}

}  // namespace test_spawn_editor

namespace test_spawn_editor {
TEST_CASE(
    "Spawn committed region overlays share immutable projections across callbacks edits and history without "
    "recompiling") {
    using namespace eve::spawn_editor;
    auto content        = std::make_shared<Content>();
    int  validations    = 0;
    content->onValidate = [&] { ++validations; };
    auto  source        = mapSource();
    auto& instances     = *source.find("instances")->getIf<eve::Value::Array>();
    auto  points        = instances.front();
    points.set("id", "points");
    points.set(
        "region",
        eve::Value::fromJson(R"({"kind":"points","space":"world2d","points":[[10,20],[30,40]]})").expect("points"));
    auto polygon = instances.front();
    polygon.set("id", "polygon");
    polygon.set("region",
                eve::Value::fromJson(R"({"kind":"polygon","space":"world2d","vertices":[[0,0],[10,0],[0,10]]})")
                    .expect("polygon"));
    instances.push_back(points);
    instances.push_back(polygon);
    auto target =
        eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("regions"), source, content, content)
            .expect("canonical document");
    auto owner = target->shareCompiled();
    REQUIRE(owner.get() == &target->compiled());
    std::shared_ptr<SpawnForm> form  = SpawnForm::create(std::move(target)).expect("form");
    auto                       first = form->captureMap().expect("committed projection");
    REQUIRE(first.map.get() == owner.get());
    auto workspace = SpawnEditorWorkspace::create(form, {{100, -50}, 2}, {10, 20, 1, 2, 3}).expect("workspace");
    struct Overlay final : eve::editor::IEditorOverlay {
        struct Circle {
            float    x, y, radius;
            unsigned color;
        };
        std::vector<Circle>   circles;
        int                   lines = 0;
        std::function<void()> callback;
        void                  line(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                                   const eve::editor::OverlayStyle&) override {
            ++lines;
        }
        void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                       const eve::editor::OverlayStyle&) override {}
        void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}
        void circle(const eve::editor::OverlayPoint& point, float radius,
                    const eve::editor::OverlayStyle& style) override {
            circles.push_back({point.x, point.y, radius, style.color});
            if (callback) callback();
        }
        void clear() {
            circles.clear();
            lines = 0;
        }
    } overlay;
    const auto admittedCalls = validations;
    for (int i = 0; i < 3; ++i) {
        overlay.clear();
        workspace->drawOverlay(overlay).expect("idle committed overlay without selection");
        REQUIRE_EQ(overlay.circles.size(), std::size_t(3));
        REQUIRE_EQ(overlay.lines, 3);
        REQUIRE_EQ(overlay.circles[0].x, -143.f);
        REQUIRE_EQ(overlay.circles[0].y, 89.f);
        REQUIRE_EQ(overlay.circles[0].radius, 4.5f);
        REQUIRE_EQ(overlay.circles[0].color, 0xff40c8ffU);
        REQUIRE_EQ(overlay.circles[1].radius, 3.f);
    }
    REQUIRE_EQ(validations, admittedCalls);
    bool edited  = false;
    auto changed = source;
    changed.find("instances")->at(0).find("region")->set("radius", 6);
    overlay.callback = [&] {
        if (edited) return;
        edited = true;
        REQUIRE(!workspace->setFrame({}).ok());
        form->view().write("$source", changed).expect("edit during overlay callback");
    };
    overlay.clear();
    workspace->drawOverlay(overlay).expect("old snapshot remains live during replacement");
    REQUIRE(edited);
    REQUIRE_EQ(overlay.circles[0].radius, 4.5f);
    auto current = form->captureMap().expect("current immutable projection");
    REQUIRE(current.map.get() != first.map.get());
    REQUIRE(current.revision != first.revision);
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(first.map->instances()[0].region).radius, 3.0);
    overlay.callback = {};
    overlay.clear();
    workspace->drawOverlay(overlay).expect("updated preview");
    REQUIRE_EQ(overlay.circles[0].radius, 9.f);
    form->undo().expect("undo region edit");
    overlay.clear();
    workspace->drawOverlay(overlay).expect("undo preview");
    REQUIRE_EQ(overlay.circles[0].radius, 4.5f);
    form->redo().expect("redo region edit");
    overlay.clear();
    workspace->drawOverlay(overlay).expect("redo preview");
    REQUIRE_EQ(overlay.circles[0].radius, 9.f);
    auto beforeFailure = form->captureMap().expect("before invalid source");
    auto invalidSource = changed;
    invalidSource.find("instances")->at(0).find("region")->set("radius", -1);
    REQUIRE(!form->view().write("$source", invalidSource).ok());
    auto afterFailure = form->captureMap().expect("committed state after rejected edit");
    REQUIRE(beforeFailure.map.get() == afterFailure.map.get());
    REQUIRE_EQ(beforeFailure.revision, afterFailure.revision);
    overlay.clear();
    workspace->drawOverlay(overlay).expect("rejected edit preserves preview");
    REQUIRE_EQ(overlay.circles[0].radius, 9.f);
    const auto revision = form->sourceRevision();
    overlay.callback    = [] { throw std::runtime_error("overlay rejected"); };
    REQUIRE(!workspace->drawOverlay(overlay).ok());
    REQUIRE_EQ(form->sourceRevision(), revision);
    overlay.callback = {};
    overlay.clear();
    workspace->drawOverlay(overlay).expect("guard recovered");
    workspace.reset();
    form.reset();
    content.reset();
    REQUIRE_EQ(first.map->id(), "forest");
    REQUIRE_EQ(first.map->instances().size(), std::size_t(3));
    auto preciseSource = mapSource();
    preciseSource.find("instances")->at(0).find("region")->set("center", eve::Value::array({1e12 + 100, -1e12 + 50}));
    auto preciseContent = std::make_shared<Content>();
    auto preciseTarget  = eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("precise"),
                                                                          preciseSource, preciseContent, preciseContent)
                             .expect("large origin document");
    std::shared_ptr<SpawnForm> preciseForm = SpawnForm::create(std::move(preciseTarget)).expect("precise form");
    auto                       preciseWorkspace =
        SpawnEditorWorkspace::create(preciseForm, {{1e12, -1e12}, 2}, {}).expect("precise workspace");
    overlay.clear();
    preciseWorkspace->drawOverlay(overlay).expect("double projection precedes float sink conversion");
    REQUIRE_EQ(overlay.circles[0].x, 50.f);
    REQUIRE_EQ(overlay.circles[0].y, 25.f);
    REQUIRE_EQ(overlay.circles[0].radius, 1.5f);
}
}  // namespace test_spawn_editor
