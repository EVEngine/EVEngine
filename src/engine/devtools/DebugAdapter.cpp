#include "devtools/DebugAdapter.hpp"

#include "devtools/Snapshot.hpp"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Exception.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <cstdlib>
#include <sstream>

namespace eve::dev {
namespace {

std::string stringify(const Poco::Dynamic::Var& v) {
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(v, oss);
    return oss.str();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

}  // namespace

DebugAdapter& DebugAdapter::instance() {
    static DebugAdapter inst;
    return inst;
}

DebugAdapter::DebugAdapter() = default;

DebugAdapter::~DebugAdapter() { stop(); }

int DebugAdapter::listen(uint16_t port) {
    stop();
    try {
        Poco::Net::SocketAddress addr("127.0.0.1", port);
        server_ = std::make_unique<Poco::Net::ServerSocket>(addr);
        server_->setBlocking(false);
        const int bound = static_cast<int>(server_->address().port());
        port_.store(bound);
        listening_.store(true);
        return bound;
    } catch (...) {
        server_.reset();
        listening_.store(false);
        port_.store(0);
        return 0;
    }
}

void DebugAdapter::stop() {
    std::lock_guard<std::mutex> lock(ioMu_);
    if (client_) {
        try {
            client_->close();
        } catch (...) {
        }
        client_.reset();
    }
    if (server_) {
        try {
            server_->close();
        } catch (...) {
        }
        server_.reset();
    }
    recvBuf_.clear();
    listening_.store(false);
    hasClient_.store(false);
    port_.store(0);
    configured_ = false;
}

void DebugAdapter::acceptNonBlocking() {
    if (!server_ || client_) return;
    try {
        Poco::Net::SocketAddress clientAddr;
        Poco::Net::StreamSocket  ss = server_->acceptConnection(clientAddr);
        ss.setBlocking(false);
        client_ = std::make_unique<Poco::Net::StreamSocket>(ss);
        hasClient_.store(true);
        recvBuf_.clear();
    } catch (const Poco::TimeoutException&) {
    } catch (const Poco::Net::NetException&) {
        // WouldBlock / no pending connection.
    } catch (...) {
    }
}

bool DebugAdapter::sendMessage(const std::string& json) {
    if (!client_) return false;
    const std::string frame =
        "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    try {
        const int sent = client_->sendBytes(frame.data(), static_cast<int>(frame.size()));
        return sent == static_cast<int>(frame.size());
    } catch (...) {
        client_.reset();
        hasClient_.store(false);
        return false;
    }
}

std::string DebugAdapter::makeResponse(int /*seq*/, int requestSeq, const std::string& command,
                                       bool ok, const std::string& bodyJson,
                                       const std::string& message) {
    const int outSeq = seq_.fetch_add(1);
    std::ostringstream oss;
    oss << "{\"seq\":" << outSeq << ",\"type\":\"response\",\"request_seq\":" << requestSeq
        << ",\"success\":" << (ok ? "true" : "false") << ",\"command\":\"" << jsonEscape(command)
        << "\"";
    if (!ok && !message.empty()) oss << ",\"message\":\"" << jsonEscape(message) << "\"";
    if (!bodyJson.empty()) oss << ",\"body\":" << bodyJson;
    oss << "}";
    return oss.str();
}

std::string DebugAdapter::makeEvent(const std::string& event, const std::string& bodyJson) {
    const int outSeq = seq_.fetch_add(1);
    std::ostringstream oss;
    oss << "{\"seq\":" << outSeq << ",\"type\":\"event\",\"event\":\"" << jsonEscape(event) << "\"";
    if (!bodyJson.empty()) oss << ",\"body\":" << bodyJson;
    oss << "}";
    return oss.str();
}

std::string DebugAdapter::reasonString(PauseReason r) {
    switch (r) {
        case PauseReason::Breakpoint:
            return "breakpoint";
        case PauseReason::Step:
            return "step";
        case PauseReason::Exception:
            return "exception";
        case PauseReason::PauseKey:
        case PauseReason::Snapshot:
        default:
            return "pause";
    }
}

void DebugAdapter::notifyStopped(PauseReason reason, const SourceLoc& loc) {
    auto body = new Poco::JSON::Object();
    body->set("reason", reasonString(reason));
    body->set("threadId", 1);
    body->set("allThreadsStopped", true);
    if (!loc.source.empty()) {
        auto src = new Poco::JSON::Object();
        src->set("name", loc.source);
        src->set("path", loc.source);
        // description only — VS Code resolves via stackTrace sources
        (void)src;
    }
    sendMessage(makeEvent("stopped", stringify(Poco::Dynamic::Var(body))));
}

void DebugAdapter::notifyContinued() {
    auto body = new Poco::JSON::Object();
    body->set("threadId", 1);
    body->set("allThreadsContinued", true);
    sendMessage(makeEvent("continued", stringify(Poco::Dynamic::Var(body))));
}

void DebugAdapter::notifyTerminated() {
    sendMessage(makeEvent("terminated", "{}"));
}

void DebugAdapter::readAndDispatch() {
    if (!client_) return;
    char buf[4096];
    try {
        const int n = client_->receiveBytes(buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0) {
                client_.reset();
                hasClient_.store(false);
            }
            return;
        }
        recvBuf_.append(buf, static_cast<size_t>(n));
    } catch (const Poco::TimeoutException&) {
        return;
    } catch (const Poco::Net::NetException&) {
        return;
    } catch (...) {
        client_.reset();
        hasClient_.store(false);
        return;
    }

    while (true) {
        const auto headerEnd = recvBuf_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) break;
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
            continue;
        }
        const size_t total = headerEnd + 4 + static_cast<size_t>(contentLength);
        if (recvBuf_.size() < total) break;
        const std::string json = recvBuf_.substr(headerEnd + 4, static_cast<size_t>(contentLength));
        recvBuf_.erase(0, total);
        handleRequest(json);
    }
}

void DebugAdapter::handleRequest(const std::string& json) {
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(json).extract<Poco::JSON::Object::Ptr>();
        if (!root) return;
        const std::string type = root->optValue<std::string>("type", "");
        if (type != "request") return;
        const std::string command = root->optValue<std::string>("command", "");
        const int         reqSeq  = root->optValue<int>("seq", 0);
        Poco::JSON::Object::Ptr args =
            root->has("arguments") ? root->getObject("arguments") : nullptr;

        auto& dbg = Debugger::instance();

        if (command == "initialize") {
            auto body = new Poco::JSON::Object();
            body->set("supportsConfigurationDoneRequest", true);
            body->set("supportsEvaluateForHovers", true);
            body->set("supportsSetVariable", false);
            body->set("supportsStepInTargetsRequest", false);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            sendMessage(makeEvent("initialized", "{}"));
            return;
        }
        if (command == "launch" || command == "attach") {
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            return;
        }
        if (command == "configurationDone") {
            configured_ = true;
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            return;
        }
        if (command == "setBreakpoints") {
            std::string sourcePath;
            if (args && args->has("source")) {
                auto src = args->getObject("source");
                if (src) {
                    sourcePath = src->optValue<std::string>("path", "");
                    if (sourcePath.empty()) sourcePath = src->optValue<std::string>("name", "");
                }
            }
            dbg.clearBreakpoints(sourcePath);
            auto outBps = new Poco::JSON::Array();
            if (args && args->has("breakpoints")) {
                auto arr = args->getArray("breakpoints");
                if (arr) {
                    for (size_t i = 0; i < arr->size(); ++i) {
                        auto bp = arr->getObject(i);
                        if (!bp) continue;
                        const int line = bp->optValue<int>("line", 0);
                        const int id   = dbg.setBreakpoint(sourcePath, line, true);
                        auto ob = new Poco::JSON::Object();
                        ob->set("id", id);
                        ob->set("verified", id > 0);
                        ob->set("line", line);
                        outBps->add(ob);
                    }
                }
            }
            auto body = new Poco::JSON::Object();
            body->set("breakpoints", outBps);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "threads") {
            auto th = new Poco::JSON::Object();
            th->set("id", 1);
            th->set("name", "main");
            auto arr = new Poco::JSON::Array();
            arr->add(th);
            auto body = new Poco::JSON::Object();
            body->set("threads", arr);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "stackTrace") {
            auto frames = dbg.stackTrace(64);
            // If paused between frames with no SQ stack, synthesize one from pauseLoc.
            if (frames.empty() && !dbg.pauseLocation().empty()) {
                StackFrameInfo f;
                f.id   = 1;
                f.loc  = dbg.pauseLocation();
                f.name = f.loc.function.empty() ? "frame" : f.loc.function;
                frames.push_back(f);
            }
            auto arr = new Poco::JSON::Array();
            for (const auto& f : frames) {
                auto fo = new Poco::JSON::Object();
                fo->set("id", f.id);
                fo->set("name", f.name);
                fo->set("line", f.loc.line);
                fo->set("column", 0);
                auto src = new Poco::JSON::Object();
                src->set("name", f.loc.source);
                src->set("path", f.loc.source);
                fo->set("source", src);
                arr->add(fo);
            }
            auto body = new Poco::JSON::Object();
            body->set("stackFrames", arr);
            body->set("totalFrames", static_cast<int>(frames.size()));
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "scopes") {
            auto locals = new Poco::JSON::Object();
            locals->set("name", "Locals");
            locals->set("variablesReference", varRefLocals_);
            locals->set("expensive", false);
            auto watches = new Poco::JSON::Object();
            watches->set("name", "Watches");
            watches->set("variablesReference", varRefWatches_);
            watches->set("expensive", false);
            auto arr = new Poco::JSON::Array();
            arr->add(locals);
            arr->add(watches);
            auto body = new Poco::JSON::Object();
            body->set("scopes", arr);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "variables") {
            const int ref = args ? args->optValue<int>("variablesReference", 0) : 0;
            auto      arr = new Poco::JSON::Array();
            if (ref == varRefLocals_) {
                for (const auto& v : dbg.locals(1)) {
                    auto o = new Poco::JSON::Object();
                    o->set("name", v.name);
                    o->set("value", v.value);
                    o->set("type", v.type);
                    o->set("variablesReference", 0);
                    arr->add(o);
                }
            } else if (ref == varRefWatches_) {
                dbg.refreshWatches();
                for (const auto& w : dbg.watches()) {
                    auto o = new Poco::JSON::Object();
                    o->set("name", w.expression);
                    o->set("value", w.value);
                    o->set("type", w.ok ? "watch" : "error");
                    o->set("variablesReference", 0);
                    arr->add(o);
                }
            }
            auto body = new Poco::JSON::Object();
            body->set("variables", arr);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "continue") {
            dbg.resume();
            notifyContinued();
            auto body = new Poco::JSON::Object();
            body->set("allThreadsContinued", true);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "next" || command == "stepIn" || command == "stepOut") {
            // Script step when mid-hook; otherwise step one game frame.
            if (!dbg.pauseLocation().empty() && dbg.lastPauseReason() == PauseReason::Breakpoint)
                dbg.stepLine();
            else if (dbg.lastPauseReason() == PauseReason::Step && !dbg.pauseLocation().empty())
                dbg.stepLine();
            else
                dbg.stepFrame();
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            return;
        }
        if (command == "pause") {
            dbg.pause(PauseReason::PauseKey);
            notifyStopped(PauseReason::PauseKey, dbg.pauseLocation());
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            return;
        }
        if (command == "evaluate") {
            const std::string expr = args ? args->optValue<std::string>("expression", "") : "";
            // Treat evaluate as an implicit watch registration for the Watches scope.
            if (!expr.empty()) dbg.addWatch(expr);
            auto info = dbg.evaluate(expr);
            auto body = new Poco::JSON::Object();
            body->set("result", info.value);
            body->set("type", info.type);
            body->set("variablesReference", 0);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "disconnect" || command == "terminate") {
            dbg.resume();
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            notifyTerminated();
            return;
        }
        if (command == "setExceptionBreakpoints") {
            sendMessage(makeResponse(0, reqSeq, command, true, "{\"filters\":[]}"));
            return;
        }
        // Custom: snapshot helpers via evaluate-like extensions are enough;
        // acknowledge unknown with failure.
        sendMessage(makeResponse(0, reqSeq, command, false, "{}", "unsupported: " + command));
    } catch (const std::exception& e) {
        (void)e;
    }
}

void DebugAdapter::poll() {
    if (!listening_.load()) return;
    std::lock_guard<std::mutex> lock(ioMu_);
    acceptNonBlocking();
    readAndDispatch();
}

}  // namespace eve::dev
