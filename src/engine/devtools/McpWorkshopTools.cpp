#include "devtools/McpWorkshopTools.hpp"

#include "devtools/Debugger.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpRecipeTools.hpp"
#include "devtools/McpSceneDirector.hpp"
#include "devtools/McpTaskProfile.hpp"
#include "devtools/PlayHost.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <mutex>
#include <string>
#include <string_view>

namespace eve::dev {
namespace {

std::mutex  g_modeMu;
std::string g_mode{"play"};

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message, const std::string& hint = {}, const char* code = nullptr) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", false);
    out->set("error", message);
    if (code) out->set("errorCode", std::string(code));
    if (!hint.empty()) out->set("hint", hint);
    return textPayload(out, true);
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

std::string workshopStatus() {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("schemaId", "eve.workshop.status");
    out->set("schemaVersion", 1);
    {
        std::lock_guard lock(g_modeMu);
        out->set("mode", g_mode);
    }
    out->set("processProfile", mcpProcessProfile());
    out->set("sessionProfile", mcpSessionProfile());
    out->set("sceneDirectorPending", sceneDirectorInstallPending());
    Debugger& dbg = Debugger::instance();
    out->set("vmAttached", dbg.vm() != nullptr);
    Poco::JSON::Array::Ptr ops = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const char* op : {"status", "set_mode", "recipe.apply", "map.brush", "play.observe", "play.submit"})
        ops->add(std::string(op));
    out->set("ops", ops);
    out->set("hint", "Use recipe.apply for content; play.observe/play.submit for verify evidence");
    return textPayload(out);
}

std::string workshopSetMode(Poco::JSON::Object::Ptr args) {
    const std::string mode = getArgString(args, "mode");
    if (mode != "edit" && mode != "play" && mode != "battle") {
        return errorPayload("mode must be edit|play|battle",
                            "Workshop mode gates input routing hints; it does not enable Godot toolsets");
    }
    {
        std::lock_guard lock(g_modeMu);
        g_mode = mode;
    }
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("mode", mode);
    out->set("hint", "edit → content/recipe; play/battle → verify.play observe/submit");
    return textPayload(out);
}

std::string workshopRecipeApply(Poco::JSON::Object::Ptr args) {
    Poco::JSON::Object::Ptr recipeArgs = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (args) {
        if (args->has("id")) recipeArgs->set("id", args->get("id"));
        if (args->has("params")) recipeArgs->set("params", args->get("params"));
        if (args->has("expectedRevision")) recipeArgs->set("expectedRevision", args->get("expectedRevision"));
        if (args->has("stopOnError")) recipeArgs->set("stopOnError", args->get("stopOnError"));
    }
    return callMcpRecipeTool("eve_recipe_apply", recipeArgs);
}

std::string workshopMapBrush(Poco::JSON::Object::Ptr /*args*/) {
    // Intentionally unsupported until a live TileBuffer authoring session is
    // published through the editor capability (EA2). Agents must escalate to
    // content.map / editor targets rather than inventing a parallel brush API.
    return errorPayload("map.brush is unsupported without a live level-editor TileBuffer session",
                        "Escalate to content.map / eve_editor_* or sync TileBuffer→TileLayer in the "
                        "workshop example before calling this op",
                        "Unsupported");
}

std::string workshopPlay(const char* op, Poco::JSON::Object::Ptr args) {
    Poco::JSON::Object::Ptr req = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    req->set("schemaId", "evengine.play-request");
    req->set("schemaVersion", 1);
    req->set("op", std::string(op));
    if (args) {
        if (args->has("id")) req->set("id", args->get("id"));
        if (args->has("observation")) req->set("observation", args->get("observation"));
        if (args->has("action")) req->set("action", args->get("action"));
        if (args->has("params")) req->set("params", args->get("params"));
        if (args->has("fields")) req->set("fields", args->get("fields"));
    }
    auto playResponse = executePlayJson(mcpStringify(Poco::Dynamic::Var(req)));
    if (!playResponse) {
        return errorPayload(playResponse.status().describe(),
                            "Play Host needs game.agent.json with paths.gameState / observations");
    }
    auto playObj = parseJsonObject(playResponse.value());
    if (!playObj) {
        Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        out->set("ok", true);
        out->set("op", std::string(op));
        out->set("raw", playResponse.value());
        return textPayload(out);
    }
    playObj->set("workshopOp", std::string("play.") + op);
    return textPayload(playObj, !playObj->optValue<bool>("ok", true));
}

std::string workshopDispatch(Poco::JSON::Object::Ptr args) {
    if (!args || !args->has("op")) {
        return errorPayload("op is required", "ops: status|set_mode|recipe.apply|map.brush|play.observe|play.submit");
    }
    const std::string op = getArgString(args, "op");
    if (op == "status") return workshopStatus();
    if (op == "set_mode") return workshopSetMode(args);
    if (op == "recipe.apply") return workshopRecipeApply(args);
    if (op == "map.brush") return workshopMapBrush(args);
    if (op == "play.observe") return workshopPlay("observe", args);
    if (op == "play.submit" || op == "play.act") return workshopPlay("act", args);
    return errorPayload("unknown workshop op '" + op + "'",
                        "ops: status|set_mode|recipe.apply|map.brush|play.observe|play.submit");
}

}  // namespace

bool isMcpWorkshopTool(std::string_view name) { return name == "eve_workshop"; }

std::string callMcpWorkshopTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_workshop") return workshopDispatch(args);
    return errorPayload("unknown workshop tool '" + std::string(name) + "'");
}

std::string_view mcpWorkshopToolSchemas() {
    return R"json({"name":"eve_workshop","description":"Narrow RPG workshop facade for workshop.rpg: status|set_mode|recipe.apply|map.brush|play.observe|play.submit. Prefer this over registering project-specific atelier_* tools.","inputSchema":{"type":"object","properties":{"op":{"type":"string","description":"status|set_mode|recipe.apply|map.brush|play.observe|play.submit"},"mode":{"type":"string","description":"For set_mode: edit|play|battle"},"id":{"type":"string","description":"Recipe id or observation id"},"params":{"type":"object"},"observation":{"type":"string"},"action":{"type":"string"},"expectedRevision":{"type":"integer"},"stopOnError":{"type":"boolean"}},"required":["op"]}})json";
}

void resetMcpWorkshopStateForTest() {
    std::lock_guard lock(g_modeMu);
    g_mode = "play";
}

}  // namespace eve::dev
