#include "map/TileConfig.h"
#include "map/TileOrientation.h"

#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "filesystem/HotReload.h"
#include "graphics/Graphics.h"

#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/Element.h>
#include <Poco/DOM/NodeList.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace eve::map {
namespace {

float asFloat(const Poco::Dynamic::Var &v, float fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return static_cast<float>(v.convert<double>());
    } catch (...) {
        return fallback;
    }
}

int asInt(const Poco::Dynamic::Var &v, int fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<int>();
    } catch (...) {
        return fallback;
    }
}

uint32_t asUInt32(const Poco::Dynamic::Var &v, uint32_t fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return uint32_t(v.convert<uint64_t>() & 0xffffffffu);
    } catch (...) {
        return fallback;
    }
}

bool asBool(const Poco::Dynamic::Var &v, bool fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<bool>();
    } catch (...) {
        return fallback;
    }
}

std::string asString(const Poco::Dynamic::Var &v) {
    try {
        if (v.isEmpty()) return {};
        return v.convert<std::string>();
    } catch (...) {
        return {};
    }
}

bool readVec2(Poco::JSON::Object::Ptr o, const char *key, float &a, float &b) {
    if (!o || !o->has(key)) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = o->getArray(key);
    } catch (...) {
        return false;
    }
    if (!arr || arr->size() < 2) return false;
    a = asFloat(arr->get(0), a);
    b = asFloat(arr->get(1), b);
    return true;
}

bool readVec4(Poco::JSON::Object::Ptr o, const char *key, float &a, float &b, float &c, float &d) {
    if (!o || !o->has(key)) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = o->getArray(key);
    } catch (...) {
        return false;
    }
    if (!arr || arr->size() < 4) return false;
    a = asFloat(arr->get(0), a);
    b = asFloat(arr->get(1), b);
    c = asFloat(arr->get(2), c);
    d = asFloat(arr->get(3), d);
    return true;
}

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

graphics::Texture *tryLoadTexture(const std::string &path) {
    if (path.empty()) return nullptr;
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return nullptr;
    try {
        graphics::Texture *tex = gfx->newTextureFromFile(path);
        if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
            hot->bind(path, "texture");
        return tex;
    } catch (...) {
        return nullptr;
    }
}

struct TilesetInfo {
    std::string image;
    std::string                                  sourcePath;
    int firstGid = 1;
    int columns = 1;
    int tileW = 32;
    int tileH = 32;
    int margin = 0;
    int spacing = 0;
    std::vector<TileLayer::Tileset::Visual> visuals;
    std::vector<TileLayer::Tileset::Animation> animations;
    std::vector<TileLayer::Tileset::TerrainRule> terrainRules;
    std::vector<TileLayer::Tileset::CustomData> customData;
};

TileLayer::Tileset::Visual readTileVisual(Poco::JSON::Object::Ptr o, int fallbackGid) {
    TileLayer::Tileset::Visual visual;
    if (!o) return visual;
    visual.gid = o->has("gid") ? asInt(o->get("gid"), fallbackGid) : fallbackGid;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (readVec4(o, "region", x, y, w, h)) {
        visual.x = int(x);
        visual.y = int(y);
        visual.width = int(w);
        visual.height = int(h);
    }
    readVec2(o, "pivot", visual.pivotX, visual.pivotY);
    visual.sortBias = o->has("sortBias") ? asFloat(o->get("sortBias"), 0.f) : 0.f;
    float fw = 1.f, fh = 1.f;
    if (readVec2(o, "footprint", fw, fh)) {
        visual.footprintW = std::max(1, int(fw));
        visual.footprintH = std::max(1, int(fh));
    }
    visual.walkable = o->has("walkable") ? asBool(o->get("walkable"), true) : true;
    visual.cost = o->has("cost") ? std::max(0.001f, asFloat(o->get("cost"), 1.f)) : 1.f;
    if (o->has("properties")) {
        try {
            auto properties = o->getArray("properties");
            for (size_t index = 0; properties && index < properties->size(); ++index) {
                auto property = properties->getObject(static_cast<unsigned int>(index));
                if (!property || !property->has("name") || !property->has("value")) continue;
                const std::string name = asString(property->get("name"));
                if (name == "walkable")
                    visual.walkable = asBool(property->get("value"), true);
                else if (name == "cost")
                    visual.cost = std::max(0.001f, asFloat(property->get("value"), 1.f));
                else if (name == "enterMask")
                    visual.enterMask = uint8_t(asInt(property->get("value"), 0xff) & 0xff);
                else if (name == "exitMask")
                    visual.exitMask = uint8_t(asInt(property->get("value"), 0xff) & 0xff);
                else if (name == "opaque")
                    visual.opaque = asBool(property->get("value"), false);
                else if (name == "semanticFlags")
                    visual.semanticFlags = uint32_t(asInt(property->get("value"), 0));
            }
        } catch (...) {
        }
    }
    if (o->has("objectgroup")) {
        try {
            auto group   = o->getObject("objectgroup");
            auto objects = group ? group->getArray("objects") : nullptr;
            for (size_t index = 0; objects && index < objects->size(); ++index) {
                auto object = objects->getObject(unsigned(index));
                if (!object) continue;
                float x      = object->has("x") ? asFloat(object->get("x"), 0.f) : 0.f;
                float y      = object->has("y") ? asFloat(object->get("y"), 0.f) : 0.f;
                float width  = object->has("width") ? asFloat(object->get("width"), 0.f) : 0.f;
                float height = object->has("height") ? asFloat(object->get("height"), 0.f) : 0.f;
                if (object->has("polygon")) {
                    auto  polygon = object->getArray("polygon");
                    float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
                    for (size_t pointIndex = 0; polygon && pointIndex < polygon->size(); ++pointIndex) {
                        auto point = polygon->getObject(unsigned(pointIndex));
                        if (!point) continue;
                        const float px = asFloat(point->get("x"), 0.f);
                        const float py = asFloat(point->get("y"), 0.f);
                        minX           = std::min(minX, px);
                        minY           = std::min(minY, py);
                        maxX           = std::max(maxX, px);
                        maxY           = std::max(maxY, py);
                    }
                    x += minX;
                    y += minY;
                    width  = maxX - minX;
                    height = maxY - minY;
                }
                if (width > 0.f && height > 0.f) visual.collisionShapes.push_back({x, y, width, height});
            }
            if (!visual.collisionShapes.empty()) visual.walkable = false;
        } catch (...) {
        }
    }
    return visual;
}

TilesetInfo readTilesetObject(Poco::JSON::Object::Ptr o) {
    TilesetInfo info;
    if (!o) return info;
    if (o->has("image")) info.image = asString(o->get("image"));
    else if (o->has("texture")) info.image = asString(o->get("texture"));
    if (o->has("firstgid")) info.firstGid = asInt(o->get("firstgid"), 1);
    else if (o->has("firstGid")) info.firstGid = asInt(o->get("firstGid"), 1);
    if (o->has("columns")) info.columns = asInt(o->get("columns"), 1);
    if (o->has("tilewidth")) info.tileW = asInt(o->get("tilewidth"), 32);
    else if (o->has("tileWidth")) info.tileW = asInt(o->get("tileWidth"), 32);
    if (o->has("tileheight")) info.tileH = asInt(o->get("tileheight"), 32);
    else if (o->has("tileHeight")) info.tileH = asInt(o->get("tileHeight"), 32);
    if (o->has("margin")) info.margin = asInt(o->get("margin"), 0);
    if (o->has("spacing")) info.spacing = asInt(o->get("spacing"), 0);
    if (info.columns <= 0 && o->has("imagewidth") && info.tileW > 0) {
        const int iw = asInt(o->get("imagewidth"), 0);
        if (iw > 0) info.columns = std::max(1, (iw - info.margin) / (info.tileW + info.spacing));
    }
    if (o->has("tiles")) {
        try {
            auto arr = o->getArray("tiles");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    auto tile = arr->getObject(static_cast<unsigned int>(i));
                    const int localId = tile && tile->has("id") ? asInt(tile->get("id"), int(i))
                                                                : int(i);
                    const int gid = tile && tile->has("gid")
                                        ? asInt(tile->get("gid"), info.firstGid + localId)
                                        : info.firstGid + localId;
                    auto visual = readTileVisual(tile, gid);
                    if (visual.gid > 0) info.visuals.push_back(visual);
                    if (!tile) continue;
                    if (tile->has("animation")) {
                        auto frames = tile->getArray("animation");
                        TileLayer::Tileset::Animation animation;
                        animation.gid = gid;
                        if (frames) {
                            for (size_t frameIndex = 0; frameIndex < frames->size(); ++frameIndex) {
                                auto frame = frames->getObject(static_cast<unsigned int>(frameIndex));
                                if (!frame) continue;
                                const int frameLocal = asInt(frame->get("tileid"), localId);
                                const int duration = frame->has("duration")
                                                         ? asInt(frame->get("duration"), 100)
                                                         : 100;
                                animation.frames.push_back(
                                    {info.firstGid + frameLocal, std::max(1, duration)});
                            }
                        }
                        if (!animation.frames.empty()) info.animations.push_back(std::move(animation));
                    }
                    if (tile->has("terrain") && tile->has("neighborMask")) {
                        info.terrainRules.push_back(
                            {gid, asInt(tile->get("terrain"), 0),
                             asInt(tile->get("neighborMask"), 0) & 0xff});
                    }
                    if (tile->has("properties")) {
                        auto properties = tile->getArray("properties");
                        if (properties) {
                            for (size_t propertyIndex = 0; propertyIndex < properties->size();
                                 ++propertyIndex) {
                                auto property =
                                    properties->getObject(static_cast<unsigned int>(propertyIndex));
                                if (!property || !property->has("name") || !property->has("value"))
                                    continue;
                                const std::string type = property->has("type")
                                                             ? asString(property->get("type"))
                                                             : "string";
                                info.customData.push_back(
                                    {gid, asString(property->get("name")), type,
                                     asString(property->get("value"))});
                            }
                        }
                    }
                }
            }
        } catch (...) {
        }
    }
    if (o->has("wangsets")) {
        try {
            auto sets = o->getArray("wangsets");
            for (size_t setIndex = 0; sets && setIndex < sets->size(); ++setIndex) {
                auto set = sets->getObject(static_cast<unsigned int>(setIndex));
                if (!set || !set->has("wangtiles")) continue;
                auto wangTiles = set->getArray("wangtiles");
                for (size_t tileIndex = 0; wangTiles && tileIndex < wangTiles->size(); ++tileIndex) {
                    auto wangTile = wangTiles->getObject(static_cast<unsigned int>(tileIndex));
                    if (!wangTile || !wangTile->has("tileid") || !wangTile->has("wangid")) continue;
                    auto wangId = wangTile->getArray("wangid");
                    if (!wangId || wangId->size() != 8) continue;
                    const int gid = info.firstGid + asInt(wangTile->get("tileid"), 0);
                    // Tiled orders Wang positions N, NE, E, SE, S, SW, W, NW.
                    // EVEngine terrain masks order NW, N, NE, E, SE, S, SW, W.
                    constexpr int tiledToTerrainBit[8] = {1, 2, 3, 4, 5, 6, 7, 0};
                    for (int color = 1; color <= 255; ++color) {
                        int  mask    = 0;
                        bool present = false;
                        for (int position = 0; position < 8; ++position) {
                            if (asInt(wangId->get(static_cast<unsigned int>(position)), 0) != color) continue;
                            present = true;
                            mask |= 1 << tiledToTerrainBit[position];
                        }
                        if (present) info.terrainRules.push_back({gid, int(setIndex) * 256 + color, mask});
                    }
                }
            }
        } catch (...) {
        }
    }
    return info;
}

std::string resolveAssetPath(const std::string &ownerPath, const std::string &referencedPath) {
    if (ownerPath.empty() || referencedPath.empty()) return referencedPath;
    const std::filesystem::path reference(referencedPath);
    if (reference.is_absolute()) return reference.lexically_normal().generic_string();
    return (std::filesystem::path(ownerPath).parent_path() / reference).lexically_normal().generic_string();
}

bool readImportText(eve::filesystem::Filesystem *fs, const std::string &path, std::string &text) {
    if (fs) {
        try {
            std::unique_ptr<eve::filesystem::FileData> bytes(fs->read(path));
            if (bytes && bytes->getSize() > 0) {
                text.assign(static_cast<const char *>(bytes->getData()), bytes->getSize());
                return true;
            }
        } catch (...) {
        }
    }
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) return false;
    std::ostringstream stream;
    stream << input.rdbuf();
    text = stream.str();
    return !text.empty();
}

Poco::JSON::Object::Ptr readJsonObject(eve::filesystem::Filesystem *fs, const std::string &path, std::string *error) {
    if (!fs) {
        if (error) *error = "map.import.filesystem-unavailable: " + path;
        return {};
    }
    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "map.import.external-tileset-empty: " + path;
        return {};
    }
    auto                               *dataModule = eve::data::DataModule::create();
    std::string                         decodeError;
    std::unique_ptr<data::JsonDocument> document(dataModule->decodeJson(text, &decodeError));
    if (!document || !document->isObject()) {
        if (error)
            *error = "map.import.external-tileset-invalid-json: " + path +
                     (decodeError.empty() ? std::string{} : " (" + decodeError + ")");
        return {};
    }
    return document->object();
}

int xmlInt(Poco::XML::Element *element, const std::string &name, int fallback) {
    if (!element || !element->hasAttribute(name)) return fallback;
    try {
        return std::stoi(element->getAttribute(name));
    } catch (...) {
        return fallback;
    }
}

TilesetInfo readTsxTileset(eve::filesystem::Filesystem *fs, const std::string &path, int firstGid, std::string *error) {
    TilesetInfo info;
    info.firstGid = firstGid;
    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "map.import.external-tileset-empty: " + path;
        return info;
    }
    try {
        Poco::XML::DOMParser               parser;
        Poco::AutoPtr<Poco::XML::Document> document = parser.parseString(text);
        auto                              *root     = document ? document->documentElement() : nullptr;
        if (!root || root->tagName() != "tileset") throw std::runtime_error("root is not tileset");
        info.columns                              = xmlInt(root, "columns", 1);
        info.tileW                                = xmlInt(root, "tilewidth", 32);
        info.tileH                                = xmlInt(root, "tileheight", 32);
        info.margin                               = xmlInt(root, "margin", 0);
        info.spacing                              = xmlInt(root, "spacing", 0);
        Poco::AutoPtr<Poco::XML::NodeList> images = root->getElementsByTagName("image");
        if (images && images->length() > 0) {
            auto *image = dynamic_cast<Poco::XML::Element *>(images->item(0));
            if (image) info.image = resolveAssetPath(path, image->getAttribute("source"));
        }
        Poco::AutoPtr<Poco::XML::NodeList> tiles = root->getElementsByTagName("tile");
        for (unsigned long index = 0; tiles && index < tiles->length(); ++index) {
            auto *tile = dynamic_cast<Poco::XML::Element *>(tiles->item(index));
            if (!tile || tile->parentNode() != root) continue;
            TileLayer::Tileset::Visual visual;
            const int                  localId            = xmlInt(tile, "id", int(index));
            visual.gid                                    = firstGid + localId;
            Poco::AutoPtr<Poco::XML::NodeList> properties = tile->getElementsByTagName("property");
            for (unsigned long propertyIndex = 0; properties && propertyIndex < properties->length(); ++propertyIndex) {
                auto *property = dynamic_cast<Poco::XML::Element *>(properties->item(propertyIndex));
                if (!property) continue;
                const std::string name  = property->getAttribute("name");
                const std::string type  = property->getAttribute("type");
                const std::string value = property->getAttribute("value");
                info.customData.push_back({visual.gid, name, type.empty() ? "string" : type, value});
                if (name == "walkable")
                    visual.walkable = value != "false" && value != "0";
                else if (name == "cost") {
                    try {
                        visual.cost = std::max(0.001f, std::stof(value));
                    } catch (...) {
                    }
                } else if (name == "enterMask") {
                    visual.enterMask = uint8_t(xmlInt(property, "value", 0xff));
                } else if (name == "exitMask") {
                    visual.exitMask = uint8_t(xmlInt(property, "value", 0xff));
                } else if (name == "opaque") {
                    visual.opaque = value != "false" && value != "0";
                } else if (name == "semanticFlags") {
                    visual.semanticFlags = uint32_t(xmlInt(property, "value", 0));
                }
            }
            Poco::AutoPtr<Poco::XML::NodeList> collisionObjects = tile->getElementsByTagName("object");
            for (unsigned long objectIndex = 0; collisionObjects && objectIndex < collisionObjects->length();
                 ++objectIndex) {
                auto *object = dynamic_cast<Poco::XML::Element *>(collisionObjects->item(objectIndex));
                if (!object) continue;
                const float x      = float(xmlInt(object, "x", 0));
                const float y      = float(xmlInt(object, "y", 0));
                const float width  = float(xmlInt(object, "width", 0));
                const float height = float(xmlInt(object, "height", 0));
                if (width > 0.f && height > 0.f) visual.collisionShapes.push_back({x, y, width, height});
            }
            if (!visual.collisionShapes.empty()) visual.walkable = false;
            info.visuals.push_back(visual);
            Poco::AutoPtr<Poco::XML::NodeList> frames = tile->getElementsByTagName("frame");
            TileLayer::Tileset::Animation      animation;
            animation.gid = visual.gid;
            for (unsigned long frameIndex = 0; frames && frameIndex < frames->length(); ++frameIndex) {
                auto *frame = dynamic_cast<Poco::XML::Element *>(frames->item(frameIndex));
                if (frame)
                    animation.frames.push_back(
                        {firstGid + xmlInt(frame, "tileid", localId), std::max(1, xmlInt(frame, "duration", 100))});
            }
            if (!animation.frames.empty()) info.animations.push_back(std::move(animation));
        }
        Poco::AutoPtr<Poco::XML::NodeList> wangSets = root->getElementsByTagName("wangset");
        for (unsigned long setIndex = 0; wangSets && setIndex < wangSets->length(); ++setIndex) {
            auto *set = dynamic_cast<Poco::XML::Element *>(wangSets->item(setIndex));
            if (!set) continue;
            Poco::AutoPtr<Poco::XML::NodeList> wangTiles = set->getElementsByTagName("wangtile");
            for (unsigned long tileIndex = 0; wangTiles && tileIndex < wangTiles->length(); ++tileIndex) {
                auto *wangTile = dynamic_cast<Poco::XML::Element *>(wangTiles->item(tileIndex));
                if (!wangTile) continue;
                std::array<int, 8> values{};
                std::stringstream  stream(wangTile->getAttribute("wangid"));
                std::string        token;
                int                count = 0;
                while (count < 8 && std::getline(stream, token, ',')) {
                    try {
                        values[size_t(count)] = std::stoi(token);
                    } catch (...) {
                        values[size_t(count)] = 0;
                    }
                    ++count;
                }
                if (count != 8) continue;
                constexpr int tiledToTerrainBit[8] = {1, 2, 3, 4, 5, 6, 7, 0};
                for (int color = 1; color <= 255; ++color) {
                    int mask = 0;
                    for (int position = 0; position < 8; ++position)
                        if (values[size_t(position)] == color) mask |= 1 << tiledToTerrainBit[position];
                    if (mask != 0)
                        info.terrainRules.push_back(
                            {firstGid + xmlInt(wangTile, "tileid", 0), int(setIndex) * 256 + color, mask});
                }
            }
        }
    } catch (const std::exception &exception) {
        if (error) *error = "map.import.external-tsx-invalid: " + path + " (" + exception.what() + ")";
        return {};
    }
    return info;
}

void applyTileset(TileLayer *layer, const TilesetInfo &info) {
    if (!layer) return;
    layer->setTilesetTileSize(info.tileW, info.tileH);
    graphics::Texture *tex = tryLoadTexture(info.image);
    layer->resource()->texturePath = info.image;
    layer->setTileset(tex, info.firstGid, info.columns, info.margin, info.spacing);
    layer->tileset()->visuals = info.visuals;
    layer->tileset()->animations = info.animations;
    layer->tileset()->terrainRules = info.terrainRules;
    layer->tileset()->customData = info.customData;
}

void appendTileset(TileLayer *layer, const TilesetInfo &info) {
    if (!layer) return;
    auto               tileset = layer->tileset();
    graphics::Texture *texture = tryLoadTexture(info.image);
    tileset->atlases.push_back({texture, info.firstGid, std::max(1, info.columns), std::max(1, info.tileW),
                                std::max(1, info.tileH), std::max(0, info.margin), std::max(0, info.spacing),
                                info.image});
    tileset->visuals.insert(tileset->visuals.end(), info.visuals.begin(), info.visuals.end());
    tileset->animations.insert(tileset->animations.end(), info.animations.begin(), info.animations.end());
    tileset->terrainRules.insert(tileset->terrainRules.end(), info.terrainRules.begin(), info.terrainRules.end());
    tileset->customData.insert(tileset->customData.end(), info.customData.begin(), info.customData.end());
}

void applyTilesets(TileLayer *layer, const std::vector<TilesetInfo> &infos) {
    if (!layer || infos.empty()) return;
    applyTileset(layer, infos.front());
    for (size_t index = 1; index < infos.size(); ++index) appendTileset(layer, infos[index]);
}

bool decodeLayerData(Poco::JSON::Object::Ptr layerObj, size_t expectedCount,
                     std::vector<uint32_t> &out, std::string *error) {
    if (!layerObj || !layerObj->has("data")) {
        if (error) *error = "missing data";
        return false;
    }

    try {
        if (layerObj->isArray("data")) {
            auto arr = layerObj->getArray("data");
            if (!arr) {
                if (error) *error = "invalid data array";
                return false;
            }
            out.resize(arr->size());
            for (size_t i = 0; i < arr->size(); ++i) out[i] = asUInt32(arr->get(static_cast<unsigned int>(i)), 0);
            if (expectedCount > 0 && out.size() != expectedCount) {
                if (error) *error = "gid count mismatch";
                return false;
            }
            return true;
        }
    } catch (...) {
    }

    std::string encoding = layerObj->has("encoding") ? asString(layerObj->get("encoding")) : "";
    std::string compression =
        layerObj->has("compression") ? asString(layerObj->get("compression")) : "";
    if (encoding != "base64") {
        if (error) *error = "unsupported encoding";
        return false;
    }
    if (compression == "zstd") {
        if (error) *error = "zstd not supported; export as zlib";
        return false;
    }

    const std::string b64 = asString(layerObj->get("data"));
    size_t decodedLen = 0;
    std::unique_ptr<char[]> decoded(eve::data::decode("base64", b64.data(), b64.size(), decodedLen));
    if (!decoded) {
        if (error) *error = "base64 decode failed";
        return false;
    }

    const char *bytes = decoded.get();
    size_t nbytes = decodedLen;
    std::unique_ptr<char[]> inflated;
    if (!compression.empty()) {
        if (compression != "zlib" && compression != "gzip") {
            if (error) *error = "unsupported compression";
            return false;
        }
        size_t rawsize = expectedCount * 4;
        try {
            inflated.reset(eve::data::decompress(compression, bytes, nbytes, rawsize));
        } catch (...) {
            if (error) *error = "decompress failed";
            return false;
        }
        if (!inflated) {
            if (error) *error = "decompress failed";
            return false;
        }
        bytes = inflated.get();
        nbytes = rawsize;
    }

    if (expectedCount > 0 && nbytes != expectedCount * 4) {
        if (error) *error = "gid byte length mismatch";
        return false;
    }
    const size_t count = nbytes / 4;
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(bytes) + i * 4;
        out[i] = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
                 (uint32_t(p[3]) << 24);
    }
    return true;
}

void applyLayerDraw(TileLayer *layer, Poco::JSON::Object::Ptr layerObj) {
    if (!layer || !layerObj) return;
    if (layerObj->has("visible"))
        layer->setVisible(asBool(layerObj->get("visible"), true));
    if (layerObj->has("layer"))
        layer->setLayer(asInt(layerObj->get("layer"), layer->getLayer()));
    else if (layerObj->has("id"))
        layer->setLayer(asInt(layerObj->get("id"), layer->getLayer()));
    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(layerObj, "tint", r, g, b, a))
        layer->setTint(r, g, b, a);
    else if (layerObj->has("opacity")) {
        a = asFloat(layerObj->get("opacity"), 1.f);
        layer->setTint(1.f, 1.f, 1.f, a);
    }
    float ox = layer->getX(), oy = layer->getY();
    if (layerObj->has("x")) ox = asFloat(layerObj->get("x"), ox);
    if (layerObj->has("offsetx")) ox += asFloat(layerObj->get("offsetx"), 0.f);
    if (layerObj->has("y")) oy = asFloat(layerObj->get("y"), oy);
    if (layerObj->has("offsety")) oy += asFloat(layerObj->get("offsety"), 0.f);
    if (layerObj->has("x") || layerObj->has("y") || layerObj->has("offsetx") ||
        layerObj->has("offsety"))
        layer->setOrigin(ox, oy);
}

bool isTileLayerObject(Poco::JSON::Object::Ptr o) {
    if (!o) return false;
    if (o->has("type")) {
        const std::string t = asString(o->get("type"));
        if (!t.empty() && t != "tilelayer") return false;
    }
    return o->has("data") || o->has("chunks");
}

bool isObjectGroup(Poco::JSON::Object::Ptr o) {
    if (!o) return false;
    if (!o->has("type")) return false;
    return asString(o->get("type")) == "objectgroup";
}

bool parseOrientation(Poco::JSON::Object::Ptr root, TileLayer::Config *cfg, std::string *error) {
    if (!root || !cfg) return false;
    if (!root->has("orientation")) return true;
    const std::string o = asString(root->get("orientation"));
    if (o.empty() || o == "orthogonal")
        cfg->orientation = MapOrientation::Orthogonal;
    else if (o == "isometric")
        cfg->orientation = MapOrientation::Isometric;
    else if (o == "staggered")
        cfg->orientation = MapOrientation::Staggered;
    else if (o == "hexagonal")
        cfg->orientation = MapOrientation::Hexagonal;
    else {
        if (error) *error = "unknown orientation: " + o;
        return false;
    }
    if (root->has("staggeraxis")) {
        const std::string a = asString(root->get("staggeraxis"));
        cfg->staggerAxis = (a == "x") ? StaggerAxis::X : StaggerAxis::Y;
    }
    if (root->has("staggerindex")) {
        const std::string i = asString(root->get("staggerindex"));
        cfg->staggerIndex = (i == "even") ? StaggerIndex::Even : StaggerIndex::Odd;
    }
    if (root->has("hexsidelength"))
        cfg->hexSideLength = asFloat(root->get("hexsidelength"), 0.f);
    return true;
}

bool applyMapGlobals(TileLayer *layer, Poco::JSON::Object::Ptr root, std::string *error) {
    if (!layer || !root) return false;

    int mapW = layer->getMapWidth();
    int mapH = layer->getMapHeight();
    float tileW = layer->getTileWidth();
    float tileH = layer->getTileHeight();

    if (root->has("width")) mapW = asInt(root->get("width"), mapW);
    if (root->has("height")) mapH = asInt(root->get("height"), mapH);
    if (root->has("tilewidth")) tileW = asFloat(root->get("tilewidth"), tileW);
    else if (root->has("tileWidth")) tileW = asFloat(root->get("tileWidth"), tileW);
    if (root->has("tileheight")) tileH = asFloat(root->get("tileheight"), tileH);
    else if (root->has("tileHeight")) tileH = asFloat(root->get("tileHeight"), tileH);

    if (mapW != layer->getMapWidth() || mapH != layer->getMapHeight()) layer->resize(mapW, mapH);
    layer->setTileSize(tileW, tileH);

    float gapX = layer->getCellGapX();
    float gapY = layer->getCellGapY();
    if (readVec2(root, "cellGap", gapX, gapY)) {
        layer->setCellGap(gapX, gapY);
    } else {
        float spacingX = layer->getRenderSpacingX();
        float spacingY = layer->getRenderSpacingY();
        if (readVec2(root, "renderSpacing", spacingX, spacingY)) {
            layer->setRenderSpacing(spacingX, spacingY);
        } else {
            if (root->has("cellGapX")) gapX = asFloat(root->get("cellGapX"), gapX);
            if (root->has("cellGapY")) gapY = asFloat(root->get("cellGapY"), gapY);
            if (root->has("cellGapX") || root->has("cellGapY"))
                layer->setCellGap(gapX, gapY);
        }
    }

    if (!parseOrientation(root, &(*layer->config()), error)) return false;

    float ox = layer->getX(), oy = layer->getY();
    if (readVec2(root, "origin", ox, oy))
        layer->setOrigin(ox, oy);
    else {
        if (root->has("x")) ox = asFloat(root->get("x"), ox);
        if (root->has("y")) oy = asFloat(root->get("y"), oy);
        if (root->has("x") || root->has("y")) layer->setOrigin(ox, oy);
    }

    if (root->has("layer"))
        layer->setLayer(asInt(root->get("layer"), layer->getLayer()));
    if (root->has("visible"))
        layer->setVisible(asBool(root->get("visible"), layer->isVisible()));
    if (root->has("autoReload"))
        layer->resource()->autoReload = asBool(root->get("autoReload"), true);

    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(root, "tint", r, g, b, a)) layer->setTint(r, g, b, a);

    if (root->has("tileset") && !root->isArray("tileset")) {
        try {
            auto o = root->getObject("tileset");
            if (o && !o->has("source")) applyTileset(layer, readTilesetObject(o));
        } catch (...) {
        }
    } else if (root->has("tilesets")) {
        try {
            auto arr = root->getArray("tilesets");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    try {
                        auto o = arr->getObject(static_cast<unsigned int>(i));
                        if (o && o->has("source")) continue;
                        applyTileset(layer, readTilesetObject(o));
                        break;
                    } catch (...) {
                    }
                }
            }
        } catch (...) {
        }
    } else if (root->has("image") || root->has("texture")) {
        applyTileset(layer, readTilesetObject(root));
    }
    return true;
}

bool applyFlatLayerData(TileLayer *layer, Poco::JSON::Object::Ptr root, std::string *error) {
    const int mapW = layer->getMapWidth();
    const int mapH = layer->getMapHeight();
    const size_t need = size_t(std::max(0, mapW) * std::max(0, mapH));
    if (need == 0) {
        if (error) *error = "empty map size";
        return false;
    }
    std::vector<uint32_t> data;
    if (!decodeLayerData(root, need, data, error)) return false;
    auto &gids = layer->tiles()->gids;
    gids.assign(need, 0u);
    const size_t n = std::min(need, data.size());
    for (size_t i = 0; i < n; ++i) gids[i] = data[i];
    layer->rebuildSpatialIndex();
    return true;
}

bool applyOneLayerObject(TileLayer *layer, Poco::JSON::Object::Ptr layerObj, int mapW, int mapH,
                         std::string *error) {
    if (!layer || !layerObj) return false;
    if (layerObj->has("chunks")) {
        try {
            auto chunks = layerObj->getArray("chunks");
            if (!chunks || chunks->size() == 0) return false;
            int minX = 0, minY = 0, maxX = 0, maxY = 0;
            bool first = true;
            for (size_t i = 0; i < chunks->size(); ++i) {
                auto chunk = chunks->getObject(static_cast<unsigned int>(i));
                if (!chunk) continue;
                const int x = asInt(chunk->get("x"), 0), y = asInt(chunk->get("y"), 0);
                const int w = asInt(chunk->get("width"), 0), h = asInt(chunk->get("height"), 0);
                if (w <= 0 || h <= 0) continue;
                if (first) {
                    minX = x; minY = y; maxX = x + w; maxY = y + h; first = false;
                } else {
                    minX = std::min(minX, x); minY = std::min(minY, y);
                    maxX = std::max(maxX, x + w); maxY = std::max(maxY, y + h);
                }
            }
            if (first) return false;
            float shiftX = 0.f, shiftY = 0.f;
            layer->tileToWorld(minX, minY, shiftX, shiftY);
            layer->resize(maxX - minX, maxY - minY);
            layer->setOrigin(shiftX, shiftY);
            auto &gids = layer->tiles()->gids;
            for (size_t i = 0; i < chunks->size(); ++i) {
                auto chunk = chunks->getObject(static_cast<unsigned int>(i));
                if (!chunk) continue;
                const int x = asInt(chunk->get("x"), 0), y = asInt(chunk->get("y"), 0);
                const int w = asInt(chunk->get("width"), 0), h = asInt(chunk->get("height"), 0);
                if (w <= 0 || h <= 0) continue;
                std::vector<uint32_t> data;
                if (!decodeLayerData(chunk, size_t(w * h), data, error)) return false;
                for (int cy = 0; cy < h; ++cy)
                    for (int cx = 0; cx < w; ++cx)
                        gids[size_t((y - minY + cy) * layer->getMapWidth() + x - minX + cx)] =
                            data[size_t(cy * w + cx)];
            }
            layer->rebuildSpatialIndex();
            applyLayerDraw(layer, layerObj);
            return true;
        } catch (...) {
            if (error) *error = "invalid chunk layer";
            return false;
        }
    }
    int w = mapW, h = mapH;
    if (layerObj->has("width")) w = asInt(layerObj->get("width"), w);
    if (layerObj->has("height")) h = asInt(layerObj->get("height"), h);
    if (w != layer->getMapWidth() || h != layer->getMapHeight()) layer->resize(w, h);

    const size_t need =
        size_t(std::max(0, layer->getMapWidth()) * std::max(0, layer->getMapHeight()));
    std::vector<uint32_t> data;
    if (!decodeLayerData(layerObj, need, data, error)) return false;
    auto &gids = layer->tiles()->gids;
    gids.assign(need, 0u);
    const size_t n = std::min(need, data.size());
    for (size_t i = 0; i < n; ++i) gids[i] = data[i];
    layer->rebuildSpatialIndex();

    applyLayerDraw(layer, layerObj);
    return true;
}

void parseObjectGroup(Poco::JSON::Object::Ptr group, std::vector<MapObject> &out) {
    if (!group || !group->has("objects")) return;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = group->getArray("objects");
    } catch (...) {
        return;
    }
    if (!arr) return;
    for (size_t i = 0; i < arr->size(); ++i) {
        Poco::JSON::Object::Ptr o;
        try {
            o = arr->getObject(static_cast<unsigned int>(i));
        } catch (...) {
            continue;
        }
        if (!o) continue;
        MapObject mo;
        if (o->has("name")) mo.name = asString(o->get("name"));
        if (o->has("type")) mo.type = asString(o->get("type"));
        else if (o->has("class")) mo.type = asString(o->get("class"));
        mo.x = o->has("x") ? asFloat(o->get("x"), 0.f) : 0.f;
        mo.y = o->has("y") ? asFloat(o->get("y"), 0.f) : 0.f;
        mo.width = o->has("width") ? asFloat(o->get("width"), 0.f) : 0.f;
        mo.height = o->has("height") ? asFloat(o->get("height"), 0.f) : 0.f;
        if (o->has("gid")) mo.gid = uint32_t(asInt(o->get("gid"), 0));
        if (o->has("properties")) {
            try {
                auto properties = o->getArray("properties");
                for (size_t propertyIndex = 0; properties && propertyIndex < properties->size();
                     ++propertyIndex) {
                    auto property = properties->getObject(static_cast<unsigned int>(propertyIndex));
                    if (!property || !property->has("name") || !property->has("value")) continue;
                    const std::string name = asString(property->get("name"));
                    if (!name.empty()) mo.properties.emplace(name, asString(property->get("value")));
                }
            } catch (...) {
            }
        }
        out.push_back(std::move(mo));
    }
}

void abandonLayers(std::vector<TileLayer *> &layers) {
    for (TileLayer *layer : layers) {
        if (!layer) continue;
        layer->clear();
        layer->setVisible(false);
    }
    layers.clear();
}

struct LayerEntry {
    Poco::JSON::Object::Ptr object;
    float                   offsetX = 0.f;
    float                   offsetY = 0.f;
    float                   opacity = 1.f;
    bool                    visible = true;
};

void flattenLayers(Poco::JSON::Array::Ptr source, std::vector<LayerEntry> &out, float parentX = 0.f,
                   float parentY = 0.f, float parentOpacity = 1.f, bool parentVisible = true) {
    for (size_t index = 0; source && index < source->size(); ++index) {
        Poco::JSON::Object::Ptr object;
        try {
            object = source->getObject(unsigned(index));
        } catch (...) {
            continue;
        }
        if (!object) continue;
        const float offsetX = parentX + (object->has("offsetx") ? asFloat(object->get("offsetx"), 0.f) : 0.f);
        const float offsetY = parentY + (object->has("offsety") ? asFloat(object->get("offsety"), 0.f) : 0.f);
        const float opacity = parentOpacity * (object->has("opacity") ? asFloat(object->get("opacity"), 1.f) : 1.f);
        const bool  visible = parentVisible && (!object->has("visible") || asBool(object->get("visible"), true));
        if (object->has("type") && asString(object->get("type")) == "group" && object->has("layers")) {
            try {
                flattenLayers(object->getArray("layers"), out, offsetX, offsetY, opacity, visible);
            } catch (...) {
            }
            continue;
        }
        out.push_back({object, parentX, parentY, parentOpacity, parentVisible});
    }
}

std::vector<TilesetInfo> readTilesets(Poco::JSON::Object::Ptr root, const std::string &mapPath,
                                      eve::filesystem::Filesystem *fs, std::string *error) {
    std::vector<TilesetInfo> result;
    if (root->has("tilesets")) {
        try {
            auto arr = root->getArray("tilesets");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    try {
                        auto o = arr->getObject(static_cast<unsigned int>(i));
                        if (!o) continue;
                        const int firstGid = o->has("firstgid") ? asInt(o->get("firstgid"), 1) : 1;
                        if (o->has("source")) {
                            const std::string source = resolveAssetPath(mapPath, asString(o->get("source")));
                            if (std::filesystem::path(source).extension() == ".tsx") {
                                TilesetInfo info = readTsxTileset(fs, source, firstGid, error);
                                if (error && !error->empty()) return {};
                                info.sourcePath = source;
                                result.push_back(std::move(info));
                                continue;
                            }
                            auto external = readJsonObject(fs, source, error);
                            if (!external) return {};
                            external->set("firstgid", firstGid);
                            TilesetInfo info = readTilesetObject(external);
                            info.sourcePath  = source;
                            info.image       = resolveAssetPath(source, info.image);
                            result.push_back(std::move(info));
                            continue;
                        }
                        TilesetInfo info = readTilesetObject(o);
                        info.image       = resolveAssetPath(mapPath, info.image);
                        result.push_back(std::move(info));
                    } catch (...) {
                        if (error) *error = "map.import.tileset-entry-invalid: index=" + std::to_string(i);
                        return {};
                    }
                }
            }
        } catch (...) {
            if (error) *error = "map.import.tilesets-invalid";
            return {};
        }
    } else if (root->has("tileset") && !root->isArray("tileset")) {
        try {
            auto o = root->getObject("tileset");
            if (o && !o->has("source")) {
                TilesetInfo info = readTilesetObject(o);
                info.image       = resolveAssetPath(mapPath, info.image);
                result.push_back(std::move(info));
            }
        } catch (...) {
            if (error) *error = "map.import.tileset-invalid";
            return {};
        }
    } else if (root->has("image") || root->has("texture")) {
        TilesetInfo info = readTilesetObject(root);
        info.image       = resolveAssetPath(mapPath, info.image);
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(),
              [](const TilesetInfo &left, const TilesetInfo &right) { return left.firstGid < right.firstGid; });
    return result;
}

std::vector<TileLayer *> loadMapObject(Poco::JSON::Object::Ptr root, const std::string &path,
                                       eve::filesystem::Filesystem *fs,
                                       std::vector<MapObject> *objects, std::string *error) {
    std::vector<TileLayer *> out;
    if (!root) {
        if (error) *error = "config root must be object";
        return out;
    }

    int mapW = 10, mapH = 10;
    float tileW = 32.f, tileH = 32.f;
    if (root->has("width")) mapW = asInt(root->get("width"), mapW);
    if (root->has("height")) mapH = asInt(root->get("height"), mapH);
    if (root->has("tilewidth")) tileW = asFloat(root->get("tilewidth"), tileW);
    else if (root->has("tileWidth")) tileW = asFloat(root->get("tileWidth"), tileW);
    if (root->has("tileheight")) tileH = asFloat(root->get("tileheight"), tileH);
    else if (root->has("tileHeight")) tileH = asFloat(root->get("tileHeight"), tileH);

    // Validate orientation early on a temp config.
    TileLayer::Config orientCheck;
    if (!parseOrientation(root, &orientCheck, error)) return out;

    std::vector<TilesetInfo> tilesets = readTilesets(root, path, fs, error);
    if (root->has("tilesets") && tilesets.empty() && error && !error->empty()) return out;
    if (objects) objects->clear();

    auto bindResource = [&](TileLayer *layer) {
        auto res = layer->resource();
        res->path = path;
        res->dependencyPaths.clear();
        res->dependencyModtimes.clear();
        if (!path.empty()) {
            res->modtime = fileModtime(path);
            if (fs) {
                fs->watch(path);
                if (auto *hot =
                        eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
                    hot->bind(path, "tilemap");
            }
        }
        for (const TilesetInfo &tileset : tilesets) {
            if (tileset.sourcePath.empty()) continue;
            res->dependencyPaths.push_back(tileset.sourcePath);
            res->dependencyModtimes.push_back(fileModtime(tileset.sourcePath));
            if (fs) fs->watch(tileset.sourcePath);
            if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
                hot->bind(tileset.sourcePath, "tilemap");
        }
        if (root->has("autoReload"))
            res->autoReload = asBool(root->get("autoReload"), true);
    };

    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                std::vector<LayerEntry> entries;
                flattenLayers(arr, entries);
                int sort = 0;
                for (const LayerEntry &entry : entries) {
                    Poco::JSON::Object::Ptr lo = entry.object;
                    if (objects && isObjectGroup(lo)) {
                        parseObjectGroup(lo, *objects);
                        continue;
                    }
                    if (!isTileLayerObject(lo)) continue;
                    TileLayer *layer = TileLayer::createLayer(mapW, mapH, tileW, tileH);
                    if (!applyMapGlobals(layer, root, error)) {
                        abandonLayers(out);
                        layer->clear();
                        layer->setVisible(false);
                        return {};
                    }
                    applyTilesets(layer, tilesets);
                    if (!applyOneLayerObject(layer, lo, mapW, mapH, error)) {
                        abandonLayers(out);
                        layer->clear();
                        layer->setVisible(false);
                        return {};
                    }
                    if (!lo->has("layer") && !lo->has("id")) layer->setLayer(sort);
                    layer->setOrigin(layer->getX() + entry.offsetX, layer->getY() + entry.offsetY);
                    layer->draw()->visible = layer->draw()->visible && entry.visible;
                    layer->draw()->tint.a *= entry.opacity;
                    ++sort;
                    bindResource(layer);
                    out.push_back(layer);
                }
            }
        } catch (...) {
        }
        if (!out.empty()) return out;
    }

    TileLayer *layer = TileLayer::createLayer(mapW, mapH, tileW, tileH);
    if (!applyMapGlobals(layer, root, error)) {
        layer->clear();
        layer->setVisible(false);
        return {};
    }
    applyTilesets(layer, tilesets);
    if (root->has("data")) {
        if (!applyFlatLayerData(layer, root, error)) {
            layer->clear();
            layer->setVisible(false);
            return {};
        }
    }
    bindResource(layer);
    out.push_back(layer);
    return out;
}

}  // namespace

bool applyConfigDocument(TileLayer *layer, data::JsonDocument *doc) {
    if (!layer || !doc || !doc->isObject()) return false;
    auto root = doc->object();
    if (!root) return false;

    std::string err;
    if (!applyMapGlobals(layer, root, &err)) return false;

    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr lo;
                    try {
                        lo = arr->getObject(static_cast<unsigned int>(i));
                    } catch (...) {
                        continue;
                    }
                    if (!isTileLayerObject(lo)) continue;
                    return applyOneLayerObject(layer, lo, layer->getMapWidth(),
                                               layer->getMapHeight(), nullptr);
                }
            }
        } catch (...) {
        }
    }

    if (root->has("data")) return applyFlatLayerData(layer, root, nullptr);
    return true;
}

bool applyConfigText(TileLayer *layer, const std::string &json, std::string *error) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return false;
    }
    if (!doc->isObject()) {
        if (error) *error = "config root must be object";
        return false;
    }
    auto root = doc->object();
    if (!root) {
        if (error) *error = "config root must be object";
        return false;
    }
    const TileLayer::Config   oldConfig   = *layer->config();
    const TileLayer::Tiles    oldTiles    = *layer->tiles();
    const TileLayer::Tileset  oldTileset  = *layer->tileset();
    const TileLayer::Draw     oldDraw     = *layer->draw();
    const TileLayer::Resource oldResource = *layer->resource();
    auto                      rollback    = [&]() {
        *layer->config()   = oldConfig;
        *layer->tiles()    = oldTiles;
        *layer->tileset()  = oldTileset;
        *layer->draw()     = oldDraw;
        *layer->resource() = oldResource;
    };
    if (!applyMapGlobals(layer, root, error)) {
        rollback();
        return false;
    }
    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr lo;
                    try {
                        lo = arr->getObject(static_cast<unsigned int>(i));
                    } catch (...) {
                        continue;
                    }
                    if (!isTileLayerObject(lo)) continue;
                    if (applyOneLayerObject(layer, lo, layer->getMapWidth(), layer->getMapHeight(), error)) return true;
                    rollback();
                    return false;
                }
            }
        } catch (...) {
        }
    }
    if (root->has("data")) {
        if (applyFlatLayerData(layer, root, error)) return true;
        rollback();
        return false;
    }
    return true;
}

bool loadConfigFile(TileLayer *layer, const std::string &path, std::string *error) {
    if (error) error->clear();
    if (!layer || path.empty()) {
        if (error) *error = "empty path";
        return false;
    }
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "empty file: " + path;
        return false;
    }

    auto                               *dataModule = eve::data::DataModule::create();
    std::string                         decodeError;
    std::unique_ptr<data::JsonDocument> document(dataModule->decodeJson(text, &decodeError));
    if (!document || !document->isObject()) {
        if (error) *error = decodeError.empty() ? "map.import.invalid-json" : decodeError;
        return false;
    }
    auto                     root     = document->object();
    std::vector<TilesetInfo> tilesets = readTilesets(root, path, fs, error);
    if (root->has("tilesets") && tilesets.empty() && error && !error->empty()) return false;

    const TileLayer::Config   oldConfig   = *layer->config();
    const TileLayer::Tiles    oldTiles    = *layer->tiles();
    const TileLayer::Tileset  oldTileset  = *layer->tileset();
    const TileLayer::Draw     oldDraw     = *layer->draw();
    const TileLayer::Resource oldResource = *layer->resource();
    if (!applyConfigText(layer, text, error)) {
        *layer->config()   = oldConfig;
        *layer->tiles()    = oldTiles;
        *layer->tileset()  = oldTileset;
        *layer->draw()     = oldDraw;
        *layer->resource() = oldResource;
        return false;
    }
    if (!tilesets.empty()) applyTilesets(layer, tilesets);

    auto res = layer->resource();
    res->path = path;
    res->modtime = fileModtime(path);
    res->dependencyPaths.clear();
    res->dependencyModtimes.clear();
    for (const auto &tileset : tilesets) {
        if (tileset.sourcePath.empty()) continue;
        res->dependencyPaths.push_back(tileset.sourcePath);
        res->dependencyModtimes.push_back(fileModtime(tileset.sourcePath));
        fs->watch(tileset.sourcePath);
        if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
            hot->bind(tileset.sourcePath, "tilemap");
    }
    fs->watch(path);
    if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
        hot->bind(path, "tilemap");
    return true;
}

bool reloadConfigFile(TileLayer *layer, std::string *error) {
    if (!layer) return false;
    const std::string &path = layer->resource()->path;
    if (path.empty()) {
        if (error) *error = "no config path";
        return false;
    }
    return loadConfigFile(layer, path, error);
}

bool loadTilesetManifestFile(TileLayer *layer, const std::string &path, std::string *error) {
    if (!layer || path.empty()) {
        if (error) *error = "empty path";
        return false;
    }
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return false;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return false;
    }
    const std::string text(static_cast<const char *>(data->getData()), data->getSize());
    auto *dm = eve::data::DataModule::create();
    std::string decodeError;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(text, &decodeError));
    if (!doc || !doc->isObject()) {
        if (error) *error = decodeError.empty() ? "invalid tileset manifest" : decodeError;
        return false;
    }
    auto root = doc->object();
    if (root->has("tileset")) {
        try {
            root = root->getObject("tileset");
        } catch (...) {
            if (error) *error = "tileset must be an object";
            return false;
        }
    }
    if (!root) {
        if (error) *error = "tileset manifest root must be an object";
        return false;
    }
    const TilesetInfo info = readTilesetObject(root);
    if (info.image.empty()) {
        if (error) *error = "tileset manifest has no image";
        return false;
    }
    applyTileset(layer, info);
    fs->watch(path);
    if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
        hot->bind(path, "tilemap");
    return true;
}

std::vector<TileLayer *> loadMapText(const std::string &json, std::vector<MapObject> *objects,
                                     std::string *error) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc || !doc->isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return {};
    }
    return loadMapObject(doc->object(), {}, nullptr, objects, error);
}

std::vector<TileLayer *> loadMapFile(const std::string &path, std::vector<MapObject> *objects,
                                     std::string *error) {
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "empty path";
        return {};
    }

    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "empty file: " + path;
        return {};
    }
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(text, &err));
    if (!doc || !doc->isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return {};
    }
    return loadMapObject(doc->object(), path, fs, objects, error);
}

std::vector<TileLayer *> loadMapFile(const std::string &path, std::string *error) {
    return loadMapFile(path, nullptr, error);
}

#include "map/RpgMakerTileImporter.inl"

}  // namespace eve::map
