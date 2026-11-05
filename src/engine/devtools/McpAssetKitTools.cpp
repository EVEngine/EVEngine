#include "devtools/McpAssetKitTools.hpp"
#include "devtools/McpToolFamilies.hpp"

#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {
namespace {

#include "AssetKitsEmbedded.inc"

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message, const std::string& hint = {}) {
    return textPayload(mcpFailureObject(message, hint), true);
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

Poco::JSON::Object::Ptr summarizeKit(Poco::JSON::Object::Ptr kit) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (!kit) return out;
    out->set("id", kit->optValue<std::string>("id", ""));
    out->set("genre", kit->optValue<std::string>("genre", ""));
    out->set("title", kit->optValue<std::string>("title", ""));
    out->set("summary", kit->optValue<std::string>("summary", ""));
    if (kit->has("modules")) out->set("modules", kit->get("modules"));
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
    return out;
}

std::string assetKitList() {
    auto catalog = parseCatalog();
    if (!catalog)
        return errorPayload("embedded asset-kit catalog failed to parse",
                            "Rebuild after running python3 scripts/embed_asset_kits.py");
    Poco::JSON::Object::Ptr out   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  kits  = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    int                     count = 0;
    if (catalog->has("kits") && catalog->isArray("kits")) {
        auto arr = catalog->getArray("kits");
        count    = static_cast<int>(arr->size());
        for (std::size_t i = 0; i < arr->size(); ++i) {
            kits->add(summarizeKit(arr->getObject(static_cast<unsigned>(i))));
        }
    }
    out->set("ok", true);
    out->set("schema", catalog->optValue<std::string>("schema", "eve.asset-kit.catalog"));
    out->set("schemaVersion", catalog->optValue<int>("schemaVersion", 1));
    out->set("count", count);
    out->set("kits", kits);
    out->set("hint", "Call eve_asset_kit_get with {id} before generating art; do not invent Unity/Godot sizes");
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
                out->set("ok", true);
                out->set("kit", kit);
                out->set("hint", "Generate each required slot at pixelSize; write pathPattern; bind via consumer");
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

}  // namespace

bool isMcpAssetKitTool(std::string_view name) { return name == "eve_asset_kit_list" || name == "eve_asset_kit_get"; }

std::string callMcpAssetKitTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_asset_kit_list") return assetKitList();
    if (name == "eve_asset_kit_get") return assetKitGet(args);
    return errorPayload("unknown asset-kit tool '" + std::string(name) + "'");
}

std::string_view mcpAssetKitToolSchemas() {
    return R"json({"name":"eve_asset_kit_list","description":"List engine-owned AI asset kits (card / visual-novel pixel contracts). Call before generating game art; do not invent Unity/Godot sizes.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_asset_kit_get","description":"Return one asset kit: slot pixelSize, aspectRatio, format, constraints, promptTemplate, pathPattern, and EveScript consumer hints.","inputSchema":{"type":"object","properties":{"id":{"type":"string","description":"Kit id from eve_asset_kit_list (e.g. card.playing, visual-novel.dialogue)"}},"required":["id"]}})json";
}

}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(AssetKit, mcpAssetKitToolSchemas, callMcpAssetKitTool, ContentEnvelope)
}  // namespace eve::dev
