#include "common/Runtime.h"
#include "devtools/DevTool.hpp"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;

// The Runtime error-handler sink wired by DevTool::attach(Runtime&) routes
// errors that the uncaught-error VM hook never sees (compile / reflect /
// unload) into the DevTool slicer/report, without double-reporting the
// uncaught runtime errors the hook already handled.

TEST_CASE("devtools.runtimeErrorSink.reportsCompileError") {
    dev::DevTool::instance().detach();
    Runtime runtime(256, ssq::Libs::ALL);
    runtime.initialize();
    dev::DevTool::instance().attach(runtime, /*sampleLocals=*/true);

    bool caught = false;
    try {
        runtime.runSource("class Broken {", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK_EQ(error.source(), std::string("broken.nut"));
    }
    CHECK(caught);

    // The Runtime error-handler sink must have routed the compile error into the
    // DevTool report even though the VM error hook never fires for a compile
    // failure.
    const std::string report = dev::DevTool::instance().lastReport();
    CHECK(!report.empty());
    CHECK(report.find("broken.nut") != std::string::npos);

    dev::DevTool::instance().detach();
}

TEST_CASE("devtools.runtimeErrorSink.noDoubleReportOnRuntimeError") {
    dev::DevTool::instance().detach();
    Runtime runtime(256, ssq::Libs::ALL);
    runtime.initialize();
    dev::DevTool::instance().attach(runtime, /*sampleLocals=*/true);

    bool caught = false;
    try {
        runtime.runSource(R"SQ(
function boom() { throw "kaboom" }
boom();
)SQ", "boom.nut");
    } catch (const ScriptException& error) {
        caught = true;
        // The VM error hook reported this uncaught error, so the Runtime sink
        // must have skipped it (reported()==true); the exception still carries
        // the live location + stack.
        CHECK(error.reported());
        CHECK(error.hasLocation());
        CHECK(!error.stackTrace().empty());
    }
    CHECK(caught);

    // Reported exactly once through the DevTool report pipeline.
    const std::string report = dev::DevTool::instance().lastReport();
    CHECK(!report.empty());
    CHECK(report.find("kaboom") != std::string::npos);

    dev::DevTool::instance().detach();
}
