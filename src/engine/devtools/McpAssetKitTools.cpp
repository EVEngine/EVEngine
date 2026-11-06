#include "devtools/McpAssetKitTools.hpp"
#include "devtools/McpToolFamilies.hpp"

#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::dev {
namespace {

#include "AssetKitsEmbedded.inc"

constexpr const char* kApiId      = "eve.asset-kit.api";
constexpr int         kApiVersion = 1;

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message, const std::string& hint = {}) {
    auto failure = mcpFailureObject(message, hint);
    failure->set("api", kApiId);
    failure->set("apiVersion", kApiVersion);
    return textPayload(failure, true);
}

void stampApi(Poco::JSON::Object::Ptr out, Poco::JSON::Object::Ptr catalog) {
    out->set("api", kApiId);
    out->set("apiVersion", kApiVersion);
    out->set("schema", catalog ? catalog->optValue<std::string>("schema", "eve.asset-kit.catalog")
                               : "eve.asset-kit.catalog");
    out->set("schemaVersion", catalog ? catalog->optValue<int>("schemaVersion", 1) : 1);
}

Poco::JSON::Object::Ptr parseCatalog() {
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(std::string(kEmbeddedAssetKitCatalog));
        if (root.type() == typeid(Poco::JSON::Object::Ptr)) return root.extract<Poco::JSON::Object::Ptr>();
    } catch (...) {
    }
    return {};
}

bool arrayContainsString(Poco::JSON::Array::Ptr arr, const std::string& needle) {
    if (!arr) return false;
    for (std::size_t i = 0; i < arr->size(); ++i) {
        try {
            if (arr->getElement<std::string>(static_cast<unsigned>(i)) == needle) return true;
        } catch (...) {
        }
    }
    return false;
}

std::string toLower(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

Poco::JSON::Object::Ptr summarizeKit(Poco::JSON::Object::Ptr kit) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (!kit) return out;
    out->set("id", kit->optValue<std::string>("id", ""));
    out->set("genre", kit->optValue<std::string>("genre", ""));
    out->set("title", kit->optValue<std::string>("title", ""));
    out->set("summary", kit->optValue<std::string>("summary", ""));
    if (kit->has("modules")) out->set("modules", kit->get("modules"));
    if (kit->has("tags")) out->set("tags", kit->get("tags"));
    if (kit->has("referenceExample")) out->set("referenceExample", kit->get("referenceExample"));
    Poco::JSON::Array::Ptr required  = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    int                    slotCount = 0;
    if (kit->has("slots") && kit->isArray("slots")) {
        auto slots = kit->getArray("slots");
        slotCount  = static_cast<int>(slots->size());
        for (std::size_t i = 0; i < slots->size(); ++i) {
            auto slot = slots->getObject(static_cast<unsigned>(i));
            if (slot && slot->optValue<bool>("required", false)) required->add(slot->optValue<std::string>("id", ""));
        }
    }
    out->set("slotCount", slotCount);
    out->set("requiredSlots", required);
    out->set("source", "engine");
    return out;
}

bool kitMatchesFilters(Poco::JSON::Object::Ptr kit, const std::string& genre, const std::string& module,
                       const std::string& tag, const std::string& q) {
    if (!kit) return false;
    if (!genre.empty() && kit->optValue<std::string>("genre", "") != genre) return false;
    if (!module.empty()) {
        if (!kit->has("modules") || !kit->isArray("modules") ||
            !arrayContainsString(kit->getArray("modules"), module))
            return false;
    }
    if (!tag.empty()) {
        if (!kit->has("tags") || !kit->isArray("tags") || !arrayContainsString(kit->getArray("tags"), tag))
            return false;
    }
    if (!q.empty()) {
        const std::string qLower = toLower(q);
        std::string       blob   = kit->optValue<std::string>("id", "") + " " +
                           kit->optValue<std::string>("title", "") + " " +
                           kit->optValue<std::string>("summary", "") + " " +
                           kit->optValue<std::string>("genre", "");
        if (kit->has("tags") && kit->isArray("tags")) {
            auto tags = kit->getArray("tags");
            for (std::size_t i = 0; i < tags->size(); ++i) {
                try {
                    blob += " ";
                    blob += tags->getElement<std::string>(static_cast<unsigned>(i));
                } catch (...) {
                }
            }
        }
        if (toLower(blob).find(qLower) == std::string::npos) return false;
    }
    return true;
}

std::string fillTemplate(const std::string& templ, const std::unordered_map<std::string, std::string>& values,
                         std::vector<std::string>* unresolved) {
    std::string out;
    out.reserve(templ.size());
    for (std::size_t i = 0; i < templ.size();) {
        if (templ[i] != '{') {
            out.push_back(templ[i++]);
            continue;
        }
        const std::size_t end = templ.find('}', i + 1);
        if (end == std::string::npos) {
            out.push_back(templ[i++]);
            continue;
        }
        const std::string key = templ.substr(i + 1, end - i - 1);
        auto              it  = values.find(key);
        if (it == values.end() || it->second.empty()) {
            if (unresolved) unresolved->push_back(key);
            out.append(templ, i, end - i + 1);
        } else {
            out += it->second;
        }
        i = end + 1;
    }
    return out;
}

std::string assetKitList(Poco::JSON::Object::Ptr args) {
    auto catalog = parseCatalog();
    if (!catalog)
        return errorPayload("embedded asset-kit catalog failed to parse",
                            "Rebuild after running python3 scripts/embed_asset_kits.py");

    const std::string genre  = getArgString(args, "genre", "");
    const std::string module = getArgString(args, "module", "");
    const std::string tag    = getArgString(args, "tag", "");
    const std::string q      = getArgString(args, "q", "");

    Poco::JSON::Object::Ptr out   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  kits  = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    int                     count = 0;
    if (catalog->has("kits") && catalog->isArray("kits")) {
        auto arr = catalog->getArray("kits");
        for (std::size_t i = 0; i < arr->size(); ++i) {
            auto kit = arr->getObject(static_cast<unsigned>(i));
            if (!kitMatchesFilters(kit, genre, module, tag, q)) continue;
            kits->add(summarizeKit(kit));
            ++count;
        }
    }
    stampApi(out, catalog);
    out->set("ok", true);
    out->set("count", count);
    out->set("kits", kits);
    out->set("hint",
             "Call eve_asset_kit_get / eve_asset_kit_resolve before generating art; do not invent "
             "Unity/Godot sizes");
    return textPayload(out);
}

std::string assetKitGet(Poco::JSON::Object::Ptr args) {
    const std::string id = getArgString(args, "id", "");
    if (id.empty())
        return errorPayload("id is required", "Pass {id:\"visual-novel.dialogue\"} from eve_asset_kit_list");

    auto catalog = parseCatalog();
    if (!catalog) return errorPayload("embedded asset-kit catalog failed to parse");

    std::vector<std::string> known;
    if (catalog->has("kits") && catalog->isArray("kits")) {
        auto arr = catalog->getArray("kits");
        for (std::size_t i = 0; i < arr->size(); ++i) {
            auto kit = arr->getObject(static_cast<unsigned>(i));
            if (!kit) continue;
            const std::string kid = kit->optValue<std::string>("id", "");
            known.push_back(kid);
            if (kid == id) {
                Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
                stampApi(out, catalog);
                out->set("ok", true);
                out->set("kit", kit);
                out->set("source", "engine");
                out->set("hint",
                         "Generate each required slot at pixelSize; write pathPattern; bind via "
                         "consumer/bindings");
                return textPayload(out);
            }
        }
    }

    std::string knownList;
    for (std::size_t i = 0; i < known.size(); ++i) {
        if (i) knownList += ", ";
        knownList += known[i];
    }
    return errorPayload("unknown kit id: " + id, "known ids: " + knownList);
}

std::string assetKitResolve(Poco::JSON::Object::Ptr args) {
    const std::string kitId  = getArgString(args, "kit", getArgString(args, "id", ""));
    const std::string slotId = getArgString(args, "slot", "");
    if (kitId.empty() || slotId.empty())
        return errorPayload("kit and slot are required",
                            "Pass {kit:\"card.playing\", slot:\"face\", vars:{...}}");

    auto catalog = parseCatalog();
    if (!catalog) return errorPayload("embedded asset-kit catalog failed to parse");

    Poco::JSON::Object::Ptr kit;
    std::vector<std::string> knownKits;
    if (catalog->has("kits") && catalog->isArray("kits")) {
        auto arr = catalog->getArray("kits");
        for (std::size_t i = 0; i < arr->size(); ++i) {
            auto candidate = arr->getObject(static_cast<unsigned>(i));
            if (!candidate) continue;
            const std::string kid = candidate->optValue<std::string>("id", "");
            knownKits.push_back(kid);
            if (kid == kitId) kit = candidate;
        }
    }
    if (!kit) {
        std::string knownList;
        for (std::size_t i = 0; i < knownKits.size(); ++i) {
            if (i) knownList += ", ";
            knownList += knownKits[i];
        }
        return errorPayload("unknown kit id: " + kitId, "known ids: " + knownList);
    }

    Poco::JSON::Object::Ptr slot;
    std::vector<std::string> knownSlots;
    if (kit->has("slots") && kit->isArray("slots")) {
        auto slots = kit->getArray("slots");
        for (std::size_t i = 0; i < slots->size(); ++i) {
            auto candidate = slots->getObject(static_cast<unsigned>(i));
            if (!candidate) continue;
            const std::string sid = candidate->optValue<std::string>("id", "");
            knownSlots.push_back(sid);
            if (sid == slotId) slot = candidate;
        }
    }
    if (!slot) {
        std::string knownList;
        for (std::size_t i = 0; i < knownSlots.size(); ++i) {
            if (i) knownList += ", ";
            knownList += knownSlots[i];
        }
        return errorPayload("unknown slot id: " + slotId, "kit " + kitId + " slots: " + knownList);
    }

    std::unordered_map<std::string, std::string> values;
    auto                                         size = slot->has("pixelSize") && slot->isObject("pixelSize")
                                                            ? slot->getObject("pixelSize")
                                                            : Poco::JSON::Object::Ptr{};
    if (size) {
        values["width"]  = std::to_string(size->optValue<int>("width", 0));
        values["height"] = std::to_string(size->optValue<int>("height", 0));
    }
    values["aspectRatio"] = slot->optValue<std::string>("aspectRatio", "");
    values["kitId"]       = kitId;
    values["slotId"]      = slotId;
    values["format"]      = slot->optValue<std::string>("format", "");

    if (args && args->has("vars") && args->isObject("vars")) {
        auto vars = args->getObject("vars");
        for (const auto& name : vars->getNames()) {
            try {
                values[name] = vars->getValue<std::string>(name);
            } catch (...) {
                try {
                    values[name] = std::to_string(vars->getValue<int>(name));
                } catch (...) {
                    try {
                        values[name] = std::to_string(vars->getValue<double>(name));
                    } catch (...) {
                    }
                }
            }
        }
    }

    std::vector<std::string> unresolved;
    const std::string        path =
        fillTemplate(slot->optValue<std::string>("pathPattern", ""), values, &unresolved);
    const std::string prompt =
        fillTemplate(slot->optValue<std::string>("promptTemplate", ""), values, &unresolved);
    std::unordered_set<std::string> uniq(unresolved.begin(), unresolved.end());
    unresolved.assign(uniq.begin(), uniq.end());
    std::sort(unresolved.begin(), unresolved.end());

    Poco::JSON::Object::Ptr varsOut = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    for (const auto& [key, value] : values) varsOut->set(key, value);

    Poco::JSON::Array::Ptr unresolvedArr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& key : unresolved) unresolvedArr->add(key);

    Poco::JSON::Array::Ptr bindings = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    if (kit->has("bindings") && kit->isArray("bindings")) {
        auto arr = kit->getArray("bindings");
        for (std::size_t i = 0; i < arr->size(); ++i) bindings->add(arr->get(static_cast<unsigned>(i)));
    }
    if (slot->has("bindings") && slot->isArray("bindings")) {
        auto arr = slot->getArray("bindings");
        for (std::size_t i = 0; i < arr->size(); ++i) bindings->add(arr->get(static_cast<unsigned>(i)));
    }

    Poco::JSON::Object::Ptr job = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    job->set("kitId", kitId);
    job->set("slotId", slotId);
    job->set("role", slot->optValue<std::string>("role", ""));
    job->set("required", slot->optValue<bool>("required", false));
    if (size) job->set("pixelSize", size);
    if (slot->has("displaySize")) job->set("displaySize", slot->get("displaySize"));
    job->set("aspectRatio", slot->optValue<std::string>("aspectRatio", ""));
    job->set("format", slot->optValue<std::string>("format", ""));
    job->set("colorType", slot->optValue<std::string>("colorType", ""));
    job->set("alpha", slot->optValue<std::string>("alpha", ""));
    if (slot->has("safeMargin")) job->set("safeMargin", slot->get("safeMargin"));
    job->set("path", path);
    job->set("pathPattern", slot->optValue<std::string>("pathPattern", ""));
    job->set("prompt", prompt);
    if (slot->has("promptTemplate")) job->set("promptTemplate", slot->get("promptTemplate"));
    if (slot->has("constraints")) job->set("constraints", slot->get("constraints"));
    if (slot->has("consumer")) job->set("consumer", slot->get("consumer"));
    job->set("bindings", bindings);
    job->set("vars", varsOut);
    job->set("unresolved", unresolvedArr);
    job->set("source", "engine");

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    stampApi(out, catalog);
    out->set("ok", true);
    out->set("job", job);
    out->set("hint", unresolved.empty()
                         ? "Generate at pixelSize; write job.path; bind via consumer/bindings"
                         : "Fill unresolved placeholders then generate at pixelSize");
    return textPayload(out);
}

}  // namespace

bool isMcpAssetKitTool(std::string_view name) {
    return name == "eve_asset_kit_list" || name == "eve_asset_kit_get" || name == "eve_asset_kit_resolve";
}

std::string callMcpAssetKitTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_asset_kit_list") return assetKitList(args);
    if (name == "eve_asset_kit_get") return assetKitGet(args);
    if (name == "eve_asset_kit_resolve") return assetKitResolve(args);
    return errorPayload("unknown asset-kit tool '" + std::string(name) + "'");
}

std::string_view mcpAssetKitToolSchemas() {
    return R"json({"name":"eve_asset_kit_list","description":"List engine-owned AI asset kits (pixel/format contracts). Filter with genre/module/tag/q. Call before generating game art; do not invent Unity/Godot sizes. Same contract as scripts/asset_kits.py list and share/eve/ai/asset-kits/.","inputSchema":{"type":"object","properties":{"genre":{"type":"string","description":"Optional genre filter (card, visual-novel, …)"},"module":{"type":"string","description":"Optional module name filter (card, dialogue, …)"},"tag":{"type":"string","description":"Optional discovery tag filter"},"q":{"type":"string","description":"Optional substring on id/title/summary/tags"}}}},{"name":"eve_asset_kit_get","description":"Return one asset kit document: slots with pixelSize, aspectRatio, format, constraints, promptTemplate, pathPattern, consumer, and multi-system bindings.","inputSchema":{"type":"object","properties":{"id":{"type":"string","description":"Kit id from eve_asset_kit_list (e.g. card.playing, visual-novel.dialogue)"}},"required":["id"]}},{"name":"eve_asset_kit_resolve","description":"Resolve one kit slot into a concrete generation job (path, prompt, pixelSize, bindings). Pass vars to fill {placeholders}. Universal hand-off for pipelines and internal systems.","inputSchema":{"type":"object","properties":{"kit":{"type":"string","description":"Kit id"},"slot":{"type":"string","description":"Slot id within the kit"},"vars":{"type":"object","description":"Placeholder values (suit, rank, style, characterId, …)"}},"required":["kit","slot"]}})json";
}

}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(AssetKit, mcpAssetKitToolSchemas, callMcpAssetKitTool, ContentEnvelope)
}  // namespace eve::dev
