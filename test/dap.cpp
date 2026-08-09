#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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

#include <chrono>
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

    // ---- pause (arms break-on-next-line; stopped comes from the line hook) ----
    client.clearEvents();
    client.sendRequest("pause", "{\"threadId\":1}");
    auto pauseResp = client.expectResponse("pause");
    REQUIRE(pauseResp);
    CHECK(pauseResp->optValue<bool>("success", false));
    CHECK(static_cast<int>(dbg.mode()) == static_cast<int>(RunMode::StepInto));
    CHECK(!client.findEvent("stopped"));
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
