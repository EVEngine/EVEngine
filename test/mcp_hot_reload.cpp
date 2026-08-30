#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/EditorHost.h"

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out << text;
    REQUIRE(out.good());
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.hostResourceHotReload") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_hot_reload_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto editorPath = root / "editors" / "terrain.editor.json";
    const auto vmPath     = root / "editors" / "terrain.vm.nut";
    const auto mcpPath    = root / "mcp.nut";

    writeText(
        editorPath,
        R"({"id":"terrain","title":"Terrain v1","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":5,"bind":"vm.brushSize","onChange":"vm.onChange"}]})");
    writeText(
        vmPath,
        R"(::TerrainVM <- { version = 1, brushSize = 5, changedCount = 0, onChange = function(widget, value) { this.changedCount++; } };)");
    writeText(mcpPath, R"(::mcpReloadVersion <- 1;)");

    ssq::VM vm(1024, ssq::Libs::ALL);
    host.start(vm, root.string(), /*allowWindow=*/false);

    REQUIRE(host.listEditors().find("terrain") != std::string::npos);
    REQUIRE(host.setEditorValue("terrain", "brushSize", "12") == "ok");
    CHECK(vm.find("TerrainVM").toTable().find("brushSize").toInt() == 12);
    CHECK(vm.find("TerrainVM").toTable().find("changedCount").toInt() == 1);
    CHECK(host.consumeEvents("terrain").find("change") != std::string::npos);

    writeText(
        vmPath,
        R"(::TerrainVM <- { version = 2, brushSize = 5, changedCount = 0, onChange = function(widget, value) { this.changedCount++; } };)");
    CHECK(host.reloadResource("editors/terrain.vm.nut") == "ok");
    auto terrainVm = vm.find("TerrainVM").toTable();
    CHECK(terrainVm.find("version").toInt() == 2);
    CHECK(terrainVm.find("brushSize").toInt() == 12);
    CHECK(terrainVm.find("changedCount").toInt() == 0);
    CHECK(host.consumeEvents("terrain") == "[]");

    writeText(vmPath, "::TerrainVM <- { broken = ; };");
    CHECK(host.reloadResource("editors/terrain.vm.nut").rfind("error:", 0) == 0);
    terrainVm = vm.find("TerrainVM").toTable();
    CHECK(terrainVm.find("version").toInt() == 2);
    CHECK(terrainVm.find("brushSize").toInt() == 12);

    writeText(
        editorPath,
        R"({"id":"terrain","title":"Terrain v2","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":3,"bind":"vm.brushSize","onChange":"vm.onChange"},{"type":"text","id":"status","value":"ready"}]})");
    CHECK(host.reloadResource("editors/terrain.editor.json").rfind("error:", 0) != 0);
    CHECK(host.listEditors().find("Terrain v2") != std::string::npos);
    CHECK(host.editorState("terrain").find("\"brushSize\":12") != std::string::npos);
    CHECK(host.consumeEvents("terrain") == "[]");

    writeText(editorPath, R"({"id":"other","title":"Wrong id","children":[]})");
    CHECK(host.reloadResource("editors/terrain.editor.json").find("must match") != std::string::npos);
    CHECK(host.listEditors().find("Terrain v2") != std::string::npos);

    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(vm.find("mcpReloadVersion").toInt() == 1);
    CHECK(host.reloadResource("../outside.nut").find("outside project root") != std::string::npos);

    host.setHotReloadWatchCount(1);
    const auto status = parseObject(host.hotReloadStatus());
    REQUIRE(status);
    CHECK(status->getValue<bool>("enabled"));
    CHECK(status->getValue<int>("watchCount") == 1);
    CHECK(status->getValue<int>("reloadCount") >= 3);
    CHECK(status->getValue<int>("failureCount") >= 2);

    host.stop();
    std::filesystem::remove_all(root);
}
