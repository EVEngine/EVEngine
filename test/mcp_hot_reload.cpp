#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/ServiceInterfaces.h"
#include "ui/EditorHost.h"

#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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

TEST_CASE("devtools.mcp.hostScriptFailureCarriesTheReason") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_host_text_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto vmPath = root / "editors" / "terrain.vm.nut";
    std::filesystem::create_directories(root / "editors");

    writeText(vmPath, R"(::TerrainVM <- { version = 1 };)");
    writeText(
        root / "editors" / "terrain.editor.json",
        R"({"id":"terrain","title":"Terrain","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":5,"bind":"vm.brushSize"}]})");

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm.getHandle());
    host.start(vm, root.string(), /*allowWindow=*/false);
    REQUIRE(host.listEditors().find("terrain") != std::string::npos);

    // ViewModel 的编译失败必须带上出处与位置，而不是只回一个通用词。dev 之后把来源名从
    // 缺省的 "host_vm.nut" 换成真实资源路径（`game:/editors/<id>.vm.nut`），所以断言的是
    // 文件名的后半段而不是旧名字。实测字符串形如
    // "error: compile failed:  error at game:/editors/terrain.vm.nut:1 (<anonymous>): expression expected"。
    writeText(vmPath, "::TerrainVM <- { broken = ; };");
    const std::string compileError = host.reloadResource("editors/terrain.vm.nut");
    REQUIRE(compileError.rfind("error:", 0) == 0);
    REQUIRE(compileError.find("compile failed") != std::string::npos);
    REQUIRE(compileError.find("terrain.vm.nut") != std::string::npos);

    // 运行期抛出必须带上脚本错误原文与行号：
    // "error: runtime failed: game:/editors/terrain.vm.nut:2 (main): vm chunk exploded"。
    writeText(vmPath, "::TerrainVM <- { version = 2 };\nthrow \"vm chunk exploded\";\n");
    const std::string runtimeError = host.reloadResource("editors/terrain.vm.nut");
    REQUIRE(runtimeError.rfind("error:", 0) == 0);
    REQUIRE(runtimeError.find("runtime failed") != std::string::npos);
    REQUIRE(runtimeError.find("terrain.vm.nut:2") != std::string::npos);
    REQUIRE(runtimeError.find("vm chunk exploded") != std::string::npos);

    // 片段路径（mcp.nut）走的是同一个 runSquirrel。
    writeText(root / "mcp.nut", "throw \"snippet exploded\";\n");
    const std::string snippetError = host.reloadResource("mcp.nut");
    REQUIRE(snippetError.find("runtime failed") != std::string::npos);
    REQUIRE(snippetError.find("snippet exploded") != std::string::npos);

    host.stop();
    std::filesystem::remove_all(root);

    // onChange 抛出：`setEditorValue` 仍然回 "ok"（绑定值已经写进 ViewModel），但失败必须
    // 被计数上报——`callTableFunc` 过去把原因压成 "runtime failed: <name>" 并只在 stderr
    // 打一行。用新的项目目录，避免上面那个会抛的 terrain VM 干扰计数。
    const auto root2 =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_onchange_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root2 / "editors");
    writeText(
        root2 / "editors" / "err.editor.json",
        R"({"id":"err","title":"Err","vm":"ErrVM","children":[{"type":"slider","id":"v","value":1,"bind":"vm.v","onChange":"vm.onChange"}]})");
    writeText(root2 / "editors" / "err.vm.nut",
              R"(::ErrVM <- { v = 1, onChange = function(widget, value) { throw "onChange exploded"; } };)");

    ssq::VM vm2(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm2.getHandle());
    eve::script::resetReportedScriptCallFailures();
    host.start(vm2, root2.string(), /*allowWindow=*/false);
    REQUIRE(eve::script::reportedScriptCallFailures() == 0);

    REQUIRE(host.setEditorValue("err", "v", "7") == "ok");
    REQUIRE(eve::script::reportedScriptCallFailures() == 1);
    REQUIRE(vm2.find("ErrVM").toTable().find("v").toFloat() == 7.0);

    host.stop();
    std::filesystem::remove_all(root2);
}

TEST_CASE("devtools.mcp.startupViewModelFailureIsVisible") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_startup_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "editors");
    writeText(
        root / "editors" / "broken.editor.json",
        R"({"id":"broken","title":"Broken","vm":"BrokenVM","children":[{"type":"slider","id":"v","value":1,"bind":"vm.v"}]})");
    // The manifest is fine; the ViewModel throws while it is loaded at startup.
    writeText(root / "editors" / "broken.vm.nut", "::BrokenVM <- { v = 1 };\nthrow \"startup vm exploded\";\n");

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm.getHandle());
    eve::script::resetReportedScriptCallFailures();
    host.start(vm, root.string(), /*allowWindow=*/false);

    // The editor itself still loads -- only its script failed -- and that failure must be
    // visible through the status an MCP client reads. Before this, the result was
    // discarded and hotReloadStatus() reported a healthy host.
    REQUIRE(host.listEditors().find("broken") != std::string::npos);
    const auto status = parseObject(host.hotReloadStatus());
    REQUIRE(status);
    REQUIRE(status->getValue<int>("failureCount") >= 1);
    const std::string lastResult = status->getValue<std::string>("lastResult");
    REQUIRE(lastResult.rfind("error:", 0) == 0);
    REQUIRE(lastResult.find("startup vm exploded") != std::string::npos);

    host.stop();
    std::filesystem::remove_all(root);
}

namespace {

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::unordered_map<std::string, std::string> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        const auto found = files.find(path);
        if (found == files.end()) return false;
        output.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }
};

}  // namespace

TEST_CASE("devtools.mcp.hostScriptImport") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_host_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    writeText(root / "mcp.nut",
              "import { VALUE } from \"game:/lib/value.nut\"\n"
              "imported_value <- VALUE + 1\n");

    MemoryFileSystem filesystem;
    filesystem.files["lib/value.nut"] = "export const VALUE = 41\n";
    eve::cap::detail::clearAllRaw();
    eve::cap::provide<eve::service::IFileSystem>(&filesystem);

    eve::Runtime runtime(512, ssq::Libs::ALL);
    eve::ModuleManager::expose(runtime);
    host.start(runtime.vm(), root.string(), /*allowWindow=*/false);

    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(runtime.vm().get<int64_t>("imported_value") == int64_t(42));

    filesystem.files["lib/value.nut"] = "export const VALUE = 7\n";
    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(runtime.vm().get<int64_t>("imported_value") == int64_t(8));

    host.stop();
    eve::cap::detail::clearAllRaw();
    std::filesystem::remove_all(root);
}
