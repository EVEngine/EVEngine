#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "cmdline/cmdline.h"
#include "common/Module.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace {

// Runs the cmdline module in-process with the given argv (argv[0] is the
// program name) and returns the exit code. CWD-relative commands operate on
// the current directory, so tests wrap this with PushDir.
int runCli(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    auto* cmd = eve::ModuleManager::requireInstance<eve::cmd::Cmdline>("Cmdline");
    return cmd->runArgs(static_cast<unsigned>(argv.size()), argv.data());
}

// RAII: chdir into `dir` for the lifetime of the object, restoring afterwards
// so tests sharing a process cannot leak the working directory into each other.
class PushDir {
public:
    explicit PushDir(const std::filesystem::path& dir) : old_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~PushDir() {
        std::error_code ec;
        std::filesystem::current_path(old_, ec);
    }

private:
    std::filesystem::path old_;
};

// Swaps cout/cerr buffers so the test can inspect what a command printed
// (rang color codes are dropped automatically when the sink is not a TTY).
class CaptureStreams {
public:
    CaptureStreams() {
        cout_old_ = std::cout.rdbuf(cout_buf_.rdbuf());
        cerr_old_ = std::cerr.rdbuf(cerr_buf_.rdbuf());
    }
    ~CaptureStreams() {
        std::cout.rdbuf(cout_old_);
        std::cerr.rdbuf(cerr_old_);
    }

    std::string out() const { return cout_buf_.str(); }
    std::string err() const { return cerr_buf_.str(); }
    std::string all() const { return out() + err(); }

private:
    std::ostringstream cout_buf_, cerr_buf_;
    std::streambuf*    cout_old_ = nullptr;
    std::streambuf*    cerr_old_ = nullptr;
};

std::filesystem::path tempDir(const char* tag) {
    const auto p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

void writeFile(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

// Reads the ZIP central directory (EOCD -> central entries) and returns the
// entry names in archive order. Returns {} when the file is not a valid ZIP.
std::vector<std::string> zipEntryNames(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 22) return {};

    const size_t eocd = data.rfind("PK\x05\x06");
    if (eocd == std::string::npos || data.size() - eocd < 22) return {};

    const auto le16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(static_cast<unsigned char>(data[off]) |
                                     (static_cast<unsigned char>(data[off + 1]) << 8));
    };
    const auto le32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(static_cast<unsigned char>(data[off]) |
                                     (static_cast<unsigned char>(data[off + 1]) << 8) |
                                     (static_cast<unsigned char>(data[off + 2]) << 16) |
                                     (static_cast<unsigned char>(data[off + 3]) << 24));
    };

    const uint32_t cdSize   = le32(eocd + 12);
    const uint32_t cdOffset = le32(eocd + 16);
    if (cdOffset > data.size() || cdSize > data.size() - cdOffset) return {};

    std::vector<std::string> names;
    size_t                   pos = cdOffset;
    const size_t             end = static_cast<size_t>(cdOffset) + cdSize;
    while (pos + 46 <= end && data.compare(pos, 4, "PK\x01\x02") == 0) {
        const size_t nameLen = le16(pos + 28);
        const size_t extraLen = le16(pos + 30);
        const size_t commentLen = le16(pos + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > end) return {};
        names.emplace_back(data.substr(pos + 46, nameLen));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return names;
}

}  // namespace

TEST_CASE("cmdline.helpListsSubcommands") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--help"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Subcommands:") != std::string::npos);
    for (const char* sub : {"run", "create", "zip", "package", "dev", "doc", "clean", "test", "build", "get", "mcp"})
        CHECK(cap.out().find(sub) != std::string::npos);
}

TEST_CASE("cmdline.versionPrintsVersion") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--version"});
    CHECK(rc == 0);
    CHECK(cap.out().find('v') == 0);
}

TEST_CASE("cmdline.unknownCommandFails") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "frobnicate"});
    CHECK(rc != 0);
    CHECK(cap.all().find("not expected") != std::string::npos);
}

TEST_CASE("cmdline.createScaffoldsGame") {
    const auto dir = tempDir("eve_ut_cmdline_create");
    PushDir   pd(dir);
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("Created mygame") != std::string::npos);
    }
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "config.nut"));
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "main.nut"));

    // A second run must not clobber existing files.
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("skip existing config.nut") != std::string::npos);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.cleanDoesNotScaffoldProject") {
    // Regression: the clean handler used to call Create(), so `eve clean`
    // scaffolded a fresh "debug" project in the current directory.
    const auto dir = tempDir("eve_ut_cmdline_clean");
    PushDir   pd(dir);
    CaptureStreams cap;
    const int      rc = runCli({"eve", "clean"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Clean debug build") != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "debug"));
    CHECK(!std::filesystem::exists(dir / "config.nut"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.docPrintsUrlAndSymbol") {
    // Regression: the doc handler looked up the "create" subcommand, so
    // `eve doc` never ran. --no-open keeps the test (and headless/CI calls)
    // from spawning a browser.
    CaptureStreams cap;
    const int      rc = runCli({"eve", "doc", "--no-open", "gfx.drawSolidRect"});
    CHECK(rc == 0);
    CHECK(cap.out().find("EVEngine docs for 'gfx.drawSolidRect'") != std::string::npos);
    CHECK(cap.out().find("https://evengine.github.io/EVEngine/") != std::string::npos);

    CaptureStreams cap2;
    const int      rc2 = runCli({"eve", "doc", "--no-open"});
    CHECK(rc2 == 0);
    CHECK(cap2.out().find("EVEngine docs:") != std::string::npos);
}

TEST_CASE("cmdline.zipDefaultNamesArchiveAfterCwd") {
    // Regression: `eve zip` with no path (default ".") used to write a file
    // literally named "..eve" instead of <current-dir-name>.eve.
    const auto dir = tempDir("eve_ut_cmdline_zip_default");
    PushDir   pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);
    const std::string expected = dir.filename().string() + ".eve";
    CHECK(cap.out().find(expected) != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "..eve"));
    REQUIRE(std::filesystem::is_regular_file(dir / expected));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.zipProducesValidArchiveWithoutItself") {
    const auto dir = tempDir("eve_ut_cmdline_zip_entries");
    PushDir   pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");
    writeFile(dir / "scripts" / "a.nut", "a <- 1\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);

    const std::string archive = dir.filename().string() + ".eve";
    REQUIRE(std::filesystem::is_regular_file(dir / archive));

    const auto names = zipEntryNames(dir / archive);
    REQUIRE(names.size() == 2);
    // Directory iteration order is not guaranteed, so compare sorted sets.
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted[0] == "main.nut");
    CHECK(sorted[1] == "scripts/a.nut");
    // The writer must never embed the output archive into itself.
    for (const auto& n : names) CHECK(n != archive);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
