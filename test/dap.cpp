#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Runtime.h"

#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"

#include <Poco/Net/NetException.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Timespan.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
        oss << "{\"seq\":" << (++seq_) << ",\"type\":\"request\",\"command\":\"" << command
            << "\"";
        if (!argsJson.empty()) oss << ",\"arguments\":" << argsJson;
        oss << "}";
        sendFrame(oss.str());
    }

    void sendFrame(const std::string& json) {
        const std::string frame =
            "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
        const int n = sock_.sendBytes(frame.data(), static_cast<int>(frame.size()));
        REQUIRE(n == static_cast<int>(frame.size()));
    }

    /** Read one DAP message; pumps DebugAdapter::poll while waiting. */
    Poco::JSON::Object::Ptr recv(int timeoutMs = 2000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
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
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
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
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = recv(50);
            if (!msg) continue;
            if (msg->optValue<std::string>("type", "") == "event" &&
                msg->optValue<std::string>("event", "") == name)
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
        const std::string header = recvBuf_.substr(0, headerEnd);
        int contentLength        = -1;
        {
            const std::string key = "Content-Length:";
            auto pos              = header.find(key);
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
        auto root = parser.parse(json).extract<Poco::JSON::Object::Ptr>();
        return root;
    }

    Poco::Net::StreamSocket sock_;
    std::string             recvBuf_;
    int                     seq_ = 0;
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
    client.sendRequest("launch",
                       "{\"program\":\"/tmp/eve_game\",\"cwd\":\"/tmp/eve_game\",\"stopOnEntry\":false}");
    auto launchResp = client.expectResponse("launch");
    REQUIRE(launchResp);
    CHECK(launchResp->optValue<bool>("success", false));
    REQUIRE(client.expectEvent("process", 1000));

    // ---- setBreakpoints ----
    client.sendRequest(
        "setBreakpoints",
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
    CHECK_EQ(pauseStopped->getObject("body")->optValue<std::string>("reason", ""),
             std::string("pause"));
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
        HSQUIRRELVM v = vm.getHandle();
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
    auto& dt  = DevTool::instance();
    auto& dap = DebugAdapter::instance();
    auto& dbg = Debugger::instance();

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
        "function inner() {\n"      // line 1
        "    local a = 1\n"         // line 2
        "    local b = a + 1\n"     // line 3 <-- breakpoint
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
        ssq::VM vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const SQInteger len = static_cast<SQInteger>(std::strlen(src));
        SQRESULT rc = sq_compilebuffer(v, src, len, _SC("e2e.nut"), SQTrue);
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

    client.sendRequest("setExceptionBreakpoints",
                       "{\"filters\":[\"uncaught\",\"script_error\"]}");
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
    CHECK(body->optValue<std::string>("description", "").find("index error") !=
          std::string::npos);
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
        "function boom() {\n"   // line 1
        "    local x = 1\n"     // line 2
        "    throw \"kaboom\"\n" // line 3
        "}\n"                   // line 4
        "boom()\n";             // line 5

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};
    std::atomic<bool> callFailed{false};

    std::thread scriptThread([&] {
        ssq::VM vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)),
                                              _SC("boom.nut"), SQTrue)));
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
    client.sendRequest("setExceptionBreakpoints",
                       "{\"filters\":[\"script_error\"]}");
    REQUIRE(client.expectResponse("setExceptionBreakpoints"));
    CHECK(dbg.breakOnError());
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    client.clearEvents();
    go = true;
    auto stopped = client.expectEvent("stopped", 8000);
    REQUIRE(stopped);
    auto body = stopped->getObject("body");
    REQUIRE(body);
    CHECK_EQ(body->optValue<std::string>("reason", ""), std::string("exception"));
    CHECK(body->optValue<std::string>("description", "").find("kaboom") !=
          std::string::npos);
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
        if (s->optValue<std::string>("name", "") == "Locals")
            localsRef = s->optValue<int>("variablesReference", -1);
    }
    REQUIRE(localsRef > 0);
    client.sendRequest("variables",
                       "{\"variablesReference\":" + std::to_string(localsRef) + "}");
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
        "function boom() {\n"                                   // line 1
        "    throw \"kaboom\"\n"                                // line 2
        "}\n"                                                   // line 3
        "function caller() {\n"                                 // line 4
        "    try {\n"                                           // line 5
        "        boom()\n"                                      // line 6
        "    } catch (e) {\n"                                   // line 7
        "        if (\"dev\" in eve) eve.dev.reportError(\"\" + e)\n"  // line 8
        "    }\n"                                               // line 9
        "}\n"                                                   // line 10
        "caller()\n";                                           // line 11

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};

    std::thread scriptThread([&] {
        ssq::VM vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        // Provide the `eve` root table the dev script API binds into.
        const char* pre = "eve <- {};\n";
        REQUIRE(SQ_SUCCEEDED(
            sq_compilebuffer(v, pre, static_cast<SQInteger>(std::strlen(pre)), _SC("pre.nut"),
                             SQTrue)));
        sq_pushroottable(v);
        REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
        sq_poptop(v);
        dt.exposeScriptApi(vm);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)),
                                              _SC("caught.nut"), SQTrue)));
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
    client.sendRequest("setExceptionBreakpoints",
                       "{\"filters\":[\"script_error\"]}");
    REQUIRE(client.expectResponse("setExceptionBreakpoints"));
    client.sendRequest("configurationDone");
    REQUIRE(client.expectResponse("configurationDone"));

    client.clearEvents();
    go = true;
    auto stopped = client.expectEvent("stopped", 8000);
    REQUIRE(stopped);
    auto body = stopped->getObject("body");
    REQUIRE(body);
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
    if (scriptThread.joinable()) {
        if (scriptDone.load())
            scriptThread.join();
        else
            scriptThread.detach();
    }
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

    client.sendRequest(
        "setBreakpoints",
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
        "function inner() {\n"          // line 1
        "    local a = 1\n"             // line 2
        "    local tab = { x = 5 }\n"   // line 3
        "    local arr = [1, 2, 3]\n"   // line 4
        "    local b = a + 1\n"         // line 5 <-- breakpoint
        "}\n"                           // line 6
        "function outer() {\n"          // line 7
        "    local o1 = 99\n"           // line 8
        "    inner()\n"                 // line 9
        "}\n"                           // line 10
        "outer()\n";                    // line 11

    std::atomic<bool> attached{false};
    std::atomic<bool> go{false};
    std::atomic<bool> scriptDone{false};

    std::thread scriptThread([&] {
        ssq::VM vm(1024, ssq::Libs::ALL);
        HSQUIRRELVM v = vm.getHandle();
        dt.attach(v, /*sampleLocals=*/false);
        attached = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)),
                                              _SC("scopes.nut"), SQTrue)));
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
        if (s->optValue<std::string>("name", "") == "Locals")
            localsRef = s->optValue<int>("variablesReference", -1);
    }
    REQUIRE(localsRef > 1);  // differs from frame-0 locals ref (1)

    client.sendRequest("variables",
                       "{\"variablesReference\":" + std::to_string(localsRef) + "}");
    auto varsResp = client.expectResponse("variables");
    REQUIRE(varsResp);
    auto vars = varsResp->getObject("body")->getArray("variables");
    REQUIRE(vars);
    bool foundO1 = false;
    for (size_t i = 0; i < vars->size(); ++i) {
        auto vv = vars->getObject(static_cast<unsigned int>(i));
        const std::string name = vv->optValue<std::string>("name", "");
        if (name == "o1" && vv->optValue<std::string>("value", "").find("99") !=
                                std::string::npos)
            foundO1 = true;
        // Leaf variables must NOT carry the inspect marker.
        if (name == "o1")
            CHECK(!vv->has("__vscodeVariableMenuContext"));
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
        auto vv = innerVars->getObject(static_cast<unsigned int>(i));
        const std::string name = vv->optValue<std::string>("name", "");
        if (name == "tab") tabRef = vv->optValue<int>("variablesReference", -1);
        if (name == "arr") arrRef = vv->optValue<int>("variablesReference", -1);
        // Expandable containers expose the marker so the VS Code VARIABLES
        // context menu can offer "查看实例" only for objects.
        if (name == "tab" || name == "arr") {
            CHECK_EQ(vv->optValue<std::string>("__vscodeVariableMenuContext", ""),
                     std::string("object"));
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
    CHECK(arrVars->getObject(2)->optValue<std::string>("value", "").find("3") !=
          std::string::npos);

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
