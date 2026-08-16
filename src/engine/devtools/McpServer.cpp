#include "devtools/McpServer.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpDevBridge.hpp"
#include "devtools/SceneInspect.hpp"
#include "devtools/Snapshot.hpp"

#include "common/Module.h"
#include "audio/Audio.h"
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "particles/Particles.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "procgen/Procgen.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <squirrel.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace eve::dev {
namespace {

std::string mcpStringify(const Poco::Dynamic::Var& v) {
    std::ostringstream oss;
    // indent=0, step=0 => compact single-line JSON (required for newline framing)
    Poco::JSON::Stringifier::stringify(v, oss, 0, 0);
    return oss.str();
}

std::string mcpJsonEscape(const std::string& s) {
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
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string idToJson(const Poco::Dynamic::Var& id) {
    if (id.isEmpty()) return "null";
    try {
        if (id.isInteger() || id.isNumeric()) return std::to_string(id.convert<Poco::Int64>());
    } catch (...) {
    }
    try {
        return std::string("\"") + mcpJsonEscape(id.convert<std::string>()) + "\"";
    } catch (...) {
        return "null";
    }
}

std::string makeResult(const std::string& idJson, const std::string& resultJson) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson + ",\"result\":" + resultJson + "}";
}

std::string makeError(const std::string& idJson, int code, const std::string& message) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson +
           ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + mcpJsonEscape(message) +
           "\"}}";
}

std::string textContentResult(const std::string& text, bool isError = false) {
    Poco::JSON::Object::Ptr result  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr item    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("type", "text");
    item->set("text", text);
    content->add(item);
    result->set("content", content);
    if (isError) result->set("isError", true);
    return mcpStringify(Poco::Dynamic::Var(result));
}

std::string pauseReasonName(PauseReason r) {
    switch (r) {
        case PauseReason::Breakpoint:
            return "breakpoint";
        case PauseReason::Step:
            return "step";
        case PauseReason::Exception:
            return "exception";
        case PauseReason::Snapshot:
            return "snapshot";
        case PauseReason::PauseKey:
            return "pause";
        default:
            return "none";
    }
}

std::string engineStatusJson(const McpServer& mcp) {
    auto& dbg = Debugger::instance();
    Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    o->set("attached", mcpDevAttached());
    o->set("paused", dbg.isPaused());
    o->set("pauseReason", pauseReasonName(dbg.lastPauseReason()));
    const auto& loc = dbg.pauseLocation();
    o->set("source", loc.source);
    o->set("line", loc.line);
    o->set("function", loc.function);
    o->set("mcpPort", mcp.port());
    o->set("mcpConnected", mcp.hasClient());
    o->set("dapPort", DebugAdapter::instance().port());
    o->set("gameRoot", mcp.gameRoot());
    o->set("callgraphEvents", static_cast<int>(mcpCallgraphEvents()));
    o->set("ai", AiPanel::instance().statusLine());
    return mcpStringify(Poco::Dynamic::Var(o));
}

std::string argString(Poco::JSON::Object::Ptr args, const char* key, const std::string& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<std::string>();
    } catch (...) {
        return def;
    }
}

int argInt(Poco::JSON::Object::Ptr args, const char* key, int def = 0) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<int>();
    } catch (...) {
        return def;
    }
}

float argFloat(Poco::JSON::Object::Ptr args, const char* key, float def = 0.f) {
    if (!args || !args->has(key)) return def;
    try {
        return static_cast<float>(args->get(key).convert<double>());
    } catch (...) {
        return def;
    }
}

glm::vec3 argVec3(Poco::JSON::Object::Ptr args, const char* key, const glm::vec3& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        auto arr = args->getArray(key);
        if (arr && arr->size() >= 3) {
            return glm::vec3(static_cast<float>(arr->get(0).convert<double>()),
                             static_cast<float>(arr->get(1).convert<double>()),
                             static_cast<float>(arr->get(2).convert<double>()));
        }
    } catch (...) {
    }
    return def;
}

Poco::JSON::Array::Ptr vec3ToArray(const glm::vec3& v) {
    Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arr->add(v.x);
    arr->add(v.y);
    arr->add(v.z);
    return arr;
}

bool argBool(Poco::JSON::Object::Ptr args, const char* key, bool def = false) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<bool>();
    } catch (...) {
        return def;
    }
}

// --- Game-feature registries (MCP tools create transient worlds/emitters) ---
std::vector<eve::physics::World*>& mcpPhysicsWorlds() {
    static std::vector<eve::physics::World*> worlds;
    return worlds;
}

eve::physics::World* mcpPhysicsWorld(int id) {
    auto& ws = mcpPhysicsWorlds();
    return (id >= 0 && id < static_cast<int>(ws.size())) ? ws[static_cast<size_t>(id)] : nullptr;
}

std::string callTool(McpServer& mcp, const std::string& name, Poco::JSON::Object::Ptr args) {
    auto& dbg = Debugger::instance();
    auto& dap = DebugAdapter::instance();

    if (name == "eve_status") return engineStatusJson(mcp);

    // ============================= Scene / Entity =============================
    if (name == "eve_scene_status") {
        auto* scene = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!scene) {
            o->set("error", "Scene module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        auto* host = scene->current();
        if (!host) {
            o->set("activeHost", Poco::Dynamic::Var());
            o->set("nodeCount", 0);
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("activeHost", host->getName());
        o->set("nodeCount", host->getNodeCount());
        o->set("rootId", host->getRoot() ? host->getRoot()->id : std::string(""));
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_scene_nodes") {
        auto* scene = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        if (!scene || !scene->current()) return "error: no active scene host";
        auto* host = scene->current();
        const int limit = argInt(args, "limit", 500);
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        host->walkDepthFirst([&](eve::scene::SceneHost*, int, eve::scene::SceneNode& n) {
            if (static_cast<int>(arr->size()) >= limit) return;
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", n.id);
            o->set("name", n.name);
            o->set("path", host->getPathById(n.id));
            o->set("visible", n.visible);
            arr->add(o);
        });
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_scene_node_get") {
        auto* scene = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        if (!scene || !scene->current()) return "error: no active scene host";
        auto* host = scene->current();
        const std::string id = argString(args, "id");
        auto* n = id.empty() ? nullptr : host->findById(id);
        if (!n) return "error: node not found: " + id;
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("id", n->id);
        o->set("name", n->name);
        o->set("path", host->getPathById(id));
        o->set("visible", n->visible);
        o->set("x", n->x); o->set("y", n->y); o->set("z", n->z);
        o->set("yaw", n->yaw); o->set("pitch", n->pitch); o->set("roll", n->roll);
        o->set("sx", n->sx); o->set("sy", n->sy); o->set("sz", n->sz);
        auto* parent = host->getParentById(id);
        o->set("parent", parent ? parent->id : std::string(""));
        Poco::JSON::Array::Ptr kids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (int i = 0; i < host->getChildCountById(id); ++i) {
            auto* c = host->getChildAtById(id, i);
            if (c) kids->add(c->id);
        }
        o->set("children", kids);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_scene_node_set") {
        auto* scene = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        if (!scene || !scene->current()) return "error: no active scene host";
        auto* host = scene->current();
        const std::string id = argString(args, "id");
        auto* n = id.empty() ? nullptr : host->findById(id);
        if (!n) return "error: node not found: " + id;
        bool changed = false;
        if (args && args->has("x") && args->has("y") && args->has("z")) {
            n->x = argFloat(args, "x"); n->y = argFloat(args, "y"); n->z = argFloat(args, "z");
            changed = true;
        }
        if (args && args->has("visible")) {
            n->visible = argBool(args, "visible");
            changed = true;
        }
        if (changed) host->markTransformDirty();
        return "ok";
    }

    // ============================= Procgen =============================
    if (name == "eve_procgen_recipes") {
        auto* pg = eve::ModuleManager::getInstance<eve::procgen::Procgen>("Procgen");
        if (!pg) return "error: Procgen module not available";
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto addList = [&](const char* key, int count, auto getId) {
            Poco::JSON::Array::Ptr a = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            for (int i = 0; i < count; ++i) a->add(getId(i));
            o->set(key, a);
        };
        addList("algorithms", pg->getAlgorithmCount(),
                [&](int i) { return pg->getAlgorithmId(i); });
        addList("meshRecipes", pg->getMeshRecipeCount(),
                [&](int i) { return pg->getMeshRecipeId(i); });
        addList("textureRecipes", pg->getTextureRecipeCount(),
                [&](int i) { return pg->getTextureRecipeId(i); });
        addList("pbrRecipes", pg->getPbrRecipeCount(),
                [&](int i) { return pg->getPbrRecipeId(i); });
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_procgen_map") {
        auto* pg = eve::ModuleManager::getInstance<eve::procgen::Procgen>("Procgen");
        if (!pg) return "error: Procgen module not available";
        const std::string algorithm = argString(args, "algorithm");
        if (algorithm.empty()) return "error: missing algorithm";
        auto* params = pg->newParams();
        params->setSize(argInt(args, "width", 32), argInt(args, "height", 32));
        if (args && args->has("seed")) params->setSeed(static_cast<uint32_t>(argInt(args, "seed")));
        for (const auto& key : {"roomCount", "roomMin", "roomMax", "corridorWidth", "autotile",
                                "scale", "octaves"}) {
            if (args && args->has(key)) params->setInt(key, argInt(args, key));
        }
        if (args && args->has("corridorStyle"))
            params->setString("corridorStyle", argString(args, "corridorStyle"));
        auto* grid = pg->generate(algorithm, params);
        if (!grid) return "error: generate failed: " + pg->lastError();
        std::string json = pg->gridToJson(grid);
        delete grid;
        return json.empty() ? "error: empty grid" : json;
    }

    if (name == "eve_procgen_mesh") {
        auto* pg = eve::ModuleManager::getInstance<eve::procgen::Procgen>("Procgen");
        if (!pg) return "error: Procgen module not available";
        const std::string recipe = argString(args, "recipe");
        if (recipe.empty()) return "error: missing recipe";
        auto* params = pg->newParams();
        if (args && args->has("seed")) params->setSeed(static_cast<uint32_t>(argInt(args, "seed")));
        if (args && args->has("width")) params->setInt("width", argInt(args, "width"));
        if (args && args->has("height")) params->setInt("height", argInt(args, "height"));
        if (args && args->has("depth")) params->setInt("depth", argInt(args, "depth"));
        auto* mesh = pg->buildMesh(recipe, params);
        if (!mesh) return "error: build failed: " + pg->lastError();
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("recipe", recipe);
        o->set("vertices", mesh->getVertexCount());
        o->set("triangles", mesh->getIndexCount() / 3);
        std::string meta = mesh->getMeta("error", "");
        if (!meta.empty()) o->set("error", meta);
        delete mesh;
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Physics =============================
    if (name == "eve_physics_new_world") {
        auto* ph = eve::ModuleManager::getInstance<eve::physics::Physics>("Physics");
        if (!ph) return "error: Physics module not available";
        eve::physics::World* w =
            ph->newWorld(argFloat(args, "gravityX", 0.f), argFloat(args, "gravityY", 900.f));
        auto& ws = mcpPhysicsWorlds();
        ws.push_back(w);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("id", static_cast<int>(ws.size()) - 1);
        o->set("gravityX", w->getGravityX());
        o->set("gravityY", w->getGravityY());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_physics_list_worlds") {
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        const auto& ws = mcpPhysicsWorlds();
        for (size_t i = 0; i < ws.size(); ++i) {
            if (!ws[i]) continue;
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", static_cast<int>(i));
            o->set("gravityX", ws[i]->getGravityX());
            o->set("gravityY", ws[i]->getGravityY());
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_physics_raycast") {
        auto* w = mcpPhysicsWorld(argInt(args, "world", 0));
        if (!w) return "error: unknown physics world id";
        w->rayCast(argFloat(args, "x1"), argFloat(args, "y1"),
                   argFloat(args, "x2"), argFloat(args, "y2"));
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("hit", w->hasRayHit());
        if (w->hasRayHit()) {
            o->set("bodyId", w->getRayHitBodyId());
            o->set("x", w->getRayHitX());
            o->set("y", w->getRayHitY());
            o->set("normalX", w->getRayHitNormalX());
            o->set("normalY", w->getRayHitNormalY());
            o->set("fraction", w->getRayHitFraction());
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_physics_remove_world") {
        auto& ws = mcpPhysicsWorlds();
        const int id = argInt(args, "world", -1);
        if (id < 0 || id >= static_cast<int>(ws.size()) || !ws[static_cast<size_t>(id)])
            return "error: unknown physics world id";
        delete ws[static_cast<size_t>(id)];
        ws[static_cast<size_t>(id)] = nullptr;
        return "ok";
    }

    // ============================= Render =============================
    if (name == "eve_render_status") {
        auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!gfx) {
            o->set("error", "Graphics module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("width", gfx->getWidth());
        o->set("height", gfx->getHeight());
        o->set("pixelWidth", gfx->getPixelWidth());
        o->set("pixelHeight", gfx->getPixelHeight());
        o->set("had3DThisFrame", gfx->had3DThisFrame());
        o->set("readbackEnabled", gfx->isScreenReadbackEnabled());
        o->set("renderFlowEvents", static_cast<int>(DevTool::instance().renderFlow().eventCount()));
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_screenshot") {
        auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (!gfx) return "error: Graphics module not available";
        std::string path = argString(args, "path");
        if (path.empty()) path = "mcp_screenshot.png";
        gfx->setScreenReadbackEnabled(true);
        eve::image::ImageData* img = gfx->newImageData();
        if (!img) return "error: readback returned no image";
        const int w = gfx->getPixelWidth();
        const int h = gfx->getPixelHeight();
        img->encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, path.c_str(), true);
        delete img;
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("path", path);
        o->set("width", w);
        o->set("height", h);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Particles / Weather =============================
    if (name == "eve_particles_status") {
        auto* part = eve::ModuleManager::getInstance<eve::particles::Particles>("Particles");
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!part) {
            o->set("error", "Particles module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("emitterCount", part->getEmitterCount());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_particles_emit") {
        auto* part = eve::ModuleManager::getInstance<eve::particles::Particles>("Particles");
        if (!part) return "error: Particles module not available";
        auto* em = part->newEmitter(argInt(args, "buffer", 1000));
        if (!em) return "error: failed to create emitter";
        em->setPosition(argFloat(args, "x"), argFloat(args, "y"));
        const std::string preset = argString(args, "preset");
        if (!preset.empty()) em->applyPreset(preset);
        em->start();
        em->emit(argInt(args, "count", 100));
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("x", em->getX());
        o->set("y", em->getY());
        o->set("count", em->getCount());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Audio =============================
    if (name == "eve_audio_status") {
        auto* audio = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio");
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!audio) {
            o->set("error", "Audio module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("volume", audio->getVolume());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_audio_set_volume") {
        auto* audio = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio");
        if (!audio) return "error: Audio module not available";
        audio->setVolume(argFloat(args, "volume", 1.f));
        return "ok";
    }

    if (name == "eve_audio_stop_all") {
        auto* audio = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio");
        if (!audio) return "error: Audio module not available";
        audio->stopAll();
        return "ok";
    }

    if (name == "eve_eval") {
        const std::string expr = argString(args, "expression");
        if (expr.empty()) return "error: missing expression";
        auto info = dbg.evaluate(expr);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("expression", expr);
        o->set("name", info.name);
        o->set("value", info.value);
        o->set("type", info.type);
        o->set("ok", !info.value.empty() || info.type == "null" || !info.name.empty());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_pause") {
        dbg.pause(PauseReason::PauseKey);
        dap.notifyStopped(PauseReason::PauseKey, dbg.pauseLocation());
        return "paused";
    }
    if (name == "eve_continue") {
        dbg.resume();
        dap.notifyContinued();
        return "continued";
    }
    if (name == "eve_step_over") {
        dbg.stepOver();
        dap.notifyContinued();
        return "step_over";
    }
    if (name == "eve_step_into") {
        dbg.stepInto();
        dap.notifyContinued();
        return "step_into";
    }
    if (name == "eve_step_out") {
        dbg.stepOut();
        dap.notifyContinued();
        return "step_out";
    }
    if (name == "eve_step_frame") {
        dbg.stepFrame();
        dap.notifyContinued();
        return "step_frame";
    }

    if (name == "eve_stack") {
        auto frames = dbg.stackTrace();
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& f : frames) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", f.id);
            o->set("name", f.name);
            o->set("source", f.loc.source);
            o->set("line", f.loc.line);
            o->set("function", f.loc.function);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_locals") {
        const int level = argInt(args, "level", 1);
        auto      vars  = dbg.locals(level);
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& v : vars) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("name", v.name);
            o->set("value", v.value);
            o->set("type", v.type);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_set_breakpoint") {
        const std::string source = argString(args, "source");
        const int         line   = argInt(args, "line");
        if (source.empty() || line <= 0) return "error: need source and line";
        const int id = dbg.setBreakpoint(source, line, true);
        return "ok id=" + std::to_string(id);
    }
    if (name == "eve_clear_breakpoint") {
        const std::string source = argString(args, "source");
        const int         line   = argInt(args, "line");
        if (source.empty() || line <= 0) return "error: need source and line";
        return dbg.clearBreakpoint(source, line) ? "cleared" : "not_found";
    }
    if (name == "eve_list_breakpoints") {
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& bp : dbg.breakpoints()) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", bp.id);
            o->set("source", bp.source);
            o->set("line", bp.line);
            o->set("enabled", bp.enabled);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_watch_add") {
        const std::string expr = argString(args, "expression");
        if (expr.empty()) return "error: missing expression";
        dbg.addWatch(expr);
        dbg.refreshWatches();
        return "ok";
    }
    if (name == "eve_watch_list") {
        dbg.refreshWatches();
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& w : dbg.watches()) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("expression", w.expression);
            o->set("value", w.value);
            o->set("ok", w.ok);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_snapshot_capture") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        std::string json = Snapshot::instance().capture(vm, &err);
        if (!err.empty() && json.empty()) return "error: " + err;
        return json;
    }
    if (name == "eve_snapshot_restore") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string json = argString(args, "json");
        std::string       err;
        if (!Snapshot::instance().restore(vm, json, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_save") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = argString(args, "path");
        std::string       err;
        if (!Snapshot::instance().saveFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_load") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = argString(args, "path");
        std::string       err;
        if (!Snapshot::instance().loadFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }

    if (name == "eve_error_slice") {
        const std::string& last = mcpLastReport();
        if (!last.empty()) return last;
        return mcpFormatError("no prior error; callgraph slice at latest site");
    }

    if (name == "eve_run_script") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string source = argString(args, "source");
        if (source.empty()) return "error: missing source";
        const SQInteger top = sq_gettop(vm);
        if (SQ_FAILED(sq_compilebuffer(vm, source.c_str(),
                                       static_cast<SQInteger>(source.size()),
                                       _SC("mcp_snippet.nut"), SQTrue))) {
            sq_settop(vm, top);
            return "error: compile failed";
        }
        sq_pushroottable(vm);
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return "error: runtime failed";
        }
        sq_settop(vm, top);
        return "ok";
    }

    if (name == "eve_ai_note") {
        const std::string text = argString(args, "text");
        if (text.empty()) return "error: missing text";
        AiPanel::instance().addNote(text);
        return "ok";
    }
    if (name == "eve_ai_log") return AiPanel::instance().formatLog(100);

    // ---- 场景巡检工具集（图像与 3D 几何数据严格同步） ----
    if (name == "inspect_generate_scene_camera_views") {
        const glm::vec3 center = argVec3(args, "center");
        const float     fov    = argFloat(args, "fov", 60.f);
        auto            views  = SceneInspect::instance().generateViews(center, fov);
        Poco::JSON::Object::Ptr root = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        Poco::JSON::Array::Ptr  arr  = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& v : views) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("name", v.name);
            o->set("kind", v.kind);
            o->set("eye", vec3ToArray(v.eye));
            o->set("target", vec3ToArray(v.target));
            o->set("fov", static_cast<double>(v.fovYDeg));
            arr->add(o);
        }
        root->set("views", arr);
        return mcpStringify(Poco::Dynamic::Var(root));
    }

    if (name == "set_camera_pose") {
        const glm::vec3 pos = argVec3(args, "pos", glm::vec3(0.f, 1.8f, 0.f));
        const glm::vec3 rot = argVec3(args, "rot", glm::vec3(0.f));
        const float     fov = argFloat(args, "fov", 0.f);
        const bool      ok  = SceneInspect::instance().setCameraPose(pos, rot, fov);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("ok", ok);
        if (!ok) {
            o->set("error", "failed to set camera pose (no Graphics/Camera3D)");
        } else {
            try {
                Poco::JSON::Parser parser;
                o->set("pose", parser.parse(SceneInspect::instance().currentPoseJson()));
            } catch (...) {
            }
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "capture_render_frame") {
        const std::string dir = argString(args, "dir");
        const std::string tag = argString(args, "tag", "frame");
        std::vector<std::string> buffers;
        if (args && args->has("buffers")) {
            try {
                auto arr = args->getArray("buffers");
                if (arr) {
                    for (size_t i = 0; i < arr->size(); ++i)
                        buffers.push_back(arr->get(i).convert<std::string>());
                }
            } catch (...) {
            }
        }
        const auto res = SceneInspect::instance().capture(dir, tag, buffers);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("ok", res.ok);
        if (res.ok) {
            o->set("png", res.pngPath);
            o->set("json", res.jsonPath);
            o->set("width", res.width);
            o->set("height", res.height);
            o->set("entityCount", res.entityCount);
            if (!res.depthPngPath.empty()) o->set("depthPng", res.depthPngPath);
            if (!res.normalPngPath.empty()) o->set("normalPng", res.normalPngPath);
            if (!res.idPngPath.empty()) o->set("idPng", res.idPngPath);
            if (!res.idJsonPath.empty()) o->set("idJson", res.idJsonPath);
            if (!res.unsupported.empty()) {
                Poco::JSON::Array::Ptr uns = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
                for (const auto& u : res.unsupported) uns->add(u);
                o->set("unsupported", uns);
            }
        } else {
            o->set("error", res.error);
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "get_visible_entities_screen_bbox") {
        const bool hasPos    = args && args->has("pos");
        const bool hasTarget = args && args->has("target");
        if (hasPos && hasTarget) {
            const glm::vec3 eye = argVec3(args, "pos");
            const glm::vec3 tgt = argVec3(args, "target");
            const float     fov = argFloat(args, "fov", 0.f);
            return SceneInspect::instance().visibleEntitiesJson(&eye, &tgt, fov);
        }
        const float fov = argFloat(args, "fov", 0.f);
        return SceneInspect::instance().visibleEntitiesJson(nullptr, nullptr, fov);
    }

    return "error: unknown tool " + name;
}

std::string handleInitialize(McpServer& mcp, const std::string& idJson,
                             Poco::JSON::Object::Ptr params) {
    std::string clientName = "mcp-client";
    std::string protocol   = "2025-06-18";
    if (params) {
        try {
            if (params->has("clientInfo")) {
                auto info = params->getObject("clientInfo");
                if (info && info->has("name"))
                    clientName = info->get("name").convert<std::string>();
            }
            if (params->has("protocolVersion"))
                protocol = params->get("protocolVersion").convert<std::string>();
        } catch (...) {
        }
    }
    (void)mcp;
    AiPanel::instance().setClientName(clientName);
    AiPanel::instance().addLog("system", "mcp.initialize", clientName);

    // Hand-built JSON keeps initialize compact (newline framing) without a Poco Object tree.
    const std::string resultJson =
        std::string("{\"protocolVersion\":\"") + mcpJsonEscape(protocol) +
        "\",\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}},"
        "\"serverInfo\":{\"name\":\"evengine\",\"title\":\"EVEngine MCP\",\"version\":\"0.1.0\"},"
        "\"instructions\":\"EVEngine runtime MCP for AI-assisted game development and testing.\"}";
    return makeResult(idJson, resultJson);
}

std::string handleToolsList(const std::string& idJson) {
    static const char* kToolsJson =
        "{\"tools\":["
        "{\"name\":\"eve_status\",\"description\":\"Runtime + debugger + MCP/DAP status JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_eval\",\"description\":\"Evaluate a Squirrel expression (local or roottable).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
        "{\"name\":\"eve_pause\",\"description\":\"Pause the game / script.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_continue\",\"description\":\"Continue from pause.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_over\",\"description\":\"Step over next statement.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_into\",\"description\":\"Step into next statement.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_out\",\"description\":\"Step out of current function.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_frame\",\"description\":\"Run one game frame then pause.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_stack\",\"description\":\"Return current script stack frames.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_locals\",\"description\":\"List locals at a stack level (default 1).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_set_breakpoint\",\"description\":\"Set a script breakpoint.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_clear_breakpoint\",\"description\":\"Clear a script breakpoint.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_list_breakpoints\",\"description\":\"List breakpoints.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_watch_add\",\"description\":\"Add a watch expression.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
        "{\"name\":\"eve_watch_list\",\"description\":\"List watches with last values.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_capture\",\"description\":\"Capture script-state snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_restore\",\"description\":\"Restore script-state from snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"json\":{\"type\":\"string\"}},\"required\":[\"json\"]}},"
        "{\"name\":\"eve_snapshot_save\",\"description\":\"Save snapshot to a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
        "{\"name\":\"eve_snapshot_load\",\"description\":\"Load snapshot from a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
        "{\"name\":\"eve_error_slice\",\"description\":\"Return last error report / backward slice (script + render).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_run_script\",\"description\":\"Compile and run a short Squirrel snippet in the live VM.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":[\"source\"]}},"
        "{\"name\":\"eve_ai_note\",\"description\":\"Append a note to the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}},"
        "{\"name\":\"eve_ai_log\",\"description\":\"Read the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"inspect_generate_scene_camera_views\",\"description\":\"Generate a standard set of inspection camera views (road-level, bird's-eye, corner close-up, vista) around a center point.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"center\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"[x,y,z] center point to orbit (default [0,0,0])\"},\"fov\":{\"type\":\"number\",\"description\":\"base vertical FOV in degrees (default 60)\"}}}}"
        ","
        "{\"name\":\"set_camera_pose\",\"description\":\"Set the active camera pose from position + Euler rotation + optional FOV.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"[x,y,z] camera position\"},\"rot\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"[yawDeg,pitchDeg] facing\"},\"fov\":{\"type\":\"number\",\"description\":\"vertical FOV in degrees (0=keep current)\"}},\"required\":[\"pos\",\"rot\"]}}"
        ","
        "{\"name\":\"capture_render_frame\",\"description\":\"Atomically capture the current view and export matching buffers. PNG color frame + geometry JSON always; 'buffers' may add depth/normal (GBuffer), id (per-pixel render ID mask with JSON mapping) — shadow is unsupported on current backend.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\",\"description\":\"output directory (default: cache dir)\"},\"tag\":{\"type\":\"string\",\"description\":\"file name tag (default 'frame')\"},\"buffers\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"optional: 'color'|'depth'|'normal'|'id' (default ['color'])\"}}}"
        ","
        "{\"name\":\"get_visible_entities_screen_bbox\",\"description\":\"Return visible scene entities in the frustum with their screen-space bbox, world AABB, id and asset label.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"optional [x,y,z] camera eye override\"},\"target\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"optional [x,y,z] look target override\"},\"fov\":{\"type\":\"number\",\"description\":\"optional FOV override (degrees)\"}}}}"
        ","
        "{\"name\":\"eve_scene_status\",\"description\":\"Active scene host name, node count and root id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_nodes\",\"description\":\"List nodes of the active scene host (id/name/path/visible).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"limit\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_scene_node_get\",\"description\":\"Read transform/visibility/parent/children of a scene node by id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}},"
        "{\"name\":\"eve_scene_node_set\",\"description\":\"Set position (x,y,z) or visibility of a scene node by id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"z\":{\"type\":\"number\"},\"visible\":{\"type\":\"boolean\"}},\"required\":[\"id\"]}},"
        "{\"name\":\"eve_procgen_recipes\",\"description\":\"List available procgen map algorithms and mesh/texture/PBR recipes.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_procgen_map\",\"description\":\"Generate a semantic tile grid with a procgen map algorithm.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"algorithm\":{\"type\":\"string\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},\"seed\":{\"type\":\"integer\"}},\"required\":[\"algorithm\"]}},"
        "{\"name\":\"eve_procgen_mesh\",\"description\":\"Build a procedural CPU mesh (mesh.* recipe) and return stats.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"recipe\":{\"type\":\"string\"},\"seed\":{\"type\":\"integer\"}},\"required\":[\"recipe\"]}},"
        "{\"name\":\"eve_physics_new_world\",\"description\":\"Create a 2D physics world and return its id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"gravityX\":{\"type\":\"number\"},\"gravityY\":{\"type\":\"number\"}}}},"
        "{\"name\":\"eve_physics_list_worlds\",\"description\":\"List live 2D physics worlds with gravity.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_physics_raycast\",\"description\":\"Raycast a segment in a physics world and report the closest hit.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"world\":{\"type\":\"integer\"},\"x1\":{\"type\":\"number\"},\"y1\":{\"type\":\"number\"},\"x2\":{\"type\":\"number\"},\"y2\":{\"type\":\"number\"}},\"required\":[\"world\",\"x1\",\"y1\",\"x2\",\"y2\"]}},"
        "{\"name\":\"eve_physics_remove_world\",\"description\":\"Destroy a 2D physics world by id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"world\":{\"type\":\"integer\"}},\"required\":[\"world\"]}},"
        "{\"name\":\"eve_render_status\",\"description\":\"Render window size, 3D frame flag, readback state and RenderFlow event count.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_screenshot\",\"description\":\"Capture the current frame to a PNG file (enables readback).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}},"
        "{\"name\":\"eve_particles_status\",\"description\":\"Report live particle emitter count.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_particles_emit\",\"description\":\"Spawn a particle emitter at a position (optionally a preset) and emit particles.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"preset\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_audio_status\",\"description\":\"Report master audio volume.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_audio_set_volume\",\"description\":\"Set master audio volume (0..1).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"volume\":{\"type\":\"number\"}},\"required\":[\"volume\"]}},"
        "{\"name\":\"eve_audio_stop_all\",\"description\":\"Stop all playing audio sources.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
        "]}";
    return makeResult(idJson, kToolsJson);
}

std::string handleToolsCall(McpServer& mcp, const std::string& idJson,
                            Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name"))
        return makeError(idJson, -32602, "tools/call requires params.name");
    std::string name;
    try {
        name = params->get("name").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "tools/call params.name must be a string");
    }
    Poco::JSON::Object::Ptr args = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (params->has("arguments")) {
        try {
            args = params->getObject("arguments");
        } catch (...) {
        }
    }

    std::string detail;
    try {
        if (args) detail = mcpStringify(Poco::Dynamic::Var(args));
    } catch (...) {
    }
    AiPanel::instance().addLog("tool", name, detail);

    try {
        const std::string out   = callTool(mcp, name, args);
        const bool        isErr = out.rfind("error:", 0) == 0;
        return makeResult(idJson, textContentResult(out, isErr));
    } catch (const std::exception& e) {
        AiPanel::instance().addLog("error", name, e.what());
        return makeResult(idJson, textContentResult(std::string("error: ") + e.what(), true));
    }
}

std::string handleResourcesList(const std::string& idJson) {
    return makeResult(idJson,
        "{\"resources\":["
        "{\"uri\":\"eve://status\",\"name\":\"status\",\"description\":\"Debugger / MCP / DAP status\",\"mimeType\":\"application/json\"},"
        "{\"uri\":\"eve://error-report\",\"name\":\"error-report\",\"description\":\"Last DevTools error slice report\",\"mimeType\":\"text/plain\"},"
        "{\"uri\":\"eve://ai-session\",\"name\":\"ai-session\",\"description\":\"AI / MCP session log\",\"mimeType\":\"text/plain\"},"
        "{\"uri\":\"eve://callgraph\",\"name\":\"callgraph\",\"description\":\"CallGraph event summary\",\"mimeType\":\"application/json\"}"
        "]}");
}

std::string handleResourcesRead(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("uri"))
        return makeError(idJson, -32602, "resources/read requires params.uri");
    std::string uri;
    try {
        uri = params->get("uri").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "resources/read params.uri must be a string");
    }
    std::string       text;
    std::string       mime = "text/plain";
    if (uri == "eve://status") {
        text = engineStatusJson(McpServer::instance());
        mime = "application/json";
    } else if (uri == "eve://error-report") {
        text = mcpLastReport();
        if (text.empty()) text = "(no error report yet)\n";
    } else if (uri == "eve://ai-session") {
        text = AiPanel::instance().formatLog(200);
        if (text.empty()) text = "(empty)\n";
    } else if (uri == "eve://callgraph") {
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("events", static_cast<int>(mcpCallgraphEvents()));
        o->set("stackDepth", static_cast<int>(mcpCallgraphStackDepth()));
        text = mcpStringify(Poco::Dynamic::Var(o));
        mime = "application/json";
    } else {
        return makeError(idJson, -32002, "Unknown resource: " + uri);
    }

    Poco::JSON::Object::Ptr contentsItem = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    contentsItem->set("uri", uri);
    contentsItem->set("mimeType", mime);
    contentsItem->set("text", text);
    Poco::JSON::Array::Ptr contents = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    contents->add(contentsItem);
    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("contents", contents);
    return makeResult(idJson, mcpStringify(Poco::Dynamic::Var(result)));
}

std::string handlePromptsList(const std::string& idJson) {
    return makeResult(idJson,
        "{\"prompts\":["
        "{\"name\":\"debug_failure\",\"description\":\"Investigate the latest script/render error using MCP tools and the error slice.\"},"
        "{\"name\":\"test_scenario\",\"description\":\"Drive a reproducible in-engine test: pause, snapshot, eval assertions, continue.\"},"
        "{\"name\":\"ai_game_review\",\"description\":\"Review live game state for AI-generated content issues.\"}"
        "]}");
}

std::string handlePromptsGet(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name"))
        return makeError(idJson, -32602, "prompts/get requires params.name");
    std::string name;
    try {
        name = params->get("name").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "prompts/get params.name must be a string");
    }
    std::string       text;
    if (name == "debug_failure") {
        text =
            "You are debugging an EVEngine game via MCP.\n"
            "1) Call eve_status and read resource eve://error-report.\n"
            "2) Use eve_stack / eve_locals / eve_eval to inspect state.\n"
            "3) Use eve_error_slice to narrow script/render causes.\n"
            "4) Propose a minimal fix; verify with eve_run_script or snapshot restore.";
    } else if (name == "test_scenario") {
        text =
            "Design a short automated test against the live EVEngine session:\n"
            "1) eve_pause then eve_snapshot_capture as baseline.\n"
            "2) Mutate or advance with eve_step_frame / eve_run_script.\n"
            "3) Assert with eve_eval / eve_watch_list.\n"
            "4) eve_snapshot_restore to reset; record notes via eve_ai_note.";
    } else if (name == "ai_game_review") {
        text =
            "Review this AI-generated or AI-assisted game build:\n"
            "1) eve_status + eve_ai_log for recent agent actions.\n"
            "2) Spot-check critical roots with eve_eval.\n"
            "3) If unstable, pause and capture a snapshot.\n"
            "4) Summarize risks and suggested script fixes.";
    } else {
        return makeError(idJson, -32602, "Unknown prompt: " + name);
    }

    Poco::JSON::Object::Ptr msg = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    msg->set("role", "user");
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr part    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    part->set("type", "text");
    part->set("text", text);
    content->add(part);
    msg->set("content", content);
    Poco::JSON::Array::Ptr messages = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    messages->add(msg);
    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("description", name);
    result->set("messages", messages);
    return makeResult(idJson, mcpStringify(Poco::Dynamic::Var(result)));
}

}  // namespace

McpServer& McpServer::instance() {
    // Intentionally leaked so AiPanel/McpServer mutexes are never destroyed
    // while the other singleton's destructor still calls into them (macOS abort).
    static McpServer* inst = new McpServer();
    return *inst;
}

McpServer::McpServer() = default;

McpServer::~McpServer() {
    // Only reached if someone deletes the instance; keep sockets tidy without
    // touching AiPanel (may already be torn down in other lifetime models).
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
    listening_.store(false);
    hasClient_.store(false);
    port_.store(0);
}

void McpServer::setGameRoot(std::string root) {
    for (char& c : root) {
        if (c == '\\') c = '/';
    }
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    gameRoot_ = std::move(root);
}

int McpServer::listen(uint16_t port) {
    stop();
    try {
        Poco::Net::SocketAddress addr("127.0.0.1", port);
        server_ = std::make_unique<Poco::Net::ServerSocket>(addr);
        server_->setBlocking(false);
        const int bound = static_cast<int>(server_->address().port());
        port_.store(bound);
        listening_.store(true);
        initialized_ = false;
        try {
            if (gameRoot_.empty()) setGameRoot(std::filesystem::current_path().string());
        } catch (...) {
        }
        AiPanel::instance().setMcpPort(bound);
        AiPanel::instance().setMcpConnected(false);
        AiPanel::instance().addLog("system", "mcp.listen",
                                   "listening on 127.0.0.1:" + std::to_string(bound));
        return bound;
    } catch (...) {
        server_.reset();
        listening_.store(false);
        port_.store(0);
        AiPanel::instance().setMcpPort(0);
        return 0;
    }
}

void McpServer::stop() {
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
    initialized_ = false;
    AiPanel::instance().setMcpPort(0);
    AiPanel::instance().setMcpConnected(false);
    AiPanel::instance().setClientName({});
}

void McpServer::poll() {
    if (!listening_.load()) return;
    acceptNonBlocking();
    readAndDispatch();
}

void McpServer::acceptNonBlocking() {
    if (!server_ || client_) return;
    try {
        Poco::Net::SocketAddress clientAddr;
        Poco::Net::StreamSocket  ss = server_->acceptConnection(clientAddr);
        ss.setBlocking(true);
        ss.setReceiveTimeout(Poco::Timespan(0, 1000));
        client_ = std::make_unique<Poco::Net::StreamSocket>(ss);
        hasClient_.store(true);
        recvBuf_.clear();
        initialized_ = false;
        AiPanel::instance().setMcpConnected(true);
        AiPanel::instance().addLog("system", "mcp.accept", "client connected");
    } catch (const Poco::TimeoutException&) {
    } catch (const Poco::Net::NetException&) {
    } catch (...) {
    }
}

bool McpServer::sendLine(const std::string& json) {
    if (!client_) return false;
    const std::string frame = json + "\n";
    try {
        const int sent = client_->sendBytes(frame.data(), static_cast<int>(frame.size()));
        return sent == static_cast<int>(frame.size());
    } catch (...) {
        client_.reset();
        hasClient_.store(false);
        AiPanel::instance().setMcpConnected(false);
        return false;
    }
}

void McpServer::readAndDispatch() {
    if (!client_) return;
    char buf[8192];
    try {
        const int n = client_->receiveBytes(buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0) {
                client_.reset();
                hasClient_.store(false);
                AiPanel::instance().setMcpConnected(false);
                AiPanel::instance().addLog("system", "mcp.disconnect", "client closed");
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
        AiPanel::instance().setMcpConnected(false);
        return;
    }

    while (true) {
        const auto nl = recvBuf_.find('\n');
        if (nl == std::string::npos) break;
        std::string line = recvBuf_.substr(0, nl);
        recvBuf_.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        handleMessage(line);
    }
}

void McpServer::handleMessage(const std::string& json) {
    try {
        Poco::JSON::Parser parser;
        auto               var = parser.parse(json);
        auto               obj = var.extract<Poco::JSON::Object::Ptr>();
        if (!obj) return;

        std::string method;
        if (obj->has("method")) {
            try {
                method = obj->get("method").convert<std::string>();
            } catch (const Poco::Exception& e) {
                sendLine(makeError("null", -32600, std::string("bad method: ") + e.displayText()));
                return;
            }
        }
        const bool        hasId  = obj->has("id");
        Poco::Dynamic::Var idVar;
        if (hasId) idVar = obj->get("id");
        std::string idJson = "null";
        try {
            if (hasId) idJson = idToJson(idVar);
        } catch (const Poco::Exception& e) {
            sendLine(makeError("null", -32600, std::string("bad id: ") + e.displayText()));
            return;
        }

        if (!hasId) {
            if (method == "notifications/initialized" || method == "initialized") {
                initialized_ = true;
                AiPanel::instance().addLog("system", "mcp.initialized", "handshake complete");
            }
            return;
        }

        auto params = [&]() -> Poco::JSON::Object::Ptr {
            if (!obj->has("params")) return Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            try {
                return obj->getObject("params");
            } catch (...) {
                return Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            }
        };

        if (method == "initialize") {
            sendLine(handleInitialize(*this, idJson, params()));
            return;
        }
        if (method == "ping") {
            sendLine(makeResult(idJson, "{}"));
            return;
        }
        if (method == "tools/list") {
            sendLine(handleToolsList(idJson));
            return;
        }
        if (method == "tools/call") {
            sendLine(handleToolsCall(*this, idJson, params()));
            return;
        }
        if (method == "resources/list") {
            sendLine(handleResourcesList(idJson));
            return;
        }
        if (method == "resources/read") {
            sendLine(handleResourcesRead(idJson, params()));
            return;
        }
        if (method == "prompts/list") {
            sendLine(handlePromptsList(idJson));
            return;
        }
        if (method == "prompts/get") {
            sendLine(handlePromptsGet(idJson, params()));
            return;
        }

        sendLine(makeError(idJson, -32601, "Method not found: " + method));
    } catch (const Poco::Exception& e) {
        sendLine(makeError("null", -32700, std::string("Parse error: ") + e.displayText()));
    } catch (const std::exception& e) {
        sendLine(makeError("null", -32603, e.what()));
    } catch (...) {
        sendLine(makeError("null", -32603, "Internal error"));
    }
}

}  // namespace eve::dev
