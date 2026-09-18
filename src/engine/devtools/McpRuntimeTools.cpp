#include "devtools/McpRuntimeTools.hpp"

#include "devtools/ConsolePanel.hpp"
#include "devtools/McpJson.hpp"

#include "common/Capability.h"
#include "common/RenderCapture.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::dev {
namespace {

constexpr int kDefaultConsoleRead = 200;
constexpr int kMaxConsoleRead     = 1000;

constexpr int kDefaultScreenshotBytes = 2 * 1024 * 1024;
constexpr int kMinScreenshotBytes     = 1024;
constexpr int kMaxScreenshotBytes     = 32 * 1024 * 1024;

/** Levels ConsolePanel stores for script/agent output (see ConsolePanel.hpp). */
const std::vector<std::string>& consoleLevels() {
    static const std::vector<std::string> levels{"debug", "info", "warn", "error", "print", "cmd", "result"};
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

std::string argString(Poco::JSON::Object::Ptr args, const char* key, const std::string& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<std::string>();
    } catch (...) {
        return def;
    }
}

int argInt(Poco::JSON::Object::Ptr args, const char* key, int def) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<int>();
    } catch (...) {
        return def;
    }
}

std::int64_t argSeq(Poco::JSON::Object::Ptr args, const char* key) {
    if (!args || !args->has(key)) return 0;
    try {
        const std::int64_t value = args->get(key).convert<Poco::Int64>();
        return value > 0 ? value : 0;
    } catch (...) {
        return 0;
    }
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
    const std::string level = argString(args, "level");
    if (!level.empty() && !isListed(consoleLevels(), level)) {
        return errorPayload("unknown level '" + level + "'; expected one of " + joined(consoleLevels()));
    }
    const int           limit    = std::clamp(argInt(args, "limit", kDefaultConsoleRead), 1, kMaxConsoleRead);
    const std::uint64_t afterSeq = static_cast<std::uint64_t>(argSeq(args, "sinceSeq"));

    const ConsoleSlice slice = ConsolePanel::instance().read(afterSeq, static_cast<std::size_t>(limit), level);

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("attached", ConsolePanel::instance().isAttached());
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
    const std::string text = argString(args, "text");
    if (text.empty()) return errorPayload("missing text");
    const std::string level = argString(args, "level", "info");
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

std::string screenshotImage(Poco::JSON::Object::Ptr args) {
    auto* capture = eve::cap::query<eve::IRenderCapture>();
    if (!capture) return errorPayload("graphics module not available");

    const int budget =
        std::clamp(argInt(args, "maxBytes", kDefaultScreenshotBytes), kMinScreenshotBytes, kMaxScreenshotBytes);

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
           name == "eve_screenshot_image";
}

std::string callMcpRuntimeTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_console_read") return consoleRead(args);
    if (name == "eve_console_write") return consoleWrite(args);
    if (name == "eve_console_clear") return consoleClear();
    if (name == "eve_screenshot_image") return screenshotImage(args);
    return errorPayload("unknown runtime tool '" + std::string(name) + "'");
}

std::string_view mcpRuntimeToolSchemas() {
    return R"json({"name":"eve_console_read","description":"Read retained runtime console lines (Squirrel print/error, eve.dev.console.* and agent markers). Lines carry a monotonic seq; pass the returned cursor back as sinceSeq for an incremental read (sinceSeq=0 reads the newest lines). truncated=true means lines were dropped before sinceSeq.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":1000},"sinceSeq":{"type":"integer","minimum":0},"level":{"type":"string","enum":["debug","info","warn","error","print","cmd","result"]}}}},{"name":"eve_console_write","description":"Append one agent marker line to the runtime console so it shares the game's ordered diagnosis stream.","inputSchema":{"type":"object","properties":{"text":{"type":"string"},"level":{"type":"string","enum":["debug","info","warn","error"]}},"required":["text"]}},{"name":"eve_console_clear","description":"Drop retained console lines. The sequence cursor keeps advancing, so an existing sinceSeq never re-reads or stalls.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_screenshot_image","description":"Capture the presented frame and return it as an MCP image content item, so a client without filesystem access can look at the game. After readback is enabled the first call reports ok=false with retryable=true; wait one rendered frame and call again.","inputSchema":{"type":"object","properties":{"maxBytes":{"type":"integer","minimum":1024,"maximum":33554432,"description":"base64 payload budget (default 2097152)"}}}})json";
}

}  // namespace eve::dev
