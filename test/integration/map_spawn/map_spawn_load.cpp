// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"map_spawn","cases":{"platform-file":[
"Map spawn staged root preserves map-relative external tileset resolution"]}}
*/
// clang-format on
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include "common/ServiceInterfaces.h"
#include "filesystem/Filesystem.h"
#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/Pathfinder.h"
#include "map/PathfinderState.h"
#include "map/spawn/MapSpawnAssets.h"
#include "map/spawn/MapSpawnBinding.h"
#include "map/spawn/MapSpawnLoad.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_map_spawn {
namespace {
struct Files final : eve::service::IFileSystem {
    std::map<std::string, std::string> assets;
    int                                reads = 0;
    std::function<void()>              onRead;
    eve::service::IFileSystem*         dependencies = nullptr;
    bool                               readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        ++reads;
        if (onRead) onRead();
        auto it = assets.find(path);
        if (it == assets.end()) return dependencies ? dependencies->readFile(path, out) : false;
        out.assign(it->second.begin(), it->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override { return assets.contains(path); }
};
struct Content final : eve::spawn::IUnitDefinitionResolver, eve::spawn::ITemplateResolver {
    eve::Result<void> validate(const eve::DefinitionRef&) const override { return eve::Result<void>::success(); }
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
        auto source = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"camp",
          "content":{"kind":"fixed","members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1}]},
          "schedule":{"kind":"once"},"limits":{"alive":1,"total":{"kind":"unlimited"},"budget_group":"hostiles"},
          "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":1},
          "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                          .expect("template");
        return eve::spawn::compileTemplate(source, *this);
    }
};
struct Factory final : eve::spawn::ISpawnFactory {
    int                                     preparations = 0;
    eve::Result<eve::spawn::PrepareOutcome> prepare(const eve::spawn::BatchRequest&,
                                                    std::span<const eve::spawn::Point>) override {
        ++preparations;
        return eve::Result<eve::spawn::PrepareOutcome>::success(eve::spawn::PlacementBlocked{});
    }
    eve::Result<eve::spawn::RemovalReceipt> remove(const eve::spawn::RemovalRequest& request) override {
        return eve::Result<eve::spawn::RemovalReceipt>::success({request.subjects.size()});
    }
};
struct FailAdmission final : ecs::IComponentSlotBuffer {
    int                   calls = 0;
    bool                  armed = true;
    std::uint32_t         size() const override { return 0; }
    const std::type_info& getType() const override { return typeid(FailAdmission); }
    void                  ensure_space(std::uint32_t) override {
        if (armed && ++calls == 2) throw std::runtime_error("second layer admission");
    }
    void                       transfer_slot(std::uint32_t, std::uint32_t, ecs::IComponentSlotBuffer*) override {}
    ecs::IComponentSlotBuffer* ensure_equivalent(ecs::IComponentManager*) override { return nullptr; }
    void                       reset_slot(std::uint32_t) override {}
};
struct Fixture {
    Files                    files;
    Content                  content;
    std::shared_ptr<Factory> factory = std::make_shared<Factory>();
    eve::map::Map            target;
    eve::Value binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://maps/forest.json","spawns_uri":"project://maps/forest.spawns.json",
        "pixel_to_world":{"origin":[10,-20],"scale":0.5}})")
                             .expect("binding");
    Fixture() {
        files.assets["maps/forest.json"]        = R"({"width":1,"height":1,"tilewidth":32,"tileheight":32,"data":[1]})";
        files.assets["maps/forest.spawns.json"] = R"({"schema":"eve.map-spawns","version":1,"map":"forest",
          "budgets":[{"id":"hostiles","alive_limit":1}],"instances":[{"id":"camp",
          "template":{"type":"spawn.template","id":"camp"},"region":{"kind":"circle","space":"world2d","center":[0,0],"radius":5},
          "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})";
    }
    eve::Result<eve::map_spawn::PreparedMapSpawn> prepare(
        std::string_view objectContract = R"({"schema":"eve.map.object-contract","version":1,"types":[]})") {
        return eve::map_spawn::PreparedMapSpawn::prepare(
            target, binding, files, content, content, *eve::PersistentId::parse("01900000-0000-7000-8000-000000000088"),
            42, objectContract, {factory, nullptr, {}});
    }
};
constexpr auto contract = R"({"schema":"eve.map.object-contract","version":1,"types":[]})";
}  // namespace
TEST_CASE("Map spawn load rejects invalid binding references and mismatched sidecar identity") {
    Fixture f;
    f.binding.set("map_id", eve::Value("other"));
    REQUIRE(!f.prepare().ok());
    f.binding.set("map_id", eve::Value("forest"));
    f.binding.set("spawns_uri", eve::Value("project://missing.json"));
    CHECK(!f.prepare().ok());
    f.binding.set("spawns_uri", eve::Value("project://maps/forest.spawns.json"));
    f.binding.set("unexpected", eve::Value(1));
    CHECK(!f.prepare().ok());
    CHECK_EQ(f.factory->preparations, 0);
}
TEST_CASE("Map spawn staged load publishes map before handing out its runtime session") {
    Fixture f;
    auto    candidate = f.prepare();
    REQUIRE(candidate.ok());
    CHECK_EQ(f.factory->preparations, 0);
    {
        auto view = ecs::View<eve::map::TileLayer>();
        REQUIRE(!candidate.value().commit(f.target).ok());
    }
    const auto reads = f.files.reads;
    REQUIRE(reads >= 2);
    auto loaded = candidate.value().commit(f.target);
    CHECK_EQ(f.files.reads, reads);
    REQUIRE(loaded.ok());
    REQUIRE(f.target.getLayerCount() == 1);
    auto point = loaded.value()->toWorld({4, 8}).expect("explicit transform");
    CHECK_EQ(point.x, 12.0);
    CHECK_EQ(point.y, -16.0);
    const auto pixel = loaded.value()->toPixel(point).expect("presentation inverse transform");
    CHECK_EQ(pixel.x, 4.0);
    CHECK_EQ(pixel.y, 8.0);
    CHECK(!loaded.value()->toPixel({std::numeric_limits<double>::infinity(), 0}).ok());
    CHECK(!loaded.value()->toPixel({0, std::numeric_limits<double>::quiet_NaN()}).ok());
    CHECK(!loaded.value()->toPixel({std::numeric_limits<double>::max(), 0}).ok());
    CHECK_EQ(f.factory->preparations, 0);
    CHECK(!candidate.value().commit(f.target).ok());
    REQUIRE(loaded.value()->close(eve::spawn::DetachPolicy::KeepUnits).ok());
    CHECK(loaded.value()->close(eve::spawn::DetachPolicy::KeepUnits).ok());
}
TEST_CASE("Map spawn failed map admission preserves previous map and retryable candidate") {
    Fixture f;
    REQUIRE(f.target.loadFromTextWithObjectContract(R"({"width":2,"height":1,"data":[1,1]})", contract).ok());
    auto* previous = f.target.getLayer(0);
    REQUIRE(previous != nullptr);
    auto countLayers = [] {
        std::size_t count = 0;
        auto        view  = ecs::View<eve::map::TileLayer>();
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
        return count;
    };
    auto before = countLayers();
    REQUIRE(!f.prepare("{}").ok());
    REQUIRE(countLayers() == before);
    auto candidate = f.prepare().expect("staged load");
    REQUIRE(countLayers() == before);
    {
        auto view = ecs::View<eve::map::TileLayer>();
        REQUIRE(!candidate.commit(f.target).ok());
    }
    CHECK(f.target.getLayer(0) == previous);
    REQUIRE(candidate.commit(f.target).ok());
    CHECK_EQ(f.factory->preparations, 0);
}

TEST_CASE("Map spawn staged root preserves map-relative external tileset resolution") {
    Fixture f;
    f.files.assets["maps/forest.json"] = R"({"width":1,"height":1,"data":[1],
        "tilesets":[{"firstgid":1,"source":"wolf.tsj"}]})";
    struct Directory {
        std::filesystem::path path;
        Directory() {
            const auto base = std::filesystem::temp_directory_path();
            for (int i = 0; i < 16; ++i) {
                auto next = base / ("eve-map-spawn-" + std::to_string(std::random_device{}()));
                if (std::filesystem::create_directory(next)) {
                    path = std::move(next);
                    break;
                }
            }
        }
        ~Directory() {
            if (!path.empty()) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } directory;
    REQUIRE(!directory.path.empty());
    std::filesystem::create_directory(directory.path / "maps");
    {
        std::ofstream tileset(directory.path / "maps/wolf.tsj", std::ios::binary);
        tileset << R"({"tilewidth":32,"tileheight":32,"columns":1,"tilecount":1,
            "tiles":[{"id":0,"properties":[{"name":"walkable","value":false}]}]})";
        REQUIRE(tileset.good());
    }
    auto* files = eve::filesystem::Filesystem::create();
    REQUIRE(files != nullptr);
    REQUIRE(files->setSource(directory.path.string()));
    f.files.dependencies = files;
    const auto watches   = files->getWatchCount();
    auto       candidate = f.prepare().expect("staged asset with relative tileset");
    CHECK_EQ(files->getWatchCount(), watches);
    auto session = candidate.commit(f.target);
    REQUIRE(session.ok());
    auto* layer = f.target.getLayer(0);
    REQUIRE(layer != nullptr);
    REQUIRE(layer->resource()->dependencyPaths.size() == 1);
    CHECK_EQ(layer->resource()->path, "maps/forest.json");
    CHECK_EQ(layer->resource()->dependencyPaths[0], "maps/wolf.tsj");
    REQUIRE(session.value()->close(eve::spawn::DetachPolicy::KeepUnits).ok());
}

TEST_CASE("Map prepared candidates isolate live queries and revoke when their owner is destroyed") {
    Files files;
    auto  count = [] {
        std::size_t n    = 0;
        auto        view = ecs::View<eve::map::TileLayer>();
        for (auto it = view.begin(); it != view.end(); ++it) ++n;
        return n;
    };
    const auto                                 before = count();
    eve::map::MapLease                         lease;
    std::unique_ptr<eve::map::PreparedMapLoad> candidate;
    eve::map::NavigationGridSnapshot           capture;
    {
        eve::map::Map target;
        lease = target.observeLifetime();
        REQUIRE(lease.isCurrent());
        auto prepared = target
                            .prepareAssetTextWithObjectContract(R"({"width":2,"height":1,"data":[1,1]})",
                                                                "maps/staged.json", files, contract)
                            .expect("isolated candidate");
        REQUIRE(count() == before);
        capture = prepared->captureNavigation(0).expect("owning navigation");
        {
            eve::map::Map other;
            REQUIRE(!prepared->commit(other).ok());
            CHECK(other.getLayer(0) == nullptr);
        }
        candidate = std::move(prepared);
    }
    CHECK(!lease.isCurrent());
    CHECK(capture.valid());
    CHECK_EQ(capture.width(), 2);
    REQUIRE(!candidate->grid(0).ok());
    eve::map::Map replacement;
    CHECK(!candidate->commit(replacement).ok());
    candidate.reset();
    CHECK_EQ(count(), before);
}
TEST_CASE("Map prepared publication rolls back an admitted prefix and preserves a retryable candidate") {
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    Files            files;
    eve::map::Map    target;
    REQUIRE(target.loadFromTextWithObjectContract(R"({"width":1,"height":1,"data":[9]})", contract).ok());
    auto* previous = target.getLayer(0);
    REQUIRE(previous != nullptr);
    auto count = [] {
        std::size_t n    = 0;
        auto        view = ecs::View<eve::map::TileLayer>();
        for (auto it = view.begin(); it != view.end(); ++it) ++n;
        return n;
    };
    const auto before  = count();
    auto&      manager = world.getOrCreateManager<eve::map::TileLayer>();
    auto*      failing = new FailAdmission;
    manager.components.emplace(std::type_index(typeid(FailAdmission)), failing);
    auto candidate = target
                         .prepareAssetTextWithObjectContract(R"({"width":1,"height":1,"layers":[
        {"type":"tilelayer","data":[1]},{"type":"tilelayer","data":[2]}]})",
                                                             "maps/staged.json", files, contract)
                         .expect("isolated two layer candidate");
    REQUIRE(count() == before);
    REQUIRE(!candidate->commit(target).ok());
    REQUIRE(count() == before);
    CHECK(target.getLayer(0) == previous);
    CHECK_EQ(previous->getTile(0, 0), 9);
    CHECK_EQ(candidate->layerCount(), std::size_t(2));
    failing->armed = false;
    REQUIRE(candidate->commit(target).ok());
    REQUIRE(target.getLayer(1) != nullptr);
    CHECK(target.getLayer(2) == nullptr);
    CHECK_EQ(target.getLayer(0)->getTile(0, 0), 1);
    CHECK_EQ(target.getLayer(1)->getTile(0, 0), 2);
    CHECK_EQ(count(), before + 2);
}
}  // namespace test_map_spawn

namespace test_map_spawn {
namespace {
eve::Result<eve::ContentId> assetsHash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (const unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        state = state * 6364136223846793005ULL + 1;
        byte  = static_cast<std::uint8_t>(state >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
eve::SnapshotEnvelope resealAssets(eve::SnapshotEnvelope value) {
    return eve::makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId, value.revision,
                                     value.tick, std::move(value.payload), assetsHash)
        .expect("reseal malformed asset fixture");
}
}  // namespace
TEST_CASE("Map Spawn freezes actual structural dependency bytes and replays the original map without live files") {
    Fixture f;
    f.files.assets["maps/forest.json"] =
        R"({"width":1,"height":1,"data":[1],"tilesets":[{"firstgid":1,"source":"wolf.tsj"}]})";
    f.files.assets["maps/wolf.tsj"] =
        R"({"tilewidth":32,"tileheight":32,"columns":1,"tilecount":1,"tiles":[{"id":0,"properties":[{"name":"walkable","value":false}]}]})";
    auto candidate = f.prepare().expect("actual structural load");
    auto session   = candidate.commit(f.target).expect("published scene");
    auto assets    = session->assets();
    REQUIRE(assets != nullptr);
    REQUIRE_EQ(assets->files().size(), std::size_t(3));
    session->close(eve::spawn::DetachPolicy::KeepUnits).expect("close original");
    session.reset();
    const auto epoch = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000099");
    auto       envelope =
        assets->seal(epoch, eve::Revision(4), eve::SimulationTick(12), assetsHash).expect("source participant");
    f.files.assets.clear();
    assets.reset();
    auto parsed = eve::map_spawn::MapSpawnAssets::parse(envelope, epoch, assetsHash).expect("own original closure");
    REQUIRE_EQ(parsed->files().size(), std::size_t(3));
    eve::map::Map replay;
    replay.loadFromTextWithObjectContract(R"({"width":1,"height":1,"data":[9]})", contract).expect("previous target");
    const auto previous = ecs::handle_of(replay.getLayer(0));
    const auto reads    = f.files.reads;
    auto       staged   = parsed->prepareMap(replay).expect("canonical original map staging");
    REQUIRE(ecs::try_get(previous) == replay.getLayer(0));
    REQUIRE_EQ(staged->grid(0).expect("original grid").stepX, 32.0);
    staged->commit(replay).expect("publish original map");
    eve::map::Pathfinder navigation(replay.getLayer(0));
    REQUIRE(!navigation.isWalkable(0, 0));
    REQUIRE_EQ(f.files.reads, reads);
    auto  missing = envelope;
    auto* rows    = missing.payload.find("files")->getIf<eve::Value::Array>();
    std::erase_if(*rows, [](const auto& row) { return row.find("path")->asString() == "maps/wolf.tsj"; });
    auto incomplete = eve::map_spawn::MapSpawnAssets::parse(resealAssets(missing), epoch, assetsHash)
                          .expect("structural facts await semantic dependency validation");
    const auto live = ecs::handle_of(replay.getLayer(0));
    REQUIRE(!incomplete->prepareMap(replay).ok());
    REQUIRE(ecs::try_get(live) == replay.getLayer(0));
}
TEST_CASE(
    "Map Spawn asset capture freezes binding before filesystem callbacks and rejects same-path layer replacement") {
    Fixture f;
    bool    changed = false;
    f.files.onRead  = [&] {
        if (changed) return;
        changed = true;
        f.binding.set("map_uri", "project://maps/other.json");
        f.binding.find("pixel_to_world")->set("scale", 99);
    };
    auto candidate = f.prepare().expect("original binding copied before file callback");
    auto session   = candidate.commit(f.target).expect("original scene");
    REQUIRE(changed);
    REQUIRE_EQ(session->pixelTransform().scale, 0.5);
    REQUIRE_EQ(session->assets()->binding().find("map_uri")->asString(), "project://maps/forest.json");
    auto replacement = session->assets()->prepareMap(f.target).expect("same original path and geometry");
    replacement->commit(f.target).expect("different actual layer incarnation");
    REQUIRE(!session->navigationNeedsRefresh(f.target).ok());
    REQUIRE(!session->prepareNavigationRefresh(f.target).ok());
    REQUIRE(!session->advance(f.target, {eve::SimulationTick(1), eve::Duration(0)}).ok());
    REQUIRE_EQ(f.factory->preparations, 0);
    session->close(eve::spawn::DetachPolicy::KeepUnits).expect("close invalidated scene");
}
TEST_CASE("Map Spawn asset participant rejects corrupt schema binding and byte records before map publication") {
    Fixture    f;
    auto       candidate = f.prepare().expect("source");
    auto       session   = candidate.commit(f.target).expect("scene");
    auto       assets    = session->assets();
    const auto epoch     = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000099");
    auto original = assets->seal(epoch, eve::Revision(1), eve::SimulationTick(0), assetsHash).expect("participant");
    auto corrupt  = original;
    corrupt.payload.set("unknown", true);
    REQUIRE(!eve::map_spawn::MapSpawnAssets::parse(corrupt, epoch, assetsHash).ok());
    for (unsigned mode = 0; mode < 9; ++mode) {
        auto  invalid = original;
        auto* rows    = invalid.payload.find("files")->getIf<eve::Value::Array>();
        switch (mode) {
            case 0: invalid.payload.set("unknown", true); break;
            case 1: invalid.payload.find("binding")->set("unknown", true); break;
            case 2: invalid.payload.find("binding")->find("pixel_to_world")->set("scale", 0); break;
            case 3: rows->at(0).set("bytes_hex", "0"); break;
            case 4: rows->at(0).set("bytes_hex", "FF"); break;
            case 5: rows->push_back(rows->at(0)); break;
            case 6: rows->erase(rows->begin()); break;
            case 7: invalid.schemaVersion = eve::SchemaVersion(2); break;
            case 8: rows->at(0).set("unknown", true); break;
        }
        REQUIRE(!eve::map_spawn::MapSpawnAssets::parse(resealAssets(invalid), epoch, assetsHash).ok());
    }
    auto copied   = original;
    auto mutating = [&](std::string_view text) {
        copied.payload = eve::Value::object({});
        return assetsHash(text);
    };
    auto parsed = eve::map_spawn::MapSpawnAssets::parse(copied, epoch, mutating)
                      .expect("envelope copied before digest callbacks");
    REQUIRE_EQ(parsed->files().size(), std::size_t(2));
    session->close(eve::spawn::DetachPolicy::KeepUnits).expect("close");
}
}  // namespace test_map_spawn

namespace test_map_spawn {
TEST_CASE("Map Spawn source callbacks cannot continue admission after the borrowed target owner is revoked") {
    Fixture f;
    auto    target   = std::make_unique<eve::map::Map>();
    auto&   borrowed = *target;
    f.files.onRead   = [&] { target.reset(); };
    auto result      = eve::map_spawn::PreparedMapSpawn::prepare(
        borrowed, f.binding, f.files, f.content, f.content,
        *eve::PersistentId::parse("01900000-0000-7000-8000-000000000088"), 42, contract, {f.factory, nullptr, {}});
    REQUIRE(!result.ok());
    REQUIRE(!target);
    REQUIRE_EQ(f.factory->preparations, 0);
}
}  // namespace test_map_spawn

namespace test_map_spawn {
template <class Component>
void rejectMissingGridComponent(eve::map::Map& map, eve::map::TileLayer& layer) {
    auto* buffer = layer.getComponentManager().template getComponentBuffer<Component>();
    REQUIRE(buffer != nullptr);
    auto saved = *buffer->ptr(layer.id);
    buffer->reset_slot(layer.id);
    const auto before = layer.getComponentManager().components.size();
    REQUIRE(!eve::map::MapGridState::capture(map).ok());
    REQUIRE(!buffer->has(layer.id));
    REQUIRE_EQ(layer.getComponentManager().components.size(), before);
    buffer->get(layer.id) = std::move(saved);  // Explicit repair by the owning test, never by capture.
}
TEST_CASE("Map runtime grid roundtrip owns flags terrain navigation and rejects malformed candidates") {
    Fixture f;
    auto    candidate = f.prepare().expect("prepare");
    auto    session   = candidate.commit(f.target).expect("commit");
    auto*   layer     = f.target.getLayer(0);
    layer->setTile(0, 0, static_cast<int>(0x80000002u));
    layer->tiles()->terrainIds[0] = 7;
    eve::map::TileLayer::Tileset::Visual visual;
    visual.gid      = 2;
    visual.walkable = false;
    visual.cost     = 3.5f;
    layer->tileset()->visuals.push_back(visual);
    layer->tileset()->terrainRules.push_back({2, 7, 0, 3});
    layer->tileset()->customData.push_back({2, "tag", "string", "saved"});
    auto       frozen    = eve::map::MapGridState::capture(f.target).expect("capture actual grid");
    const auto epoch     = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000099");
    auto       envelope  = frozen->seal(epoch, eve::Revision(1), eve::SimulationTick(0), assetsHash).expect("seal");
    auto       parsed    = eve::map::MapGridState::parse(envelope, epoch, assetsHash).expect("parse");
    auto       restored  = session->assets()->prepareMap(f.target).expect("original structure");
    auto       wrongPath = envelope;
    wrongPath.payload.find("layers")->getIf<eve::Value::Array>()->at(0).getIf<eve::Value::Array>()->at(0) = "other-map";
    auto wrong = eve::map::MapGridState::parse(resealAssets(wrongPath), epoch, assetsHash)
                     .expect("structurally valid different source");
    REQUIRE(!restored->applyGridState(*wrong).ok());
    auto copied   = envelope;
    auto mutating = [&](std::string_view input) {
        copied.payload = eve::Value{};
        return assetsHash(input);
    };
    REQUIRE_EQ(eve::map::MapGridState::parse(copied, epoch, mutating).expect("copy before callback")->layerCount(),
               std::size_t(1));
    restored->applyGridState(*parsed).expect("runtime overlay");
    restored->commit(f.target).expect("publish");
    layer = f.target.getLayer(0);
    REQUIRE_EQ(layer->tiles()->gids[0], 0x80000002u);
    REQUIRE_EQ(layer->tiles()->terrainIds[0], 7);
    REQUIRE_EQ(layer->tiles()->chunks[0].nonEmpty, 1);
    REQUIRE_EQ(layer->tileset()->visuals.back().cost, 3.5f);
    REQUIRE(!layer->tileset()->visuals.back().walkable);
    REQUIRE_EQ(layer->tileset()->customData[0].value, "saved");
    eve::map::Pathfinder navigation(layer);
    REQUIRE(!navigation.isWalkable(0, 0));
    rejectMissingGridComponent<eve::map::TileLayer::Resource>(f.target, *layer);
    rejectMissingGridComponent<eve::map::TileLayer::Config>(f.target, *layer);
    rejectMissingGridComponent<eve::map::TileLayer::Tiles>(f.target, *layer);
    rejectMissingGridComponent<eve::map::TileLayer::Tileset>(f.target, *layer);

    for (int mode = 0; mode < 4; ++mode) {
        auto  bad    = envelope;
        auto& row    = bad.payload.find("layers")->getIf<eve::Value::Array>()->at(0);
        auto& values = *row.getIf<eve::Value::Array>();
        if (mode == 0) values[3] = eve::Value::Array{};
        if (mode == 1) values[2] = "00";
        if (mode == 2) values[5].getIf<eve::Value::Array>()->push_back(values[5].getIf<eve::Value::Array>()->at(0));
        if (mode == 3) bad.schemaVersion = eve::SchemaVersion(2);
        REQUIRE(!eve::map::MapGridState::parse(resealAssets(bad), epoch, assetsHash).ok());
    }
    session->close(eve::spawn::DetachPolicy::KeepUnits).expect("close");
}
TEST_CASE("Map binding authoring admission shares exact runtime validation and owns immutable facts") {
    Fixture    fixture;
    const auto original = fixture.binding;
    auto       source   = original;
    source.set("navigation_layer", 2);
    auto admitted = eve::map_spawn::MapSpawnBinding::decode(source).expect("binding facts only");
    REQUIRE_EQ(admitted.mapId(), std::string("forest"));
    REQUIRE_EQ(admitted.mapUri(), std::string("project://maps/forest.json"));
    REQUIRE_EQ(admitted.spawnsUri(), std::string("project://maps/forest.spawns.json"));
    REQUIRE_EQ(admitted.transform().origin.x, 10.0);
    REQUIRE_EQ(admitted.transform().origin.y, -20.0);
    REQUIRE_EQ(admitted.transform().scale, 0.5);
    REQUIRE_EQ(admitted.navigationLayer(), std::size_t(2));
    source.set("map_id", "changed");
    auto moved = std::move(admitted);
    REQUIRE_EQ(moved.mapId(), std::string("forest"));
    REQUIRE_EQ(fixture.files.reads, 0);
    REQUIRE_EQ(fixture.factory->preparations, 0);
    const std::vector<std::function<void(eve::Value&)>> invalid{
        [](auto& v) { v.set("unknown", 1); },
        [](auto& v) { v.set("version", 2); },
        [](auto& v) { v.set("version", 1.0); },
        [](auto& v) { v.set("schema", "other"); },
        [](auto& v) { v.set("map_uri", "file:///map.json"); },
        [](auto& v) { v.set("map_uri", "project://map.json?new=1"); },
        [](auto& v) { v.set("spawns_uri", "project://spawns.json#fragment"); },
        [](auto& v) { v.set("navigation_layer", -1); },
        [](auto& v) { v.set("navigation_layer", 0.5); },
        [](auto& v) { v.find("pixel_to_world")->set("scale", 0); },
        [](auto& v) { v.find("pixel_to_world")->set("scale", std::numeric_limits<double>::infinity()); },
        [](auto& v) { v.find("pixel_to_world")->set("extra", 1); },
        [](auto& v) { v.find("pixel_to_world")->set("origin", eve::Value::Array{1}); }};
    for (const auto& change : invalid) {
        fixture.binding = original;
        change(fixture.binding);
        auto author  = eve::map_spawn::MapSpawnBinding::decode(fixture.binding);
        auto runtime = fixture.prepare();
        REQUIRE(!author.ok());
        REQUIRE(!runtime.ok());
        REQUIRE_EQ(author.code(), runtime.code());
        REQUIRE_EQ(author.diagnostics()[0].message(), runtime.diagnostics()[0].message());
        REQUIRE_EQ(fixture.files.reads, 0);
        REQUIRE_EQ(fixture.factory->preparations, 0);
    }
    fixture.binding = original;
    auto defaults   = eve::map_spawn::MapSpawnBinding::decode(original).expect("default navigation");
    REQUIRE_EQ(defaults.navigationLayer(), std::size_t(0));
    fixture.prepare().expect("same binding accepted by actual map loader");
    REQUIRE_EQ(fixture.factory->preparations, 0);
}
}  // namespace test_map_spawn

namespace test_map_spawn_native_navigation {
using namespace eve::map;
PathfinderState wireRoundTrip(const PathfinderState& source) {
    const auto text = source.encodeState().expect("encode full provider").toJson().expect("provider file");
    auto       result =
        PathfinderState::prepareState(eve::Value::fromJson(text).expect("owning file bytes")).expect("admit provider");
    REQUIRE_EQ(result.encodeState().expect("repeat provider").toJson().expect("repeat file"), text);
    return result;
}
TEST_CASE("Map spawn native provider survives source death with exact capture history") {
    std::optional<PathfinderState> saved;
    std::uint64_t                  revision = 0;
    {
        Pathfinder source(4, 3);
        source.setBlocked(1, 1, true);
        source.setDiagonal(true);
        std::unique_ptr<FlowField> flow(source.buildFlowField(3, 2));
        REQUIRE(flow->isReachable(0, 0));
        revision = source.captureNavigationGrid().takeValue().revision();
        saved.emplace(wireRoundTrip(source.captureState().takeValue()));
    }
    REQUIRE(!saved->bound());
    auto candidate = Pathfinder::prepareRestored(*saved, std::nullopt).takeValue();
    REQUIRE(candidate->isRestoredFrom(*saved));
    REQUIRE_EQ(candidate->captureNavigationGrid().takeValue().revision(), revision);
    REQUIRE(candidate->isRestoredFrom(*saved));
    std::unique_ptr<FlowField> flow(candidate->buildFlowField(3, 2));
    REQUIRE(flow->isReachable(0, 0));
    REQUIRE(!flow->isReachable(1, 1));
    REQUIRE(!candidate->isRestoredFrom(*saved));
    auto another = Pathfinder::prepareRestored(*saved, std::nullopt).takeValue();
    REQUIRE(another->isRestoredFrom(*saved));
    candidate->setCellCost(0, 0, 4.f);
    REQUIRE_EQ(another->getCellCost(0, 0), 1.f);
    REQUIRE_EQ(another->captureNavigationGrid().takeValue().revision(), revision);
}
TEST_CASE("Map spawn native save leaves uncaptured changes and pending layer synchronization intact") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            layer = TileLayer::createLayer(3, 2, 1.f, 1.f);
    Pathfinder       source(layer);
    source.setBlockEmpty(false);
    source.blockGid(3);
    const auto first = source.captureNavigationGrid().takeValue();
    layer->setTile(0, 0, 3);
    const auto hint = source.getNavigationChangeStamp();
    REQUIRE_EQ(source.getCellCost(0, 0), 1.f);  // Actual cache is still A, current layer is B.
    auto saved = wireRoundTrip(source.captureState().takeValue());
    REQUIRE(saved.bound());
    REQUIRE(source.getNavigationChangeStamp() == hint);
    REQUIRE_EQ(source.getCellCost(0, 0), 1.f);
    auto* restoredLayer = TileLayer::createLayer(3, 2, 1.f, 1.f);
    REQUIRE(!Pathfinder::prepareRestored(saved, std::ref(*restoredLayer)).ok());
    REQUIRE(!Pathfinder::prepareRestored(saved, std::nullopt).ok());
    auto* wrongLayer = TileLayer::createLayer(3, 2, 1.f, 1.f);
    wrongLayer->setTile(1, 0, 3);  // Same revision, different navigation inputs.
    REQUIRE_EQ(wrongLayer->getRevision(), layer->getRevision());
    REQUIRE(!Pathfinder::prepareRestored(saved, std::ref(*wrongLayer)).ok());
    restoredLayer->setTile(0, 0, 3);
    auto candidate = Pathfinder::prepareRestored(saved, std::ref(*restoredLayer)).takeValue();
    REQUIRE(candidate->isRestoredFrom(saved));
    REQUIRE(candidate->getNavigationChangeStamp() == hint);
    REQUIRE_EQ(candidate->getCellCost(0, 0), 1.f);
    const auto resumed   = candidate->captureNavigationGrid().takeValue();
    const auto continued = source.captureNavigationGrid().takeValue();
    REQUIRE_EQ(resumed.revision(), first.revision() + 1);
    REQUIRE_EQ(resumed.revision(), continued.revision());
    REQUIRE_EQ(resumed.costs()[0], 0.f);
    REQUIRE(!candidate->isRestoredFrom(saved));

    // Saving B must not insert B into the actual A->B->A capture history.
    Pathfinder untouched(layer);
    untouched.setBlockEmpty(false);
    untouched.blockGid(3);
    const auto b = untouched.captureNavigationGrid().takeValue();
    layer->setTile(0, 0, 0);
    auto transient = untouched.captureState().takeValue();
    REQUIRE(transient.bound());
    layer->setTile(0, 0, 3);
    REQUIRE_EQ(untouched.captureNavigationGrid().takeValue().revision(), b.revision());
}
TEST_CASE("Map spawn native provider requires exact checkpoint provenance and explicit binding") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            layer = TileLayer::createLayer(2, 2, 1.f, 1.f);
    Pathfinder       source(2, 2);
    auto             first  = source.captureState().takeValue();
    auto             second = source.captureState().takeValue();
    REQUIRE(!Pathfinder::prepareRestored(first, std::ref(*layer)).ok());
    auto candidate = Pathfinder::prepareRestored(first, std::nullopt).takeValue();
    REQUIRE(candidate->isRestoredFrom(first));
    REQUIRE(!candidate->isRestoredFrom(second));
    candidate->setBlocked(0, 0, false);
    REQUIRE(!candidate->isRestoredFrom(first));
    auto moved = std::move(first);
    REQUIRE(!Pathfinder::prepareRestored(first, std::nullopt).ok());
    REQUIRE(Pathfinder::prepareRestored(moved, std::nullopt).ok());
    Pathfinder bound(layer);
    auto       capture        = bound.captureState().takeValue();
    auto       boundCandidate = Pathfinder::prepareRestored(capture, std::ref(*layer)).takeValue();
    REQUIRE(boundCandidate->isRestoredFrom(capture));
    layer->config()->staggerIndex = StaggerIndex::Even;
    REQUIRE(!boundCandidate->isRestoredFrom(capture));
    layer->config()->staggerIndex = StaggerIndex::Odd;
    REQUIRE(boundCandidate->isRestoredFrom(capture));
    layer->setTile(1, 1, 3);
    REQUIRE(!boundCandidate->isRestoredFrom(capture));
}
TEST_CASE("Map spawn native bound capture owns no source layer and never repairs incomplete targets") {
    std::optional<PathfinderState>  saved;
    eve::map::NavigationChangeStamp hint;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto*            layer = TileLayer::createLayer(2, 3, 1.f, 1.f);
        Pathfinder       source(layer);
        source.setBlockEmpty(false);
        source.blockGid(4);
        source.captureNavigationGrid().expect("published initial grid");
        layer->setTile(1, 2, 4);
        hint = source.getNavigationChangeStamp();
        saved.emplace(wireRoundTrip(source.captureState().takeValue()));
    }
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            incomplete = TileLayer::create();
    const auto       absent     = [&]<class T>() {
        auto* buffer = incomplete->getComponentManager().getComponentBuffer<T>();
        return !buffer || !buffer->has(incomplete->id);
    };
    REQUIRE(absent.operator()<TileLayer::Config>());
    REQUIRE(absent.operator()<TileLayer::Tiles>());
    REQUIRE(absent.operator()<TileLayer::Tileset>());
    REQUIRE(!Pathfinder::prepareRestored(*saved, std::ref(*incomplete)).ok());
    REQUIRE(absent.operator()<TileLayer::Config>());
    REQUIRE(absent.operator()<TileLayer::Tiles>());
    REQUIRE(absent.operator()<TileLayer::Tileset>());
    auto* layer = TileLayer::createLayer(2, 3, 1.f, 1.f);
    layer->setTile(1, 2, 4);
    auto candidate = Pathfinder::prepareRestored(*saved, std::ref(*layer)).takeValue();
    REQUIRE(candidate->getNavigationChangeStamp() == hint);
    REQUIRE_EQ(candidate->getCellCost(1, 2), 1.f);
    const auto resumed = candidate->captureNavigationGrid().takeValue();
    REQUIRE_EQ(resumed.revision(), 2u);
    REQUIRE_EQ(resumed.costs()[5], 0.f);
}
TEST_CASE("Map spawn provider archive preserves hex masks blocked costs and exhausted capture history") {
    using namespace eve;
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            layer        = TileLayer::createLayer(3, 2, 1, 1);
    layer->config()->orientation  = MapOrientation::Hexagonal;
    layer->config()->staggerAxis  = StaggerAxis::X;
    layer->config()->staggerIndex = StaggerIndex::Even;
    TileLayer::Tileset::Visual visual;
    visual.gid       = 7;
    visual.cost      = 2.5f;
    visual.enterMask = 0x35;
    visual.exitMask  = 0x8a;
    layer->tileset()->visuals.push_back(visual);
    layer->setTile(1, 0, 7);
    Pathfinder source(layer);
    source.setBlockEmpty(false);
    const auto first = source.captureNavigationGrid().expect("hex capture");
    auto       saved = wireRoundTrip(source.captureState().expect("capture"));
    auto       value = saved.encodeState().expect("value");
    value.getIf<Value::Object>()->at("lastCapture").getIf<Value::Array>()->at(2) = "18446744073709551615";
    auto maximal = PathfinderState::prepareState(value).expect("retain exhausted watermark");
    auto target  = Pathfinder::prepareRestored(maximal, std::ref(*layer)).expect("native provider");
    REQUIRE_EQ(target->captureNavigationGrid().expect("unchanged capture allowed").revision(),
               std::numeric_limits<std::uint64_t>::max());
    target->setCellCost(0, 0, -2);  // Finite negative costs remain blocked rather than being normalized.
    auto uncaptured = wireRoundTrip(target->captureState().expect("negative cached input"));
    auto resumed    = Pathfinder::prepareRestored(uncaptured, std::ref(*layer)).expect("resume pending sync");
    // The actual bound layer still has the old published content; capture resynchronizes the bound input.
    REQUIRE_EQ(resumed->getCellCost(0, 0), -2.f);
    layer->setTile(0, 1, 7);
    REQUIRE(!resumed->captureNavigationGrid().ok());  // Cannot silently wrap the saved publication revision.
    REQUIRE(first.topology() == NavigationTopology::Hex);
    REQUIRE(!first.isStaggerAxisY());
    REQUIRE(!first.isStaggerOdd());
    REQUIRE_EQ(first.enterMasks()[1], std::uint8_t(0x35));
    REQUIRE_EQ(first.exitMasks()[1], std::uint8_t(0x8a));
}
TEST_CASE("Map spawn provider archive rejects unknown lossy malformed grids history and flow cells") {
    using namespace eve;
    Pathfinder source(3, 2);
    source.setBlocked(1, 1, true);
    source.setCellCost(0, 1, -3);
    source.captureNavigationGrid().expect("capture");
    std::unique_ptr<FlowField> flow(source.buildFlowField(2, 1));
    const auto                 saved = source.captureState().expect("freeze");
    const auto                 value = saved.encodeState().expect("encode");
    auto grid  = [](Value& v) -> Value::Array& { return *v.getIf<Value::Object>()->at("grid").getIf<Value::Array>(); };
    auto field = [](Value& v) -> Value::Array& {
        return *v.getIf<Value::Object>()->at("cachedField").getIf<Value::Array>();
    };
    auto refuse = [&](auto edit) {
        auto bad = value;
        edit(bad);
        REQUIRE(!PathfinderState::prepareState(bad).ok());
    };
    refuse([](Value& v) { v.set("unknown", true); });
    refuse([](Value& v) { v.set("version", 2); });
    refuse([](Value& v) { v.set("schema", "other"); });
    refuse([](Value& v) { v.set("changes", "0"); });
    refuse([](Value& v) { v.set("changes", "01"); });
    refuse([&](Value& v) { grid(v).push_back(true); });
    refuse([&](Value& v) { grid(v)[0] = -1; });
    refuse([&](Value& v) {
        grid(v)[0] = 4194304;
        grid(v)[1] = 2;
    });
    refuse([&](Value& v) { grid(v)[10].getIf<Value::Array>()->pop_back(); });
    refuse([&](Value& v) { grid(v)[11].getIf<Value::Array>()->at(0) = 256; });
    refuse([&](Value& v) { grid(v)[12].getIf<Value::Array>()->at(0) = true; });
    refuse([&](Value& v) { grid(v)[10].getIf<Value::Array>()->at(0) = std::numeric_limits<double>::infinity(); });
    refuse([&](Value& v) { grid(v)[10].getIf<Value::Array>()->at(0) = std::numeric_limits<double>::max(); });
    refuse([&](Value& v) { grid(v)[13] = Value::Array{7, 7}; });
    refuse([&](Value& v) { grid(v)[13] = Value::Array{8, 7}; });
    refuse([&](Value& v) { field(v)[4].getIf<Value::Array>()->front().getIf<Value::Array>()->at(0) = -1; });
    refuse([&](Value& v) { field(v)[4].getIf<Value::Array>()->front().getIf<Value::Array>()->at(1) = 3; });
    refuse([&](Value& v) { field(v)[4].getIf<Value::Array>()->front().getIf<Value::Array>()->at(2) = -1; });
    refuse([&](Value& v) { field(v)[2] = 999; });
    refuse([](Value& v) { v.set("hasCachedField", false); });
    refuse([](Value& v) { v.getIf<Value::Object>()->at("lastCapture").getIf<Value::Array>()->at(2) = "0"; });
    refuse([](Value& v) { v.getIf<Value::Object>()->at("lastCapture").getIf<Value::Array>()->at(4) = false; });
    REQUIRE_EQ(source.getCellCost(0, 1), -3.f);
    REQUIRE_EQ(saved.encodeState().expect("source unchanged").toJson().expect("wire"),
               value.toJson().expect("original wire"));
    auto admitted = wireRoundTrip(saved);
    auto target   = Pathfinder::prepareRestored(admitted, std::nullopt).expect("retry");
    REQUIRE_EQ(target->getCellCost(0, 1), -3.f);
    std::unique_ptr<FlowField> continued(target->buildFlowField(2, 1));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) {
            REQUIRE_EQ(continued->costAt(x, y), flow->costAt(x, y));
            REQUIRE_EQ(continued->nextX(x, y), flow->nextX(x, y));
            REQUIRE_EQ(continued->nextY(x, y), flow->nextY(x, y));
        }
    Pathfinder empty;
    auto       defaultState = wireRoundTrip(empty.captureState().expect("empty capture"));
    REQUIRE(!defaultState.bound());
    REQUIRE(Pathfinder::prepareRestored(defaultState, std::nullopt).ok());
    auto moved = std::move(defaultState);
    REQUIRE(!defaultState.encodeState().ok());
    Pathfinder invalid(1, 1);
    invalid.setCellCost(0, 0, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!invalid.captureState().expect("native unchecked fact").encodeState().ok());
}
}  // namespace test_map_spawn_native_navigation
