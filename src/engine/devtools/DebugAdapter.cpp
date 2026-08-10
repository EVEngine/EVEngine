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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

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

void DebugAdapter::setSourceRoot(std::string root) {
    for (char& c : root) {
        if (c == '\\') c = '/';
    }
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    sourceRoot_ = std::move(root);
    discoverEngineScriptAliases();
}

void DebugAdapter::discoverEngineScriptAliases() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur;
    if (!sourceRoot_.empty())
        cur = fs::path(sourceRoot_);
    else
        cur = fs::current_path(ec);
    if (ec) return;
    for (int i = 0; i < 8; ++i) {
        const fs::path cand = cur / "src" / "scripts" / "load.nut";
        if (fs::exists(cand, ec) && !ec) {
            rememberSourcePath(cand.string());
            // Embedded root compiles as "load.nut" — force the alias even if
            // rememberSourcePath also keyed by basename.
            sourceAliases_["load.nut"] = Debugger::normalizeSource(cand.string());
            sourceAliases_["buffer"]   = sourceAliases_["load.nut"];
            return;
        }
        if (!cur.has_parent_path()) break;
        const fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
}

void DebugAdapter::rememberSourcePath(const std::string& path) {
    const std::string norm = Debugger::normalizeSource(path);
    if (norm.empty()) return;
    sourceAliases_[norm] = norm;
    const std::string base = Debugger::sourceBasename(norm);
    if (!base.empty()) sourceAliases_[base] = norm;
    if (!sourceRoot_.empty() && Debugger::sourcesMatch(norm, sourceRoot_ + "/" + base)) {
        // Prefer keeping a stable relative key under the game root.
        std::string rel = norm;
        if (rel.rfind(sourceRoot_, 0) == 0) {
            rel = rel.substr(sourceRoot_.size());
            while (!rel.empty() && rel[0] == '/') rel.erase(rel.begin());
            if (!rel.empty()) sourceAliases_[rel] = norm;
        }
    }
}

std::string DebugAdapter::resolveSourcePath(std::string source) const {
    source = Debugger::normalizeSource(std::move(source));
    if (source.empty()) return source;

    // Prefer paths VS Code already told us about (setBreakpoints).
    {
        auto it = sourceAliases_.find(source);
        if (it != sourceAliases_.end()) return it->second;
        const std::string base = Debugger::sourceBasename(source);
        if (!base.empty()) {
            it = sourceAliases_.find(base);
            if (it != sourceAliases_.end()) return it->second;
        }
    }

    // Absolute already (POSIX or Windows drive).
    if (source[0] == '/' || (source.size() >= 2 && source[1] == ':')) return source;
    if (sourceRoot_.empty()) return source;
    try {
        const auto joined = std::filesystem::path(sourceRoot_) / source;
        std::error_code ec;
        auto canon      = std::filesystem::weakly_canonical(joined, ec);
        std::string out = ec ? joined.string() : canon.string();
        for (char& c : out) {
            if (c == '\\') c = '/';
        }
        return out;
    } catch (...) {
        return sourceRoot_ + "/" + source;
    }
}

int DebugAdapter::listen(uint16_t port) {
    stop();
    try {
        Poco::Net::SocketAddress addr("127.0.0.1", port);
        server_ = std::make_unique<Poco::Net::ServerSocket>(addr);
        server_->setBlocking(false);
        const int bound = static_cast<int>(server_->address().port());
        port_.store(bound);
        listening_.store(true);
        // Default source root = process cwd (extension chdirs into the game folder).
        try {
            setSourceRoot(std::filesystem::current_path().string());
        } catch (...) {
            discoverEngineScriptAliases();
        }
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
    configured_  = false;
    stopOnEntry_ = false;
    sourceAliases_.clear();
}

void DebugAdapter::acceptNonBlocking() {
    if (!server_ || client_) return;
    try {
        Poco::Net::SocketAddress clientAddr;
        Poco::Net::StreamSocket  ss = server_->acceptConnection(clientAddr);
        // Keep the DAP client socket blocking so small response writes are complete.
        ss.setBlocking(true);
        ss.setReceiveTimeout(Poco::Timespan(0, 1000));  // 1ms poll-friendly
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
    Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
    body->set("reason", reasonString(reason));
    body->set("threadId", 1);
    body->set("allThreadsStopped", true);
    if (!loc.source.empty() && loc.line > 0) {
        // Hint the IDE; stackTrace still provides the authoritative source.
        Poco::JSON::Object::Ptr src = new Poco::JSON::Object();
        const std::string resolved = resolveSourcePath(loc.source);
        src->set("name", Debugger::sourceBasename(resolved));
        src->set("path", resolved);
        body->set("source", src);
        body->set("line", loc.line);
        body->set("column", 1);
    }
    sendMessage(makeEvent("stopped", stringify(Poco::Dynamic::Var(body))));
}

void DebugAdapter::notifyContinued() {
    Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
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
        // Non-blocking: nothing to read yet.
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
            // Capabilities VS Code uses to enable Continue / Step Over / Pause UI + keys.
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("supportsConfigurationDoneRequest", true);
            body->set("supportsEvaluateForHovers", true);
            body->set("supportsTerminateRequest", true);
            body->set("supportsSingleThreadExecutionRequests", true);
            body->set("supportsSetVariable", false);
            body->set("supportsStepInTargetsRequest", false);
            body->set("supportsStepBack", false);
            body->set("supportTerminateDebuggee", true);
            body->set("supportsCancelRequest", false);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            sendMessage(makeEvent("initialized", "{}"));
            return;
        }
        if (command == "launch" || command == "attach") {
            if (args) {
                // Prefer explicit cwd / program from the VS Code launch config.
                std::string root = args->optValue<std::string>("cwd", "");
                if (root.empty()) root = args->optValue<std::string>("program", "");
                if (!root.empty()) setSourceRoot(std::move(root));
                stopOnEntry_ = args->optValue<bool>("stopOnEntry", false);
            }
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            // Announce the debuggee process so the CALL STACK / debug toolbar light up.
            {
                Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
                body->set("name", "eve");
#if defined(_WIN32)
                body->set("systemProcessId", static_cast<int>(::GetCurrentProcessId()));
#else
                body->set("systemProcessId", static_cast<int>(::getpid()));
#endif
                body->set("isLocalProcess", true);
                body->set("startMethod", command == "launch" ? "launch" : "attach");
                sendMessage(makeEvent("process", stringify(Poco::Dynamic::Var(body))));
            }
            return;
        }
        if (command == "configurationDone") {
            configured_ = true;
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            if (stopOnEntry_) {
                // Break on the next script line so the IDE gets a real source location.
                dbg.stepInto();
            }
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
            if (!sourcePath.empty()) rememberSourcePath(sourcePath);
            dbg.clearBreakpoints(sourcePath);
            Poco::JSON::Array::Ptr outBps = new Poco::JSON::Array();
            if (args && args->has("breakpoints")) {
                auto arr = args->getArray("breakpoints");
                if (arr) {
                    for (size_t i = 0; i < arr->size(); ++i) {
                        auto bp = arr->getObject(i);
                        if (!bp) continue;
                        const int line = bp->optValue<int>("line", 0);
                        const int id   = dbg.setBreakpoint(sourcePath, line, true);
                        Poco::JSON::Object::Ptr ob = new Poco::JSON::Object();
                        ob->set("id", id);
                        ob->set("verified", id > 0);
                        ob->set("line", line);
                        if (id > 0 && !sourcePath.empty()) {
                            Poco::JSON::Object::Ptr src = new Poco::JSON::Object();
                            const std::string resolved = resolveSourcePath(sourcePath);
                            src->set("name", Debugger::sourceBasename(resolved));
                            src->set("path", resolved);
                            ob->set("source", src);
                        }
                        outBps->add(ob);
                    }
                }
            }
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("breakpoints", outBps);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "threads") {
            Poco::JSON::Object::Ptr th = new Poco::JSON::Object();
            th->set("id", 1);
            th->set("name", "main");
            Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
            arr->add(th);
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
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
            Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
            for (const auto& f : frames) {
                Poco::JSON::Object::Ptr fo = new Poco::JSON::Object();
                fo->set("id", f.id);
                fo->set("name", f.name);
                fo->set("line", f.loc.line);
                fo->set("column", 1);
                Poco::JSON::Object::Ptr src = new Poco::JSON::Object();
                const std::string resolved = resolveSourcePath(f.loc.source);
                const auto slash = resolved.find_last_of('/');
                src->set("name", slash == std::string::npos ? resolved : resolved.substr(slash + 1));
                src->set("path", resolved);
                fo->set("source", src);
                arr->add(fo);
            }
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("stackFrames", arr);
            body->set("totalFrames", static_cast<int>(frames.size()));
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "scopes") {
            Poco::JSON::Object::Ptr locals = new Poco::JSON::Object();
            locals->set("name", "Locals");
            locals->set("variablesReference", varRefLocals_);
            locals->set("expensive", false);
            Poco::JSON::Object::Ptr watches = new Poco::JSON::Object();
            watches->set("name", "Watches");
            watches->set("variablesReference", varRefWatches_);
            watches->set("expensive", false);
            Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
            arr->add(locals);
            arr->add(watches);
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("scopes", arr);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "variables") {
            const int ref = args ? args->optValue<int>("variablesReference", 0) : 0;
            Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
            if (ref == varRefLocals_) {
                for (const auto& v : dbg.locals(1)) {
                    Poco::JSON::Object::Ptr o = new Poco::JSON::Object();
                    o->set("name", v.name);
                    o->set("value", v.value);
                    o->set("type", v.type);
                    o->set("variablesReference", 0);
                    arr->add(o);
                }
            } else if (ref == varRefWatches_) {
                dbg.refreshWatches();
                for (const auto& w : dbg.watches()) {
                    Poco::JSON::Object::Ptr o = new Poco::JSON::Object();
                    o->set("name", w.expression);
                    o->set("value", w.value);
                    o->set("type", w.ok ? "watch" : "error");
                    o->set("variablesReference", 0);
                    arr->add(o);
                }
            }
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("variables", arr);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "continue") {
            dbg.resume();
            notifyContinued();
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("allThreadsContinued", true);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "next") {
            // F10 — step over (skip call bodies).
            dbg.stepOver();
            notifyContinued();
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("threadId", 1);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "stepIn") {
            // F11 — step into calls.
            dbg.stepInto();
            notifyContinued();
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("threadId", 1);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "stepOut") {
            // Shift+F11 — run until return to caller.
            dbg.stepOut();
            notifyContinued();
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("threadId", 1);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "stepFrame") {
            // Custom: advance one game frame then pause (secondary to statement step).
            dbg.stepFrame();
            notifyContinued();
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
            body->set("threadId", 1);
            sendMessage(makeResponse(0, reqSeq, command, true, stringify(Poco::Dynamic::Var(body))));
            return;
        }
        if (command == "pause") {
            // DAP pause = break at next script statement (not frame-level pause with
            // an empty source). stopped event is emitted from the line hook.
            if (!dbg.isPaused()) dbg.stepInto();
            sendMessage(makeResponse(0, reqSeq, command, true, "{}"));
            return;
        }
        if (command == "evaluate") {
            const std::string expr = args ? args->optValue<std::string>("expression", "") : "";
            // Treat evaluate as an implicit watch registration for the Watches scope.
            if (!expr.empty()) dbg.addWatch(expr);
            auto info = dbg.evaluate(expr);
            Poco::JSON::Object::Ptr body = new Poco::JSON::Object();
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
        // Never leave the IDE hanging on a BadCast / JSON error.
        sendMessage(makeResponse(0, 0, "error", false, "{}", e.what()));
    } catch (...) {
        sendMessage(makeResponse(0, 0, "error", false, "{}", "unknown DAP handler error"));
    }
}

void DebugAdapter::poll() {
    if (!listening_.load()) return;
    std::lock_guard<std::mutex> lock(ioMu_);
    acceptNonBlocking();
    readAndDispatch();
}

bool DebugAdapter::waitUntilConfigured(int timeoutMs) {
    if (!listening_.load()) return false;
    if (timeoutMs < 0) timeoutMs = 0;
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    // Fast path for `eve --dap-port` without an IDE: don't block the whole timeout.
    const auto clientDeadline = clock::now() + std::chrono::milliseconds(
                                    std::min(timeoutMs, 2500));
    while (clock::now() < clientDeadline) {
        poll();
        if (configured_) return true;
        if (hasClient_.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!hasClient_.load()) {
        poll();
        return configured_;
    }
    while (clock::now() < deadline) {
        poll();
        if (configured_) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    poll();
    return configured_;
}

}  // namespace eve::dev
