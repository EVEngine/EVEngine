#include <Poco/UUIDGenerator.h>
#include <zeroerr/unittest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include "GenerationClient.h"

namespace {
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("eve-ssui-test-" + Poco::UUIDGenerator::defaultGenerator().createRandom().toString());
    std::string            asset = "generated/" + std::string(64, 'a') + ".png";
    ssui::GenerationClient client;
    Fixture() { std::filesystem::create_directories(root / "generated"); }
    ~Fixture() {
        client.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void        doc(const std::string& body) { std::ofstream(root / "generated/material.json") << body; }
    std::string text() {
        std::ifstream in(root / "generated/material.json");
        return {std::istreambuf_iterator<char>(in), {}};
    }
    auto        configure() { return client.configure("http://127.0.0.1:1", root.string()); }
    std::string legacy() { return "{\"revision\":1,\"baseColor\":\"" + asset + "\",\"history\":[\"\"]}"; }
};
void require(bool value) {
    if (!value) throw std::runtime_error("Contract failed");
}
}  // namespace
TEST_CASE("ssui.validation") {
    Fixture f;
    require(!f.client.configure("https://localhost", f.root.string()).ok());
    require(f.configure().ok());
    require(!f.client.start("mock.checker@1", "[]", "{}").ok());
    require(!f.client.start("mock.checker@1", "null", "{}").ok());
    require(!f.client.apply().ok());
    require(!f.client.undo().ok());
    require(!f.client.completion().ok());
}
TEST_CASE("ssui.migration") {
    Fixture f;
    f.doc(f.legacy());
    require(f.configure().ok());
    require(f.client.applied() == f.asset);
    auto undone = f.client.undo();
    require(undone.ok());
    require(undone.value().empty());
    require(f.text().find("eve.ssui-material") != std::string::npos);
    require(f.configure().ok());
    require(f.client.applied().empty());
}
TEST_CASE("ssui.atomic_restore") {
    Fixture f;
    f.doc(f.legacy());
    require(f.configure().ok());
    for (const auto& bad :
         {"{\"schema\":\"eve.ssui-material\",\"version\":2,\"revision\":2,\"baseColor\":\"\",\"history\":[]}",
          "{\"revision\":2,\"baseColor\":\"\",\"history\":[\"../outside.png\"]}",
          "{\"revision\":2,\"baseColor\":\"\",\"history\":null}",
          "{\"revision\":2,\"baseColor\":\"\",\"history\":[],\"unknown\":true}",
          "{\"revision\":-1,\"baseColor\":\"\",\"history\":[]}"}) {
        f.doc(bad);
        require(!f.configure().ok());
        require(f.client.applied() == f.asset);
        require(f.text() == bad);
    }
}
TEST_CASE("ssui.persistence_failure") {
    Fixture f;
    f.doc(f.legacy());
    require(f.configure().ok());
    // Read succeeds, but a symlink to a document on a read-only filesystem is not needed:
    // replace the document with a directory to inject a read/commit failure on every OS.
    std::filesystem::remove(f.root / "generated/material.json");
    std::filesystem::create_directory(f.root / "generated/material.json");
    require(!f.client.undo().ok());
    require(f.client.applied() == f.asset);
    std::filesystem::remove(f.root / "generated/material.json");
    f.doc(f.legacy());
    require(f.client.undo().ok());
}
TEST_CASE("ssui.revision") {
    Fixture f;
    f.doc(f.legacy());
    require(f.configure().ok());
    f.doc("{\"revision\":3,\"baseColor\":\"\",\"history\":[]}");
    auto result = f.client.undo();
    require(!result.ok());
    require(result.status().diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
    require(f.client.applied() == f.asset);
}
TEST_CASE("ssui.cancellation") {
    Fixture f;
    require(f.configure().ok());
    require(f.client.start("mock.checker@1", "{}", "{}").ok());
    f.client.requestCancel();
    f.client.shutdown();
    require(!f.client.busy());
    require(f.client.candidate().empty());
    auto result = f.client.completion();
    require(!result.ok());
    require(result.status().diagnostics()[0].code() == eve::DiagnosticCode::Cancelled);
}
TEST_CASE("ssui.protocol") {
    Fixture     f;
    const auto* server = std::getenv("SSUI_TEST_SERVER");
    const auto* mode   = std::getenv("SSUI_TEST_MODE");
    require(server != nullptr && mode != nullptr);
    if (std::string(mode) == "destroy") {
        {
            ssui::GenerationClient active;
            require(active.configure(server, f.root.string()).ok());
            require(active.start("mock.checker@1", "{}", "{}").ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(!std::filesystem::exists(f.root / "generated/material.json"));
        require(std::filesystem::is_empty(f.root / "generated"));
        return;
    }
    require(f.client.configure(server, f.root.string()).ok());
    require(f.client.start("mock.checker@1", "{}", "{}").ok());
    if (std::string(mode) == "cancel") {
        auto duplicate = f.client.start("mock.checker@1", "{}", "{}");
        require(!duplicate.ok());
        require(duplicate.status().diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        f.client.requestCancel();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (f.client.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(!f.client.busy());
    auto completion = f.client.completion();
    if (std::string(mode) != "success") {
        require(!completion.ok());
        require(f.client.candidate().empty());
        require(!std::filesystem::exists(f.root / "generated/material.json"));
        return;
    }
    require(completion.ok());
    require(std::filesystem::exists(f.root / completion.value()));
    require(f.client.applied().empty());
    auto applied = f.client.apply();
    require(applied.ok());
    require(applied.value() == completion.value());
    require(f.configure().ok());
    require(f.client.applied() == applied.value());
    auto undone = f.client.undo();
    require(undone.ok());
    require(undone.value().empty());
}
