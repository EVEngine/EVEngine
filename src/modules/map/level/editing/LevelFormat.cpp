#include "map/level/editing/LevelFormat.h"
#include "common/Json.h"
#include "map/level/editing/LevelDocument.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace eve::level_editing {
namespace {
using Object = eve::Value::Object;
using Array  = eve::Value::Array;

bool validProperties(eve::json::Value value) {
    if (!value) return true;
    if (value.isObject()) {
        for (const auto& key : value.keys()) {
            auto item = value.get(key.c_str());
            if (!item.isString() && !item.isBool() && !item.isNumber()) return false;
        }
        return true;
    }
    if (!value.isArray()) return false;
    std::set<std::string> names;
    for (size_t index = 0; index < value.size(); ++index) {
        auto item = value.at(index);
        if (!item.isObject() || !item.get("name").isString() || !item.get("value") ||
            !names.insert(item.getString("name")).second)
            return false;
    }
    return true;
}

bool validNumbers(eve::json::Value value, std::initializer_list<const char*> fields) {
    for (const auto* key : fields) {
        auto item = value.get(key);
        if (item && (!item.isNumber() || !std::isfinite(item.asFloat()))) return false;
    }
    return true;
}

bool validId(const eve::Value& object, bool tiled) {
    const auto* id = object.find("id");
    if (!id) return true;
    return tiled ? id->isInt64() && id->asInt() > 0 && id->asInt() < std::numeric_limits<int>::max()
                 : id->isString() && !id->asString().empty();
}

eve::Value::Object extraFields(const eve::Value& value, std::initializer_list<const char*> known) {
    Object out;
    if (const auto* fields = value.getIf<Object>()) {
        for (const auto& [key, field] : *fields) {
            if (std::none_of(known.begin(), known.end(), [&](const char* name) { return key == name; }))
                out.emplace(key, field);
        }
    }
    return out;
}

// String properties remain in the existing editable map; other typed properties
// retain their complete Tiled records, without a second copy of editable values.
void readProperties(eve::json::Value v, const eve::Value* owned, std::unordered_map<std::string, std::string>& out,
                    Object& extras) {
    extras.erase("properties");
    if (v.isObject()) {
        Object typed;
        for (const auto& k : v.keys()) {
            if (v.get(k.c_str()).isString())
                out[k] = v.get(k.c_str()).asString();
            else if (owned)
                typed[k] = *owned->find(k);
        }
        if (!typed.empty()) extras["properties"] = std::move(typed);
    } else if (v.isArray()) {
        Array typed;
        for (size_t i = 0; i < v.size(); ++i) {
            auto p = v.at(i);
            if (p.getString("type", "string") == "string" && p.get("value").isString() && p.keys().size() == 3)
                out[p.getString("name")] = p.get("value").asString();
            else if (owned)
                typed.push_back(owned->at(i));
        }
        if (!typed.empty()) extras["properties"] = std::move(typed);
    }
}

eve::Value propertyValue(const std::unordered_map<std::string, std::string>& properties, const Object& extras,
                         bool tiled) {
    const auto found = extras.find("properties");
    if (!tiled && (found == extras.end() || found->second.isObject())) {
        Object out = found == extras.end() ? Object{} : *found->second.getIf<Object>();
        for (const auto& [key, value] : properties) out[key] = value;
        return out;
    }
    Array out;
    if (found != extras.end() && found->second.isArray()) {
        for (const auto& item : *found->second.getIf<Array>()) {
            const auto* name = item.find("name");
            if (!name || !name->isString() || !properties.contains(name->asString())) out.push_back(item);
        }
    }
    if (found != extras.end() && found->second.isObject()) {
        for (const auto& [key, value] : *found->second.getIf<Object>()) {
            if (properties.contains(key)) continue;
            out.emplace_back(Object{{"name", key},
                                    {"type", value.isBool()    ? "bool"
                                             : value.isInt64() ? "int"
                                                               : "float"},
                                    {"value", value}});
        }
    }
    // Sort the editable map before emission to make saves deterministic.
    for (const auto& [key, value] : std::map<std::string, std::string>(properties.begin(), properties.end()))
        out.emplace_back(Object{{"name", key}, {"type", "string"}, {"value", value}});
    return out;
}

std::int64_t numericId(const std::string& id) {
    std::int64_t value  = 0;
    auto         parsed = std::from_chars(id.data(), id.data() + id.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == id.data() + id.size() && value > 0 &&
                   value < std::numeric_limits<int>::max()
               ? value
               : 0;
}
}  // namespace

// Sole codec for the owned document and its unmodeled format fields.
class LevelFormatCodec {
public:
    static Object&       extensions(LevelDocument& document) { return document.extensions_; }
    static const Object& extensions(const LevelDocument& document) { return document.extensions_; }
};

namespace {
class JsonLevelFormat final : public LevelFormat {
public:
    std::string              id() const override { return "eve.level"; }
    std::vector<std::string> extensions() const override { return {".level.json", ".evelevel"}; }
    bool                     canRead(const std::string& s) const override {
        auto d = eve::json::Document::parse(s);
        return d.valid() && d.root().getString("format") == "eve.level";
    }
    eve::Result<std::unique_ptr<LevelDocument>> read(const std::string& s) const override { return readJson(s, false); }
    eve::Result<std::string> write(const LevelDocument& d) const override { return writeJson(d, false); }
    static eve::Result<std::unique_ptr<LevelDocument>> readJson(const std::string& s, bool tiled) {
        std::string error;
        auto        json = eve::json::Document::parse(s, &error);
        if (!json.valid())
            return eve::Result<std::unique_ptr<LevelDocument>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(error), {}, {}, "editor.level"));
        auto owned = eve::Value::fromJson(s);
        if (!owned.ok()) return eve::Result<std::unique_ptr<LevelDocument>>::failure(owned.status());
        const auto fail = [](const std::string& message, eve::DiagnosticCode code = eve::DiagnosticCode::Unsupported) {
            return eve::Result<std::unique_ptr<LevelDocument>>::failure(
                eve::Diagnostic::error(code, message, {}, {}, "editor.level"));
        };
        auto r = json.root();
        if (!r.isObject()) return fail("Map root must be an object", eve::DiagnosticCode::ParseError);
        const auto* version = owned.value().find("version");
        if (!tiled &&
            (r.getString("format") != "eve.level" || (version && (!version->isInt64() || version->asInt() != 1))))
            return fail("Unsupported eve.level schema version");
        if (tiled && r.getString("type") != "map") return fail("Expected a Tiled map");
        if (r.getBool("infinite", false)) return fail("Infinite maps are not yet editable by LevelDocument");
        if (!validProperties(r.get("properties")) || !validNumbers(r, {"tilewidth", "tileheight"}))
            return fail("Invalid map properties or tile dimensions", eve::DiagnosticCode::ParseError);
        int   w = r.getInt("width"), h = r.getInt("height");
        const auto* widthValue  = owned.value().find("width");
        const auto* heightValue = owned.value().find("height");
        if (!widthValue || !heightValue || !widthValue->isInt64() || !heightValue->isInt64())
            return fail("Map dimensions must be integers", eve::DiagnosticCode::ParseError);
        float tw = r.getFloat("tilewidth", 32), th = r.getFloat("tileheight", 32);
        if (w < 1 || h < 1 || int64_t(w) * h > 16 * 1024 * 1024 || !std::isfinite(tw) || !std::isfinite(th) ||
            tw <= 0 || th <= 0) {
            return eve::Result<std::unique_ptr<LevelDocument>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "map dimensions must be positive, finite and within the 16M-cell budget", {}, {}, "editor.level"));
        }
        const std::string orientation = r.getString("orientation", "orthogonal");
        if (orientation != "orthogonal" && orientation != "isometric" && orientation != "staggered" &&
            orientation != "hexagonal")
            return fail("Unsupported map orientation");
        auto out = std::make_unique<LevelDocument>(w, h, tw, th);
        out->setOrientation(orientation);
        LevelFormatCodec::extensions(*out) = extraFields(
            owned.value(),
            {"format", "version", "type", "width", "height", "tilewidth", "tileheight", "orientation", "layers"});
        readProperties(r.get("properties"), owned.value().find("properties"), out->properties(),
                       LevelFormatCodec::extensions(*out));
        auto ls = r.get("layers");
        if (!ls.isArray()) return fail("Map layers must be an array", eve::DiagnosticCode::ParseError);
        std::set<std::string> layerIds, objectIds;
        for (size_t i = 0; i < ls.size(); ++i) {
            auto        v    = ls.at(i);
            std::string kind = v.getString("type");
            bool        obj  = kind == "objectgroup" || kind == "objects";
            if (!v.isObject() || (!obj && kind != "tilelayer" && kind != "tiles"))
                return fail("Unsupported layer type: " + kind);
            if (!validId(owned.value().find("layers")->at(i), tiled) || !validProperties(v.get("properties")) ||
                !validNumbers(v, {"opacity", "offsetx", "offsety"}) || v.getFloat("opacity", 1) < 0 ||
                v.getFloat("opacity", 1) > 1)
                return fail("Invalid layer identity, properties or presentation", eve::DiagnosticCode::ParseError);
            if (v.get("chunks") || v.get("encoding") || v.get("compression"))
                return fail("Chunked or encoded tile layers require the runtime importer; editing is not supported");
            if (!obj && (v.getInt("width", w) != w || v.getInt("height", h) != h))
                return fail("Layer dimensions must match the editable map");
            int         li   = obj ? out->addObjectLayer(v.getString("name")) : out->addTileLayer(v.getString("name"));
            auto        layerRef = out->layer(li);
            auto*       l        = &layerRef->get();
            l->id                = tiled ? std::to_string(v.getInt("id", int(i + 1))) : v.getString("id", l->id);
            if (!layerIds.insert(l->id).second) return fail("Duplicate layer id", eve::DiagnosticCode::ParseError);
            l->extensions        = extraFields(owned.value().find("layers")->at(i),
                                               {"id", "name", "type", "visible", "locked", "opacity", "offsetx", "offsety",
                                                "objects", "data", "width", "height"});
            l->locked            = v.getBool("locked", false);
            l->visible           = v.getBool("visible", true);
            l->opacity           = v.getFloat("opacity", 1);
            l->offsetX           = v.getFloat("offsetx", 0);
            l->offsetY           = v.getFloat("offsety", 0);
            readProperties(v.get("properties"), owned.value().find("layers")->at(i).find("properties"), l->properties,
                           l->extensions);
            if (obj) {
                auto os = v.get("objects");
                if (!os.isArray()) return fail("Objects must be an array", eve::DiagnosticCode::ParseError);
                for (size_t j = 0; j < os.size(); ++j) {
                    auto q = os.at(j);
                    if (!q.isObject()) return fail("Map object must be an object", eve::DiagnosticCode::ParseError);
                    if (!validId(owned.value().find("layers")->at(i).find("objects")->at(j), tiled) ||
                        !validProperties(q.get("properties")) ||
                        !validNumbers(q, {"x", "y", "width", "height", "rotation"}))
                        return fail("Invalid object identity, properties or transform",
                                    eve::DiagnosticCode::ParseError);
                    const std::string objectClass = q.getString("class");
                    int               oi = out->addObject(li, objectClass.empty() ? q.getString("type") : objectClass,
                                                          q.getFloat("x"), q.getFloat("y"));
                    auto  objectRef = out->object(li, oi);
                    auto* ob        = &objectRef->get();
                    ob->id = tiled ? std::to_string(q.getInt("id", int(j + 1))) : q.getString("id", ob->id);
                    if (!objectIds.insert(ob->id).second)
                        return fail("Duplicate object id", eve::DiagnosticCode::ParseError);
                    const auto& ownedObject = owned.value().find("layers")->at(i).find("objects")->at(j);
                    ob->extensions  = extraFields(ownedObject, {"id", "name", "type", "class", "x", "y", "width",
                                                                "height", "rotation", "visible"});
                    ob->name        = q.getString("name");
                    ob->width       = q.getFloat("width");
                    ob->height      = q.getFloat("height");
                    ob->rotation    = q.getFloat("rotation");
                    ob->visible     = q.getBool("visible", true);
                    readProperties(q.get("properties"), ownedObject.find("properties"), ob->properties, ob->extensions);
                }
            } else {
                auto data = v.get("data");
                if (!data.isArray() || data.size() != size_t(w) * size_t(h))
                    return fail("Tile data must contain exactly width * height GIDs", eve::DiagnosticCode::ParseError);
                for (size_t n = 0; n < data.size(); ++n) {
                    const auto& raw = owned.value().find("layers")->at(i).find("data")->at(n);
                    // Native v1 historically emitted signed int GIDs. Accept
                    // that representation only on native import; emit uint32.
                    const int64_t minimumGid = tiled ? 0 : std::numeric_limits<int32_t>::min();
                    if (!raw.isInt64() || raw.asInt() < minimumGid ||
                        raw.asInt() > std::numeric_limits<uint32_t>::max())
                        return fail("Tile GID must be an unsigned 32-bit integer", eve::DiagnosticCode::ParseError);
                    l->tiles->setGid(int(n) % w, int(n) / w, std::bit_cast<int>(uint32_t(raw.asInt())));
                }
            }
        }
        return eve::Result<std::unique_ptr<LevelDocument>>::success(std::move(out));
    }
    static eve::Result<std::string> writeJson(const LevelDocument& d, bool tiled) {
        Object root = LevelFormatCodec::extensions(d);
        if (!tiled)
            root["format"] = "eve.level";
        else
            root["type"] = "map";
        root["version"]     = tiled ? eve::Value("1.10") : eve::Value(1);
        root["orientation"] = d.getOrientation();
        root["width"]       = d.getWidth();
        root["height"]      = d.getHeight();
        root["tilewidth"]   = d.getTileWidth();
        root["tileheight"]  = d.getTileHeight();
        root["properties"]  = propertyValue(d.properties(), LevelFormatCodec::extensions(d), tiled);
        int64_t nextLayer = 1, nextObject = 1;
        for (const auto& layer : d.layers()) {
            nextLayer = std::max(nextLayer, numericId(layer.id) + 1);
            for (const auto& object : layer.objects) nextObject = std::max(nextObject, numericId(object.id) + 1);
        }
        Array layers;
        for (const auto& layer : d.layers()) {
            Object value = layer.extensions;
            value["id"] =
                tiled ? eve::Value(numericId(layer.id) ? numericId(layer.id) : nextLayer++) : eve::Value(layer.id);
            value["name"]       = layer.name;
            value["type"]       = layer.kind == LevelLayer::Kind::Tiles ? "tilelayer" : "objectgroup";
            value["visible"]    = layer.visible;
            value["locked"]     = layer.locked;
            value["opacity"]    = layer.opacity;
            value["offsetx"]    = layer.offsetX;
            value["offsety"]    = layer.offsetY;
            value["properties"] = propertyValue(layer.properties, layer.extensions, tiled);
            if (layer.tiles) {
                value["width"]  = d.getWidth();
                value["height"] = d.getHeight();
                Array data;
                for (int y = 0; y < d.getHeight(); ++y)
                    for (int x = 0; x < d.getWidth(); ++x)
                        data.emplace_back(int64_t(uint32_t(layer.tiles->getGid(x, y))));
                value["data"] = std::move(data);
            } else {
                Array objects;
                for (const auto& object : layer.objects) {
                    Object item  = object.extensions;
                    item["id"]   = tiled ? eve::Value(numericId(object.id) ? numericId(object.id) : nextObject++)
                                         : eve::Value(object.id);
                    item["name"] = object.name;
                    item["type"] = object.type;
                    if (tiled) item["class"] = object.type;
                    item["x"]          = object.x;
                    item["y"]          = object.y;
                    item["width"]      = object.width;
                    item["height"]     = object.height;
                    item["rotation"]   = object.rotation;
                    item["visible"]    = object.visible;
                    item["properties"] = propertyValue(object.properties, object.extensions, tiled);
                    objects.emplace_back(std::move(item));
                }
                value["objects"] = std::move(objects);
            }
            layers.emplace_back(std::move(value));
        }
        if (tiled) {
            root["nextlayerid"]  = nextLayer;
            root["nextobjectid"] = nextObject;
        }
        root["layers"] = std::move(layers);
        return eve::json::stringify(root);
    }
};
class TiledFormat final : public LevelFormat {
public:
    std::string              id() const override { return "tiled.json"; }
    std::vector<std::string> extensions() const override { return {".tmj", ".json"}; }
    bool                     canRead(const std::string& s) const override {
        auto d = eve::json::Document::parse(s);
        return d.valid() && d.root().getString("type") == "map";
    }
    eve::Result<std::unique_ptr<LevelDocument>> read(const std::string& s) const override {
        return JsonLevelFormat::readJson(s, true);
    }
    eve::Result<std::string> write(const LevelDocument& d) const override {
        return JsonLevelFormat::writeJson(d, true);
    }
};
}  // namespace

LevelFormatRegistry::LevelFormatRegistry() {
    registerFormat(std::make_unique<JsonLevelFormat>()).expect("register native level format");
    registerFormat(std::make_unique<TiledFormat>()).expect("register Tiled level format");
}
eve::Result<void> LevelFormatRegistry::registerFormat(std::unique_ptr<LevelFormat> f) {
    if (!f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "level format must not be null", {}, {}, "editor.level"));
    for (auto& i : formats_)
        if (i->id() == f->id()) {
            i = std::move(f);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
    formats_.push_back(std::move(f));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
std::string LevelFormatRegistry::getFormatId(int i) const {
    return i < 0 || i >= int(formats_.size()) ? std::string() : formats_[i]->id();
}
eve::OptionalRef<const LevelFormat> LevelFormatRegistry::find(const std::string& id) const {
    for (auto& i : formats_)
        if (i->id() == id) return std::cref(*i);
    return {};
}
std::string LevelFormatRegistry::detect(const std::string& p, const std::string& t) const {
    for (auto& i : formats_)
        if (i->canRead(t)) return i->id();
    for (auto& i : formats_)
        for (auto& e : i->extensions())
            if (p.size() >= e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0) return i->id();
    return {};
}
eve::Result<std::unique_ptr<LevelDocument>> LevelFormatRegistry::decode(const std::string& id,
                                                                        const std::string& t) const {
    auto f = find(id);
    if (!f)
        return eve::Result<std::unique_ptr<LevelDocument>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "unknown level format: " + id, {}, {}, "editor.level"));
    return f->get().read(t);
}
eve::Result<std::string> LevelFormatRegistry::encode(const std::string& id, const LevelDocument& d) const {
    auto f = find(id);
    if (!f)
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "unknown level format: " + id, {}, {}, "editor.level"));
    return f->get().write(d);
}
eve::Result<std::unique_ptr<LevelDocument>> LevelFormatRegistry::load(const std::string& p,
                                                                      const std::string& id) const {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return eve::Result<std::unique_ptr<LevelDocument>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "cannot open level", p, {}, "editor.level"));
    }
    std::ostringstream s;
    s << f.rdbuf();
    auto fmt = id.empty() ? detect(p, s.str()) : id;
    return decode(fmt, s.str());
}
eve::Result<void> LevelFormatRegistry::save(const std::string& p, const LevelDocument& d, const std::string& id) const {
    auto fmt = id.empty() ? detect(p, "") : id;
    if (fmt.empty()) fmt = "eve.level";
    auto encoded = encode(fmt, d);
    if (!encoded) return eve::Result<void>::failure(encoded.status());
    const std::filesystem::path destination(p);
    // A uniquely created sibling directory reserves our temporary path. The
    // rename stays on the destination filesystem and never truncates its file.
    static std::atomic<unsigned long long> sequence{0};
    std::filesystem::path                  staging;
    std::error_code                        error;
    for (int attempt = 0; attempt < 8; ++attempt) {
        staging = destination;
        staging += ".save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                   std::to_string(sequence.fetch_add(1));
        if (std::filesystem::create_directory(staging, error)) break;
        staging.clear();
        if (error) break;
    }
    if (staging.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "cannot create save staging directory", p, {}, "editor.level"));
    struct StagingCleanup {
        std::filesystem::path path;
        ~StagingCleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{staging};
    const auto    temporary = staging / "document.json";
    std::ofstream f(temporary, std::ios::binary);
    if (!f) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "cannot write level", p, {}, "editor.level"));
    }
    f << encoded.value();
    f.close();
    if (!f)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "failed while writing level", p, {}, "editor.level"));
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "cannot replace level file", p, {}, "editor.level"));
#else
    std::filesystem::rename(temporary, destination, error);
    if (error)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "cannot replace level file: " + error.message(), p, {}, "editor.level"));
#endif
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::level_editing
