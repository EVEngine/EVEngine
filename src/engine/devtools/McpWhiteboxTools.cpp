#include "devtools/McpWhiteboxTools.hpp"
#include "devtools/McpToolFamilies.hpp"

#include "devtools/Debugger.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpInputTools.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpSceneDirector.hpp"
#include "devtools/McpSceneLiteral.hpp"
#include "devtools/PlayHost.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <string>
#include <string_view>

namespace eve::dev {
namespace {

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message, const std::string& hint = {}) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", false);
    out->set("error", message);
    if (message == "deferred-to-update") out->set("retryable", true);
    if (!hint.empty()) out->set("hint", hint);
    return textPayload(out, true);
}

/** Unwrap satellite tools/call envelopes to the inner JSON object text. */
Poco::JSON::Object::Ptr parseToolTextObject(const std::string& envelopeOrText) {
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(envelopeOrText);
        if (root.type() == typeid(Poco::JSON::Object::Ptr)) {
            auto object = root.extract<Poco::JSON::Object::Ptr>();
            if (object && object->has("content")) {
                auto content = object->getArray("content");
                if (content && content->size() > 0) {
                    auto item = content->getObject(0);
                    if (item && item->has("text")) {
                        auto inner = parser.parse(item->getValue<std::string>("text"));
                        if (inner.type() == typeid(Poco::JSON::Object::Ptr))
                            return inner.extract<Poco::JSON::Object::Ptr>();
                    }
                }
            }
            return object;
        }
    } catch (...) {
    }
    return {};
}

Poco::JSON::Object::Ptr parseJsonObject(const std::string& text) {
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(text);
        if (root.type() == typeid(Poco::JSON::Object::Ptr)) return root.extract<Poco::JSON::Object::Ptr>();
    } catch (...) {
    }
    return {};
}

std::string whiteboxPlaytest(Poco::JSON::Object::Ptr args) {
    if (!args) {
        return errorPayload("recipe object is required",
                            "Pass {reset?, scene:[...], inputs?, waitFrames?, capture?, capturePath?, play?}");
    }

    Debugger&   dbg = Debugger::instance();
    HSQUIRRELVM vm  = dbg.vm();
    if (!vm) return errorPayload("no VM", "Attach a live eve run --mcp-port session first");

    std::string err;
    if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) {
        if (err == "deferred-to-update") {
            return errorPayload(
                "deferred-to-update",
                "scene_director install was queued for the next eve.dev.poll; retry eve_whitebox_playtest");
        }
        return errorPayload(err.empty() ? "scene_director kit unavailable" : err);
    }

    Poco::JSON::Object::Ptr report = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    report->set("schemaId", "eve.whitebox.playtest-report");
    report->set("schemaVersion", 1);

    const bool doReset = getArgBool(args, "reset", true);
    if (doReset) {
        const std::string resetOut = callSceneDirectorReturn(vm, "return ::scene_director.reset();", &err);
        if (!err.empty()) return errorPayload(err);
        if (auto resetObj = parseJsonObject(resetOut))
            report->set("reset", resetObj);
        else
            report->set("reset", resetOut);
    }

    if (args->has("scene")) {
        Poco::Dynamic::Var actionsVar;
        try {
            actionsVar = Poco::Dynamic::Var(args->getArray("scene"));
        } catch (...) {
            return errorPayload("scene must be a JSON array of {action,target?,params?}");
        }
        Poco::JSON::Object::Ptr options = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        options->set("stopOnError", getArgBool(args, "stopOnError", true));
        auto actionsLiteral = sceneLiteral(actionsVar);
        if (!actionsLiteral.ok()) return errorPayload(actionsLiteral.status().describe());
        auto optionsLiteral = sceneLiteral(Poco::Dynamic::Var(options));
        if (!optionsLiteral.ok()) return errorPayload(optionsLiteral.status().describe());
        err.clear();
        const std::string snippet =
            "return ::scene_director.batch(" + actionsLiteral.value() + "," + optionsLiteral.value() + ");";
        const std::string sceneOut = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return errorPayload(err);
        auto sceneObj = parseJsonObject(sceneOut);
        if (!sceneObj) return errorPayload("scene batch returned non-object JSON", sceneOut);
        report->set("scene", sceneObj);
        if (!sceneObj->optValue<bool>("ok", false)) {
            report->set("ok", false);
            report->set("error", "scene batch failed");
            report->set("hint", "Inspect scene.results for the first failing white-box action");
            return textPayload(report, true);
        }
    }

    if (args->has("inputs")) {
        Poco::JSON::Object::Ptr inputArgs = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        try {
            inputArgs->set("actions", args->getArray("inputs"));
        } catch (...) {
            return errorPayload("inputs must be a JSON array of eve_input_batch actions");
        }
        if (args->has("waitFrames")) inputArgs->set("waitFrames", getArgInt(args, "waitFrames", 0));
        if (args->has("capture")) inputArgs->set("capture", getArgBool(args, "capture", false));
        if (args->has("capturePath"))
            inputArgs->set("capturePath", getArgString(args, "capturePath", "wb_playtest.png"));
        auto inputObj = parseToolTextObject(callMcpInputTool("eve_input_batch", inputArgs));
        if (!inputObj) return errorPayload("input batch returned an unexpected payload");
        report->set("input", inputObj);
        if (!inputObj->optValue<bool>("ok", false)) {
            report->set("ok", false);
            report->set("error", "input batch failed");
            report->set("hint", "Fix inputs[] action ops; then call eve_input_poll if status=pending");
            return textPayload(report, true);
        }
    }

    if (args->has("play")) {
        Poco::JSON::Array::Ptr playRequests;
        try {
            playRequests = args->getArray("play");
        } catch (...) {
            return errorPayload("play must be a JSON array of eve_play request objects");
        }
        Poco::JSON::Object::Ptr batchReq = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        batchReq->set("schemaId", "evengine.play-request");
        batchReq->set("schemaVersion", 1);
        batchReq->set("op", "batch");
        Poco::JSON::Array::Ptr wrapped = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (std::size_t i = 0; i < playRequests->size(); ++i) {
            Poco::JSON::Object::Ptr one;
            try {
                one = playRequests->getObject(static_cast<unsigned>(i));
            } catch (...) {
                return errorPayload("play[" + std::to_string(i) + "] must be an object");
            }
            if (!one) return errorPayload("play[" + std::to_string(i) + "] must be an object");
            if (!one->has("schemaId")) one->set("schemaId", "evengine.play-request");
            if (!one->has("schemaVersion")) one->set("schemaVersion", 1);
            wrapped->add(one);
        }
        batchReq->set("requests", wrapped);
        auto playResponse = executePlayJson(mcpStringify(Poco::Dynamic::Var(batchReq)));
        if (!playResponse) {
            report->set("ok", false);
            report->set("error", playResponse.status().describe());
            report->set("hint", "Play Host requires game.agent.json for observe/act; use clock/step/status freely");
            return textPayload(report, true);
        }
        auto playObj = parseJsonObject(playResponse.value());
        if (playObj)
            report->set("play", playObj);
        else
            report->set("play", playResponse.value());
    }

    // Fresh scene truth for assertions / agent follow-up.
    err.clear();
    const std::string infoOut = callSceneDirectorReturn(vm, "return ::scene_director.info();", &err);
    if (err.empty()) {
        if (auto info = parseJsonObject(infoOut)) report->set("info", info);
    }

    report->set("ok", true);
    if (report->has("input")) {
        auto input = report->getObject("input");
        if (input && input->optValue<std::string>("status", "") == "pending") {
            report->set("status", "pending");
            report->set("hint", "Call eve_input_poll until status=ready, then continue play asserts if needed");
            Poco::JSON::Array::Ptr next = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            next->add("eve_input_poll");
            report->set("nextTools", next);
            return textPayload(report);
        }
    }
    report->set("status", "ready");
    report->set("hint", "White-box scene + playtest submitted; inspect info / play / input");
    return textPayload(report);
}

}  // namespace

bool isMcpWhiteboxTool(std::string_view name) { return name == "eve_whitebox_playtest"; }

std::string callMcpWhiteboxTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_whitebox_playtest") return whiteboxPlaytest(args);
    return errorPayload("unknown whitebox tool '" + std::string(name) + "'");
}

std::string_view mcpWhiteboxToolSchemas() {
    return R"json({"name":"eve_whitebox_playtest","description":"Fast white-box recipe: optional reset, batch scene_director actions (scene), inject playtest inputs, and optionally run an eve_play batch. Does not block the game loop — when input wait/capture is pending, poll with eve_input_poll. Returns {ok,status,scene,input,play,info}.","inputSchema":{"type":"object","properties":{"reset":{"type":"boolean","description":"Clear staged props first (default true)"},"scene":{"type":"array","items":{"type":"object","properties":{"action":{"type":"string"},"target":{"type":"string"},"params":{"type":"object"}},"required":["action"]},"description":"eve_scene_batch actions"},"stopOnError":{"type":"boolean"},"inputs":{"type":"array","items":{"type":"object"},"description":"eve_input_batch actions"},"waitFrames":{"type":"integer","minimum":0},"capture":{"type":"boolean"},"capturePath":{"type":"string"},"play":{"type":"array","items":{"type":"object"},"description":"eve_play request objects executed as one batch"}}}})json";
}

}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(Whitebox, mcpWhiteboxToolSchemas, callMcpWhiteboxTool, ContentEnvelope)
}  // namespace eve::dev
