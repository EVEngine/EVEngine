#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::dev;


TEST_CASE("devtools.console.attachEvalPrintCapture") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    console.clear();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    {
        auto script = vm.compileSource("score <- 40;\nfunction double(x) { return x * 2; }\n");
        vm.run(script);
    }
    dt.attach(vm, false);
    CHECK(console.isAttached());

    // REPL: evaluate a root-table expression.
    const std::string result = console.eval("score + 2");
    CHECK(result == "42");
    CHECK(console.format(16).find("42") != std::string::npos);

    // REPL: call a user function.
    CHECK(console.eval("double(21)") == "42");

    // REPL: string literal echoes quoted.
    CHECK(console.eval("\"hi\"") == "\"hi\"");

    // REPL: compile error reported without crashing.
    CHECK(console.eval("1 +") == "error: compile failed");

    // print() from a script is captured into the log.
    {
        auto script = vm.compileSource("print(\"captured-line\");\n");
        vm.run(script);
    }
    CHECK(console.format(16).find("captured-line") != std::string::npos);

    dt.detach();
    CHECK(!console.isAttached());
    console.clear();
}

TEST_CASE("devtools.console.persistentAcrossVms") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    console.clear();

    // The ring buffer survives a detach/re-attach cycle (DevTools singleton).
    ssq::VM vm1(512, ssq::Libs::ALL);
    dt.attach(vm1, false);
    console.addInfo("sticky");
    dt.detach();

    ssq::VM vm2(512, ssq::Libs::ALL);
    dt.attach(vm2, false);
    CHECK(console.format(8).find("sticky") != std::string::npos);
    dt.detach();
    console.clear();
}
