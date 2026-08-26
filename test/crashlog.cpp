#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/CrashLog.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace eve;

TEST_CASE("crashLog.initAndRecord") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eve_crashlog_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    eve::initSystemLogging(dir.string());
    CHECK(!eve::crashLogPath().empty());

    eve::recordLogEvent("error", "boom-test");

    std::ifstream in(eve::crashLogPath());
    CHECK(in.good());
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    CHECK(content.find("boom-test") != std::string::npos);
    CHECK(content.find("error") != std::string::npos);
    CHECK(content.find("session start") != std::string::npos);
}

TEST_CASE("crashLog.recordCrashEvent") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eve_crashlog_test";
    eve::recordCrashEvent("[crash] code=0xC0000005 at 0x0\nframe0\n");

    std::ifstream in(eve::crashLogPath());
    CHECK(in.good());
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    CHECK(content.find("code=0xC0000005") != std::string::npos);
    CHECK(content.find("frame0") != std::string::npos);
    CHECK(content.find("crash") != std::string::npos);
}
