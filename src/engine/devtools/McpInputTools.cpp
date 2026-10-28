#include "devtools/McpInputTools.hpp"
#include "devtools/McpToolFamilies.hpp"

#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include "common/Capability.h"
#include "common/InputInjection.h"
#include "common/RenderCapture.h"
#include "common/Result.h"
#include "common/Status.h"
#include "common/UIAutomation.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {
namespace {

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message, const std::string& hint = {},
                         const std::vector<std::string>& nextTools = {}) {
    return textPayload(mcpFailureObject(message, hint, false, nextTools), true);
}

std::string unavailablePayload(const char* capability) {
    Poco::JSON::Object::Ptr out = mcpFailureObject(
        std::string(capability) + " is not provided by the loaded modules",
        "Load the platform_event (and graphics for capture) modules, or start a live eve run session", false,
        {"eve_status"});
    out->set("unavailable", capability);
    return textPayload(out);
}

struct PendingBatch {
    std::int64_t id            = 0;
    int          framesLeft    = 0;
    bool         wantCapture   = false;
    std::string  capturePath;
    bool         done          = false;
    Poco::JSON::Object::Ptr result;
};

std::mutex                g_mu;
std::int64_t              g_nextId = 1;
std::vector<PendingBatch> g_pending;
std::vector<Poco::JSON::Object::Ptr> g_receipts;  // completed results keyed by id in object

Poco::JSON::Object::Ptr findReceipt(std::int64_t id) {
    for (auto& receipt : g_receipts) {
        if (receipt && receipt->has("id") && receipt->getValue<Poco::Int64>("id") == id) return receipt;
    }
    return {};
}

std::string diagnosticMessage(const eve::Status& status) {
    const auto* diag = status.primaryDiagnostic();
    if (diag && !diag->message().empty()) return diag->message();
    return std::string(statusCodeName(status.code()));
}

std::string applyOneAction(Poco::JSON::Object::Ptr action, Poco::JSON::Object::Ptr receipt) {
    if (!action || !action->has("op")) return "action.op is required";
    std::string op;
    try {
        op = action->get("op").convert<std::string>();
    } catch (...) {
        return "action.op must be a string";
    }

    auto* input = eve::cap::query<eve::IInputInjection>();
    auto* ui    = eve::cap::query<eve::IUIAutomation>();

    if (op == "wait") {
        const int frames = std::max(0, getArgInt(action, "frames", 1));
        receipt->set("waitFrames", receipt->has("waitFrames")
                                       ? receipt->getValue<int>("waitFrames") + frames
                                       : frames);
        return {};
    }

    if (op == "key") {
        if (!input) return std::string(eve::IInputInjection::capabilityName) + " unavailable";
        const std::string key = getArgString(action, "key");
        const bool        down = getArgBool(action, "down", true);
        const bool        repeat = getArgBool(action, "repeat", false);
        auto              result = input->injectKey(key, down, repeat);
        if (!result) return diagnosticMessage(result.status());
        return {};
    }
    if (op == "mouse_move") {
        if (!input) return std::string(eve::IInputInjection::capabilityName) + " unavailable";
        auto result = input->injectMouseMove(getArgFloat(action, "x"), getArgFloat(action, "y"),
                                             getArgFloat(action, "dx"), getArgFloat(action, "dy"));
        if (!result) return diagnosticMessage(result.status());
        return {};
    }
    if (op == "mouse_button") {
        if (!input) return std::string(eve::IInputInjection::capabilityName) + " unavailable";
        auto result = input->injectMouseButton(getArgInt(action, "button", 1), getArgBool(action, "down", true),
                                               getArgFloat(action, "x"), getArgFloat(action, "y"));
        if (!result) return diagnosticMessage(result.status());
        return {};
    }
    if (op == "wheel") {
        if (!input) return std::string(eve::IInputInjection::capabilityName) + " unavailable";
        auto result = input->injectWheel(getArgFloat(action, "x"), getArgFloat(action, "y", -1.f));
        if (!result) return diagnosticMessage(result.status());
        return {};
    }
    if (op == "text") {
        if (!input) return std::string(eve::IInputInjection::capabilityName) + " unavailable";
        auto result = input->injectText(getArgString(action, "text"));
        if (!result) return diagnosticMessage(result.status());
        return {};
    }
    if (op == "ui_click") {
        if (!ui) return std::string(eve::IUIAutomation::capabilityName) + " unavailable";
        const std::string host   = getArgString(action, "host");
        const std::string widget = getArgString(action, "widget");
        if (widget.empty()) return "ui_click requires widget";
        const std::string out = ui->click(host, widget);
        if (out.rfind("error:", 0) == 0) return out.substr(6);
        return {};
    }
    return "unknown action.op '" + op + "' (key|mouse_move|mouse_button|wheel|text|ui_click|wait)";
}

void finishCapture(PendingBatch& batch) {
    Poco::JSON::Object::Ptr out = batch.result ? batch.result : Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("id", static_cast<Poco::Int64>(batch.id));
    out->set("status", "ready");
    if (!batch.wantCapture) {
        out->set("ok", true);
        batch.result = out;
        batch.done   = true;
        return;
    }
    auto* capture = eve::cap::query<eve::IRenderCapture>();
    if (!capture) {
        out->set("ok", false);
        out->set("error", std::string(eve::IRenderCapture::capabilityName) + " unavailable");
        out->set("hint", "Wait until graphics has presented a frame, then eve_screenshot / eve_input_poll");
        Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        next->add("eve_render_status");
        next->add("eve_screenshot");
        out->set("nextTools", next);
        batch.result = out;
        batch.done   = true;
        return;
    }
    std::string path = batch.capturePath.empty() ? "mcp_input_capture.png" : batch.capturePath;
    auto        saved = capture->savePng(path);
    if (!saved) {
        const auto& status = saved.status();
        out->set("ok", false);
        out->set("retryable", true);
        out->set("reason", "no-presented-frame");
        out->set("error", diagnosticMessage(status));
        out->set("hint", "Call eve_input_poll again after one rendered frame, or use eve_screenshot");
        Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        next->add("eve_input_poll");
        next->add("eve_screenshot");
        out->set("nextTools", next);
        batch.result = out;
        batch.done   = true;
        return;
    }
    out->set("ok", true);
    out->set("captured", true);
    out->set("path", path);
    out->set("width", saved.value().width);
    out->set("height", saved.value().height);
    batch.result = out;
    batch.done   = true;
}

std::string inputStatus() {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("injection", eve::cap::query<eve::IInputInjection>() != nullptr);
    out->set("ui", eve::cap::query<eve::IUIAutomation>() != nullptr);
    out->set("capture", eve::cap::query<eve::IRenderCapture>() != nullptr);
    std::lock_guard lock(g_mu);
    out->set("pending", static_cast<int>(g_pending.size()));
    out->set("completed", static_cast<int>(g_receipts.size()));
    return textPayload(out);
}

std::string inputPoll(Poco::JSON::Object::Ptr args) {
    const std::int64_t id = getArgInt64(args, "id", 0);
    std::lock_guard    lock(g_mu);
    if (id > 0) {
        if (auto receipt = findReceipt(id)) return textPayload(receipt);
        for (auto& batch : g_pending) {
            if (batch.id == id) {
                Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
                out->set("ok", true);
                out->set("status", "pending");
                out->set("id", static_cast<Poco::Int64>(id));
                out->set("framesLeft", batch.framesLeft);
                out->set("hint", "Keep calling eve_input_poll each frame until status=ready");
                Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
                next->add("eve_input_poll");
                out->set("nextTools", next);
                return textPayload(out);
            }
        }
        return errorPayload("unknown input batch id " + std::to_string(id),
                            "Call eve_input_batch first and reuse the returned id", {"eve_input_batch"});
    }
    if (!g_receipts.empty()) return textPayload(g_receipts.back());
    if (!g_pending.empty()) {
        Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        out->set("ok", true);
        out->set("status", "pending");
        out->set("id", static_cast<Poco::Int64>(g_pending.back().id));
        out->set("framesLeft", g_pending.back().framesLeft);
        out->set("hint", "Keep calling eve_input_poll each frame until status=ready");
        return textPayload(out);
    }
    return errorPayload("no input batch to poll", "Submit actions with eve_input_batch", {"eve_input_batch"});
}

std::string inputBatch(Poco::JSON::Object::Ptr args) {
    if (!args || !args->has("actions")) {
        return errorPayload("actions array is required",
                            "Pass actions:[{op:key|mouse_move|mouse_button|wheel|text|ui_click|wait},...]",
                            {"eve_input_status"});
    }
    Poco::JSON::Array::Ptr actions;
    try {
        actions = args->getArray("actions");
    } catch (...) {
        return errorPayload("actions must be a JSON array");
    }
    if (!actions || actions->size() == 0) return errorPayload("actions must not be empty");

    if (!eve::cap::query<eve::IInputInjection>() && !eve::cap::query<eve::IUIAutomation>()) {
        return unavailablePayload(eve::IInputInjection::capabilityName);
    }

    Poco::JSON::Object::Ptr receipt = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    receipt->set("injected", 0);
    Poco::JSON::Array::Ptr steps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    int                    waitFrames = 0;

    for (std::size_t i = 0; i < actions->size(); ++i) {
        Poco::JSON::Object::Ptr action;
        try {
            action = actions->getObject(static_cast<unsigned>(i));
        } catch (...) {
            return errorPayload("actions[" + std::to_string(i) + "] must be an object");
        }
        const int beforeWait = receipt->has("waitFrames") ? receipt->getValue<int>("waitFrames") : 0;
        const std::string err = applyOneAction(action, receipt);
        Poco::JSON::Object::Ptr step = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        step->set("index", static_cast<int>(i));
        if (action && action->has("op")) step->set("op", action->get("op"));
        if (!err.empty()) {
            step->set("ok", false);
            step->set("error", err);
            steps->add(step);
            receipt->set("ok", false);
            receipt->set("error", err);
            receipt->set("steps", steps);
            receipt->set("hint", "Fix the failing action; key names use SDL_GetKeyName spelling");
            Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            next->add("eve_input_status");
            receipt->set("nextTools", next);
            return textPayload(receipt, true);
        }
        step->set("ok", true);
        steps->add(step);
        const int afterWait = receipt->has("waitFrames") ? receipt->getValue<int>("waitFrames") : 0;
        if (afterWait == beforeWait) receipt->set("injected", receipt->getValue<int>("injected") + 1);
        waitFrames = afterWait;
    }

    const bool wantCapture = getArgBool(args, "capture", false);
    std::string capturePath = getArgString(args, "capturePath", "mcp_input_capture.png");
    // Top-level waitFrames adds to action waits (Godot-style batch trailer).
    waitFrames += std::max(0, getArgInt(args, "waitFrames", 0));

    receipt->set("steps", steps);
    receipt->set("waitFrames", waitFrames);
    receipt->set("capture", wantCapture);

    if (waitFrames <= 0 && !wantCapture) {
        receipt->set("ok", true);
        receipt->set("status", "ready");
        receipt->set("hint", "Events are queued for the next PlatformEvent::pump; compose with eve_play step "
                             "or waitFrames/capture for a closed loop");
        Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        next->add("eve_play");
        next->add("eve_screenshot");
        receipt->set("nextTools", next);
        return textPayload(receipt);
    }

    PendingBatch batch;
    {
        std::lock_guard lock(g_mu);
        batch.id          = g_nextId++;
        batch.framesLeft  = waitFrames;
        batch.wantCapture = wantCapture;
        batch.capturePath = std::move(capturePath);
        batch.result      = receipt;
        if (batch.framesLeft <= 0) {
            finishCapture(batch);
            g_receipts.push_back(batch.result);
            return textPayload(batch.result);
        }
        g_pending.push_back(batch);
    }

    receipt->set("ok", true);
    receipt->set("status", "pending");
    receipt->set("id", static_cast<Poco::Int64>(batch.id));
    receipt->set("hint", "Call eve_input_poll after each game frame until status=ready");
    Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    next->add("eve_input_poll");
    receipt->set("nextTools", next);
    return textPayload(receipt);
}

}  // namespace

bool isMcpInputTool(std::string_view name) {
    return name == "eve_input_batch" || name == "eve_input_poll" || name == "eve_input_status";
}

std::string callMcpInputTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_input_status") return inputStatus();
    if (name == "eve_input_poll") return inputPoll(args);
    if (name == "eve_input_batch") return inputBatch(args);
    return errorPayload("unknown input tool '" + std::string(name) + "'");
}

std::string_view mcpInputToolSchemas() {
    return R"json({"name":"eve_input_status","description":"Whether IInputInjection / IUIAutomation / IRenderCapture are available and how many input batches are pending.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_input_batch","description":"Inject a playtest action sequence (key/mouse_move/mouse_button/wheel/text/ui_click/wait) through IInputInjection + IUIAutomation. Optional waitFrames/capture completes across subsequent McpServer::poll ticks — call eve_input_poll until status=ready. Does not block the game loop.","inputSchema":{"type":"object","properties":{"actions":{"type":"array","items":{"type":"object","properties":{"op":{"type":"string","enum":["key","mouse_move","mouse_button","wheel","text","ui_click","wait"]},"key":{"type":"string"},"down":{"type":"boolean"},"repeat":{"type":"boolean"},"button":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"dx":{"type":"number"},"dy":{"type":"number"},"text":{"type":"string"},"host":{"type":"string"},"widget":{"type":"string"},"frames":{"type":"integer"}},"required":["op"]}},"waitFrames":{"type":"integer","minimum":0},"capture":{"type":"boolean"},"capturePath":{"type":"string"}},"required":["actions"]}},{"name":"eve_input_poll","description":"Poll a pending eve_input_batch wait/capture. Pass id from the batch response (omit to use the latest). Returns status=pending|ready with optional PNG path.","inputSchema":{"type":"object","properties":{"id":{"type":"integer","minimum":1}}}})json";
}

void pollMcpInputBatches() {
    std::vector<PendingBatch> finished;
    {
        std::lock_guard lock(g_mu);
        for (auto& batch : g_pending) {
            if (batch.framesLeft > 0) --batch.framesLeft;
            if (batch.framesLeft <= 0) {
                finishCapture(batch);
                finished.push_back(batch);
            }
        }
        g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(),
                                       [](const PendingBatch& b) { return b.framesLeft <= 0 && b.done; }),
                        g_pending.end());
        for (auto& batch : finished) g_receipts.push_back(batch.result);
        constexpr std::size_t kMaxReceipts = 32;
        if (g_receipts.size() > kMaxReceipts) {
            g_receipts.erase(g_receipts.begin(), g_receipts.end() - static_cast<std::ptrdiff_t>(kMaxReceipts));
        }
    }
}

void resetMcpInputBatchesForTest() {
    std::lock_guard lock(g_mu);
    g_pending.clear();
    g_receipts.clear();
    g_nextId = 1;
}

}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(Input, mcpInputToolSchemas, callMcpInputTool, ContentEnvelope)
}  // namespace eve::dev
