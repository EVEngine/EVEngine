#include "devtools/McpRuntimeTools.hpp"

#include "devtools/ConsolePanel.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/RenderCapture.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eve::dev {
namespace {

constexpr int kDefaultConsoleRead = 200;
constexpr int kMaxConsoleRead     = 1000;

constexpr int kDefaultScreenshotBytes = 2 * 1024 * 1024;
constexpr int kMinScreenshotBytes     = 1024;
constexpr int kMaxScreenshotBytes     = 32 * 1024 * 1024;

/** Levels ConsolePanel stores for script/agent/engine output (see ConsolePanel.hpp). */
const std::vector<std::string>& consoleLevels() {
    static const std::vector<std::string> levels{"debug", "info", "warn", "error", "print", "cmd", "result", "engine"};
    return levels;
}

/** Levels an agent marker may use; it must not impersonate a game print/result. */
const std::vector<std::string>& markerLevels() {
    static const std::vector<std::string> levels{"debug", "info", "warn", "error"};
    return levels;
}

bool isListed(const std::vector<std::string>& allowed, const std::string& value) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

std::string joined(const std::vector<std::string>& values) {
    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) out += '|';
        out += value;
    }
    return out;
}

/** Console cursors are non-negative; a negative request means "from the start". */
std::int64_t getArgSeq(Poco::JSON::Object::Ptr args, const char* key) {
    const long long value = getArgInt64(args, key, 0);
    return value > 0 ? static_cast<std::int64_t>(value) : 0;
}

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", false);
    out->set("error", message);
    return textPayload(out, true);
}

std::string consoleRead(Poco::JSON::Object::Ptr args) {
    const std::string level = getArgString(args, "level");
    if (!level.empty() && !isListed(consoleLevels(), level)) {
        return errorPayload("unknown level '" + level + "'; expected one of " + joined(consoleLevels()));
    }
    const int           limit    = std::clamp(getArgInt(args, "limit", kDefaultConsoleRead), 1, kMaxConsoleRead);
    const std::uint64_t afterSeq = static_cast<std::uint64_t>(getArgSeq(args, "sinceSeq"));

    const ConsoleSlice slice = ConsolePanel::instance().read(afterSeq, static_cast<std::size_t>(limit), level);

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("attached", ConsolePanel::instance().isAttached());
    out->set("stderrCaptured", ConsolePanel::instance().isCapturingStderr());
    out->set("firstSeq", static_cast<Poco::Int64>(slice.firstSeq));
    // `cursor` is the resumable value for the next sinceSeq; `nextSeq` is only
    // the seq the next appended line will get, which is not a valid cursor.
    out->set("cursor", static_cast<Poco::Int64>(slice.cursor));
    out->set("nextSeq", static_cast<Poco::Int64>(slice.nextSeq));
    out->set("droppedThrough", static_cast<Poco::Int64>(slice.droppedThrough));
    out->set("truncated", slice.truncated);
    out->set("count", static_cast<int>(slice.lines.size()));
    Poco::JSON::Array::Ptr lines = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& line : slice.lines) {
        Poco::JSON::Object::Ptr item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        item->set("seq", static_cast<Poco::Int64>(line.seq));
        item->set("time", line.timestamp);
        item->set("level", line.level);
        item->set("text", line.text);
        lines->add(item);
    }
    out->set("lines", lines);
    return textPayload(out);
}

std::string consoleWrite(Poco::JSON::Object::Ptr args) {
    const std::string text = getArgString(args, "text");
    if (text.empty()) return errorPayload("missing text");
    const std::string level = getArgString(args, "level", "info");
    if (!isListed(markerLevels(), level)) {
        return errorPayload("unknown level '" + level + "'; expected one of " + joined(markerLevels()));
    }

    ConsolePanel& console = ConsolePanel::instance();
    console.addLog(level, text);

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("seq", static_cast<Poco::Int64>(console.nextSeq() - 1));
    out->set("nextSeq", static_cast<Poco::Int64>(console.nextSeq()));
    return textPayload(out);
}

std::string consoleClear() {
    ConsolePanel& console = ConsolePanel::instance();
    console.clear();

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("nextSeq", static_cast<Poco::Int64>(console.nextSeq()));
    out->set("droppedThrough", static_cast<Poco::Int64>(console.droppedThrough()));
    return textPayload(out);
}

// ---------------------------------------------------------------------------
// Crash / previous-session evidence
//
// The server dies with the process it serves, so "why did the last run die" is
// only answerable from the persistent log written by common/CrashLog.h.
// ---------------------------------------------------------------------------

// One window is enough to answer the question; a runaway log is never loaded
// whole.
constexpr std::size_t kCrashLogWindowBytes = 512 * 1024;

struct CrashLogScan {
    bool                     exists       = false;
    std::uint64_t            bytes        = 0;
    std::uint64_t            scannedBytes = 0;
    int                      sessions     = 0;
    int                      crashes      = 0;
    std::string              lastCrashAt;
    bool                     hasPreviousSession     = false;
    bool                     previousSessionEnded   = false;
    bool                     previousSessionCrashed = false;
    std::vector<std::string> tail;
};

/** One session segment delimited by its start marker. */
struct SessionSegment {
    bool ended   = false;
    bool crashed = false;
};

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

/** Read at most the last kCrashLogWindowBytes of the log, dropping a split head line. */
std::string readLogWindow(const std::string& path, std::uint64_t* totalBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) return {};
    *totalBytes = static_cast<std::uint64_t>(size);

    const std::streamoff window = static_cast<std::streamoff>(kCrashLogWindowBytes);
    const std::streamoff start  = size > window ? size - window : 0;
    in.seekg(start);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (start > 0) {
        const auto newline = text.find('\n');
        text.erase(0, newline == std::string::npos ? text.size() : newline + 1);
    }
    return text;
}

CrashLogScan scanCrashLog(int tailLines) {
    CrashLogScan      scan;
    const std::string path = eve::crashLogPath();
    std::error_code   ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return scan;
    scan.exists = true;

    const std::string window = readLogWindow(path, &scan.bytes);
    scan.scannedBytes        = window.size();

    std::istringstream          stream(window);
    std::string                 line;
    std::vector<std::string>    all;
    std::vector<SessionSegment> segments;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        all.push_back(line);
        if (contains(line, eve::kSessionStartMarker)) {
            ++scan.sessions;
            segments.emplace_back();
            continue;
        }
        if (segments.empty()) continue;  // pre-session noise in a truncated window
        if (contains(line, eve::kSessionEndMarker)) {
            segments.back().ended = true;
            continue;
        }
        if (contains(line, "] crash | ")) {
            ++scan.crashes;
            segments.back().crashed = true;
            const auto open         = line.find('[');
            const auto close        = line.find(']');
            if (open != std::string::npos && close != std::string::npos && close > open + 1)
                scan.lastCrashAt = line.substr(open + 1, close - open - 1);
        }
    }

    // The newest start marker belongs to the session that is reading this log, so
    // the interesting verdict is about the one before it: "did the run that came
    // before me finish, or die?".
    if (segments.size() >= 2) {
        const SessionSegment& previous = segments[segments.size() - 2];
        scan.hasPreviousSession        = true;
        scan.previousSessionEnded      = previous.ended;
        scan.previousSessionCrashed    = previous.crashed;
    }

    const std::size_t keep = tailLines > 0 ? static_cast<std::size_t>(tailLines) : 0;
    const std::size_t from = all.size() > keep ? all.size() - keep : 0;
    scan.tail.assign(all.begin() + static_cast<std::ptrdiff_t>(from), all.end());
    return scan;
}

Poco::JSON::Object::Ptr crashLogScanObject(const CrashLogScan& scan) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("path", eve::crashLogPath());
    out->set("exists", scan.exists);
    out->set("bytes", static_cast<Poco::Int64>(scan.bytes));
    out->set("scannedBytes", static_cast<Poco::Int64>(scan.scannedBytes));
    out->set("sessions", scan.sessions);
    out->set("crashes", scan.crashes);
    if (!scan.lastCrashAt.empty()) out->set("lastCrashAt", scan.lastCrashAt);
    if (scan.hasPreviousSession) {
        out->set("previousSessionEnded", scan.previousSessionEnded);
        out->set("previousSessionCrashed", scan.previousSessionCrashed);
    }
    return out;
}

std::string crashReport(Poco::JSON::Object::Ptr args) {
    const int    lines = std::clamp(getArgInt(args, "lines", 200), 1, 2000);
    CrashLogScan scan  = scanCrashLog(lines);

    Poco::JSON::Object::Ptr out = crashLogScanObject(scan);
    out->set("ok", true);
    Poco::JSON::Array::Ptr tail = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& line : scan.tail) tail->add(line);
    out->set("tail", tail);
    if (!scan.exists)
        out->set("message",
                 "no crash/error log yet; common/CrashLog.h opens it at process start (EVE_LOG_DIR overrides the "
                 "directory)");
    return textPayload(out);
}

std::string screenshotImage(Poco::JSON::Object::Ptr args) {
    auto* capture = eve::cap::query<eve::IRenderCapture>();
    if (!capture) return errorPayload("graphics module not available");

    const int budget =
        std::clamp(getArgInt(args, "maxBytes", kDefaultScreenshotBytes), kMinScreenshotBytes, kMaxScreenshotBytes);

    static const std::string kDataUrlPrefix = "data:image/png;base64,";
    const std::string        dataUrl        = capture->capturePngDataUrl();
    if (dataUrl.size() <= kDataUrlPrefix.size() || dataUrl.compare(0, kDataUrlPrefix.size(), kDataUrlPrefix) != 0) {
        // Enabling readback and reading it back cannot happen inside one
        // presented frame: the swapchain copy is recorded by the next present.
        // This is a retryable state, not a failed call, so isError stays false.
        Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        out->set("ok", false);
        out->set("retryable", true);
        out->set("reason", "no-presented-frame");
        out->set("readbackEnabled", capture->status().readbackEnabled);
        out->set("message",
                 "readback was just enabled or no frame has been presented yet; wait for one rendered "
                 "frame and call again (eve_render_status.readbackEnabled flips to true)");
        return textPayload(out);
    }

    const std::string payload = dataUrl.substr(kDataUrlPrefix.size());
    if (static_cast<int>(payload.size()) > budget) {
        Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        out->set("ok", false);
        out->set("retryable", false);
        out->set("reason", "image-too-large");
        out->set("base64Bytes", static_cast<Poco::Int64>(payload.size()));
        out->set("maxBytes", budget);
        out->set("message", "raise maxBytes or use eve_screenshot with a path");
        return textPayload(out);
    }

    const eve::RenderStatusInfo status  = capture->status();
    Poco::JSON::Object::Ptr     caption = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    caption->set("ok", true);
    caption->set("mimeType", "image/png");
    caption->set("width", status.width);
    caption->set("height", status.height);
    caption->set("base64Bytes", static_cast<Poco::Int64>(payload.size()));
    caption->set("backend", status.backend);
    return imageContentResult(payload, "image/png", mcpStringify(Poco::Dynamic::Var(caption)));
}

}  // namespace

bool isMcpRuntimeTool(std::string_view name) {
    return name == "eve_console_read" || name == "eve_console_write" || name == "eve_console_clear" ||
           name == "eve_screenshot_image" || name == "eve_crash_report";
}

std::string callMcpRuntimeTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_console_read") return consoleRead(args);
    if (name == "eve_console_write") return consoleWrite(args);
    if (name == "eve_console_clear") return consoleClear();
    if (name == "eve_screenshot_image") return screenshotImage(args);
    if (name == "eve_crash_report") return crashReport(args);
    return errorPayload("unknown runtime tool '" + std::string(name) + "'");
}

Poco::JSON::Object::Ptr crashLogSummary() {
    // Cheap by design: `eve_status` must not read the whole log. Counts and the
    // tail come from eve_crash_report.
    Poco::JSON::Object::Ptr out  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    const std::string       path = eve::crashLogPath();
    out->set("path", path);
    std::error_code ec;
    const bool      exists = !path.empty() && std::filesystem::exists(path, ec);
    out->set("exists", exists);
    Poco::Int64 bytes = 0;
    if (exists) {
        const auto size = std::filesystem::file_size(path, ec);
        if (!ec) bytes = static_cast<Poco::Int64>(size);
    }
    out->set("bytes", bytes);
    return out;
}

std::string_view mcpRuntimeToolSchemas() {
    return R"json({"name":"eve_console_read","description":"Read retained runtime console lines: Squirrel print/error, engine stderr (level engine), eve.dev.console.* and agent markers. Lines carry a monotonic seq; pass the returned cursor back as sinceSeq for an incremental read (sinceSeq=0 reads the newest lines). truncated=true means lines were dropped before sinceSeq.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":1000},"sinceSeq":{"type":"integer","minimum":0},"level":{"type":"string","enum":["debug","info","warn","error","print","cmd","result","engine"]}}}},{"name":"eve_console_write","description":"Append one agent marker line to the runtime console so it shares the game's ordered diagnosis stream.","inputSchema":{"type":"object","properties":{"text":{"type":"string"},"level":{"type":"string","enum":["debug","info","warn","error"]}},"required":["text"]}},{"name":"eve_console_clear","description":"Drop retained console lines. The sequence cursor keeps advancing, so an existing sinceSeq never re-reads or stalls.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_screenshot_image","description":"Capture the presented frame and return it as an MCP image content item, so a client without filesystem access can look at the game. After readback is enabled the first call reports ok=false with retryable=true; wait one rendered frame and call again.","inputSchema":{"type":"object","properties":{"maxBytes":{"type":"integer","minimum":1024,"maximum":33554432,"description":"base64 payload budget (default 2097152)"}}}},{"name":"eve_crash_report","description":"Explain why a previous run ended: reads the persistent eve.log written by common/CrashLog.h and returns session/crash counts (window-scoped), the last crash timestamp, whether the session before the current one ended cleanly or crashed, and a bounded tail. Use it after reconnecting to a restarted game, since the MCP server dies with the process it served.","inputSchema":{"type":"object","properties":{"lines":{"type":"integer","minimum":1,"maximum":2000,"description":"tail lines to return (default 200)"}}}})json";
}

}  // namespace eve::dev
