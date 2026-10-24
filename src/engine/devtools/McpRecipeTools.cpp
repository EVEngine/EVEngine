#include "devtools/McpRecipeTools.hpp"

#include "devtools/Debugger.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpSceneDirector.hpp"
#include "devtools/McpSceneLiteral.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {
namespace {

struct RecipeParam {
    const char* name;
    const char* kind;  // "int" | "number" | "bool"
    const char* description;
    double      defaultValue;
};

struct RecipeDef {
    const char*              id;
    const char*              description;
    std::vector<RecipeParam> params;
};

const std::vector<RecipeDef>& builtinRecipes() {
    static const std::vector<RecipeDef> kRecipes = {
        {"empty.clear", "Clear staged scene_director props (reset only).", {}},
        {"ruins.moonlit",
         "Moonlit ruins: ground plane, ruin block, and seeded trees.",
         {{"seed", "int", "Placement RNG seed", 7.0}, {"trees", "int", "Number of trees (0-12)", 3.0}}},
        {"plaza.sunlit",
         "Sunlit plaza: ground, center marker, and perimeter posts.",
         {{"seed", "int", "Placement RNG seed", 11.0}, {"posts", "int", "Perimeter posts (0-8)", 4.0}}},
    };
    return kRecipes;
}

const RecipeDef* findRecipe(std::string_view id) {
    for (const auto& recipe : builtinRecipes()) {
        if (recipe.id == id) return &recipe;
    }
    return nullptr;
}

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

Poco::JSON::Object::Ptr parseJsonObject(const std::string& text) {
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(text);
        if (root.type() == typeid(Poco::JSON::Object::Ptr)) return root.extract<Poco::JSON::Object::Ptr>();
    } catch (...) {
    }
    return {};
}

int paramInt(Poco::JSON::Object::Ptr params, const char* key, int fallback) {
    if (!params || !params->has(key)) return fallback;
    try {
        return params->getValue<int>(key);
    } catch (...) {
    }
    try {
        return static_cast<int>(std::lround(params->getValue<double>(key)));
    } catch (...) {
    }
    return fallback;
}

uint32_t lcgNext(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

double lcgUnit(uint32_t& state) { return static_cast<double>(lcgNext(state) & 0xFFFFFFu) / 16777216.0; }

Poco::JSON::Object::Ptr actionObject(const std::string& action, const std::string& target,
                                     Poco::JSON::Object::Ptr params) {
    Poco::JSON::Object::Ptr item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("action", action);
    if (!target.empty()) item->set("target", target);
    if (params) item->set("params", params);
    return item;
}

Poco::JSON::Object::Ptr spawnParams(const std::string& id, const std::string& kind, double x, double y, double z,
                                    double sx = 1.0, double sy = 1.0, double sz = 1.0, int seed = 7) {
    Poco::JSON::Object::Ptr p = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    p->set("id", id);
    p->set("kind", kind);
    p->set("seed", seed);
    Poco::JSON::Array::Ptr pos = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    pos->add(x);
    pos->add(y);
    pos->add(z);
    p->set("pos", pos);
    Poco::JSON::Array::Ptr scale = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    scale->add(sx);
    scale->add(sy);
    scale->add(sz);
    p->set("scale", scale);
    return p;
}

Poco::JSON::Array::Ptr expandRecipeActions(const RecipeDef& recipe, Poco::JSON::Object::Ptr params) {
    Poco::JSON::Array::Ptr actions = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    if (std::string_view(recipe.id) == "empty.clear") {
        actions->add(actionObject("reset", "", {}));
        return actions;
    }
    if (std::string_view(recipe.id) == "ruins.moonlit") {
        const int seed  = paramInt(params, "seed", 7);
        int       trees = paramInt(params, "trees", 3);
        if (trees < 0) trees = 0;
        if (trees > 12) trees = 12;
        actions->add(actionObject("reset", "", {}));
        actions->add(actionObject("spawn", "", spawnParams("ground", "ground", 0.0, 0.0, 0.0, 12.0, 1.0, 12.0, seed)));
        actions->add(
            actionObject("spawn", "", spawnParams("ruin_core", "box", 0.0, 0.6, 0.0, 2.2, 1.4, 2.2, seed + 1)));
        {
            Poco::JSON::Object::Ptr light = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            light->set("ambient", 0.18);
            light->set("moonlit", true);
            actions->add(actionObject("lighting", "", light));
        }
        uint32_t rng = static_cast<uint32_t>(seed) * 2654435761u + 1u;
        for (int i = 0; i < trees; ++i) {
            const double angle  = lcgUnit(rng) * 6.283185307179586;
            const double radius = 3.5 + lcgUnit(rng) * 3.0;
            const double x      = std::cos(angle) * radius;
            const double z      = std::sin(angle) * radius;
            actions->add(actionObject(
                "spawn", "",
                spawnParams("tree_" + std::to_string(i), "tree", x, 0.0, z, 1.0, 1.0, 1.0, seed + 10 + i)));
        }
        return actions;
    }
    if (std::string_view(recipe.id) == "plaza.sunlit") {
        const int seed  = paramInt(params, "seed", 11);
        int       posts = paramInt(params, "posts", 4);
        if (posts < 0) posts = 0;
        if (posts > 8) posts = 8;
        actions->add(actionObject("reset", "", {}));
        actions->add(
            actionObject("spawn", "", spawnParams("plaza_ground", "ground", 0.0, 0.0, 0.0, 10.0, 1.0, 10.0, seed)));
        actions->add(
            actionObject("spawn", "", spawnParams("plaza_marker", "box", 0.0, 0.4, 0.0, 1.0, 0.8, 1.0, seed + 2)));
        for (int i = 0; i < posts; ++i) {
            const double angle = (6.283185307179586 * static_cast<double>(i)) / static_cast<double>(posts);
            const double x     = std::cos(angle) * 4.0;
            const double z     = std::sin(angle) * 4.0;
            actions->add(actionObject(
                "spawn", "",
                spawnParams("post_" + std::to_string(i), "box", x, 0.7, z, 0.25, 1.4, 0.25, seed + 20 + i)));
        }
        return actions;
    }
    return actions;
}

std::string recipeList() {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("schemaId", "eve.recipe.list");
    out->set("schemaVersion", 1);
    Poco::JSON::Array::Ptr recipes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& recipe : builtinRecipes()) {
        Poco::JSON::Object::Ptr item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        item->set("id", std::string(recipe.id));
        item->set("description", std::string(recipe.description));
        Poco::JSON::Array::Ptr params = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& param : recipe.params) {
            Poco::JSON::Object::Ptr p = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            p->set("name", std::string(param.name));
            p->set("kind", std::string(param.kind));
            p->set("description", std::string(param.description));
            p->set("default", param.defaultValue);
            params->add(p);
        }
        item->set("params", params);
        recipes->add(item);
    }
    out->set("recipes", recipes);
    out->set("count", static_cast<int>(builtinRecipes().size()));
    out->set("hint", "Call eve_recipe_apply with {id, params?} under content.scene / workshop.rpg");
    return textPayload(out);
}

std::string recipeApply(Poco::JSON::Object::Ptr args) {
    if (!args || !args->has("id")) {
        return errorPayload("id is required", "Pass {id:\"ruins.moonlit\", params:{seed,trees}} from eve_recipe_list");
    }
    const std::string id     = getArgString(args, "id");
    const RecipeDef*  recipe = findRecipe(id);
    if (!recipe) {
        return errorPayload("unknown recipe id '" + id + "'",
                            "Call eve_recipe_list; ids are registry strings, not free-form geometry");
    }

    Debugger&   dbg = Debugger::instance();
    HSQUIRRELVM vm  = dbg.vm();
    if (!vm) return errorPayload("no VM", "Attach a live eve run --mcp-port session first");

    std::string err;
    if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) {
        if (err == "deferred-to-update") {
            return errorPayload("deferred-to-update",
                                "scene_director install was queued for the next eve.dev.poll; retry eve_recipe_apply");
        }
        return errorPayload(err.empty() ? "scene_director kit unavailable" : err);
    }

    Poco::JSON::Object::Ptr params;
    if (args->has("params")) {
        try {
            params = args->getObject("params");
        } catch (...) {
            return errorPayload("params must be a JSON object");
        }
    }

    Poco::JSON::Array::Ptr  actions = expandRecipeActions(*recipe, params);
    Poco::JSON::Object::Ptr options = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    options->set("stopOnError", getArgBool(args, "stopOnError", true));
    auto actionsLiteral = sceneLiteral(Poco::Dynamic::Var(actions));
    if (!actionsLiteral.ok()) return errorPayload(actionsLiteral.status().describe());
    auto optionsLiteral = sceneLiteral(Poco::Dynamic::Var(options));
    if (!optionsLiteral.ok()) return errorPayload(optionsLiteral.status().describe());

    err.clear();
    const std::string snippet =
        "return ::scene_director.batch(" + actionsLiteral.value() + "," + optionsLiteral.value() + ");";
    const std::string sceneOut = callSceneDirectorReturn(vm, snippet, &err);
    if (!err.empty()) return errorPayload(err);

    Poco::JSON::Object::Ptr report = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    report->set("schemaId", "eve.recipe.apply-report");
    report->set("schemaVersion", 1);
    report->set("id", id);
    auto sceneObj = parseJsonObject(sceneOut);
    if (sceneObj) {
        report->set("scene", sceneObj);
        const bool ok = sceneObj->optValue<bool>("ok", false);
        report->set("ok", ok);
        if (sceneObj->has("revision")) report->set("revision", sceneObj->get("revision"));
        if (!ok) {
            report->set("error", "recipe apply failed");
            report->set("hint", "Inspect scene.results; recipe apply is transactional when stopOnError=true");
            return textPayload(report, true);
        }
    } else {
        report->set("ok", false);
        report->set("error", "scene batch returned non-object JSON");
        report->set("raw", sceneOut);
        return textPayload(report, true);
    }

    if (args->has("expectedRevision")) {
        const int expected = getArgInt(args, "expectedRevision", -1);
        const int actual   = sceneObj->optValue<int>("revision", -1);
        if (expected >= 0 && actual >= 0 && expected != actual) {
            report->set("ok", false);
            report->set("error", "revision mismatch");
            report->set("expectedRevision", expected);
            report->set("revision", actual);
            return textPayload(report, true);
        }
    }

    err.clear();
    const std::string infoOut = callSceneDirectorReturn(vm, "return ::scene_director.info();", &err);
    if (err.empty()) {
        if (auto info = parseJsonObject(infoOut)) report->set("info", info);
    }
    report->set("hint", "Recipe applied; escalate to verify.play for reachable/dialogue criteria");
    return textPayload(report);
}

}  // namespace

bool isMcpRecipeTool(std::string_view name) { return name == "eve_recipe_list" || name == "eve_recipe_apply"; }

std::string callMcpRecipeTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_recipe_list") return recipeList();
    if (name == "eve_recipe_apply") return recipeApply(args);
    return errorPayload("unknown recipe tool '" + std::string(name) + "'");
}

std::string_view mcpRecipeToolSchemas() {
    return R"json({"name":"eve_recipe_list","description":"List registered content recipes (id + params). Prefer recipe id over free-form prop scripts under content.scene.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_recipe_apply","description":"Apply one registered content recipe as a single scene_director.batch transaction. Returns {ok,id,scene,revision?,info}. Retry when retryable deferred-to-update.","inputSchema":{"type":"object","properties":{"id":{"type":"string","description":"Recipe id from eve_recipe_list"},"params":{"type":"object","description":"Recipe parameters (seed, trees, …)"},"expectedRevision":{"type":"integer","description":"Optional optimistic concurrency check"},"stopOnError":{"type":"boolean"}},"required":["id"]}})json";
}

}  // namespace eve::dev
