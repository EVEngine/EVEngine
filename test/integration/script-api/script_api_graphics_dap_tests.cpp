// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "cases": {
    "platform-network": [
      "devtools.dap.stopOnEntry",
      "devtools.dap.exceptionBreakpoints",
      "devtools.dap.breakpointVerificationEvent",
      "devtools.dap.setBreakpointsStoresCondition"
    ]
  },
  "module": "devtools",
  "modules": [
    "decal",
    "definitions",
    "devtools",
    "dialogue",
    "economy",
    "editor",
    "effects",
    "filesystem",
    "game_event",
    "graphics",
    "hexmap",
    "i18n",
    "image",
    "inventory",
    "joystick"
  ],
  "origins": [
    "dap.cpp",
    "debugger.cpp",
    "debugger_audit.cpp",
    "decal_script.cpp",
    "definitions.cpp",
    "dialogue_authoring.cpp",
    "dialogue_proc_script.cpp",
    "economy.cpp",
    "editor_voxel_catalog_script.cpp",
    "effects.cpp",
    "eventstream.cpp",
    "filesystem.cpp",
    "graphics_shader_spv_script.cpp",
    "graphics_vegetation_script.cpp",
    "grass_wind.cpp",
    "hexmap_sphere_script.cpp",
    "i18n_script.cpp",
    "image_script.cpp",
    "inventory_binding_architecture.cpp",
    "inventory_gameplay_control.cpp",
    "joystick_script.cpp"
  ],
  "generated": [
    "FilesystemTest.",
    "I18nScriptTest.",
    "JoystickScript."
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dap)
#include "PrivateTestScope.h"
#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <squirrel.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "common/Runtime.h"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dap.cpp
namespace test_dap {
using namespace eve::dev;

namespace {

class DapClient {
public:
    explicit DapClient(int port) {
        Poco::Net::SocketAddress addr("127.0.0.1", static_cast<uint16_t>(port));
        sock_.connect(addr, Poco::Timespan(2, 0));
        sock_.setBlocking(true);
        sock_.setReceiveTimeout(Poco::Timespan(0, 50000));  // 50ms
        sock_.setSendTimeout(Poco::Timespan(2, 0));
        // Ensure the DAP server accepts before we speak.
        for (int i = 0; i < 100 && !DebugAdapter::instance().hasClient(); ++i) {
            DebugAdapter::instance().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(DebugAdapter::instance().hasClient());
    }

    void sendRequest(const std::string& command, const std::string& argsJson = "{}") {
        std::ostringstream oss;
        oss << "{\"seq\":" << (++seq_) << ",\"type\":\"request\",\"command\":\"" << command << "\"";
        if (!argsJson.empty()) oss << ",\"arguments\":" << argsJson;
        oss << "}";
        sendFrame(oss.str());
    }

    void sendFrame(const std::string& json) {
        const std::string frame = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
        const int         n     = sock_.sendBytes(frame.data(), static_cast<int>(frame.size()));
        REQUIRE(n == static_cast<int>(frame.size()));
    }

    /** Read one DAP message; pumps DebugAdapter::poll while waiting. */
    Poco::JSON::Object::Ptr recv(int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            DebugAdapter::instance().poll();
            char buf[4096];
            try {
                const int n = sock_.receiveBytes(buf, sizeof(buf));
                if (n > 0) recvBuf_.append(buf, static_cast<size_t>(n));
            } catch (const Poco::TimeoutException&) {
            } catch (const Poco::Net::NetException&) {
            }

            auto msg = tryParseOne();
            if (!msg) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (msg->optValue<std::string>("type", "") == "event") events_.push_back(msg);
            return msg;
        }
        return nullptr;
    }

    /** Drain until a response for `command` (or timeout). Collects events. */
    Poco::JSON::Object::Ptr expectResponse(const std::string& command, int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = recv(50);
            if (!msg) continue;
            if (msg->optValue<std::string>("type", "") == "response" &&
                msg->optValue<std::string>("command", "") == command)
                return msg;
        }
        return nullptr;
    }

    /** Wait until a named event arrives. */
    Poco::JSON::Object::Ptr expectEvent(const std::string& name, int timeoutMs = 2000) {
        if (auto e = findEvent(name)) return e;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = recv(50);
            if (!msg) continue;
            if (msg->optValue<std::string>("type", "") == "event" && msg->optValue<std::string>("event", "") == name)
                return msg;
        }
        return findEvent(name);
    }

    Poco::JSON::Object::Ptr findEvent(const std::string& name) const {
        for (const auto& e : events_) {
            if (e->optValue<std::string>("event", "") == name) return e;
        }
        return nullptr;
    }

    void clearEvents() { events_.clear(); }

    const std::vector<Poco::JSON::Object::Ptr>& events() const { return events_; }

private:
    Poco::JSON::Object::Ptr tryParseOne() {
        const auto headerEnd = recvBuf_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return nullptr;
        const std::string header        = recvBuf_.substr(0, headerEnd);
        int               contentLength = -1;
        {
            const std::string key = "Content-Length:";
            auto              pos = header.find(key);
            if (pos != std::string::npos) {
                pos += key.size();
                while (pos < header.size() && (header[pos] == ' ' || header[pos] == '\t')) ++pos;
                contentLength = std::atoi(header.c_str() + pos);
            }
        }
        if (contentLength < 0) {
            recvBuf_.erase(0, headerEnd + 4);
            return nullptr;
        }
        const size_t total = headerEnd + 4 + static_cast<size_t>(contentLength);
        if (recvBuf_.size() < total) return nullptr;
        const std::string json = recvBuf_.substr(headerEnd + 4, static_cast<size_t>(contentLength));
        recvBuf_.erase(0, total);

        Poco::JSON::Parser parser;
        auto               root = parser.parse(json).extract<Poco::JSON::Object::Ptr>();
        return root;
    }

    Poco::Net::StreamSocket              sock_;
    std::string                          recvBuf_;
    int                                  seq_ = 0;
    std::vector<Poco::JSON::Object::Ptr> events_;
};

void pump(int ms = 20) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        DebugAdapter::instance().poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}  // namespace

TEST_CASE("devtools.dap.initializeLaunchContinueStep") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    dbg.detach();
    dbg.clearBreakpoints();
    dbg.clearWatches();
    dap.stop();

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    dap.setSourceRoot("/tmp/eve_game");

    DapClient client(port);
    pump(30);

    // ---- initialize ----
    client.sendRequest("initialize",
                       "{\"clientID\":\"eve-test\",\"adapterID\":\"eve\",\"linesStartAt1\":true,"
                       "\"columnsStartAt1\":true,\"pathFormat\":\"path\"}");
    auto initResp = client.expectResponse("initialize");
    REQUIRE(initResp);
    CHECK(initResp->optValue<bool>("success", false));
    auto initBody = initResp->getObject("body");
    REQUIRE(initBody);
    CHECK(initBody->optValue<bool>("supportsConfigurationDoneRequest", false));
    CHECK(initBody->optValue<bool>("supportsTerminateRequest", false));
    CHECK(initBody->optValue<bool>("supportsConditionalBreakpoints", false));
    CHECK(initBody->optValue<bool>("supportsExceptionInfoRequest", false));
    CHECK(initBody->has("exceptionBreakpointFilters"));
    // `initialized` is sent right after the initialize response; drain it.
    REQUIRE(client.expectEvent("initialized", 1000));

    // ---- launch ----
    client.clearEvents();
    client.sendRequest("launch", "{\"program\":\"/tmp/eve_game\",\"cwd\":\"/tmp/eve_game\",\"stopOnEntry\":false}");
    auto launchResp = client.expectResponse("launch");
    REQUIRE(launchResp);
    CHECK(launchResp->optValue<bool>("success", false));
    REQUIRE(client.expectEvent("process", 1000));

    // ---- setBreakpoints ----
    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"/tmp/eve_game/main.nut\",\"name\":\"main.nut\"},"
                       "\"breakpoints\":[{\"line\":10},{\"line\":20}]}");
    auto bpResp = client.expectResponse("setBreakpoints");
    REQUIRE(bpResp);
    CHECK(bpResp->optValue<bool>("success", false));
    {
        auto body = bpResp->getObject("body");
        REQUIRE(body);
        auto arr = body->getArray("breakpoints");
        REQUIRE(arr);
        CHECK_EQ(static_cast<int>(arr->size()), 2);
        CHECK(dbg.hasBreakpoint("main.nut", 10));
        CHECK(dbg.hasBreakpoint("/tmp/eve_game/main.nut", 20));
    }

    // ---- configurationDone ----
    client.sendRequest("configurationDone");
    auto cfgResp = client.expectResponse("configurationDone");
    REQUIRE(cfgResp);
    CHECK(cfgResp->optValue<bool>("success", false));

    // ---- threads / stackTrace while paused at a breakpoint ----
    dbg.resume();
    SourceLoc loc;
    loc.source   = "main.nut";
    loc.line     = 10;
    loc.function = "eve_update";
    // Hit BP while Running so pauseLoc_ is recorded for stack synthesis.
    CHECK(dbg.onScriptLine(loc));
    CHECK(dbg.isPaused());
    CHECK(static_cast<int>(dbg.lastPauseReason()) == static_cast<int>(PauseReason::Breakpoint));

    client.sendRequest("threads");
    auto thResp = client.expectResponse("threads");
    REQUIRE(thResp);
    CHECK(thResp->optValue<bool>("success", false));

    client.sendRequest("stackTrace", "{\"threadId\":1,\"startFrame\":0,\"levels\":20}");
    auto stResp = client.expectResponse("stackTrace");
    REQUIRE(stResp);
    CHECK(stResp->optValue<bool>("success", false));
    {
        auto body = stResp->getObject("body");
        REQUIRE(body);
        auto frames = body->getArray("stackFrames");
        REQUIRE(frames);
        CHECK(frames->size() >= 1);
        auto frame0 = frames->getObject(0);
        REQUIRE(frame0);
        auto src = frame0->getObject("source");
        REQUIRE(src);
        const std::string path = src->optValue<std::string>("path", "");
        // Alias from setBreakpoints absolute path must win over relative Squirrel names.
        CHECK_EQ(path, std::string("/tmp/eve_game/main.nut"));
    }

    // ---- continue (F5) ----
    client.clearEvents();
    client.sendRequest("continue", "{\"threadId\":1}");
    auto contResp = client.expectResponse("continue");
    REQUIRE(contResp);
    CHECK(contResp->optValue<bool>("success", false));
    CHECK(!dbg.isPaused());
    REQUIRE(client.findEvent("continued"));

    // ---- pause (replies immediately; next script line emits a located stop) ----
    client.clearEvents();
    client.sendRequest("pause", "{\"threadId\":1}");
    auto pauseResp = client.expectResponse("pause");
    REQUIRE(pauseResp);
    CHECK(pauseResp->optValue<bool>("success", false));
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));
    // The IDE must not wait on a script line (native code may run a while), so
    // pause emits a stopped event with no source immediately.
    auto pauseStopped = client.expectEvent("stopped", 1000);
    REQUIRE(pauseStopped);
    CHECK_EQ(pauseStopped->getObject("body")->optValue<std::string>("reason", ""), std::string("pause"));
    {
        SourceLoc hit;
        hit.source   = "main.nut";
        hit.line     = 11;
        hit.function = "eve_update";
        CHECK(dbg.onScriptLine(hit));
        CHECK(dbg.isPaused());
        dap.notifyStopped(PauseReason::Step, hit);
        REQUIRE(client.expectEvent("stopped", 1000));
    }

    // ---- next / stepIn / stepOut (F10 / F11 / Shift+F11) ----
    client.clearEvents();
    client.sendRequest("next", "{\"threadId\":1}");
    auto nextResp = client.expectResponse("next");
    REQUIRE(nextResp);
    CHECK(nextResp->optValue<bool>("success", false));
    {
        auto body = nextResp->getObject("body");
        REQUIRE(body);
        CHECK_EQ(body->optValue<int>("threadId", 0), 1);
    }
    // No VM stack → stepOver opens as StepInto.
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));
    REQUIRE(client.findEvent("continued"));

    dbg.pause(PauseReason::PauseKey);
    client.clearEvents();
    client.sendRequest("stepIn", "{\"threadId\":1}");
    auto stepInResp = client.expectResponse("stepIn");
    REQUIRE(stepInResp);
    CHECK(stepInResp->optValue<bool>("success", false));
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));

    dbg.pause(PauseReason::PauseKey);
    client.clearEvents();
    client.sendRequest("stepOut", "{\"threadId\":1}");
    auto stepOutResp = client.expectResponse("stepOut");
    REQUIRE(stepOutResp);
    CHECK(stepOutResp->optValue<bool>("success", false));
    // No caller → stepOut falls back to stepOver, depth 0 ⇒ StepInto.
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));

    dbg.pause(PauseReason::PauseKey);
    client.clearEvents();
    client.sendRequest("stepFrame", "{\"threadId\":1}");
    auto stepFrameResp = client.expectResponse("stepFrame");
    REQUIRE(stepFrameResp);
    CHECK(stepFrameResp->optValue<bool>("success", false));
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepFrame));
    dbg.notifyFrameDone();
    CHECK(dbg.isPaused());

    // ---- evaluate / watches ----
    ssq::VM vm(1024, ssq::Libs::ALL);
    dbg.attach(vm.getHandle());
    {
        HSQUIRRELVM     v   = vm.getHandle();
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "score", -1);
        sq_pushinteger(v, 42);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    client.sendRequest("evaluate", "{\"expression\":\"score\",\"context\":\"watch\"}");
    auto evalResp = client.expectResponse("evaluate");
    REQUIRE(evalResp);
    CHECK(evalResp->optValue<bool>("success", false));
    {
        auto body = evalResp->getObject("body");
        REQUIRE(body);
        const std::string result = body->optValue<std::string>("result", "");
        CHECK(result.find("42") != std::string::npos);
    }

    // ---- disconnect ----
    client.clearEvents();
    client.sendRequest("disconnect", "{\"terminateDebuggee\":true}");
    auto discResp = client.expectResponse("disconnect");
    REQUIRE(discResp);
    CHECK(discResp->optValue<bool>("success", false));
    REQUIRE(client.expectEvent("terminated", 1000));
    CHECK(!dbg.isPaused());

    dbg.detach();
    dap.stop();
}

TEST_CASE("devtools.dap.stopOnEntry") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    dbg.detach();
    dbg.clearBreakpoints();
    dap.stop();

    const int port = dap.listen(0);
    REQUIRE(port > 0);

    DapClient client(port);
    pump(20);

    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));

    client.clearEvents();
    client.sendRequest("launch", "{\"program\":\".\",\"stopOnEntry\":true}");
    REQUIRE(client.expectResponse("launch"));

    client.clearEvents();
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));
    // stopOnEntry arms stepInto; stopped is emitted when the next line runs.
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));
    CHECK(!client.findEvent("stopped"));

    dbg.resume();
    dap.stop();
}

TEST_CASE("devtools.dap.scriptBreakpointHitViaHook") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto&   dt  = DevTool::instance();
    auto&   dap = DebugAdapter::instance();
    auto&   dbg = Debugger::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();

    dt.attach(vm, /*sampleLocals=*/false);
    const int port = dap.listen(0);
    REQUIRE(port > 0);

    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("launch", "{\"program\":\".\"}");
    REQUIRE(client.expectResponse("launch"));
    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"hit.nut\",\"name\":\"hit.nut\"},"
                       "\"breakpoints\":[{\"line\":3}]}");
    REQUIRE(client.expectResponse("setBreakpoints"));
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    // Compile a tiny script with debug info and run until BP.
    // Line numbers in compiled buffer: function body lines map via squirrel.
    // Drive the line hook directly (avoids Squirrel line-number ambiguity across versions).
    client.clearEvents();
    SourceLoc hit;
    hit.source   = "hit.nut";
    hit.line     = 3;
    hit.function = "target";
    CHECK(dbg.onScriptLine(hit));
    CHECK(dbg.isPaused());
    // notifyStopped is normally done by DevTool::handleDebugEvent; emit it here.
    dap.notifyStopped(PauseReason::Breakpoint, hit);
    REQUIRE(client.expectEvent("stopped", 1000));

    // F5 continue from VS Code.
    client.sendRequest("continue", "{\"threadId\":1}");
    auto cont = client.expectResponse("continue", 2000);
    REQUIRE(cont);
    CHECK(cont->optValue<bool>("success", false));
    CHECK(!dbg.isPaused());

    dt.detach();
    dap.stop();
}

TEST_CASE("devtools.dap.realScriptBreakpointAndCallStack") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    auto& dt  = DevTool::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();

    const char* src =
        "function inner() {\n"   // line 1
        "    local a = 1\n"      // line 2
        "    local b = a + 1\n"  // line 3 <-- breakpoint
        "}\n"
        "function outer() {\n"
        "    inner()\n"
        "}\n"
        "outer()\n";

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};

    // Run the script on its own thread. DevTool must be attached HERE because
    // g_active is thread_local and the Squirrel debug hook fires on this thread.
    // DevTool::attach() internally calls detach()->stopDap(), so the DAP server
    // must NOT be listening yet — the main thread waits for `attached` first.
    std::thread scriptThread([&] {
        ssq::VM     vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const SQInteger len = static_cast<SQInteger>(std::strlen(src));
        SQRESULT        rc  = sq_compilebuffer(v, src, len, _SC("e2e.nut"), SQTrue);
        REQUIRE(SQ_SUCCEEDED(rc));
        sq_pushroottable(v);
        REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
        sq_poptop(v);
        dt.detach();
        scriptDone = true;
    });

    // Wait until DevTool is attached (and DAP stopped), then start DAP + handshake.
    while (!attached.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const int port = dap.listen(0);
    REQUIRE(port > 0);

    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("launch", "{\"program\":\".\"}");
    REQUIRE(client.expectResponse("launch"));
    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"e2e.nut\",\"name\":\"e2e.nut\"},"
                       "\"breakpoints\":[{\"line\":3}]}");
    REQUIRE(client.expectResponse("setBreakpoints"));
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    // Release the script now that breakpoints are installed.
    client.clearEvents();
    go = true;

    // The breakpoint hit produces a `stopped` event with the real location.
    auto stopped = client.expectEvent("stopped", 8000);
    REQUIRE(stopped);
    {
        auto body = stopped->getObject("body");
        REQUIRE(body);
        CHECK_EQ(body->optValue<std::string>("reason", ""), std::string("breakpoint"));
        CHECK_EQ(body->optValue<int>("line", 0), 3);
    }

    // The call stack must include the CURRENT (innermost) frame `inner` first.
    client.sendRequest("stackTrace", "{\"threadId\":1,\"startFrame\":0,\"levels\":20}");
    auto stResp = client.expectResponse("stackTrace");
    REQUIRE(stResp);
    {
        auto body = stResp->getObject("body");
        REQUIRE(body);
        auto frames = body->getArray("stackFrames");
        REQUIRE(frames);
        REQUIRE(frames->size() >= 3u);
        auto f0 = frames->getObject(0);
        REQUIRE(f0);
        CHECK_EQ(f0->optValue<int>("line", 0), 3);
        CHECK(f0->optValue<std::string>("name", "").find("inner") != std::string::npos);
        auto f1 = frames->getObject(1);
        REQUIRE(f1);
        CHECK(f1->optValue<std::string>("name", "").find("outer") != std::string::npos);
        auto f2 = frames->getObject(2);
        REQUIRE(f2);
        CHECK(f2->optValue<std::string>("name", "").find("main") != std::string::npos);
    }

    // F5 continue from VS Code → script finishes.
    client.sendRequest("continue", "{\"threadId\":1}");
    REQUIRE(client.expectResponse("continue"));

    // Poll-based join: if a previous CHECK/REQUIRE failed the script may never
    // finish, so never block on an unconditional join (would hang the suite).
    for (int i = 0; i < 200 && !scriptDone.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dap.poll();
    }
    if (scriptThread.joinable()) {
        if (scriptDone.load()) {
            scriptThread.join();
        } else {
            scriptThread.detach();  // test already failed; avoid std::terminate
        }
    }
    const bool done = scriptDone.load();
    CHECK(done);
    CHECK(!dbg.isPaused());

    dap.stop();
}

TEST_CASE("devtools.dap.exceptionBreakpoints") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    dbg.detach();
    dbg.clearBreakpoints();
    dbg.setBreakOnError(false);
    dap.stop();

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.clearEvents();

    // Empty filters -> break on error off.
    client.sendRequest("setExceptionBreakpoints", "{\"filters\":[]}");
    auto offResp = client.expectResponse("setExceptionBreakpoints");
    REQUIRE(offResp);
    CHECK(offResp->optValue<bool>("success", false));
    CHECK(!dbg.breakOnError());

    client.sendRequest("setExceptionBreakpoints", "{\"filters\":[\"uncaught\",\"script_error\"]}");
    auto onResp = client.expectResponse("setExceptionBreakpoints");
    REQUIRE(onResp);
    CHECK(onResp->optValue<bool>("success", false));
    CHECK(dbg.breakOnError());

    // exceptionInfo reflects the last exception + break mode.
    SourceLoc loc;
    loc.source = "boom.nut";
    loc.line   = 9;
    dap.notifyStopped(PauseReason::Exception, loc, "index error on boom");
    client.sendRequest("exceptionInfo", "{\"threadId\":1}");
    auto infoResp = client.expectResponse("exceptionInfo");
    REQUIRE(infoResp);
    auto body = infoResp->getObject("body");
    REQUIRE(body);
    CHECK_EQ(body->optValue<std::string>("exceptionId", ""), std::string("script_error"));
    CHECK(body->optValue<std::string>("description", "").find("index error") != std::string::npos);
    CHECK_EQ(body->optValue<std::string>("breakMode", ""), std::string("always"));

    dbg.setBreakOnError(false);
    dap.stop();
}

TEST_CASE("devtools.dap.uncaughtErrorPausesAtThrowSite") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    auto& dt  = DevTool::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();
    dbg.setBreakOnError(true);

    // boom.nut line 3 is the throw site; the error hook must report exactly
    // that line (not the sq_call / report site).
    const char* src =
        "function boom() {\n"     // line 1
        "    local x = 1\n"       // line 2
        "    throw \"kaboom\"\n"  // line 3
        "}\n"                     // line 4
        "boom()\n";               // line 5

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};
    std::atomic<bool> callFailed{false};

    std::thread scriptThread([&] {
        ssq::VM     vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(
            SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("boom.nut"), SQTrue)));
        sq_pushroottable(v);
        // Uncaught: the error hook fires at the throw site and blocks
        // (break-on-error), so sq_call returns SQ_ERROR only after resume.
        callFailed = SQ_FAILED(sq_call(v, 1, SQFalse, SQTrue));
        sq_poptop(v);
        dt.detach();
        scriptDone = true;
    });

    while (!attached.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("launch", "{\"program\":\".\"}");
    REQUIRE(client.expectResponse("launch"));
    client.sendRequest("setExceptionBreakpoints", "{\"filters\":[\"script_error\"]}");
    REQUIRE(client.expectResponse("setExceptionBreakpoints"));
    CHECK(dbg.breakOnError());
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    client.clearEvents();
    go           = true;
    auto stopped = client.expectEvent("stopped", 8000);
    REQUIRE(stopped);
    auto body = stopped->getObject("body");
    REQUIRE(body);
    CHECK_EQ(body->optValue<std::string>("reason", ""), std::string("exception"));
    CHECK(body->optValue<std::string>("description", "").find("kaboom") != std::string::npos);
    // The pause must be at the THROW line, not the catch / report site.
    auto srcObj = body->getObject("source");
    REQUIRE(srcObj);
    CHECK(srcObj->optValue<std::string>("name", "").find("boom.nut") != std::string::npos);
    CHECK_EQ(body->optValue<int>("line", -1), 3);

    // Call stack: the native error hook frame is skipped, so frame 0 is the
    // throwing script function with its locals intact.
    client.sendRequest("stackTrace", "{\"threadId\":1,\"startFrame\":0,\"levels\":8}");
    auto stackResp = client.expectResponse("stackTrace");
    REQUIRE(stackResp);
    auto frames = stackResp->getObject("body")->getArray("stackFrames");
    REQUIRE(frames);
    REQUIRE(frames->size() >= 2u);
    auto f0 = frames->getObject(0);
    REQUIRE(f0);
    CHECK(f0->optValue<std::string>("name", "").find("boom") != std::string::npos);
    CHECK_EQ(f0->optValue<int>("id", -1), 1);  // stack level 1 (hook skipped)
    CHECK_EQ(f0->optValue<int>("line", -1), 3);
    auto f1 = frames->getObject(1);
    REQUIRE(f1);
    CHECK(f1->optValue<std::string>("name", "").find("main") != std::string::npos);
    CHECK_EQ(f1->optValue<int>("line", -1), 5);

    // Frame 0 (boom) locals: x == 1.
    client.sendRequest("scopes", "{\"frameId\":1}");
    auto scResp = client.expectResponse("scopes");
    REQUIRE(scResp);
    auto scopes = scResp->getObject("body")->getArray("scopes");
    REQUIRE(scopes);
    int localsRef = -1;
    for (size_t i = 0; i < scopes->size(); ++i) {
        auto s = scopes->getObject(static_cast<unsigned int>(i));
        if (s->optValue<std::string>("name", "") == "Locals") localsRef = s->optValue<int>("variablesReference", -1);
    }
    REQUIRE(localsRef > 0);
    client.sendRequest("variables", "{\"variablesReference\":" + std::to_string(localsRef) + "}");
    auto varsResp = client.expectResponse("variables");
    REQUIRE(varsResp);
    auto vars = varsResp->getObject("body")->getArray("variables");
    REQUIRE(vars);
    bool foundX = false;
    for (size_t i = 0; i < vars->size(); ++i) {
        auto vv = vars->getObject(static_cast<unsigned int>(i));
        if (vv->optValue<std::string>("name", "") == "x" &&
            vv->optValue<std::string>("value", "").find("1") != std::string::npos)
            foundX = true;
    }
    CHECK(foundX);

    client.sendRequest("continue", "{\"threadId\":1}");
    REQUIRE(client.expectResponse("continue"));

    for (int i = 0; i < 200 && !scriptDone.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dap.poll();
    }
    if (scriptThread.joinable()) {
        if (scriptDone.load())
            scriptThread.join();
        else
            scriptThread.detach();
    }
    CHECK(scriptDone.load());
    CHECK(callFailed.load());  // the error did propagate after resume
    CHECK(!dbg.isPaused());
    dbg.setBreakOnError(false);
    dap.stop();
}

TEST_CASE("devtools.dap.caughtErrorPausesAtReportSite") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    auto& dt  = DevTool::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();
    dbg.setBreakOnError(true);

    // Mirrors the load.nut pattern: the catch block reports the error by
    // calling the native `eve.dev.reportError` directly. The pause lands on
    // that catch statement (the deepest script frame), not on engine internals.
    const char* src =
        "function boom() {\n"                                          // line 1
        "    throw \"kaboom\"\n"                                       // line 2
        "}\n"                                                          // line 3
        "function caller() {\n"                                        // line 4
        "    try {\n"                                                  // line 5
        "        boom()\n"                                             // line 6
        "    } catch (e) {\n"                                          // line 7
        "        if (\"dev\" in eve) eve.dev.reportError(\"\" + e)\n"  // line 8
        "    }\n"                                                      // line 9
        "}\n"                                                          // line 10
        "caller()\n";                                                  // line 11

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};

    std::thread scriptThread([&] {
        ssq::VM     vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        // Provide the `eve` root table the dev script API binds into.
        const char* pre = "eve <- {};\n";
        REQUIRE(
            SQ_SUCCEEDED(sq_compilebuffer(v, pre, static_cast<SQInteger>(std::strlen(pre)), _SC("pre.nut"), SQTrue)));
        sq_pushroottable(v);
        REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
        sq_poptop(v);
        dt.exposeScriptApi(vm);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(SQ_SUCCEEDED(
            sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("caught.nut"), SQTrue)));
        sq_pushroottable(v);
        REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
        sq_poptop(v);
        dt.detach();
        scriptDone = true;
    });

    while (!attached.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("launch", "{\"program\":\".\"}");
    REQUIRE(client.expectResponse("launch"));
    client.sendRequest("setExceptionBreakpoints", "{\"filters\":[\"script_error\"]}");
    REQUIRE(client.expectResponse("setExceptionBreakpoints"));
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    client.clearEvents();
    go           = true;
    auto stopped = client.expectEvent("stopped", 8000);
    REQUIRE(stopped);
    auto body = stopped->getObject("body");
    REQUIRE(body);
    // The VM may report the caught throw before the explicit reportError call.
    // Resume that intermediate stop and validate the report site that follows.
    if (body->optValue<int>("line", -1) != 8) {
        client.sendRequest("continue", "{\"threadId\":1}");
        REQUIRE(client.expectResponse("continue"));
        stopped = client.expectEvent("stopped", 8000);
        REQUIRE(stopped);
        body = stopped->getObject("body");
        REQUIRE(body);
    }
    CHECK_EQ(body->optValue<std::string>("reason", ""), std::string("exception"));
    auto srcObj = body->getObject("source");
    REQUIRE(srcObj);
    CHECK(srcObj->optValue<std::string>("name", "").find("caught.nut") != std::string::npos);
    // The deepest script frame is the game's catch statement calling the
    // native reporter (line 8), not load.nut internals.
    CHECK_EQ(body->optValue<int>("line", -1), 8);

    client.sendRequest("continue", "{\"threadId\":1}");
    REQUIRE(client.expectResponse("continue"));

    for (int i = 0; i < 200 && !scriptDone.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dap.poll();
    }
    if (!scriptDone.load()) {
        // Never detach a thread that captures this test's stack. Release a
        // possible late debugger stop and wait for deterministic teardown.
        dbg.setBreakOnError(false);
        if (dbg.isPaused()) dbg.resume();
        for (int i = 0; i < 200 && !scriptDone.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            dap.poll();
        }
    }
    if (scriptThread.joinable()) scriptThread.join();
    CHECK(scriptDone.load());
    CHECK(!dbg.isPaused());
    dbg.setBreakOnError(false);
    dap.stop();
}

TEST_CASE("devtools.dap.breakpointVerificationEvent") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    dbg.detach();
    dbg.clearBreakpoints();
    dbg.setBreakpointsEnabled(true);
    dap.stop();

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.clearEvents();

    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"verify.nut\",\"name\":\"verify.nut\"},"
                       "\"breakpoints\":[{\"line\":5}]}");
    auto bpResp = client.expectResponse("setBreakpoints");
    REQUIRE(bpResp);
    auto arr = bpResp->getObject("body")->getArray("breakpoints");
    REQUIRE(arr);
    REQUIRE(arr->size() == 1u);
    auto bp0 = arr->getObject(0);
    // Unverified until the line is observed by the line hook.
    CHECK(!bp0->optValue<bool>("verified", true));
    const int id = bp0->optValue<int>("id", 0);
    REQUIRE(id > 0);

    // Disable the master switch so the observation does not pause; the
    // verification event must still fire.
    dbg.setBreakpointsEnabled(false);
    SourceLoc loc;
    loc.source = "verify.nut";
    loc.line   = 5;
    CHECK(!dbg.onScriptLine(loc));
    REQUIRE(client.expectEvent("breakpoint", 1000));
    auto evBp = client.findEvent("breakpoint")->getObject("body")->getObject("breakpoint");
    REQUIRE(evBp);
    CHECK(evBp->optValue<bool>("verified", false));
    CHECK_EQ(evBp->optValue<int>("id", 0), id);

    dbg.setBreakpointsEnabled(true);
    dap.stop();
}

TEST_CASE("devtools.dap.setBreakpointsStoresCondition") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    dbg.detach();
    dbg.clearBreakpoints();
    dap.stop();

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));

    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"cond.nut\",\"name\":\"cond.nut\"},"
                       "\"breakpoints\":[{\"line\":4,\"condition\":\"score > 10\"}]}");
    auto bpResp = client.expectResponse("setBreakpoints");
    REQUIRE(bpResp);
    CHECK(bpResp->optValue<bool>("success", false));

    auto bps = dbg.breakpoints();
    REQUIRE(bps.size() == 1u);
    CHECK_EQ(bps[0].line, 4);
    CHECK_EQ(bps[0].condition, std::string("score > 10"));

    dbg.clearBreakpoints();
    dap.stop();
}

TEST_CASE("devtools.dap.frameScopesAndVariableTree") {
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();
    auto& dt  = DevTool::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();

    const char* src =
        "function inner() {\n"         // line 1
        "    local a = 1\n"            // line 2
        "    local tab = { x = 5 }\n"  // line 3
        "    local arr = [1, 2, 3]\n"  // line 4
        "    local b = a + 1\n"        // line 5 <-- breakpoint
        "}\n"                          // line 6
        "function outer() {\n"         // line 7
        "    local o1 = 99\n"          // line 8
        "    inner()\n"                // line 9
        "}\n"                          // line 10
        "outer()\n";                   // line 11

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};

    std::thread scriptThread([&] {
        ssq::VM     vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(SQ_SUCCEEDED(
            sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("scopes.nut"), SQTrue)));
        sq_pushroottable(v);
        REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
        sq_poptop(v);
        dt.detach();
        scriptDone = true;
    });

    while (!attached.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    pump(20);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("launch", "{\"program\":\".\"}");
    REQUIRE(client.expectResponse("launch"));
    client.sendRequest("setBreakpoints",
                       "{\"source\":{\"path\":\"scopes.nut\",\"name\":\"scopes.nut\"},"
                       "\"breakpoints\":[{\"line\":5}]}");
    REQUIRE(client.expectResponse("setBreakpoints"));
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    client.clearEvents();
    go = true;
    REQUIRE(client.expectEvent("stopped", 8000));

    // Frame 1 = outer: has o1; frame 0 = inner: has tab/arr.
    client.sendRequest("scopes", "{\"frameId\":1}");
    auto scResp = client.expectResponse("scopes");
    REQUIRE(scResp);
    auto scopes = scResp->getObject("body")->getArray("scopes");
    REQUIRE(scopes);
    int localsRef = -1;
    for (size_t i = 0; i < scopes->size(); ++i) {
        auto s = scopes->getObject(static_cast<unsigned int>(i));
        if (s->optValue<std::string>("name", "") == "Locals") localsRef = s->optValue<int>("variablesReference", -1);
    }
    REQUIRE(localsRef > 1);  // differs from frame-0 locals ref (1)

    client.sendRequest("variables", "{\"variablesReference\":" + std::to_string(localsRef) + "}");
    auto varsResp = client.expectResponse("variables");
    REQUIRE(varsResp);
    auto vars = varsResp->getObject("body")->getArray("variables");
    REQUIRE(vars);
    bool foundO1 = false;
    for (size_t i = 0; i < vars->size(); ++i) {
        auto              vv   = vars->getObject(static_cast<unsigned int>(i));
        const std::string name = vv->optValue<std::string>("name", "");
        if (name == "o1" && vv->optValue<std::string>("value", "").find("99") != std::string::npos) foundO1 = true;
        // Leaf variables must NOT carry the inspect marker.
        if (name == "o1") CHECK(!vv->has("__vscodeVariableMenuContext"));
    }
    CHECK(foundO1);

    // Frame 0 (inner) locals: tab and arr are expandable containers.
    client.sendRequest("variables", "{\"variablesReference\":1}");
    auto innerResp = client.expectResponse("variables");
    REQUIRE(innerResp);
    auto innerVars = innerResp->getObject("body")->getArray("variables");
    REQUIRE(innerVars);
    int tabRef = -1;
    int arrRef = -1;
    for (size_t i = 0; i < innerVars->size(); ++i) {
        auto              vv   = innerVars->getObject(static_cast<unsigned int>(i));
        const std::string name = vv->optValue<std::string>("name", "");
        if (name == "tab") tabRef = vv->optValue<int>("variablesReference", -1);
        if (name == "arr") arrRef = vv->optValue<int>("variablesReference", -1);
        // Expandable containers expose the marker so the VS Code VARIABLES
        // context menu can offer "查看实例" only for objects.
        if (name == "tab" || name == "arr") {
            CHECK_EQ(vv->optValue<std::string>("__vscodeVariableMenuContext", ""), std::string("object"));
        }
    }
    REQUIRE(tabRef > 0);
    REQUIRE(arrRef > 0);

    // Expand tab -> x = 5.
    client.sendRequest("variables", "{\"variablesReference\":" + std::to_string(tabRef) + "}");
    auto tabResp = client.expectResponse("variables");
    REQUIRE(tabResp);
    auto tabVars = tabResp->getObject("body")->getArray("variables");
    REQUIRE(tabVars);
    bool foundX = false;
    for (size_t i = 0; i < tabVars->size(); ++i) {
        auto vv = tabVars->getObject(static_cast<unsigned int>(i));
        if (vv->optValue<std::string>("name", "") == "x" &&
            vv->optValue<std::string>("value", "").find("5") != std::string::npos)
            foundX = true;
        // Children of a container are leaves here: no inspect marker.
        CHECK(!vv->has("__vscodeVariableMenuContext"));
    }
    CHECK(foundX);

    // Expand arr -> three indexed children.
    client.sendRequest("variables", "{\"variablesReference\":" + std::to_string(arrRef) + "}");
    auto arrResp = client.expectResponse("variables");
    REQUIRE(arrResp);
    auto arrVars = arrResp->getObject("body")->getArray("variables");
    REQUIRE(arrVars);
    REQUIRE(arrVars->size() == 3u);
    CHECK(arrVars->getObject(2)->optValue<std::string>("value", "").find("3") != std::string::npos);

    client.sendRequest("continue", "{\"threadId\":1}");
    REQUIRE(client.expectResponse("continue"));

    for (int i = 0; i < 200 && !scriptDone.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dap.poll();
    }
    if (scriptThread.joinable()) {
        if (scriptDone.load())
            scriptThread.join();
        else
            scriptThread.detach();
    }
    CHECK(scriptDone.load());
    CHECK(!dbg.isPaused());
    dap.stop();
}

TEST_CASE("devtools.dap.runtimeExecuteReportsThroughDevToolOnce") {
    auto& dt = DevTool::instance();
    dt.detach();
    Debugger::instance().setBreakOnError(false);

    // DevTool replaces the Runtime error hook; the failing script must still
    // surface as an enriched ScriptException (no ssq null-runtimeException
    // crash) and must be flagged as already reported so Run() does not
    // slice/report the same error twice.
    eve::Runtime runtime(1024, ssq::Libs::ALL);
    dt.attach(runtime.vm(), /*sampleLocals=*/false);

    bool caught = false;
    try {
        runtime.runSource("function inner() { throw \"kaboom\" }\ninner();\n", "dt-boom.nut");
    } catch (const eve::ScriptException& error) {
        caught = true;
        CHECK(error.reported());
        CHECK(error.hasLocation());
        CHECK(error.line() > 0);
        const std::string message = error.what();
        CHECK(message.find("dt-boom.nut") != std::string::npos);
        CHECK(message.find("kaboom") != std::string::npos);
        CHECK(message.find("Stack:") != std::string::npos);
        CHECK(!error.stackTrace().empty());
        CHECK(error.stackTrace().find("inner") != std::string::npos);
    }
    CHECK(caught);
    CHECK(!dt.lastReport().empty());
    dt.detach();
}

TEST_CASE("devtools.dap.errorSliceReturnsLastReportAndLocations") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto&   dt  = DevTool::instance();
    auto&   dap = DebugAdapter::instance();
    auto&   dbg = Debugger::instance();

    dap.stop();
    dt.detach();
    dbg.clearBreakpoints();
    dbg.setBreakOnError(false);
    dt.attach(vm, /*sampleLocals=*/false);

    SourceLoc site;
    site.source   = "slice.nut";
    site.line     = 7;
    site.function = "boom";
    dt.graph().onLine(site);
    dt.graph().onDef(site, "score");
    dt.notifyError("slice boom", {"score"});
    CHECK(!dt.lastReport().empty());
    CHECK(!dt.lastSlice().locations.empty());

    const int port = dap.listen(0);
    REQUIRE(port > 0);
    DapClient client(port);
    client.sendRequest("initialize", "{\"adapterID\":\"eve\"}");
    REQUIRE(client.expectResponse("initialize"));
    client.sendRequest("errorSlice");
    auto resp = client.expectResponse("errorSlice");
    REQUIRE(resp);
    CHECK(resp->optValue<bool>("success", false));
    auto body = resp->getObject("body");
    REQUIRE(body);
    CHECK(body->optValue<std::string>("report", "").find("slice boom") != std::string::npos);
    auto locations = body->getArray("locations");
    REQUIRE(locations);
    CHECK(locations->size() >= 1);
    bool foundSite = false;
    for (unsigned i = 0; i < locations->size(); ++i) {
        auto item = locations->getObject(i);
        if (!item) continue;
        if (item->optValue<std::string>("name", "").find("slice.nut") != std::string::npos &&
            item->optValue<int>("line", 0) == 7)
            foundSite = true;
    }
    CHECK(foundSite);

    dt.detach();
    dap.stop();
}
}  // namespace test_dap
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_debugger)
#include <squirrel.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "common/StateValue.h"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/Snapshot.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// debugger.cpp
namespace test_debugger {
using namespace eve::dev;


namespace {

struct HookState {
    Debugger*   dbg      = nullptr;
    int         stops    = 0;
    int         lastLine = -1;
    std::string lastFunc;
};

[[maybe_unused]] void testDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                                    const SQChar* funcname) {
    auto* st = static_cast<HookState*>(sq_getforeignptr(v));
    if (!st || !st->dbg) return;
    if (type != 'l') return;
    SourceLoc loc;
    loc.source   = sourcename ? sourcename : "";
    loc.line     = static_cast<int>(line);
    loc.function = funcname ? funcname : "";
    if (st->dbg->onScriptLine(loc)) {
        ++st->stops;
        st->lastLine = loc.line;
        st->lastFunc = loc.function;
        // Drive step from the hook without waitWhilePaused.
        // First stop: stepOver; second stop ends the test script.
        if (st->stops == 1) {
            st->dbg->stepOver();
        } else {
            st->dbg->resume();
        }
    }
}

}  // namespace

TEST_CASE("devtools.debugger.stepOverSkipsCalleeLines") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    // Lines roughly:
    // 1 function callee() { local x = 1 }
    // 2 function caller() { callee(); local y = 2 }
    // 3 caller()
    const char* src =
        "function callee() {\n"
        "    local x = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local y = 2\n"
        "}\n"
        "caller()\n";

    HookState st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, testDebugHook);

    d.setBreakpoint("buffer", 5);  // callee() call site inside caller
    // Normalize: compile as named buffer
    SQRESULT rc = sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"), SQTrue);
    REQUIRE(SQ_SUCCEEDED(rc));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    // Hit bp on call line, stepOver, then stop on `local y = 2` (not inside callee).
    CHECK(st.stops >= 2);
    CHECK_EQ(st.lastLine, 6);
    CHECK(st.lastFunc.find("caller") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
}

TEST_CASE("devtools.debugger.stepIntoEntersCallee") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    const char* src =
        "function callee() {\n"
        "    local x = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local y = 2\n"
        "}\n"
        "caller()\n";

    struct IntoState {
        Debugger*   dbg      = nullptr;
        int         stops    = 0;
        int         lastLine = -1;
        std::string lastFunc;
    } st;
    st.dbg = &d;

    auto hook = [](HSQUIRRELVM vmh, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
        auto* s = static_cast<IntoState*>(sq_getforeignptr(vmh));
        if (!s || !s->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (s->dbg->onScriptLine(loc)) {
            ++s->stops;
            s->lastLine = loc.line;
            s->lastFunc = loc.function;
            if (s->stops == 1)
                s->dbg->stepInto();
            else
                s->dbg->resume();
        }
    };

    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);
    d.setBreakpoint("buffer", 5);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK(st.stops >= 2);
    CHECK_EQ(st.lastLine, 2);  // first line body of callee
    CHECK(st.lastFunc.find("callee") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
}

TEST_CASE("devtools.debugger.watchesAndEvaluate") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    // Seed roottable slots.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "score", -1);
        sq_pushinteger(v, 7);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, "player", -1);
        sq_newtable(v);
        sq_pushstring(v, "hp", -1);
        sq_pushinteger(v, 100);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    Debugger& d = Debugger::instance();
    d.attach(v);
    d.clearWatches();
    d.addWatch("score");
    d.addWatch("player.hp");
    d.refreshWatches();
    auto watches = d.watches();
    REQUIRE(watches.size() == 2u);
    CHECK(watches[0].ok);
    CHECK(watches[0].value.find("7") != std::string::npos);
    CHECK(watches[1].ok);
    CHECK(watches[1].value.find("100") != std::string::npos);

    auto ev = d.evaluate("score");
    CHECK_EQ(ev.type, std::string("integer"));
    CHECK(ev.value.find("7") != std::string::npos);

    CHECK(d.removeWatch("score"));
    d.clearWatches();
    CHECK(d.watches().empty());
    d.detach();
}

TEST_CASE("devtools.snapshot.roundTripRoots") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, "name", -1);
        sq_pushstring(v, "hero", -1);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    // Prefer conventional gameState without markRoot.
    std::string       err;
    const std::string json = snap.capture(v, &err);
    CHECK(err.empty());
    CHECK(!json.empty());
    CHECK(json.find("gameState") != std::string::npos);
    CHECK(json.find("hero") != std::string::npos);

    // Mutate then restore.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 99);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    err.clear();
    CHECK(snap.restore(v, json, &err));
    CHECK(err.empty());

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        sq_pushstring(v, "level", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        SQInteger level = 0;
        sq_getinteger(v, -1, &level);
        CHECK_EQ(static_cast<int>(level), 3);
        sq_settop(v, top);
    }

    const char* path = "eve_test_snapshot.json";
    CHECK(snap.saveFile(v, path, &err));
    CHECK(snap.loadFile(v, path, &err));
    std::remove(path);
}


namespace {

/** Fake provider: reports a fixed value and remembers what it restored. */
class MockStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "mock"; }

    eve::Result<eve::Value> capture() override {
        eve::Value out(eve::Value::Object{});
        out.set("value", eve::Value::integer(7));
        return eve::Result<eve::Value>::success(std::move(out));
    }

    eve::Result<void> restore(const eve::Value& in) override {
        lastRestored = eve::toLegacyStateValue(in);
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override { return eve::Result<void>::success(); }

    eve::StateValue lastRestored;
};

}  // namespace

TEST_CASE("devtools.snapshot.v2NativeSection") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    MockStateProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    std::string       err;
    const std::string json = snap.capture(v, &err);
    CHECK(err.empty());
    CHECK(json.find("version") != std::string::npos);
    CHECK(json.find("native") != std::string::npos);
    CHECK(json.find("mock") != std::string::npos);
    CHECK(json.find("gameState") != std::string::npos);

    // Mutate script state, then restore; both the script root and the native
    // provider state come back.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 99);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    err.clear();
    CHECK(snap.restore(v, json, &err));
    CHECK(err.empty());

    CHECK(provider.lastRestored.isObject());
    const eve::StateValue* value = provider.lastRestored.find("value");
    REQUIRE(value != nullptr);
    CHECK_EQ(value->asInt(), int64_t(7));

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        sq_pushstring(v, "level", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        SQInteger level = 0;
        sq_getinteger(v, -1, &level);
        CHECK_EQ(static_cast<int>(level), 3);
        sq_settop(v, top);
    }

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

TEST_CASE("devtools.snapshot.v1Compat") {
    ssq::VM           vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM       v  = vm.getHandle();
    const std::string v1 = R"({"version":1,"roots":{"gameState":{"level":3}}})";

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    std::string err;
    CHECK(snap.restore(v, v1, &err));
    CHECK(err.empty());

    const SQInteger top = sq_gettop(v);
    sq_pushroottable(v);
    sq_pushstring(v, "gameState", -1);
    REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
    sq_pushstring(v, "level", -1);
    REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
    SQInteger level = 0;
    sq_getinteger(v, -1, &level);
    CHECK_EQ(static_cast<int>(level), 3);
    sq_settop(v, top);
}

namespace {

/** @brief The `skipped` list a capture recorded inside the state, if any. */
std::vector<std::string> skippedEntries(const eve::StateValue& state) {
    std::vector<std::string> entries;
    const eve::StateValue*   list = state.find("skipped");
    if (!list || !list->isArray()) return entries;
    for (std::size_t i = 0; i < list->arraySize(); ++i) entries.push_back(list->at(i).asString());
    return entries;
}

}  // namespace
TEST_CASE("devtools.snapshot.skipsFunctionInsideRootAndNamesIt") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s = vm.compileSource("gameState <- { level = 3 }; gameState.tick <- function() { return 1; };");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    // A closure is not state (the reloaded script recreates it), so it is left out and
    // named -- the header always said "skipped", but the capture used to fail instead.
    // This case and the next one used to contradict each other while both "passed": every
    // assertion here was CHECK, which is warning-level.
    REQUIRE(snap.captureState(v, out, &err));
    REQUIRE(err.empty());

    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    const eve::StateValue* root = roots->find("gameState");
    REQUIRE(root != nullptr);
    const auto* objects = out.find("objects");
    REQUIRE(objects != nullptr);
    const auto* state = objects->at(static_cast<size_t>(root->find("$ref")->asInt())).find("value");
    REQUIRE(state != nullptr);
    REQUIRE(state->find("level") != nullptr);
    REQUIRE(state->find("tick") == nullptr);

    const std::vector<std::string> skipped = skippedEntries(out);
    REQUIRE(skipped.size() == 1);
    REQUIRE(skipped[0].find("gameState.tick") != std::string::npos);
    REQUIRE(skipped[0].find("function") != std::string::npos);

    // The same list travels with the captured state so a tool (or the persisted
    // eve_snapshot.json) can show what was not preserved.
    const eve::StateValue* reported = out.find("skipped");
    REQUIRE(reported != nullptr);
    REQUIRE(reported->isArray());
}

TEST_CASE("devtools.snapshot.skipsInstanceInsideContainerAndNamesIt") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    // No `gameState` / `state` / `eve_state` root, so the full root scan runs -- the prefix
    // heuristic would otherwise capture that one conventional root and ignore the rest.
    ssq::Script s = vm.compileSource(
        "class Hero { level = 1; } squad <- [Hero(), Hero()]; progress <- { level = 3, leader = Hero() };");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    // The motivating case: keeping entity instances in a container is what the script ECS
    // encourages, and it used to refuse the entire capture (so: refuse the hot reload).
    REQUIRE(snap.captureState(v, out, &err));
    REQUIRE(err.empty());

    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    const eve::StateValue* root = roots->find("progress");
    REQUIRE(root != nullptr);
    const auto* objects = out.find("objects");
    REQUIRE(objects != nullptr);
    const auto* state = objects->at(static_cast<size_t>(root->find("$ref")->asInt())).find("value");
    REQUIRE(state != nullptr);
    REQUIRE(state->find("level") != nullptr);
    REQUIRE(state->find("leader") == nullptr);
    REQUIRE(roots->find("squad") != nullptr);  // the array itself is captured, its items are not

    // Both the table member and the array elements are reported, with their paths.
    const std::vector<std::string> skipped = skippedEntries(out);
    REQUIRE(skipped.size() == 3);
    const bool namedLeader        = std::any_of(skipped.begin(), skipped.end(), [](const std::string& entry) {
        return entry.find("progress.leader") != std::string::npos && entry.find("class instance") != std::string::npos;
    });
    const bool namedArrayElements = std::any_of(skipped.begin(), skipped.end(), [](const std::string& entry) {
        return entry.find("squad[0]") != std::string::npos || entry.find("squad[1]") != std::string::npos;
    });
    REQUIRE(namedLeader);
    REQUIRE(namedArrayElements);
}

TEST_CASE("devtools.snapshot.skipsMarkedInstanceRootAndNamesIt") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s = vm.compileSource("class Hero { level = 1; } hero <- Hero(); gameState <- { level = 3 };");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    snap.markRoot("hero");
    snap.markRoot("gameState");

    eve::StateValue out;
    std::string     err;
    // An explicitly marked root of an unserializable type is treated like a member: left
    // out, named, capture continues. It used to fail the whole capture.
    REQUIRE(snap.captureState(v, out, &err));
    REQUIRE(err.empty());
    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    REQUIRE(roots->find("gameState") != nullptr);
    REQUIRE(roots->find("hero") == nullptr);
    const std::vector<std::string> skipped = skippedEntries(out);
    REQUIRE(skipped.size() == 1);
    REQUIRE(skipped[0].find("hero") != std::string::npos);
    snap.clearRoots();
}

TEST_CASE("devtools.snapshot.skipsInstanceRootInAutoScan") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    // Same reason as above: no conventional state root, so every root is examined.
    ssq::Script s = vm.compileSource("class Hero { level = 1; } hero <- Hero(); level <- 3;");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    // Class instances at the root (module singletons, ECS worlds, ...) must not abort the
    // automatic state scan; plain-data roots are still captured -- and the skip is named.
    REQUIRE(snap.captureState(v, out, &err));
    REQUIRE(err.empty());
    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    REQUIRE(roots->find("level") != nullptr);
    REQUIRE(roots->find("hero") == nullptr);
    // `Hero` (the class definition) is a root too and is deliberately *not* named: every
    // script class and function is a root, so naming those would drown the list.
    const std::vector<std::string> skipped = skippedEntries(out);
    REQUIRE(skipped.size() == 1);
    REQUIRE(skipped[0].find("hero") != std::string::npos);
}

TEST_CASE("devtools.snapshot.conventionalRootCapturesOnlyItself") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s = vm.compileSource("gameState <- { level = 3 }; squad <- [1, 2];");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    REQUIRE(snap.captureState(v, out, &err));

    // Pinning the compatibility heuristic as it is: when a conventional state root exists,
    // only that root is captured and the other globals are not part of the snapshot at
    // all. This is why the two cases above must avoid the conventional names -- and it is
    // the other half of "a global can silently miss the snapshot" that 2.5 complained
    // about. Changing it (capture every data root) is a policy decision, not a bug fix.
    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    REQUIRE(roots->find("gameState") != nullptr);
    REQUIRE(roots->find("squad") == nullptr);
    const std::vector<std::string> skipped = skippedEntries(out);
    REQUIRE(skipped.empty());
}


TEST_CASE("devtools.debugger.conditionalBreakpoint") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    // Seed roottable flag used by the condition.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "flag", -1);
        sq_pushinteger(v, 1);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    d.setBreakpoint("t.nut", 10, true, "flag == 2");
    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 10;

    // flag == 1: condition false -> keep running.
    CHECK(!d.onScriptLine(loc));
    CHECK(!d.isPaused());

    // flag == 2: condition true -> stop.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "flag", -1);
        sq_pushinteger(v, 2);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Breakpoint));

    d.resume();
    d.detach();
}

TEST_CASE("devtools.debugger.expressionEvaluateAndVariableTree") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.attach(v);
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);

        sq_pushstring(v, "score", -1);
        sq_pushinteger(v, 7);
        sq_newslot(v, -3, SQFalse);

        sq_pushstring(v, "cfg", -1);
        sq_newtable(v);
        sq_pushstring(v, "max", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);

        sq_pushstring(v, "list", -1);
        sq_newarray(v, 0);
        sq_pushinteger(v, 10);
        sq_arrayappend(v, -2);
        sq_pushinteger(v, 20);
        sq_arrayappend(v, -2);
        sq_pushinteger(v, 30);
        sq_arrayappend(v, -2);
        sq_newslot(v, -3, SQFalse);

        sq_settop(v, top);
    }

    // Full expression evaluation (arithmetic + member access + calls).
    auto r1 = d.evaluate("score * 2");
    CHECK_EQ(r1.type, std::string("integer"));
    CHECK(r1.value.find("14") != std::string::npos);

    auto r2 = d.evaluate("cfg.max + 1");
    CHECK_EQ(r2.type, std::string("integer"));
    CHECK(r2.value.find("4") != std::string::npos);

    auto r3 = d.evaluate("list.len()");
    CHECK_EQ(r3.type, std::string("integer"));
    CHECK(r3.value.find("3") != std::string::npos);

    // Globals tree: containers are expandable.
    auto g        = d.globals();
    bool foundCfg = false;
    for (const auto& vv : g) {
        if (vv.name == "cfg") {
            foundCfg = vv.expandable;
        }
    }
    CHECK(foundCfg);

    auto cfgKids = d.containerChildren(VarKind::Globals, 0, {"cfg"});
    REQUIRE(cfgKids.size() >= 1u);
    bool foundMax = false;
    for (const auto& vv : cfgKids) {
        if (vv.name == "max" && vv.value.find("3") != std::string::npos) foundMax = true;
    }
    CHECK(foundMax);

    auto listKids = d.containerChildren(VarKind::Globals, 0, {"list"});
    REQUIRE(listKids.size() == 3u);
    CHECK(listKids[0].value.find("10") != std::string::npos);
    CHECK(listKids[2].value.find("30") != std::string::npos);

    d.detach();
}

TEST_CASE("devtools.debugger.evaluateLocalsInFrame") {
    struct St {
        Debugger*   dbg      = nullptr;
        bool        captured = false;
        std::string type;
        std::string value;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            auto r       = st->dbg->evaluate("x + y", 0);
            st->type     = r.type;
            st->value    = r.value;
            st->captured = true;
            st->dbg->resume();
        }
    };

    const char* src =
        "function f() {\n"       // line 1
        "    local x = 5\n"      // line 2
        "    local y = 10\n"     // line 3
        "    local z = x + y\n"  // line 4 <-- breakpoint
        "}\n"                    // line 5
        "f()\n";                 // line 6

    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 4);

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK(st.captured);
    CHECK_EQ(st.type, std::string("integer"));
    CHECK(st.value.find("15") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
}


TEST_CASE("devtools.debugger.conditionalBreakpointUsesLocals") {
    struct St {
        Debugger* dbg   = nullptr;
        int       stops = 0;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            ++st->stops;
            st->dbg->resume();
        }
    };

    const char* body =
        "function f() {\n"   // 1
        "    local n = 1\n"  // 2
        "    n = 2\n"        // 3  condition n == 2 should stop here
        "    n = 3\n"        // 4
        "}\n"
        "f()\n";

    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    Debugger&   d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 3, true, "n == 2");
    d.setBreakpoint("buffer", 2, true, "n == 2");  // n is 1 here → skip

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, body, static_cast<SQInteger>(std::strlen(body)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK_EQ(st.stops, 1);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.clearBreakpoints();
    d.detach();
}

TEST_CASE("devtools.debugger.conditionalBreakpointStopsOnEvalError") {
    struct St {
        Debugger* dbg   = nullptr;
        int       stops = 0;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            ++st->stops;
            st->dbg->resume();
        }
    };

    const char* body =
        "function f() {\n"
        "    local n = 1\n"
        "}\n"
        "f()\n";

    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    Debugger&   d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 2, true, "no_such_local + 1");

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, body, static_cast<SQInteger>(std::strlen(body)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK_EQ(st.stops, 1);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.clearBreakpoints();
    d.detach();
}

TEST_CASE("devtools.debugger.scriptApiSetBreakpointCondition") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto& dt = DevTool::instance();
    dt.detach();
    Debugger::instance().clearBreakpoints();
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);

    ssq::Script script = vm.compileSource(
        "function check() {\n"
        "    local a = eve.dev.setBreakpoint(\"api.nut\", 10)\n"
        "    local b = eve.dev.setBreakpoint(\"api.nut\", 11, \"n == 2\")\n"
        "    return a > 0 && b > 0\n"
        "}\n");
    vm.run(script);
    REQUIRE(vm.callFunc(vm.findFunc("check"), vm).toBool());

    bool sawPlain = false;
    bool sawCond  = false;
    for (const auto& bp : Debugger::instance().breakpoints()) {
        if (bp.line == 10) {
            sawPlain = true;
            CHECK(bp.condition.empty());
        }
        if (bp.line == 11) {
            sawCond = true;
            REQUIRE_EQ(bp.condition, std::string("n == 2"));
        }
    }
    CHECK(sawPlain);
    REQUIRE(sawCond);

    Debugger::instance().clearBreakpoints();
    dt.detach();
}
}  // namespace test_debugger
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_debugger_audit)
#include "PrivateTestScope.h"
#include <squirrel.h>
#include <cstring>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <vector>
#include "devtools/Debugger.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// debugger_audit.cpp
namespace test_debugger_audit {
using namespace eve::dev;

namespace {

struct AuditDriver {
    Debugger*                          dbg   = nullptr;
    int                                stops = 0;
    std::vector<int>                   stopLines;
    std::vector<std::string>           stopFuncs;
    std::vector<int>                   stopDepths;
    std::vector<std::function<void()>> actions;
    std::function<void()>              capture;  // optional: capture extra state on each stop
};

void auditHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
    auto* st = static_cast<AuditDriver*>(sq_getforeignptr(v));
    if (!st || !st->dbg) return;
    SourceLoc loc;
    loc.source   = sourcename ? sourcename : "";
    loc.line     = static_cast<int>(line);
    loc.function = funcname ? funcname : "";
    if (type == 'c') {
        st->dbg->onCall(loc);
        return;
    }
    if (type == 'r') {
        st->dbg->onReturn(loc);
        return;
    }
    if (type != 'l') return;
    if (st->dbg->onScriptLine(loc)) {
        const int idx = st->stops;
        st->stopLines.push_back(loc.line);
        st->stopFuncs.push_back(loc.function);
        st->stopDepths.push_back(st->dbg->scriptStackDepth());
        if (st->capture) st->capture();
        if (idx < static_cast<int>(st->actions.size())) {
            st->actions[idx]();
        } else {
            st->dbg->resume();
        }
        ++st->stops;
    }
}

/** Compile+run a buffer through the Debugger, driving it with `driver`. */
void runAudit(const char* src, const char* bufname, AuditDriver& driver) {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    driver.dbg = &d;
    d.attach(v);
    d.setVirtualSource(bufname, src);

    sq_setforeignptr(v, &driver);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, auditHook);

    SQRESULT rc = sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC(bufname), SQTrue);
    REQUIRE(SQ_SUCCEEDED(rc));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
    driver.dbg = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Call stack retrieval at a breakpoint (nested calls)
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.callStackAtNestedBreakpoint") {
    const char* src =
        "function inner() {\n"
        "    local a = 1\n"
        "}\n"
        "function outer() {\n"
        "    inner()\n"
        "}\n"
        "outer()\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<StackFrameInfo> captured;
    driver.capture = [&]() { captured = Debugger::instance().stackTrace(16); };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit1.nut", 2);
    runAudit(src, "audit1.nut", driver);

    REQUIRE(driver.stops >= 1);
    REQUIRE(captured.size() >= 3u);
    CHECK_EQ(captured[0].loc.line, 2);
    CHECK(captured[0].name.find("inner") != std::string::npos);
    CHECK_EQ(captured[0].loc.source, std::string("audit1.nut"));
    CHECK(captured[1].name.find("outer") != std::string::npos);
    CHECK_EQ(captured[1].loc.line, 5);
    // Top-level script frame present.
    CHECK(!captured[2].loc.source.empty());
}

TEST_CASE("devtools.audit.callStackNamesAnonymousBindings") {
    const char* src =
        "callee <- function() {\n"
        "    local a = 1\n"
        "}\n"
        "caller <- function() {\n"
        "    callee()\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<StackFrameInfo> captured;
    driver.capture = [&]() { captured = Debugger::instance().stackTrace(16); };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit_anon.nut", 2);
    runAudit(src, "audit_anon.nut", driver);

    REQUIRE(driver.stops >= 1);
    REQUIRE(captured.size() >= 2u);
    REQUIRE(captured[0].name.find("callee") != std::string::npos);
    REQUIRE(captured[1].name.find("caller") != std::string::npos);
}

TEST_CASE("devtools.audit.callStackTopStaysCurrentIfCallGraphHasExtraInnerFrame") {
    const char* src =
        "callee <- function() {\n"
        "    local a = 1\n"
        "}\n"
        "caller <- function() {\n"
        "    callee()\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<StackFrameInfo> captured;
    driver.capture = [&]() {
        SourceLoc extra;
        extra.source   = "audit_shift.nut";
        extra.line     = 4;
        extra.function = "unknown";
        Debugger::instance().onCall(extra);
        captured = Debugger::instance().stackTrace(16);
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit_shift.nut", 2);
    runAudit(src, "audit_shift.nut", driver);

    REQUIRE(driver.stops >= 1);
    REQUIRE(captured.size() >= 2u);
    REQUIRE(captured[0].name.find("callee") != std::string::npos);
    REQUIRE(captured[0].name.find("caller") == std::string::npos);
    REQUIRE(captured[1].name.find("caller") != std::string::npos);
}

// ---------------------------------------------------------------------------
// stepOut: finish current function, stop in the caller after the call
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepOutReturnsToCaller") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "    local b = 2\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local c = 3\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    // Stop 1 (callee body) -> stepOut. Stop 2 (caller after call) -> resume.
    driver.actions = {
        []() { Debugger::instance().stepOut(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit2.nut", 2);
    runAudit(src, "audit2.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[0], 2);
    CHECK(driver.stopFuncs[0].find("callee") != std::string::npos);
    // After stepOut we should be back in the caller, past the call site.
    CHECK(driver.stopFuncs[1].find("caller") != std::string::npos);
    CHECK_EQ(driver.stopLines[1], 7);
}

// ---------------------------------------------------------------------------
// stepInto: enter the callee and grow the call stack
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepIntoEntersAndGrowsStack") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local b = 2\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {
        []() { Debugger::instance().stepInto(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit3.nut", 5);  // callee() call site in caller
    runAudit(src, "audit3.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[0], 5);
    CHECK(driver.stopFuncs[1].find("callee") != std::string::npos);
    CHECK(driver.stopDepths[1] > driver.stopDepths[0]);
}

// ---------------------------------------------------------------------------
// stepOver: skip the callee, stop on the next statement in the caller
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepOverSkipsCallee") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local b = 2\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {
        []() { Debugger::instance().stepOver(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit4.nut", 5);  // callee() call site in caller
    runAudit(src, "audit4.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[1], 6);  // local b = 2 in caller, not inside callee
    CHECK(driver.stopFuncs[1].find("caller") != std::string::npos);
    CHECK_EQ(driver.stopDepths[1], driver.stopDepths[0]);
}

TEST_CASE("devtools.audit.breakpointHitsInsideStepOver") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local b = 2\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {
        []() { Debugger::instance().stepOver(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit7.nut", 5);  // call site — start stepOver from here
    d.setBreakpoint("audit7.nut", 2);  // inside callee — must still stop
    runAudit(src, "audit7.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[0], 5);
    CHECK_EQ(driver.stopLines[1], 2);
    CHECK(driver.stopFuncs[1].find("callee") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Breakpoints keep hitting on every loop iteration after continue
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.breakpointRehitsInLoop") {
    const char* src =
        "local total = 0\n"
        "for (local i = 0; i < 3; ++i) {\n"
        "    total += i\n"
        "}\n"
        "print(total)\n";

    AuditDriver driver;
    // Continue (resume) after every stop; the breakpoint must hit each iteration.
    driver.actions = {
        []() { Debugger::instance().resume(); },
        []() { Debugger::instance().resume(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit5.nut", 3);
    runAudit(src, "audit5.nut", driver);

    // i = 0,1,2 �?exactly three stops at the loop body line.
    CHECK_EQ(driver.stops, 3);
    for (int l : driver.stopLines) CHECK_EQ(l, 3);
}

// ---------------------------------------------------------------------------
// Locals of the current frame are readable at a breakpoint
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.localsAtBreakpoint") {
    const char* src =
        "function compute(x, y) {\n"
        "    local z = x + y\n"
        "    local name = \"audit\"\n"
        "    return z\n"
        "}\n"
        "compute(3, 4)\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<VariableInfo> locals;
    VariableInfo              ev;
    driver.capture = [&]() {
        locals = Debugger::instance().locals(0);
        // evaluate() must read the CURRENT frame's locals (regression: it used
        // to read the caller's frame at level 1). Called while still attached.
        ev = Debugger::instance().evaluate("z");
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    // Pause on `return z`: all four locals (x, y, z, name) are assigned by then.
    d.setBreakpoint("audit6.nut", 4);
    runAudit(src, "audit6.nut", driver);

    REQUIRE(driver.stops >= 1);
    bool sawX = false, sawY = false, sawZ = false, sawName = false;
    for (const auto& lv : locals) {
        if (lv.name == "x") {
            sawX = true;
            CHECK(lv.value.find("3") != std::string::npos);
        } else if (lv.name == "y") {
            sawY = true;
            CHECK(lv.value.find("4") != std::string::npos);
        } else if (lv.name == "z") {
            sawZ = true;
            CHECK(lv.value.find("7") != std::string::npos);
        } else if (lv.name == "name") {
            sawName = true;
            CHECK(lv.value.find("audit") != std::string::npos);
        }
    }
    CHECK(sawX);
    CHECK(sawY);
    CHECK(sawZ);
    CHECK(sawName);

    CHECK_EQ(ev.type, std::string("integer"));
    CHECK(ev.value.find("7") != std::string::npos);
}
}  // namespace test_debugger_audit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_decal_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* decal_content;
// decal_script.cpp
namespace test_decal_script {
using namespace eve;


UnitSciptTest(DecalScriptTest, decal_content);

TEST_CASE_FIXTURE(DecalScriptTest, "DecalScriptTest.basic") { REQUIRE(callScript("basic")); }
}  // namespace test_decal_script
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_definitions)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include "common/Module.h"
#include "definitions/Definitions.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// definitions.cpp
namespace test_definitions {
using namespace eve::definitions;


TEST_CASE("definitions.scriptRegisterReplaceEnumerateAndRestore") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Definitions();
        local registry = module.newRegistry();
        local inserted = registry.insert("general", "g-1", 1, "{\"rank\":2}");
        local replaced = registry.replace("general", "g-1", 2, "{\"rank\":3}");
        if (inserted.ok && replaced.ok) {
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            local restored = copy.restoreJson(snapshot);
            local resolved = copy.resolve("general", "g-1");
            local resolvedHandle = copy.resolveHandle("general", "g-1", 2);
            if (restored.ok && copy.countType("general") == 1 && resolved.ok &&
                resolved.value.version == 2 && resolvedHandle.ok &&
                resolvedHandle.value.version == 2 && resolvedHandle.value.generation == 2 &&
                copy.eventAt(1).getName() == "definition_reloaded")
                result = copy.atType("general", 0).getJson();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("{\"rank\":3}"));
}
}  // namespace test_definitions
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_authoring)
#include "PrivateTestScope.h"
#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationImporter.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DnutParser.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_authoring.cpp
namespace test_dialogue_authoring {
// dialogue_authoring.cpp
using namespace eve::dialogue;


TEST_CASE("dialogueAuthoring.scriptComposesInspectorAndAppliesDocument") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        flow <- eve.DialogueFlow();
        doc <- flow.newDocument("quest.editor");
        added <- doc.addNode("line", "line");
        entrySet <- doc.setEntry("line");
        textSet <- doc.setField("line", "text", "Composable editor");
        nextSet <- doc.setField("line", "next", "end");
        fieldCount <- doc.getFieldCount("line");
        fieldNames <- [];
        fieldKinds <- [];
        for (local i = 0; i < fieldCount; ++i) {
            fieldNames.append(doc.getFieldName("line", i));
            fieldKinds.append(doc.getFieldKind("line", i));
        }
        valid <- doc.validate();
        applied <- flow.applyDocumentChecked(doc).ok;
        copy <- flow.getDocument("quest.editor");
        copiedText <- copy.getField("line", "text");
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("entrySet").toBool());
    CHECK(vm.find("textSet").toBool());
    CHECK(vm.find("nextSet").toBool());
    CHECK(vm.find("fieldCount").toInt() >= 3);
    CHECK(vm.find("valid").toBool());
    CHECK(vm.find("applied").toBool());
    CHECK_EQ(vm.find("copiedText").toString(), std::string("Composable editor"));
}

// dialogue_compiler.cpp
using namespace eve::dialogue;


// dialogue_conversation.cpp
using namespace eve;
using namespace eve::dialogue;
using namespace eve::dnut;

namespace {

[[maybe_unused]] SequenceNode node(std::string id, std::string type, std::string next = {}) {
    SequenceNode value;
    value.id   = std::move(id);
    value.type = std::move(type);
    value.next = std::move(next);
    return value;
}

[[maybe_unused]] SequenceRoute route(std::string label, std::string target, eve::Value condition = {}) {
    SequenceRoute value;
    value.label     = std::move(label);
    value.target    = std::move(target);
    value.condition = std::move(condition);
    return value;
}

[[maybe_unused]] StepKindRegistry dialogueRegistry() {
    StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence steps");
    return registry;
}

[[maybe_unused]] SequenceAsset makeGreeting() {
    SequenceAsset asset;
    asset.id            = "common.greeting";
    asset.entry         = "decide";
    asset.parameters    = {SequenceParameter{"speaker"}, SequenceParameter{"listener"}, SequenceParameter{"location"}};
    SequenceNode decide = node("decide", "branch");
    decide.routes = {route("friendly", "friendly", eve::Value("speaker.mood == happy")), route("formal", "formal")};
    SequenceNode friendly = node("friendly", "line", "end");
    friendly.payload.set("speaker", eve::Value("speaker"));
    friendly.payload.set("pool", eve::Value("greeting.friendly"));
    SequenceNode formal = friendly;
    formal.id           = "formal";
    formal.payload.set("pool", eve::Value("greeting.formal"));
    asset.nodes = {decide, friendly, formal, node("end", "end")};
    return asset;
}

}  // namespace


// dialogue_gameplay_control.cpp
// 对话玩法域契约测试：真实 DialogueFlow 运行器走共享玩法协议，
// 覆盖发现、节点观察、路由选择、权限档位、修订号账本、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000901";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000902";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000903";

/** 一个带选项的对话：ask(choice) -> shop(line) -> end。 */
constexpr const char* kConversation = R"(
conversation greeting entry=ask
node ask choice speaker=elder
option buy -> shop
option leave -> end
node shop line speaker=elder text="here you go" next=end
node end end
endconversation
)";

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 观察状态里的字符串字段（缺失返回空串）。 */
[[maybe_unused]] std::string stateText(const eve::Value& state, const char* name) {
    const eve::Value* field = member(state, name);
    return field == nullptr || !field->isString() ? std::string{} : field->asString();
}

/** 真实模块 + 已加载的对话，并把运行器发布到共享玩法协议。 */
struct DialogueFixture {
    eve::dialogue::DialogueFlow* flow = nullptr;
    eve::SubjectRef              instance;
    eve::SubjectRef              owner;

    DialogueFixture() {
        flow = eve::dialogue::DialogueFlow::create();
        REQUIRE(flow != nullptr);
        flow->clearGameplayControls();
        const auto loaded = flow->loadDnutChecked(kConversation, "greeting.dnut");
        REQUIRE(loaded.ok());
        REQUIRE_EQ(loaded.value(), 1);
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = flow->publishGameplay(kInstanceId, kOwnerId);
        REQUIRE(published.ok());
    }

    ~DialogueFixture() {
        flow->clearGameplayControls();
        flow->clear();
    }

    DialogueFixture(const DialogueFixture&)            = delete;
    DialogueFixture& operator=(const DialogueFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "dialogue") found = candidate;
        });
        return found;
    }
};

[[maybe_unused]] eve::GameplayCommand command(const DialogueFixture& fixture, const char* id, const char* actionText,
                                              std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"dialogue\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                                         const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


TEST_CASE("gameplay.control.dialogueScriptPublishesItsRunner") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local flow = eve.DialogueFlow();
        flow.clearGameplayControls();
        scriptPublished <- flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02").ok;
        scriptInstances <- flow.getGameplayControlCount();
        local duplicate = flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02");
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = flow.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000d02");
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- flow.unpublishGameplay("00000000-0000-7000-8000-000000000d01").ok;
        scriptRemaining <- flow.getGameplayControlCount();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already published") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}

// dialogue_hot_reload.cpp
using namespace eve::dialogue;

namespace {
class DialogueMemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::string content;
    bool        readFile(const std::string&, std::vector<std::uint8_t>& out) override {
        out.assign(content.begin(), content.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string&) override { return true; }
};
}  // namespace


// dialogue_importers.cpp
using namespace eve::dialogue;
}  // namespace test_dialogue_authoring
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_proc_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* dialogue_proc_script_content;
// dialogue_proc_script.cpp
namespace test_dialogue_proc_script {
using namespace eve;


UnitSciptTest(DialogueProcScriptTest, dialogue_proc_script_content);

TEST_CASE_FIXTURE(DialogueProcScriptTest, "DialogueProcScriptTest.basicPools") { REQUIRE(callScript("basicPools")); }
}  // namespace test_dialogue_proc_script
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_economy)
#include "PrivateTestScope.h"
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/IEconomy.h"
#include "common/Module.h"
#include "economy/Collector.h"
#include "economy/Economy.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// economy.cpp
namespace test_economy {
// economy.cpp
using namespace eve::economy;

namespace {

[[maybe_unused]] ResourceTypeDef makeType(const std::string& id, int stockMax, const std::string& category = "stock") {
    ResourceTypeDef def;
    def.id          = id;
    def.category    = category;
    def.stockMax    = stockMax;
    def.depletion   = DepletionModel::Finite;
    def.displayName = id;
    return def;
}

/** 每个用例从干净状态开始（进程内单测）。 */
struct Reset {
    Reset() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
    ~Reset() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
};

/** 推进 worker 直到待命；返回消耗的 tick 数（超时返回 maxTicks）。 */
[[maybe_unused]] int runUntilIdle(Collector& worker, int player, int maxTicks = 1000) {
    int ticks = 0;
    while (!worker.isIdle() && ticks < maxTicks) {
        worker.tick(player);
        ++ticks;
    }
    return ticks;
}

}  // namespace


// economy_gameplay_control.cpp
// 经济玩法域契约测试：真实 Economy 模块 + EconomySystem 账本走共享玩法协议，
// 覆盖发现、账本观察、权限档位、上限浪费、修订号账本、事件游标与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId       = "00000000-0000-7000-8000-000000000801";
constexpr const char* kOwnerId          = "00000000-0000-7000-8000-000000000802";
constexpr const char* kStrangerId       = "00000000-0000-7000-8000-000000000803";
constexpr const char* kSecondInstanceId = "00000000-0000-7000-8000-000000000804";
constexpr const char* kSecondOwnerId    = "00000000-0000-7000-8000-000000000805";
constexpr int         kPlayer           = 7;
constexpr int         kSecondPlayer     = 9;

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 收据/事件里机械可读的生效数量。 */
[[maybe_unused]] std::int64_t appliedQuantity(const eve::Value& details) {
    const eve::Value* quantity = member(details, "quantity");
    return quantity == nullptr ? -1 : quantity->asInt();
}

/** 已发布实例账本里某个资源类型的当前余额；未列出返回 -1。 */
[[maybe_unused]] std::int64_t balanceOf(const eve::Value& state, const char* type) {
    const eve::Value* resources = member(state, "resources");
    if (resources == nullptr) return -1;
    const auto* array = resources->getIf<eve::Value::Array>();
    if (array == nullptr) return -1;
    for (const auto& entry : *array) {
        const eve::Value* id     = member(entry, "type");
        const eve::Value* amount = member(entry, "amount");
        if (id != nullptr && amount != nullptr && id->asString() == type) return amount->asInt();
    }
    return -1;
}

/** 真实模块 + 账本，并把玩家账本发布到共享玩法协议。 */
struct EconomyFixture {
    eve::economy::Economy* economy = nullptr;
    eve::SubjectRef        instance;
    eve::SubjectRef        owner;

    EconomyFixture() {
        economy = eve::economy::Economy::create();
        REQUIRE(economy != nullptr);
        eve::economy::EconomySystem::clear();
        eve::economy::Economy::clearTypes();
        eve::economy::Economy::clearEvents();
        REQUIRE(eve::economy::Economy::registerResourceType("gold", "stock", 100, "finite"));
        REQUIRE(eve::economy::Economy::registerResourceType("food", "stock", 0, "renewable"));
        REQUIRE(eve::economy::Economy::registerResourceType("crystal", "special", 10, "growing"));
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = economy->publishGameplay(kInstanceId, kOwnerId, kPlayer);
        REQUIRE(published.ok());
    }

    ~EconomyFixture() {
        economy->clearGameplayControls();
        eve::economy::EconomySystem::clear();
        eve::economy::Economy::clearTypes();
        eve::economy::Economy::clearEvents();
    }

    EconomyFixture(const EconomyFixture&)            = delete;
    EconomyFixture& operator=(const EconomyFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "economy") found = candidate;
        });
        return found;
    }

    /** 再发布第二个玩家的账本（多实例共存）。 */
    eve::Result<void> publishSecondPlayer() {
        return economy->publishGameplay(kSecondInstanceId, kSecondOwnerId, kSecondPlayer);
    }
};

[[maybe_unused]] eve::GameplayCommand command(const EconomyFixture& fixture, const char* id, const char* actionText,
                                              std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] eve::GameplayCommand move(const EconomyFixture& fixture, const char* id, const char* actionText,
                                           std::uint64_t expectedRevision, const char* type, std::int64_t amount) {
    return command(fixture, id, actionText, expectedRevision,
                   object({{"type", eve::Value(type)}, {"amount", eve::Value(amount)}}));
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"economy\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                                         const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


TEST_CASE("gameplay.control.economyScriptPublishesItsLedger") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local economy = eve.Economy();
        economy.clearTypes();
        economy.clearGameplayControls();
        economy.registerResourceType("gold", "stock", 100, "finite");
        scriptPublished <- economy.publishGameplay(
            "00000000-0000-7000-8000-000000000c01",
            "00000000-0000-7000-8000-000000000c02", 42).ok;
        scriptInstances <- economy.getGameplayControlCount();
        scriptTypeIds <- economy.getTypeId(0) + "," + economy.getTypeId(1);
        local duplicate = economy.publishGameplay(
            "00000000-0000-7000-8000-000000000c01",
            "00000000-0000-7000-8000-000000000c02", 42);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = economy.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000c02", 42);
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- economy.unpublishGameplay("00000000-0000-7000-8000-000000000c01").ok;
        scriptRemaining <- economy.getGameplayControlCount();
        economy.clearTypes();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK_EQ(vm.find("scriptTypeIds").toString(), std::string("gold,"));
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}
}  // namespace test_economy
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_voxel_catalog_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_voxel_catalog_script.cpp
namespace test_editor_voxel_catalog_script {
// editor_voxel_catalog_script.cpp
TEST_CASE("editor.voxel.catalog_editor_script_composes_workspace_and_occupancy") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.voxel", "Test Voxel Editor");
        created <- eve.VoxelEditorModule().create("test.asset.blocks");
        voxelEditor <- created.value;
        configured <- voxelEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        cubeFill <- voxelEditor.getModelFill(0);
        bedFill <- voxelEditor.getModelFill(1);
        viewport <- voxelEditor.setViewport(640.0, 400.0);
        before <- voxelEditor.getVoxelCount();
        screen <- voxelEditor.getScreenVoxelCount();
        erased <- voxelEditor.setVoxel(2, 2, 2, false);
        after <- voxelEditor.getVoxelCount();
        partialFill <- voxelEditor.getModelFill(0);
        undoResult <- voxelEditor.undo();
        restored <- voxelEditor.getVoxelCount();
        voxelX0 <- voxelEditor.getVoxelX(0);
        voxelEditor.selectModel("bed");
        bedVoxels <- voxelEditor.getVoxelCount();
        attach <- voxelEditor.setTool("attach");
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("voxel.models"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("voxel.sculpt"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK_EQ(vm.find("cubeFill").toString(), std::string("partial"));
    CHECK_EQ(vm.find("bedFill").toString(), std::string("partial"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("before").toInt(), 64);
    CHECK_EQ(vm.find("screen").toInt(), 64);
    CHECK(vm.find("erased").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("after").toInt(), 63);
    CHECK_EQ(vm.find("partialFill").toString(), std::string("partial"));
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("restored").toInt(), 64);
    CHECK_EQ(vm.find("voxelX0").toInt(), 2);
    CHECK(vm.find("bedVoxels").toInt() > 64);
    CHECK(vm.find("attach").toTable().get<bool>("ok"));
}

// editor_voxel_target_script.cpp
TEST_CASE("editor.script.composes_voxel_target_volume_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        voxelTargets <- eve.VoxelWorldTargetModule();
        voxel <- eve.Voxel();
        world <- voxel.newWorld();
        target <- voxelTargets.create("world.voxels", world);
        falloff <- editor.newConstantBrushFalloff();
        kernel <- editor.newSphereVolumeBrushKernel();
        kernel.setConstantFalloff(falloff);
        operation <- editor.newPaintIntVolumeOperation(7);
        tool <- editor.newVolumeBrushTool("voxel.paint", "Paint Voxels");
        tool.setSphereKernel(kernel);
        tool.setPaintIntOperation(operation);
        tool.setRadius(0.5);
        session <- editor.newSession();
        added <- session.addVolumeTool(tool);
        voxelTargets.bind(session, target);
        activated <- session.activateTool("voxel.paint");
        beforeRevision <- world.getRevision();
        down <- session.dispatchPointer3D(0, 4, 0, 3.0, 5.0, -2.0, 0.0, 0.0, 0.0, 1.0);
        up <- session.dispatchPointer3D(2, 4, 0, 3.0, 5.0, -2.0, 0.0, 0.0, 0.0, 1.0);
        painted <- world.getVoxel(3, 5, -2);
        afterRevision <- world.getRevision();
        dirtyX <- target.getDirtyMinX();
        dirtyY <- target.getDirtyMinY();
        dirtyZ <- target.getDirtyMinZ();
        undone <- session.undo();
        restored <- world.getVoxel(3, 5, -2);
        redone <- session.redo();
        replayed <- target.readInt3(3, 5, -2);
        beforeSameWrite <- world.getRevision();
        world.setVoxel(3, 5, -2, 7);
        afterSameWrite <- world.getRevision();
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK((vm.find("down").toInt() & 2) != 0);
    CHECK((vm.find("up").toInt() & 4) != 0);
    CHECK_EQ(vm.find("painted").toInt(), 7);
    CHECK_GT(vm.find("afterRevision").toInt(), vm.find("beforeRevision").toInt());
    CHECK_EQ(vm.find("dirtyX").toInt(), 3);
    CHECK_EQ(vm.find("dirtyY").toInt(), 5);
    CHECK_EQ(vm.find("dirtyZ").toInt(), -2);
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toInt(), 0);
    CHECK(vm.find("redone").toBool());
    CHECK_EQ(vm.find("replayed").toInt(), 7);
    CHECK_EQ(vm.find("afterSameWrite").toInt(), vm.find("beforeSameWrite").toInt());
}
}  // namespace test_editor_voxel_catalog_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_effects)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "effects/Effects.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// effects.cpp
namespace test_effects {
// effects.cpp
using namespace eve::effects;


TEST_CASE("effects.script.containerLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Effects();
        local effectsResult = module.newContainer();
        local effects = effectsResult.ok ? effectsResult.value : null;
        local idResult = { ok = false, value = "" };
        if (effects != null) {
            idResult = effects.apply("base:4", "policy.production", "leader:8", 5, 1.0,
                                     "production-policy", "refresh");
        }
        local id = idResult.ok ? idResult.value : "";
        local effect = effects != null && idResult.ok ? effects.find(id) : null;
        local tagResult = { ok = false };
        local payloadResult = { ok = false };
        if (effect != null) {
            tagResult = effect.addTag("economic");
            payloadResult = effect.getPayload().setJson("rate", "1.25");
        }
        local sameResult = { ok = false, value = "" };
        if (effects != null) {
            sameResult = effects.apply("base:4", "ignored", "ignored", 7, 2.0,
                                       "production-policy", "refresh");
        }
        local same = sameResult.ok ? sameResult.value : "";
        if (effectsResult.ok && idResult.ok && tagResult.ok && payloadResult.ok && sameResult.ok && same == id &&
            effect != null && effect.getType() == "policy.production" &&
            effect.getSource() == "leader:8" && effect.hasTag("economic") &&
            effect.getPayload().getJson("rate") == "1.25" &&
            effects.eventAt(1).getKind() == "refreshed") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

// effects_lifecycle.cpp
using namespace eve::effects;
}  // namespace test_effects
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_eventstream)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "GameEventTestSupport.h"
#include "common/Module.h"
#include "game_event/GameEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// eventstream.cpp
namespace test_eventstream {
using namespace eve::game_event;

namespace {

[[maybe_unused]] std::uint64_t appendSuccess(GameEventLog& stream, std::uint64_t serial, std::string type,
                                             std::string source, std::string subject, std::string causation,
                                             std::string correlation, std::uint64_t tick, std::uint32_t flags,
                                             std::string payload = "null") {
    auto result =
        stream.append(test::envelope(serial, std::move(type), std::move(source), std::move(subject),
                                     std::move(causation), std::move(correlation), tick, flags, std::move(payload)));
    REQUIRE(result.ok());
    return result.value().value();
}

}  // namespace


TEST_CASE("game_event.script.emitQueryCursorAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.GameEvent();
        local stream = module.newLog();
        local seq = stream.append("00000000-0000-4000-8000-000000000001", "promoted", "administration", "general-4", "", "career-4", 12, 1, "{\"rank\":4}");
        local cursor = stream.newConsumer(1);
        if (seq.ok && seq.value == 1 && stream.querySubject("general-4") == 1 && cursor.read(8) == 1) {
            local copy = module.newLog();
            if (copy.restore(stream.snapshotJson()).ok && copy.find(1).getType() == "promoted")
                result = cursor.batchAt(0).getCorrelation();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("career-4"));
}
}  // namespace test_eventstream
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_filesystem)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* filesystem_content;
// filesystem.cpp
namespace test_filesystem {
using namespace eve;


UnitSciptTest(FilesystemTest, filesystem_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(FilesystemTest, "FilesystemTest." #func) { REQUIRE(callScript(#func)); }

// clang-format off
TestScript(basic)
TestScript(getPaths)
TestScript(readDir)
TestScript(textRoundTrip)
TestScript(atomicTextRoundTrip)
// clang-format on
// TestScript(readFile)
// TestScript(writeFile)
}  // namespace test_filesystem
#undef TestScript
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_shader_spv_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_shader_spv_script.cpp
namespace test_graphics_shader_spv_script {
TEST_CASE("graphics.shaderSpvScript.failuresAreStructuredBeforeUpload") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_shader_spv_binding_test", true));
    REQUIRE(fs->setupWriteDirectory());
    auto written = fs->writeTextAtomic("malformed.spv", "This is not a SPIR-V module.");
    REQUIRE(written.ok());
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        gfx <- eve.Graphics();
        empty <- gfx.loadMeshShaderSpv("", "");
        missing <- gfx.loadMeshShaderSpv("", "absent-shader.spv");
        malformed <- gfx.loadMeshShaderSpv("", "malformed.spv");
        badVertex <- gfx.loadMeshShaderSpv("malformed.spv", "absent-shader.spv");
    )"));
    for (const char* name : {"empty", "missing", "malformed", "badVertex"}) {
        auto result = vm.find(name).toTable();
        CHECK(!result.get<bool>("ok"));
        CHECK(result.get<ssq::Array>("diagnostics").size() > 0);
    }
    REQUIRE(fs->remove("malformed.spv"));
}
}  // namespace test_graphics_shader_spv_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation_script.cpp
namespace test_graphics_vegetation_script {
TEST_CASE("graphics.vegetation.script_owned_field_roundtrip") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local creation = eve.Graphics().newVegetationField();
        assert(creation.ok && creation.ownership == "owned");
        local field = creation.value;
        local saved = field.snapshot();
        assert(saved.ok && saved.value.schema == "eve.graphics.vegetation-field");
        saved.value.globals.motion[2] = 0.8;
        assert(field.restore(saved.value).ok);
        local sampled = field.sample(0.0,0.0,0.0,0,0,0,0);
        assert(sampled.ok && sampled.value.motion[2] > 0.79);
        assert(!field.sample(0.0,0.0,0.0,256,0,0,0).ok);
        saved.value.version = 2;
        assert(!field.restore(saved.value).ok);
        assert(field.sample(0.0,0.0,0.0,0,0,0,0).value.motion[2] > 0.79);

        local detailsCreation = eve.Graphics().newVegetationDetails();
        assert(detailsCreation.ok && detailsCreation.ownership == "owned");
        local details = detailsCreation.value;
        local detailDocument = details.snapshot();
        assert(detailDocument.ok && detailDocument.value.schema == "eve.graphics.vegetation-details");
        detailDocument.value.layers = [2, 3, 4];
        detailDocument.value.global = [0.6, 0.7, 0.8, 0.9];
        assert(details.restore(detailDocument.value).ok);
        local after = details.snapshot();
        assert(after.ok && after.value.layers[2] == 4 && after.value.global[1] > 0.69);
        detailDocument.value.version = 2;
        assert(!details.restore(detailDocument.value).ok);
        assert(details.snapshot().value.layers[2] == 4);
    )"));
}
}  // namespace test_graphics_vegetation_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass_wind)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Grass.h"
#include "graphics/GrassWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
// grass_wind.cpp
namespace test_grass_wind {
namespace {
[[maybe_unused]] std::array<float, 32> snapshot(const eve::graphics::Shader& shader) {
    std::array<float, 32> result;
    std::copy_n(shader.pushConstantData(), 32, result.begin());
    return result;
}
}  // namespace


TEST_CASE("graphics.GrassWind.scriptCheckedAdvanceApplyAndRollback") {
    using namespace eve::graphics;
    Shader shader;
    grass::bindDefaults(&shader);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    table.addClass<Shader>("Shader", std::function<Shader*()>([]() { return nullptr; }), true);
    exposeGrassWindBindings(table);
    vm.addFunc("shader", [&]() { return &shader; });
    vm.run(vm.compileSource(R"(
        local state=eve.VegetationWindState();
        local profile=eve.VegetationWindProfile();
        local tree=eve.TreeWindProfile();
        tree.setDimensions(2.0,6.0);tree.bendFactor=0.35;
        assert(!profile.enabled);
        assert(eve.initializeVegetationWind(state,0.6,0.8,0.0,2.0).ok);
        assert(state.getDirectionX()==0.6 && state.getDirectionY()==-0.8);
        assert(state.strength==1.2);
        assert(state.updatePhase==0.0);
        assert(eve.advanceVegetationWind(state,1.0,0.0,0.0,1.0,4.0).ok);
        assert(state.getDirectionX()==1.0 && state.getDirectionY()==-0.5);
        assert(state.getDirectionZ()==0.0 && state.strength==1.0);
        local audio=eve.VegetationWindAudioState();
        assert(eve.advanceVegetationWindAudio(audio,1.0,5.0,1.0,true,true).ok);
        assert(audio.volume==0.2 && audio.playing && audio.processing);
        local oldPhase=state.phase;
        assert(!eve.advanceVegetationWind(state,1.0,0.0,0.0,1.0,-1.0).ok);
        assert(state.phase==oldPhase);
        state.setDirection(1.0,-0.5,0.2);
        profile.enabled=true;
        profile.setFlex(0.8,1.15,0.1);
        profile.setFrequency(0.25,0.5,1.3);
        assert(eve.applyGrassWind(shader(),state,profile,2.0).ok);
        local wrongTree=eve.applyTreeWind(shader(),state,profile,tree,2.0);
        assert(!wrongTree.ok);
        profile.maximumDistance=0.0;
        assert(!eve.applyGrassWind(shader(),state,profile,4.0).ok);
        local badType=false;
        try { eve.applyGrassWind(null,state,profile,0.0); } catch(e) { badType=true; }
        assert(badType);
        badType=false;
        try { eve.advanceVegetationWind(null,0.0,0.0,0.0,0.0,0.0); } catch(e) { badType=true; }
        assert(badType);
    )"));
    CHECK(shader.pushConstantData()[23] == 100);
    CHECK(std::abs(shader.pushConstantData()[31] - 0.909297427F) < 0.000001F);
}
}  // namespace test_grass_wind
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hexmap_sphere_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
// Earlier Windows/Poco headers define legacy near/far macros.
#undef near
#undef far
#include "hexmap/HexMapModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hexmap_sphere_script.cpp
namespace test_hexmap_sphere_script {
TEST_CASE("hexmap.sphereScriptDirectionsAreNativeArrays") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto* module = eve::hexmap::HexMapModule::create();
    REQUIRE(module->sphere().reset(1, 100.0f, 7).ok());
    vm.run(vm.compileSource(R"(
        local map=eve.HexMap();
        foreach(v in [map.sphereDirection(0),map.sphereCornerDirection(0,0)]) {
            if(typeof v!="array" || v.len()!=3) throw "direction is not a native xyz array";
            local length=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
            if(abs(length-1.0)>0.0001) throw "direction is not normalized";
        }
        local missing=map.sphereDirection(-1);
        if(typeof missing!="array" || missing[0]!=0.0 || missing[1]!=1.0 || missing[2]!=0.0)
            throw "invalid-cell north-direction contract changed";
    )"));
}
}  // namespace test_hexmap_sphere_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_i18n_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* i18n_content;
// i18n_script.cpp
namespace test_i18n_script {
using namespace eve;


UnitSciptTest(I18nScriptTest, i18n_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(I18nScriptTest, "I18nScriptTest." #func) { REQUIRE(callScript(#func)); }

// clang-format off
TestScript(basic)
// clang-format on
}  // namespace test_i18n_script
#undef TestScript
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_image_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* image_content;
// image_script.cpp
namespace test_image_script {
using namespace eve;


UnitSciptTest(ImageScript, image_content);

// One case per line, deliberately, and spelled out rather than via a wrapper macro:
// zeroerr registers each case with getTestSet(type).insert(tc) into a std::set whose
// ordering key is only (file, line) -- TestCase::operator< compares nothing else (see
// external/zeroerr/src/unittest.cpp). Two TEST_CASE* invocations on one source line
// therefore compare equivalent and the second is dropped silently: no error, no
// warning, the case simply never runs. A bare `Wrapper(x)` invocation has no trailing
// semicolon, so clang-format packs several of them onto one line and silently
// re-drops the cases; full definitions keep their own line.
// scripts/check_test_case_lines.py guards this.
TEST_CASE_FIXTURE(ImageScript, "ImageScript.basic") { REQUIRE(callScript("basic")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.imageDataClass") { REQUIRE(callScript("imageDataClass")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.newImageData") { REQUIRE(callScript("newImageData")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.pixelRoundTrip") { REQUIRE(callScript("pixelRoundTrip")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.cloneAndRotate") { REQUIRE(callScript("cloneAndRotate")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.paste") { REQUIRE(callScript("paste")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.pcgScale") { REQUIRE(callScript("pcgScale")); }
}  // namespace test_image_script
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_inventory_binding_architecture)
#include "PrivateTestScope.h"
#include <simplesquirrel/vm.hpp>
#include "common/Module.h"
#include "inventory/Bag.h"
#include "inventory/Inventory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// inventory_binding_architecture.cpp
namespace test_inventory_binding_architecture {
TEST_CASE("inventory.script.architectureExtraState") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        architectureBag <- eve.Inventory().newBag(2)
        architectureBag.setExtra("quest", "ready")
        architectureValue <- architectureBag.getExtra("quest", "missing")
        architectureFallback <- architectureBag.getExtra("unknown", "missing")
    )SQ"));
    CHECK_EQ(vm.get<std::string>("architectureValue"), std::string("ready"));
    CHECK_EQ(vm.get<std::string>("architectureFallback"), std::string("missing"));
    auto* bag = vm.get<eve::inventory::Bag*>("architectureBag");
    REQUIRE(bag != nullptr);
    CHECK_EQ(bag->getExtra("quest", "missing"), std::string("ready"));
}
}  // namespace test_inventory_binding_architecture
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_inventory_gameplay_control)
#include "PrivateTestScope.h"
#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// inventory_gameplay_control.cpp
namespace test_inventory_gameplay_control {
// inventory_gameplay_control.cpp
// 背包玩法域契约测试：真实 Inventory 模块 + Bag + EquipmentSet 走共享玩法协议，
// 覆盖发现、动作词表、权限档位、修订号账本、事件游标与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId          = "00000000-0000-7000-8000-000000000701";
constexpr const char* kOwnerId             = "00000000-0000-7000-8000-000000000702";
constexpr const char* kStrangerId          = "00000000-0000-7000-8000-000000000703";
constexpr const char* kCompanionInstanceId = "00000000-0000-7000-8000-000000000704";
constexpr const char* kCompanionOwnerId    = "00000000-0000-7000-8000-000000000705";

constexpr const char* kItemDefinitions = R"([
    {"id":"mcp.sword","displayName":"MCP Sword","maxStack":1,"weight":3.0,
     "category":"weapon","tags":["weapon"],"equipSlot":"weapon"},
    {"id":"mcp.potion","displayName":"MCP Potion","maxStack":10,"weight":0.5,
     "category":"consumable","tags":["potion"]}
])";

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 已发布实例的收据/事件里读取机械可读的生效数量。 */
[[maybe_unused]] std::int64_t appliedQuantity(const eve::Value& details) {
    const eve::Value* quantity = member(details, "quantity");
    return quantity == nullptr ? -1 : quantity->asInt();
}

/** 真实模块 + 容器 + 装备栏，并把该实例发布到共享玩法协议。 */
struct InventoryFixture {
    eve::inventory::Inventory*                 inventory = nullptr;
    eve::inventory::Bag*                       bag       = nullptr;
    eve::inventory::EquipmentSet*              equipment = nullptr;
    eve::SubjectRef                            instance;
    eve::SubjectRef                            owner;
    std::vector<eve::inventory::Bag*>          ownedBags;
    std::vector<eve::inventory::EquipmentSet*> ownedEquipment;

    InventoryFixture() {
        inventory = eve::inventory::Inventory::create();
        REQUIRE(inventory != nullptr);
        inventory->clearGameplayControls();
        inventory->clearItemDefinitions();
        const int registered = inventory->registerItemsFromJson(kItemDefinitions);
        REQUIRE_EQ(registered, 2);
        bag = inventory->newBag(6);
        REQUIRE(bag != nullptr);
        ownedBags.push_back(bag);
        bag->setId("mcp.player.bag");
        equipment = inventory->newEquipmentSet();
        REQUIRE(equipment != nullptr);
        ownedEquipment.push_back(equipment);
        equipment->defineSlot("weapon");
        equipment->addSlotAllowedTag("weapon", "weapon");
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = inventory->publishGameplay(kInstanceId, kOwnerId, bag, equipment);
        REQUIRE(published.ok());
    }

    ~InventoryFixture() {
        inventory->clearGameplayControls();
        for (auto* set : ownedEquipment)
            if (set != nullptr) set->destroy();
        for (auto* container : ownedBags)
            if (container != nullptr) container->destroy();
    }

    InventoryFixture(const InventoryFixture&)            = delete;
    InventoryFixture& operator=(const InventoryFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "inventory") found = candidate;
        });
        return found;
    }

    /** 再发布一个没有装备栏的同伴背包（多实例共存）。 */
    eve::Result<void> publishCompanion() {
        auto* companionBag = inventory->newBag(4);
        REQUIRE(companionBag != nullptr);
        ownedBags.push_back(companionBag);
        companionBag->setId("mcp.companion.bag");
        return inventory->publishGameplay(kCompanionInstanceId, kCompanionOwnerId, companionBag, nullptr);
    }

    /** 直接写入容器（测试夹具自己的准备步骤，不走适配器）。 */
    void stockBag() {
        const int swords  = bag->addItem("mcp.sword", 1);
        const int potions = bag->addItem("mcp.potion", 3);
        REQUIRE_EQ(swords, 1);
        REQUIRE_EQ(potions, 3);
    }
};

[[maybe_unused]] eve::GameplayCommand command(const InventoryFixture& fixture, const char* id, const char* actionText,
                                              std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] eve::GameplayCommand grant(const InventoryFixture& fixture, const char* id,
                                            std::uint64_t expectedRevision, std::int64_t quantity) {
    return command(fixture, id, "inventory:add-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

[[maybe_unused]] eve::GameplayCommand remove(const InventoryFixture& fixture, const char* id,
                                             std::uint64_t expectedRevision, std::int64_t quantity) {
    return command(fixture, id, "inventory:remove-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"inventory\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                                         const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


TEST_CASE("gameplay.control.inventoryScriptPublishesAndUnpublishesItsInstance") {
    constexpr const char* kScriptInstanceId = "00000000-0000-7000-8000-000000000b01";
    constexpr const char* kScriptOwnerId    = "00000000-0000-7000-8000-000000000b02";

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local inv = eve.Inventory();
        inv.clearGameplayControls();
        scriptBag <- inv.newBag(4);
        scriptBag.setId("script.bag");
        local published = inv.publishGameplay(
            "00000000-0000-7000-8000-000000000b01",
            "00000000-0000-7000-8000-000000000b02",
            scriptBag, null);
        scriptPublished <- published.ok;
        scriptPublishedMessage <- published.message;
        scriptInstances <- inv.getGameplayControlCount();
        local duplicate = inv.publishGameplay(
            "00000000-0000-7000-8000-000000000b01",
            "00000000-0000-7000-8000-000000000b02",
            scriptBag, null);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = inv.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000b02", scriptBag, null);
        scriptMalformedOk <- malformed.ok;
        scriptMalformedMessage <- malformed.message;
        scriptUnpublished <- inv.unpublishGameplay("00000000-0000-7000-8000-000000000b01").ok;
        scriptRemaining <- inv.getGameplayControlCount();
        scriptBag.destroy();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(vm.find("scriptPublishedMessage").toString(), std::string("published"));
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK_EQ(vm.find("scriptMalformedMessage").toString(),
             std::string("rejected: instance and owner ids must be canonical persistent ids [instanceId]"));
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);

    // 脚本发布走的是同一条权威路径：领域与实例都能被 MCP 侧枚举到。
    auto* inventory = eve::inventory::Inventory::create();
    REQUIRE(inventory != nullptr);
    auto* bag = inventory->newBag(2);
    REQUIRE(bag != nullptr);
    const auto republished = inventory->publishGameplay(kScriptInstanceId, kScriptOwnerId, bag, nullptr);
    REQUIRE(republished.ok());
    const auto catalog = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"inventory"})");
    REQUIRE(catalog.ok());
    CHECK(catalog.value().find(kScriptInstanceId) != std::string::npos);
    inventory->clearGameplayControls();
    bag->destroy();
}

// inventory_save_session.cpp
using namespace eve::inventory;
}  // namespace test_inventory_gameplay_control
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_joystick_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* joystick_content;
// joystick_script.cpp
namespace test_joystick_script {
using namespace eve;


UnitSciptTest(JoystickScript, joystick_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(JoystickScript, "JoystickScript." #func) { REQUIRE(callScript(#func)); }

// clang-format off
TestScript(basic)
TestScript(padClass)
TestScript(query)
TestScript(axes)
// clang-format on
}  // namespace test_joystick_script
#undef TestScript
#endif
