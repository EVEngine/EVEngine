#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::dev;

TEST_CASE("devtools.console.logRingBuffer") {
    auto& console = ConsolePanel::instance();
    console.clear();
    console.setVisible(false);
    CHECK(!console.isVisible());
    console.toggleVisible();
    CHECK(console.isVisible());
    console.setVisible(false);

    console.addInfo("hello");
    console.addWarn("careful");
    console.addError("boom");
    console.addLog("debug", "trace");

    const std::string out = console.format(16);
    CHECK(out.find("hello") != std::string::npos);
    CHECK(out.find("careful") != std::string::npos);
    CHECK(out.find("boom") != std::string::npos);
    CHECK(out.find("trace") != std::string::npos);

    const auto recent = console.recent(2);
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].text == "boom");
    CHECK(recent[1].text == "trace");

    console.setMaxEntries(3);
    console.addInfo("overflow1");
    console.addInfo("overflow2");
    console.addInfo("overflow3");
    CHECK(console.format(100).find("hello") == std::string::npos);

    console.clear();
    CHECK(console.format(8).empty());
}
