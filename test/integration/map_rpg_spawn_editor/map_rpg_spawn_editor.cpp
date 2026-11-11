// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"map_rpg_spawn_editor","default":"render-window","cases":{"platform-file":["RPG spawn preview freezes actual source and isolates canonical worlds random streams and diagnostics"]}}
*/
// clang-format on
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include "Fixtures.h"
#include "common/Capability.h"
#include "common/UIAutomation.h"
#include "common/Value.h"
#include "definitions/Definitions.h"
#include "editing/EditingProperty.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorDocumentService.h"
#include "filesystem/Filesystem.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/ISolidRectRenderer.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/editor/RPGSpawnEditor.h"
#include "map/rpg_spawn/editor/RPGSpawnPreview.h"
#include "map/spawn/MapSpawnSpace.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editor/SpawnForm.h"
#include "platform_event/PlatformEvent.h"
#include "property_access/PropertyAccess.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEvents.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnRegion.h"
#include "spawn/definitions/SpawnTemplateSource.h"
#include "ui/UI.h"
#include "ui/UIAutomationCapabilities.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("RPG spawn editor script adapter admits actual skill unit and template content and rejects a closed host") {
    ssq::VM vm(4096, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::rpg::RPG::expose(table);
    eve::window::Window::expose(table);
    eve::filesystem::Filesystem::expose(table);
    eve::map_rpg_spawn_editor::RPGSpawnEditor::expose(table);
    vm.addFunc("closedWindow", [] { return eve::window::Window::create(); });
    vm.addFunc("actualFiles", [] { return eve::filesystem::Filesystem::create(); });
    vm.run(vm.compileSource(R"(
        local editor = eve.RPGSpawnEditor();
        assert(!editor.isOpen());
        local unit = "{\"schema\":\"eve.rpg.unit\",\"version\":1,\"id\":\"editor.unit\",\"display_name\":\"Editor unit\",\"skill\":\"editor.skill\",\"attack\":1,\"defense\":0,\"max_hp\":10,\"speed\":1}";
        assert(!editor.publishUnit(unit).ok);
        local rpg = eve.RPG();
        assert(rpg.registerSkillsFromJson("[{\"id\":\"editor.skill\",\"cooldown\":1,\"castTime\":0,\"costs\":[],\"grantedEffects\":[],\"tags\":[]}]") == 1);
        assert(editor.publishUnit(unit).ok);
        assert(!editor.publishUnit("{}").ok);
        local source = "{\"schema\":\"eve.spawn-template\",\"version\":1,\"id\":\"editor.template\",\"content\":{\"kind\":\"fixed\",\"members\":[{\"id\":\"guard\",\"unit\":{\"type\":\"rpg.unit\",\"id\":\"editor.unit\"},\"count\":1}]},\"schedule\":{\"kind\":\"once\"},\"limits\":{\"alive\":1,\"total\":{\"kind\":\"unlimited\"},\"budget_group\":\"hostiles\"},\"placement\":{\"kind\":\"random_in_region\",\"constraints\":[{\"kind\":\"walkable\"}],\"attempts_per_unit\":2},\"batch_policy\":\"atomic\",\"failure\":{\"placement_blocked\":{\"kind\":\"retry\",\"delay_seconds\":1}}}";
        assert(editor.publishTemplate(source).ok);
        assert(!editor.publishTemplate("{}").ok);
        assert(!editor.open(closedWindow(), actualFiles(), "{}", "{}", ".", "test", "content://test", "out.json").ok);
        assert(!editor.isOpen());
        assert(!editor.openTemplate(closedWindow(), actualFiles(), source, ".", "template", "content://template", "out.json").ok);
        assert(!editor.isOpen());
        assert(!editor.defeatPreviewUnit("bad").ok);
        assert(!editor.prepareTiledImport(actualFiles(),"missing","bad","").ok);
        assert(!editor.commitTiledImport().ok);
        assert(!editor.cancelTiledImport().ok);
        assert(!editor.shutdown(closedWindow(), "unknown").ok);
        assert(editor.shutdown(closedWindow(), "require_clean").ok);
        assert(editor.shutdown(closedWindow(), "discard").ok);
    )"));
    REQUIRE(true);
}

TEST_CASE("RPG editor automation observes isolated UI worlds without taking their ownership") {
    using namespace eve;
    using namespace eve::ui;
    registerUIAutomationCapabilities();
    auto* automation = cap::query<IUIAutomation>();
    REQUIRE(automation != nullptr);
    REQUIRE(!UIAutomationWorld::observe({}).ok());
    auto first = std::make_shared<ecs::Table>();
    cap::revoke<IUIAutomation>(automation);
    auto withoutProvider = UIAutomationWorld::observe(first).expect("observation without optional automation provider");
    CHECK(cap::query<IUIAutomation>() == nullptr);
    withoutProvider.reset();
    registerUIAutomationCapabilities();
    auto second   = std::make_shared<ecs::Table>();
    int  clicks   = 0;
    auto makeHost = [&](const std::shared_ptr<ecs::Table>& world) {
        ecs::ScopedTable scope(*world);
        auto             handle = UIHost::createHost("isolated-automation");
        UIHost::resolve(handle)->get().setTree(ui::window("Isolated", {button("Run", "run", [&] { ++clicks; })}));
        return handle;
    };
    makeHost(first);
    makeHost(second);
    CHECK(automation->tree("isolated-automation").starts_with("error:"));
    auto lease     = UIAutomationWorld::observe(first).expect("first observation");
    auto duplicate = UIAutomationWorld::observe(first).expect("duplicate observation");
    CHECK(automation->get("isolated-automation", "run").find("button") != std::string::npos);
    CHECK(automation->click("isolated-automation", "run").find("queued") != std::string::npos);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    auto other = UIAutomationWorld::observe(second).expect("second observation");
    CHECK(automation->get("isolated-automation", "run").find("ambiguous") != std::string::npos);
    other.reset();
    CHECK(automation->click("isolated-automation", "run").find("queued") != std::string::npos);
    std::weak_ptr<ecs::Table> weak = first;
    first.reset();
    CHECK(weak.expired());
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK(automation->tree("isolated-automation").starts_with("error:"));
    duplicate.reset();
    lease.reset();
    auto last = UIAutomationWorld::observe(second).expect("lease first");
    last.reset();
    CHECK(automation->tree("isolated-automation").starts_with("error:"));
    second.reset();
}

TEST_CASE("RPG editor semantic text edits use deferred canonical UI events and reject retired trees") {
    using namespace eve;
    using namespace eve::ui;
    registerUIAutomationCapabilities();
    auto* automation = cap::query<IUIAutomation>();
    REQUIRE(automation != nullptr);
    auto         world     = std::make_shared<ecs::Table>();
    auto         lease     = UIAutomationWorld::observe(world).expect("observe actual editor controls");
    std::string  accepted  = "before";
    int          callbacks = 0;
    UIHostHandle handle;
    {
        ecs::ScopedTable scope(*world);
        handle             = UIHost::createHost("text-automation");
        auto input         = ui::inputText("Path", "before", "path");
        input.onTextChange = [&](const std::string& value) {
            accepted = value;
            ++callbacks;
        };
        auto locked      = ui::inputText("Locked", "locked", "locked");
        locked.readOnly  = true;
        auto disabled    = ui::inputText("Disabled", "disabled", "disabled");
        disabled.enabled = false;
        auto hidden      = ui::inputText("Hidden", "hidden", "hidden");
        hidden.visible   = false;
        UIHost::resolve(handle)->get().setTree(
            ui::window("Text", {input, locked, disabled, hidden, ui::button("Run", "run")}));
    }
    const std::string uuid = "01900000-0000-7000-8000-000000000041";
    automation->setText("text-automation", "path", uuid).expect("full text replacement");
    CHECK_EQ(accepted, "before");
    CHECK_EQ(callbacks, 0);
    CHECK(automation->get("text-automation", "path").find(uuid) != std::string::npos);
    UISystem::dispatchEvents();
    CHECK_EQ(accepted, uuid);
    CHECK_EQ(callbacks, 1);
    for (const auto* id : {"locked", "disabled", "hidden", "run", "missing"})
        CHECK(!automation->setText("text-automation", id, "rejected").ok());
    CHECK_EQ(callbacks, 1);
    automation->setText("text-automation", "path", "").expect("empty explicitly clears");
    UISystem::dispatchEvents();
    CHECK(accepted.empty());
    CHECK_EQ(callbacks, 2);
    automation->setText("text-automation", "path", "old tree").expect("queue before tree replacement");
    {
        ecs::ScopedTable scope(*world);
        auto             replacement = ui::inputText("Replacement", "new", "path");
        replacement.onTextChange     = [&](const std::string&) { ++callbacks; };
        UIHost::resolve(handle)->get().setTree(ui::window("New tree", {replacement}));
    }
    UISystem::dispatchEvents();
    CHECK_EQ(callbacks, 2);
    automation->setText("text-automation", "path", "dead world").expect("queue before world retirement");
    world.reset();
    UISystem::dispatchEvents();
    CHECK_EQ(callbacks, 2);
}

TEST_CASE("RPG spawn editor launch keeps close requests and overflow observable and cleans up in both window orders") {
    GfxFixture graphics(1280, 800);
    auto*      ui = eve::ui::UI::create();
    REQUIRE(ui->initBackend());
    struct StopUi {
        eve::ui::UI* ui;
        ~StopUi() { ui->shutdownBackend(); }
    } stopUi{ui};
    auto*      files   = eve::filesystem::Filesystem::create();
    const auto project = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
                         "examples/spawn-editor";
    REQUIRE(files->setSource(project.string()));
    const auto storage =
        std::filesystem::temp_directory_path() / ("eve-rpg-spawn-editor-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(storage));
    struct RemoveStore {
        std::filesystem::path path;
        ~RemoveStore() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } removeStore{storage};
    ssq::VM vm(4096, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::rpg::RPG::expose(table);
    eve::window::Window::expose(table);
    eve::filesystem::Filesystem::expose(table);
    eve::graphics::Graphics::expose(table);
    eve::ui::UI::expose(table);
    eve::map_rpg_spawn_editor::RPGSpawnEditor::expose(table);
    vm.addFunc("nativeWindow", [&] { return graphics.win; });
    vm.addFunc("actualFiles", [&] { return files; });
    vm.addFunc("actualUi", [&] { return ui; });
    vm.addFunc("actualGraphics", [&] { return graphics.gfx; });
    vm.addFunc("storageRoot", [&] { return storage.string(); });
    vm.addFunc("asset", [&](const std::string& path) {
        std::vector<std::uint8_t> bytes;
        if (!files->readFile("data/" + path, bytes)) throw std::runtime_error("Editor test asset read failed");
        return std::string(bytes.begin(), bytes.end());
    });
    auto* canvas = graphics.gfx->newCanvas(640, 640);
    vm.addFunc("targetCanvas", [&] { return canvas; });
    vm.run(vm.compileSource(R"(
        editor <- eve.RPGSpawnEditor();
        local rpg = eve.RPG();
        assert(rpg.registerSkillsFromJson(asset("skills.json")) == 1);
        assert(editor.publishUnit(asset("unit.json")).ok);
        assert(editor.publishTemplate(asset("template.json")).ok);
        openEditor <- function() {return editor.open(nativeWindow(),actualFiles(),asset("binding.json"),
            "{\"schema\":\"eve.map.object-contract\",\"version\":1,\"types\":[]}",storageRoot(),"test-editor","content://test-editor.json","test-export.json");};
        assert(openEditor().ok);
        assert(editor.isOpen());
        assert(!openEditor().ok);
        assert(!editor.publishUnit(asset("unit.json")).ok);
        assert(!editor.render(actualGraphics(),null,actualUi()).ok);
        assert(!editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(editor.isOpen());
    )"));
    auto*     native = static_cast<SDL_Window*>(graphics.win->getHandle());
    auto*     events = eve::platform_event::PlatformEvent::create();
    SDL_Event close{};
    close.type            = SDL_WINDOWEVENT;
    close.window.windowID = SDL_GetWindowID(native);
    close.window.event    = SDL_WINDOWEVENT_CLOSE;
    REQUIRE_EQ(SDL_PushEvent(&close), 1);
    events->pump();
    while (auto message = events->pollOwned()) REQUIRE(message->name != "quit");
    vm.run(vm.compileSource("assert(editor.update(nativeWindow(),actualUi()).ok); assert(editor.isOpen());"));
    graphics.gfx->setScreenReadbackEnabled(true);
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(!editor.stepPreview(1000000000).ok);
        assert(editor.stopPreview().ok);
        assert(!editor.startPreview(actualFiles(),"not-an-owner",42,6.0).ok);
        assert(editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",42,6.0).ok);
        assert(editor.stepPreview(1000000000).ok);
        assert(!editor.stepPreview(-1).ok);
        assert(!editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",-1,6.0).ok);
        assert(editor.stepPreview(0).ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    const auto panelPixel = graphics.gfx->getPixel(30, 700);
    REQUIRE(panelPixel.r + panelPixel.g + panelPixel.b > 0.05f);
    auto hasPreviewMarker = [&] {
        for (int y = 80; y < 260; y += 8)
            for (int x = 155; x < 330; x += 8) {
                const auto pixel = graphics.gfx->getPixel(x, y);
                if (pixel.g > 0.8f && pixel.r < 0.4f && pixel.b < 0.6f) return true;
            }
        return false;
    };
    REQUIRE(hasPreviewMarker());
    vm.run(vm.compileSource(R"(
        local guid = "01900000-0000-7000-8000-000000000041";
        assert(!editor.prepareTiledImport(actualFiles(),"data/import.tiled.json",guid,"").ok);
        assert(editor.stopPreview().ok);
        assert(!editor.prepareTiledImport(actualFiles(),"data/import.tiled.json","bad","").ok);
        assert(!editor.prepareTiledImport(actualFiles(),"data/missing.tiled.json",guid,"").ok);
        assert(!editor.prepareTiledImport(actualFiles(),"data/import.tiled.json",guid,"missing-prototype").ok);
        assert(editor.prepareTiledImport(actualFiles(),"data/import.tiled.json",guid,"").ok);
        assert(!editor.prepareTiledImport(actualFiles(),"data/import.tiled.json",guid,"").ok);
        assert(!editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",42,6.0).ok);
        local close = editor.shutdown(nativeWindow(),"require_clean");
        assert(!close.ok && close.status.summary.find("Tiled import") != null);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    bool amberCandidate = false;
    for (int y = 115; y < 216; ++y)
        for (int x = 130; x < 260; ++x) {
            const auto pixel = graphics.gfx->getPixel(x, y);
            if (pixel.r > .85f && pixel.g > .4f && pixel.g < .85f && pixel.b < .4f) amberCandidate = true;
        }
    REQUIRE(amberCandidate);
    vm.run(vm.compileSource(R"(
        assert(editor.cancelTiledImport().ok);
        assert(!editor.commitTiledImport().ok);
        assert(editor.prepareTiledImport(actualFiles(),"data/import.tiled.json","01900000-0000-7000-8000-000000000041","").ok);
        assert(editor.commitTiledImport().ok);
        assert(!editor.commitTiledImport().ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    SDL_Event key{};
    key.type           = SDL_KEYDOWN;
    key.key.windowID   = SDL_GetWindowID(native);
    key.key.keysym.sym = SDLK_F13;
    for (int i = 0; i < 1100; ++i) REQUIRE_EQ(SDL_PushEvent(&key), 1);
    events->pump();
    vm.run(vm.compileSource(R"(
        local overloaded=editor.update(nativeWindow(),actualUi());
        assert(!overloaded.ok);
        assert(overloaded.status.summary.find("overflow") != null);
        assert(editor.isOpen());
        assert(editor.update(nativeWindow(),actualUi()).ok);
        assert(editor.shutdown(nativeWindow(),"discard").ok);
        assert(!editor.isOpen());
        assert(openEditor().ok);
    )"));
    graphics.gfx->setScreenReadbackEnabled(false);
    ui->shutdownBackend();
    graphics.win->close();
    vm.run(vm.compileSource(R"(
        assert(!editor.update(nativeWindow(),actualUi()).ok);
        assert(!editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(editor.isOpen());
        assert(editor.shutdown(nativeWindow(),"discard").ok);
        assert(!editor.isOpen());
    )"));
    REQUIRE(!std::filesystem::exists(storage / "Content/test-editor.json"));
    std::vector<std::uint8_t> sourceBytes;
    REQUIRE(files->readFile("data/spawns.json", sourceBytes));
    auto hugeSource =
        eve::Value::fromJson(std::string(sourceBytes.begin(), sourceBytes.end())).expect("actual sidecar");
    hugeSource.find("instances")->at(0).find("region")->set("center", eve::Value::array({1e300, 0.0}));
    eve::editor::DiskAtomicDocumentStore store(storage);
    auto saved = store.compareAndSwap("content://test-editor.json", 0, "", eve::editing::toEditingValue(hugeSource))
                     .expect("persist valid but unrenderable source");
    eve::window::WindowSettings settings;
    settings.width  = 1280;
    settings.height = 800;
    REQUIRE(graphics.win->setWindowSettings(settings));
    REQUIRE(ui->initBackend());
    canvas = graphics.gfx->newCanvas(640, 640);
    graphics.gfx->setScreenReadbackEnabled(true);
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(openEditor().ok);
        local result=editor.render(actualGraphics(),targetCanvas(),actualUi());
        assert(!result.ok);
        assert(editor.isOpen());
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    const auto recoveryPanel = graphics.gfx->getPixel(30, 700);
    REQUIRE(recoveryPanel.r + recoveryPanel.g + recoveryPanel.b > 0.05f);
    auto after = store.read("content://test-editor.json").expect("preview never saves source");
    REQUIRE_EQ(after.revision, saved.revision);
    REQUIRE_EQ(after.contentHash, saved.contentHash);
    vm.run(vm.compileSource("assert(editor.shutdown(nativeWindow(),\"require_clean\").ok);"));
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        openTemplate <- function() {return editor.openTemplate(nativeWindow(),actualFiles(),asset("template.json"),
            storageRoot(),"test-template","content://test-template.json","test-template-export.json");};
        assert(!editor.openTemplate(nativeWindow(),actualFiles(),"{}",storageRoot(),"bad",
            "content://bad-template.json","bad.json").ok);
        assert(!editor.isOpen());
        assert(openTemplate().ok);
        assert(editor.isOpen());
        assert(!editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",42,6.0).ok);
        assert(!editor.stepPreview(1000000000).ok);
        assert(!editor.stopPreview().ok);
        assert(!openTemplate().ok);
        assert(!openEditor().ok);
        assert(!editor.publishTemplate(asset("template.json")).ok);
        assert(!editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(editor.render(actualGraphics(),null,actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    const auto templateForm = graphics.gfx->getPixel(30, 200);
    const auto templateSave = graphics.gfx->getPixel(900, 100);
    REQUIRE(templateForm.r + templateForm.g + templateForm.b > 0.05f);
    REQUIRE(templateSave.r + templateSave.g + templateSave.b > 0.05f);
    close.window.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(graphics.win->getHandle()));
    REQUIRE_EQ(SDL_PushEvent(&close), 1);
    events->pump();
    while (auto message = events->pollOwned()) REQUIRE(message->name != "quit");
    vm.run(vm.compileSource(R"(
        assert(editor.update(nativeWindow(),actualUi()).ok);
        assert(editor.isOpen());
        assert(editor.shutdown(nativeWindow(),"discard").ok);
        assert(!editor.isOpen());
    )"));
    REQUIRE(!std::filesystem::exists(storage / "Content/test-template.json"));
    std::vector<std::uint8_t> templateBytes;
    REQUIRE(files->readFile("data/template.json", templateBytes));
    auto templateSource =
        eve::Value::fromJson(std::string(templateBytes.begin(), templateBytes.end())).expect("template");
    templateSource.find("schedule")->set("target_alive", 2);
    templateSource.find("schedule")->set("initial_population", 2);
    auto savedTemplate =
        store.compareAndSwap("content://test-template.json", 0, "", eve::editing::toEditingValue(templateSource))
            .expect("actual saved template");
    vm.run(vm.compileSource(R"(
        assert(openTemplate().ok);
        assert(editor.render(actualGraphics(),null,actualUi()).ok);
        assert(editor.shutdown(nativeWindow(),"require_clean").ok);
    )"));
    auto templateAfter = store.read("content://test-template.json").expect("render does not rewrite template");
    REQUIRE_EQ(templateAfter.revision, savedTemplate.revision);
    REQUIRE_EQ(templateAfter.contentHash, savedTemplate.contentHash);
    auto renamedTemplate = templateSource;
    renamedTemplate.set("id", "different.template");
    auto renamed = store
                       .compareAndSwap("content://test-template.json", savedTemplate.revision,
                                       savedTemplate.contentHash, eve::editing::toEditingValue(renamedTemplate))
                       .expect("saved renamed template");
    vm.run(vm.compileSource(R"(
        assert(openTemplate().ok);
        assert(editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(!editor.isOpen());
    )"));
    store
        .compareAndSwap("content://test-template.json", renamed.revision, renamed.contentHash,
                        eve::editing::toEditingValue(templateSource))
        .expect("restore correct template");
    auto eventSource =
        eve::Value::fromJson(std::string(sourceBytes.begin(), sourceBytes.end())).expect("event sidecar");
    eventSource.find("instances")
        ->at(0)
        .set("activation", eve::Value::object({{"kind", "event"}, {"event", "gate.open"}}));
    auto eventSaved = store
                          .compareAndSwap("content://test-editor.json", saved.revision, saved.contentHash,
                                          eve::editing::toEditingValue(eventSource))
                          .expect("persist actual event configuration");
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(openEditor().ok);
        assert(editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",42,6.0).ok);
        assert(editor.stepPreview(1000000000).ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    REQUIRE(!hasPreviewMarker());
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(!editor.triggerPreviewEvent("").ok);
        assert(!editor.triggerPreviewEvent("unknown").ok);
        assert(editor.triggerPreviewEvent("gate.open").ok);
        assert(editor.triggerPreviewEvent("gate.open").ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    REQUIRE(hasPreviewMarker());
    auto afterEvent = store.read("content://test-editor.json").expect("event simulation does not save source");
    REQUIRE_EQ(afterEvent.revision, eventSaved.revision);
    REQUIRE_EQ(afterEvent.contentHash, eventSaved.contentHash);
    vm.run(vm.compileSource("assert(editor.shutdown(nativeWindow(),\"require_clean\").ok);"));
    auto proximitySource = eventSource;
    proximitySource.find("instances")
        ->at(0)
        .set("activation",
             eve::Value::object({{"kind", "player_proximity"}, {"enter_distance", 10}, {"exit_distance", 20}}));
    auto proximitySaved = store
                              .compareAndSwap("content://test-editor.json", eventSaved.revision, eventSaved.contentHash,
                                              eve::editing::toEditingValue(proximitySource))
                              .expect("persist proximity configuration");
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(openEditor().ok);
        assert(editor.startPreview(actualFiles(),"01900000-0000-7000-8000-000000000032",42,6.0).ok);
        assert(editor.applyPreviewPlayers("[[450,224]]").ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    REQUIRE(!hasPreviewMarker());
    graphics.gfx->clearScreen();
    vm.run(vm.compileSource(R"(
        assert(editor.applyPreviewPlayers("[[430,224]]").ok);
        assert(!editor.applyPreviewPlayers("null").ok);
        assert(!editor.applyPreviewPlayers("[[1,2,3]]").ok);
        assert(!editor.applyPreviewPlayers("[[true,0]]").ok);
        assert(editor.render(actualGraphics(),targetCanvas(),actualUi()).ok);
    )"));
    graphics.gfx->present();
    ui->dispatchEvents();
    REQUIRE(hasPreviewMarker());
    auto afterPlayers = store.read("content://test-editor.json").expect("player probes never save source");
    REQUIRE_EQ(afterPlayers.revision, proximitySaved.revision);
    REQUIRE_EQ(afterPlayers.contentHash, proximitySaved.contentHash);
    vm.run(vm.compileSource(R"(
        assert(editor.applyPreviewPlayers("[]").ok);
        assert(editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(openTemplate().ok);
        assert(!editor.triggerPreviewEvent("gate.open").ok);
        assert(!editor.applyPreviewPlayers("[]").ok);
    )"));
    ui->shutdownBackend();
    graphics.win->close();
    vm.run(vm.compileSource(R"(
        assert(!editor.update(nativeWindow(),actualUi()).ok);
        assert(editor.shutdown(nativeWindow(),"require_clean").ok);
        assert(!editor.isOpen());
    )"));
    graphics.gfx->setScreenReadbackEnabled(false);
}

TEST_CASE("RPG spawn preview freezes actual source and isolates canonical worlds random streams and diagnostics") {
    auto*      files   = eve::filesystem::Filesystem::create();
    const auto project = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
                         "examples/spawn-editor";
    REQUIRE(files->setSource(project.string()));
    auto text = [&](const std::string& name) {
        std::vector<std::uint8_t> bytes;
        REQUIRE(files->readFile("data/" + name, bytes));
        return std::string(bytes.begin(), bytes.end());
    };
    auto  json = [&](const std::string& name) { return eve::Value::fromJson(text(name)).expect("actual asset"); };
    auto* rpg  = eve::rpg::RPG::create();
    REQUIRE_EQ(rpg->registerSkillsFromJson(text("skills.json")), 1);
    auto registry = std::make_shared<eve::definitions::DefinitionRegistry>();
    auto units    = std::make_shared<eve::rpg_spawn::RPGUnitCatalogue>(registry);
    units->publish(json("unit.json")).expect("real unit");
    auto templates = std::make_shared<eve::spawn::SpawnTemplateSource>(*registry, *units);
    templates->publish(json("template.json")).expect("real template");
    auto source = json("spawns.json");
    // A controlled single-source baseline; the native composition above consumes the complete bundled example.
    source.find("instances")->getIf<eve::Value::Array>()->resize(1);
    source.find("instances")
        ->at(0)
        .set("overrides",
             eve::Value::fromJson(R"({"population":{"target_alive":2,"initial_population":2,"alive_limit":2}})")
                 .expect("override"));
    auto target = eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("preview-source"), source,
                                                                  templates, units)
                      .expect("actual source target");
    auto              form        = eve::spawn_editor::SpawnForm::create(std::move(target)).expect("actual form");
    auto              frozen      = form->captureSource().expect("frozen committed source");
    const auto        binding     = json("binding.json");
    const std::string contract    = R"({"schema":"eve.map.object-contract","version":1,"types":[]})";
    const auto        owner       = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000031");
    auto              countFormal = [] {
        std::size_t result = 0;
        auto        view   = ecs::View<eve::rpg::RPGActor>();
        for (auto it = view.begin(); it != view.end(); ++it) ++result;
        return result;
    };
    const auto before = countFormal();
    using Preview     = eve::map_rpg_spawn_editor::RPGSpawnPreview;
    auto first = Preview::create(binding, contract, *files, frozen, *templates, units, owner.child("first"), 42, 6)
                     .expect("isolated preview");
    auto second = Preview::create(binding, contract, *files, frozen, *templates, units, owner.child("second"), 42, 6)
                      .expect("independent replay");
    REQUIRE_EQ(first->sourceRevision(), frozen.revision);
    REQUIRE_EQ(first->captureDiagnostics().expect("initial").tick.value(), std::uint64_t(0));
    REQUIRE_EQ(countFormal(), before);
    auto changed = source;
    changed.find("instances")->at(0).find("overrides")->find("population")->set("target_alive", 3);
    changed.find("instances")->at(0).find("overrides")->find("population")->set("initial_population", 3);
    changed.find("instances")->at(0).find("overrides")->find("population")->set("alive_limit", 3);
    form->view().write("$source", changed).expect("edit after preview start");
    REQUIRE(first->sourceRevision() != form->sourceRevision());
    const eve::SimulationStep step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)};
    REQUIRE_EQ(first->advance(step, {}).expect("real private spawn").units, std::uint32_t(2));
    auto capture = first->captureDiagnostics().expect("committed private facts");
    REQUIRE_EQ(capture.sources.size(), std::size_t(1));
    REQUIRE_EQ(capture.sources[0].state.alive, std::uint32_t(2));
    REQUIRE_EQ(capture.budgets[0].alive, std::uint32_t(2));
    REQUIRE_EQ(second->captureDiagnostics().expect("unadvanced independent world").tick.value(), std::uint64_t(0));
    REQUIRE_EQ(second->advance(step, {}).expect("same seed replay").units, std::uint32_t(2));
    struct Rects final : eve::graphics::ISolidRectRenderer {
        std::vector<std::array<float, 4>> values;
        std::function<void()>             callback;
        void drawSolidRectRGBA(float x, float y, float width, float height, float, float, float, float) override {
            values.push_back({x, y, width, height});
            if (callback) callback();
        }
    } a, b;
    a.callback = [&] {
        REQUIRE(!first->stop().ok());
        REQUIRE(!first->advance(step, {}).ok());
        REQUIRE(!first->captureDiagnostics().ok());
        REQUIRE(!first->resolveEvent("gate.open").ok());
        REQUIRE(!first->defeat(capture.sources[0].state.subjects.front()).ok());
    };
    REQUIRE_EQ(first->draw(a, {}).expect("actual preview actor markers"), std::size_t(2));
    REQUIRE_EQ(second->draw(b, {}).expect("replayed markers"), std::size_t(2));
    REQUIRE(a.values == b.values);
    auto different =
        Preview::create(binding, contract, *files, frozen, *templates, units, owner.child("different"), 43, 6)
            .expect("independent configured seed");
    REQUIRE_EQ(different->advance(step, {}).expect("different seed spawn").units, std::uint32_t(2));
    Rects other;
    REQUIRE_EQ(different->draw(other, {}).expect("different seed markers"), std::size_t(2));
    REQUIRE(other.values != b.values);
    different.reset();
    auto outsideSource = frozen;
    outsideSource.source.find("instances")->at(0).find("region")->set("center", eve::Value::array({1000000, 1000000}));
    auto blocked =
        Preview::create(binding, contract, *files, outsideSource, *templates, units, owner.child("blocked"), 42, 6)
            .expect("valid source outside walkable map");
    REQUIRE_EQ(blocked->advance(step, {}).expect("accepted time step need not create units").units, std::uint32_t(0));
    const auto blockedState = blocked->captureDiagnostics().expect("actual blocked ledger");
    REQUIRE(blockedState.sources[0].state.blocked == eve::spawn::BlockReason::PlacementBlocked);
    REQUIRE_EQ(blockedState.sources[0].state.alive, std::uint32_t(0));
    blocked->stop().expect("blocked preview cleanup");
    blocked.reset();
    REQUIRE_EQ(countFormal(), before);
    auto latest = Preview::create(binding, contract, *files, form->captureSource().expect("new source"), *templates,
                                  units, owner.child("latest"), 42, 6)
                      .expect("explicit restart admits new source");
    REQUIRE_EQ(latest->advance(step, {}).expect("new population").units, std::uint32_t(3));
    auto invalid = frozen;
    invalid.source.find("instances")->at(0).find("region")->set("radius", -1);
    REQUIRE(
        !Preview::create(binding, contract, *files, invalid, *templates, units, owner.child("invalid"), 42, 6).ok());
    REQUIRE_EQ(first->captureDiagnostics().expect("failed prepare preserves live preview").tick.value(),
               std::uint64_t(1));
    a.callback = [] { throw std::runtime_error("renderer failed"); };
    bool threw = false;
    try {
        first->draw(a, {}).ignore();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(first->captureDiagnostics().ok());
    auto  eventSource    = frozen;
    auto& eventInstances = *eventSource.source.find("instances")->getIf<eve::Value::Array>();
    eventInstances[0].set("activation", eve::Value::object({{"kind", "event"}, {"event", "gate.open"}}));
    auto anotherGate = eventInstances[0];
    anotherGate.set("id", "another-gate");
    eventInstances.push_back(anotherGate);
    auto otherEvent = eventInstances[0];
    otherEvent.set("id", "other-event");
    otherEvent.set("activation", eve::Value::object({{"kind", "event"}, {"event", "other.open"}}));
    eventInstances.push_back(otherEvent);
    auto eventPreview =
        Preview::create(binding, contract, *files, eventSource, *templates, units, owner.child("events"), 42, 6)
            .expect("actual event sources");
    REQUIRE_EQ(eventPreview->advance(step, {}).expect("dormant before external event").units, std::uint32_t(0));
    REQUIRE(!eventPreview->resolveEvent("unknown").ok());
    REQUIRE(!eventPreview->resolveEvent("").ok());
    REQUIRE_EQ(eventPreview->captureDiagnostics().expect("invalid events do not advance").tick.value(),
               std::uint64_t(1));
    auto gate = eventPreview->resolveEvent("gate.open").expect("owning event bindings");
    REQUIRE_EQ(gate.size(), std::size_t(2));
    REQUIRE_EQ(eventPreview->advance({eve::SimulationTick(2), {}}, {std::nullopt, gate})
                   .expect("one event matches two sources")
                   .units,
               std::uint32_t(4));
    auto eventFacts = eventPreview->captureDiagnostics().expect("event facts");
    auto eventRow   = [&](std::string_view id) -> const eve::spawn::DiagnosticSource& {
        auto row = std::find_if(eventFacts.sources.begin(), eventFacts.sources.end(),
                                  [&](const auto& item) { return item.state.id == id; });
        REQUIRE(row != eventFacts.sources.end());
        return *row;
    };
    const auto originalId = frozen.source.find("instances")->at(0).find("id")->asString();
    REQUIRE_EQ(eventRow(originalId).state.alive, std::uint32_t(2));
    REQUIRE_EQ(eventRow("another-gate").state.alive, std::uint32_t(2));
    REQUIRE(eventRow("other-event").state.phase == eve::spawn::Phase::Dormant);
    REQUIRE_EQ(std::get<eve::spawn::EventActivation>(eventRow(originalId).activation).event, "gate.open");
    REQUIRE_EQ(eventPreview->advance({eve::SimulationTick(3), {}}, {std::nullopt, gate})
                   .expect("latched event does not duplicate population")
                   .units,
               std::uint32_t(0));
    auto foreignEvent = first->advance({eve::SimulationTick(2), {}}, {std::nullopt, gate});
    REQUIRE(!foreignEvent.ok());
    REQUIRE_EQ(foreignEvent.status().primaryDiagnostic()->code(), eve::DiagnosticCode::StaleHandle);
    REQUIRE_EQ(first->captureDiagnostics().expect("foreign event does not advance").tick.value(), std::uint64_t(1));
    eventPreview->stop().expect("event preview cleanup");
    REQUIRE(!eventPreview->advance({eve::SimulationTick(4), {}}, {std::nullopt, gate}).ok());
    REQUIRE(!eventPreview->resolveEvent("gate.open").ok());
    eventPreview.reset();
    REQUIRE_EQ(std::get<eve::spawn::EventActivation>(eventRow(originalId).activation).event, "gate.open");
    first->stop().expect("explicit private cleanup");
    first->stop().expect("idempotent stop");
    REQUIRE(!first->active());
    REQUIRE(!first->captureDiagnostics().ok());
    REQUIRE(!first->advance(step, {}).ok());
    first.reset();
    second.reset();
    latest.reset();
    form.reset();
    REQUIRE_EQ(capture.sources[0].state.alive, std::uint32_t(2));
    REQUIRE_EQ(capture.sources[0].state.subjects.size(), std::size_t(2));
    auto proximitySource = frozen;
    proximitySource.source.find("instances")
        ->at(0)
        .set("activation",
             eve::Value::object({{"kind", "player_proximity"}, {"enter_distance", 10}, {"exit_distance", 20}}));
    auto proximity =
        Preview::create(binding, contract, *files, proximitySource, *templates, units, owner.child("proximity"), 42, 6)
            .expect("actual proximity source");
    auto applyPlayers = [&](std::uint64_t tick, std::vector<eve::spawn::Point> players) {
        return proximity
            ->advance({eve::SimulationTick(tick), eve::Duration(0)}, {std::span<const eve::spawn::Point>(players), {}})
            .expect("actual coordinated player frame");
    };
    auto proximityState = [&] {
        auto facts = proximity->captureDiagnostics().expect("proximity facts");
        REQUIRE_EQ(facts.sources.size(), std::size_t(1));
        return facts.sources[0].state;
    };
    REQUIRE_EQ(applyPlayers(1, {{450, 224}}).units, std::uint32_t(0));
    REQUIRE(proximityState().phase == eve::spawn::Phase::Dormant);
    REQUIRE_EQ(applyPlayers(2, {{430, 224}}).units, std::uint32_t(2));
    REQUIRE(proximityState().phase == eve::spawn::Phase::Running);
    applyPlayers(3, {{435, 224}});
    REQUIRE(proximityState().phase == eve::spawn::Phase::Running);
    applyPlayers(4, {{441, 224}});
    REQUIRE(proximityState().phase == eve::spawn::Phase::Dormant);
    REQUIRE_EQ(proximityState().alive, std::uint32_t(2));
    applyPlayers(5, {{435, 224}});
    REQUIRE(proximityState().phase == eve::spawn::Phase::Dormant);
    applyPlayers(6, {{1000, 1000}, {430, 224}});
    REQUIRE(proximityState().phase == eve::spawn::Phase::Running);
    proximity->advance({eve::SimulationTick(7), eve::Duration(0)}, {}).expect("absent frame preserves activation");
    REQUIRE(proximityState().phase == eve::spawn::Phase::Running);
    applyPlayers(8, {});
    REQUIRE(proximityState().phase == eve::spawn::Phase::Dormant);
    REQUIRE_EQ(proximityState().total, std::uint64_t(2));
    proximity->stop().expect("proximity cleanup");
    proximity.reset();
    {
        eve::rpg::detail::SimulationEvents     formalEvents;
        eve::rpg::detail::SimulationEventScope formalScope(formalEvents);
        auto*                                  formalActor = eve::rpg::RPGActor::createActor();
        formalActor->setBaseAttribute("hp", 10);
        formalActor->setCurrent("hp", 10);
        eve::rpg::VitalsSystem::takeDamage(formalActor, "hp", 1);
        REQUIRE_EQ(formalEvents.vitals.size(), std::size_t(1));
        auto deaths =
            Preview::create(binding, contract, *files, frozen, *templates, units, owner.child("deaths"), 42, 6)
                .expect("private death preview");
        deaths->advance({eve::SimulationTick(1), {}}, {}).expect("initial private population");
        const auto initial = deaths->captureDiagnostics().expect("private identities");
        const auto subject = initial.sources[0].state.subjects.front();
        REQUIRE(!deaths->defeat(eve::SubjectRef::fromPersistentId(owner.child("unknown"))).ok());
        REQUIRE(deaths->defeat(subject).expect("canonical private lethal damage") == eve::spawn::Change::Changed);
        REQUIRE(deaths->defeat(subject).expect("same unhanded death is unchanged") == eve::spawn::Change::Unchanged);
        Rects living;
        REQUIRE_EQ(deaths->draw(living, {}).expect("dead actual actor is not rendered"), std::size_t(1));
        REQUIRE_EQ(deaths->captureDiagnostics().expect("death waits for handoff").tick.value(), std::uint64_t(1));
        REQUIRE(!deaths->advance({eve::SimulationTick(1), {}}, {}).ok());
        deaths->advance({eve::SimulationTick(2), {}}, {}).expect("retained death after rejected tick");
        const auto afterDeath = deaths->captureDiagnostics().expect("actual death ledger");
        REQUIRE_EQ(afterDeath.sources[0].state.alive, std::uint32_t(1));
        REQUIRE_EQ(afterDeath.budgets[0].alive, std::uint32_t(1));
        REQUIRE(!deaths->defeat(subject).ok());
        REQUIRE_EQ(deaths->advance({eve::SimulationTick(3), eve::Duration(1000000000)}, {})
                       .expect("replacement delay not elapsed")
                       .units,
                   std::uint32_t(0));
        REQUIRE_EQ(deaths->advance({eve::SimulationTick(4), eve::Duration(1000000000)}, {})
                       .expect("actual delayed replacement")
                       .units,
                   std::uint32_t(1));
        REQUIRE_EQ(deaths->captureDiagnostics().expect("one replacement only").sources[0].state.total,
                   std::uint64_t(3));
        REQUIRE_EQ(formalEvents.vitals.size(), std::size_t(1));
        REQUIRE_EQ(formalActor->getCurrent("hp"), 9.0);
        deaths->stop().expect("private death cleanup");
        REQUIRE(!deaths->defeat(subject).ok());
        formalActor->release();
    }
    auto waveTemplate = json("template.json");
    waveTemplate.set("id", "preview.waves");
    waveTemplate.getIf<eve::Value::Object>()->erase("content");
    waveTemplate.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
        {"id":"first","delay_seconds":0,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"},
        {"id":"second","delay_seconds":1,"members":[{"id":"guard","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"}]})")
                                     .expect("canonical finite wave source"));
    templates->publish(waveTemplate).expect("actual finite wave template");
    auto  waveSource   = frozen;
    auto& waveInstance = waveSource.source.find("instances")->at(0);
    waveInstance.find("template")->set("id", "preview.waves");
    waveInstance.getIf<eve::Value::Object>()->erase("overrides");
    auto waves = Preview::create(binding, contract, *files, waveSource, *templates, units, owner.child("waves"), 42, 6)
                     .expect("actual wave preview");
    REQUIRE_EQ(waves->advance({eve::SimulationTick(1), {}}, {}).expect("first actual wave").units, std::uint32_t(1));
    auto firstWave = waves->captureDiagnostics().expect("first wave identity");
    waves->defeat(firstWave.sources[0].state.subjects.front()).expect("defeat first wave member");
    waves->advance({eve::SimulationTick(2), {}}, {}).expect("complete first wave");
    REQUIRE_EQ(waves->advance({eve::SimulationTick(3), eve::Duration(500000000)}, {}).expect("inter-wave delay").units,
               std::uint32_t(0));
    REQUIRE_EQ(
        waves->advance({eve::SimulationTick(4), eve::Duration(500000000)}, {}).expect("second actual wave").units,
        std::uint32_t(1));
    auto secondWave = waves->captureDiagnostics().expect("second wave identity");
    waves->defeat(secondWave.sources[0].state.subjects.front()).expect("defeat final member");
    waves->advance({eve::SimulationTick(5), {}}, {}).expect("complete final wave");
    const auto completed = waves->captureDiagnostics().expect("actual finite completion");
    REQUIRE(completed.sources[0].state.phase == eve::spawn::Phase::Completed);
    REQUIRE_EQ(completed.sources[0].state.alive, std::uint32_t(0));
    REQUIRE_EQ(completed.sources[0].state.total, std::uint64_t(2));
    REQUIRE_EQ(completed.budgets[0].alive, std::uint32_t(0));
    waves->stop().expect("wave preview cleanup");
    waves.reset();
    auto exclusionTemplate = json("template.json");
    exclusionTemplate.set("id", "player.exclusion");
    exclusionTemplate.find("placement")
        ->find("constraints")
        ->getIf<eve::Value::Array>()
        ->push_back(eve::Value::object({{"kind", "distance_from_players"}, {"min", 1000}}));
    templates->publish(exclusionTemplate).expect("actual player exclusion template");
    auto exclusionSource = frozen;
    exclusionSource.source.find("instances")->at(0).find("template")->set("id", "player.exclusion");
    auto exclusion =
        Preview::create(binding, contract, *files, exclusionSource, *templates, units, owner.child("exclusion"), 42, 6)
            .expect("actual exclusion world");
    {
        std::vector<eve::spawn::Point> near{{320, 224}};
        REQUIRE_EQ(
            exclusion
                ->advance({eve::SimulationTick(1), eve::Duration(0)}, {std::span<const eve::spawn::Point>(near), {}})
                .expect("player exclusion blocks placement")
                .units,
            std::uint32_t(0));
    }
    REQUIRE_EQ(exclusion->advance({eve::SimulationTick(2), eve::Duration(1000000000)}, {})
                   .expect("absent frame preserves exclusion")
                   .units,
               std::uint32_t(0));
    std::vector<eve::spawn::Point> empty;
    REQUIRE_EQ(exclusion
                   ->advance({eve::SimulationTick(3), eve::Duration(1000000000)},
                             {std::span<const eve::spawn::Point>(empty), {}})
                   .expect("empty frame clears exclusion")
                   .units,
               std::uint32_t(2));
    const auto placementTrace = exclusion->capturePlacementTrace().expect("actual owning placement diagnostics");
    REQUIRE(placementTrace.mode == eve::map_spawn::PlacementTraceMode::RecentAttempts);
    REQUIRE(std::any_of(placementTrace.attempts.begin(), placementTrace.attempts.end(), [](const auto& row) {
        return row.decision == eve::map_spawn::PlacementDecision::PlayerDistance && row.point.has_value();
    }));
    Rects rejected;
    rejected.callback = [&] {
        REQUIRE(!exclusion->capturePlacementTrace().ok());
        REQUIRE(!exclusion->stop().ok());
    };
    REQUIRE(exclusion->drawPlacementTrace(rejected).expect("actual rejected candidate markers") > 0);
    REQUIRE(!rejected.values.empty());
    exclusion->stop().expect("exclusion cleanup");
    exclusion.reset();
    REQUIRE(!placementTrace.attempts.empty());
    REQUIRE_EQ(countFormal(), before);
}
