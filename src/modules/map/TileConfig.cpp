#include "map/TileConfig.h"
#include "map/TileOrientation.h"

#include "common/Json.h"
#include "common/Module.h"
#include "common/Xml.h"
#include "data/DataModule.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "filesystem/HotReload.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace eve::map {
namespace {

using Json = eve::json::Value;

uint32_t asUInt32(const Json &v, uint32_t fallback) {
    if (!v) return fallback;
    if (v.isInt64()) return uint32_t(uint64_t(v.asInt64(0)) & 0xffffffffu);
    const double d = v.asDouble(static_cast<double>(fallback));
    if (!std::isfinite(d) || d < 0.0 || d > 4294967295.0) return fallback;
    return uint32_t(d);
}

bool readVec2(const Json &o, const char *key, float &a, float &b) {
    if (!o || !o.has(key)) return false;
    const Json arr = o.get(key);
    if (!arr.isArray() || arr.size() < 2) return false;
    a = arr.at(0).asFloat(a);
    b = arr.at(1).asFloat(b);
    return true;
}

bool readVec4(const Json &o, const char *key, float &a, float &b, float &c, float &d) {
    if (!o || !o.has(key)) return false;
    const Json arr = o.get(key);
    if (!arr.isArray() || arr.size() < 4) return false;
    a = arr.at(0).asFloat(a);
    b = arr.at(1).asFloat(b);
    c = arr.at(2).asFloat(c);
    d = arr.at(3).asFloat(d);
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
    std::string                                  image;
    std::string                                  sourcePath;
    int                                          firstGid = 1;
    int                                          columns  = 1;
    int                                          tileW    = 32;
    int                                          tileH    = 32;
    int                                          margin   = 0;
    int                                          spacing  = 0;
    std::vector<TileLayer::Tileset::Visual>      visuals;
    std::vector<TileLayer::Tileset::Animation>   animations;
    std::vector<TileLayer::Tileset::TerrainRule> terrainRules;
    std::vector<TileLayer::Tileset::CustomData>  customData;
};

TileLayer::Tileset::Visual readTileVisual(const Json &o, int fallbackGid) {
    TileLayer::Tileset::Visual visual;
    if (!o || !o.isObject()) return visual;
    visual.gid = o.has("gid") ? o.getInt("gid", fallbackGid) : fallbackGid;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (readVec4(o, "region", x, y, w, h)) {
        visual.x      = int(x);
        visual.y      = int(y);
        visual.width  = int(w);
        visual.height = int(h);
    }
    readVec2(o, "pivot", visual.pivotX, visual.pivotY);
    visual.sortBias = o.has("sortBias") ? o.getFloat("sortBias", 0.f) : 0.f;
    float fw = 1.f, fh = 1.f;
    if (readVec2(o, "footprint", fw, fh)) {
        visual.footprintW = std::max(1, int(fw));
        visual.footprintH = std::max(1, int(fh));
    }
    visual.walkable = o.has("walkable") ? o.getBool("walkable", true) : true;
    visual.cost     = o.has("cost") ? std::max(0.001f, o.getFloat("cost", 1.f)) : 1.f;
    if (o.has("properties")) {
        const Json properties = o.get("properties");
        for (size_t index = 0; properties.isArray() && index < properties.size(); ++index) {
            const Json property = properties.at(index);
            if (!property.isObject() || !property.has("name") || !property.has("value")) continue;
            const std::string name = property.getString("name");
            if (name == "walkable")
                visual.walkable = property.get("value").asBool(true);
            else if (name == "cost")
                visual.cost = std::max(0.001f, property.get("value").asFloat(1.f));
            else if (name == "enterMask")
                visual.enterMask = uint8_t(property.get("value").asInt(0xff) & 0xff);
            else if (name == "exitMask")
                visual.exitMask = uint8_t(property.get("value").asInt(0xff) & 0xff);
            else if (name == "opaque")
                visual.opaque = property.get("value").asBool(false);
            else if (name == "semanticFlags")
                visual.semanticFlags = uint32_t(property.get("value").asInt(0));
        }
    }
    if (o.has("objectgroup")) {
        const Json group   = o.get("objectgroup");
        const Json objects = group.isObject() ? group.get("objects") : Json{};
        for (size_t index = 0; objects.isArray() && index < objects.size(); ++index) {
            const Json object = objects.at(index);
            if (!object.isObject()) continue;
            float ox     = object.has("x") ? object.getFloat("x", 0.f) : 0.f;
            float oy     = object.has("y") ? object.getFloat("y", 0.f) : 0.f;
            float width  = object.has("width") ? object.getFloat("width", 0.f) : 0.f;
            float height = object.has("height") ? object.getFloat("height", 0.f) : 0.f;
            if (object.has("polygon")) {
                const Json polygon = object.get("polygon");
                float      minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
                for (size_t pointIndex = 0; polygon.isArray() && pointIndex < polygon.size(); ++pointIndex) {
                    const Json point = polygon.at(pointIndex);
                    if (!point.isObject()) continue;
                    const float px = point.getFloat("x", 0.f);
                    const float py = point.getFloat("y", 0.f);
                    minX           = std::min(minX, px);
                    minY           = std::min(minY, py);
                    maxX           = std::max(maxX, px);
                    maxY           = std::max(maxY, py);
                }
                ox += minX;
                oy += minY;
                width  = maxX - minX;
                height = maxY - minY;
            }
            if (width > 0.f && height > 0.f) visual.collisionShapes.push_back({ox, oy, width, height});
        }
        if (!visual.collisionShapes.empty()) visual.walkable = false;
    }
    return visual;
}

TilesetInfo readTilesetObject(const Json &o) {
    TilesetInfo info;
    if (!o || !o.isObject()) return info;
    if (o.has("image"))
        info.image = o.getString("image");
    else if (o.has("texture"))
        info.image = o.getString("texture");
    if (o.has("firstgid"))
        info.firstGid = o.getInt("firstgid", 1);
    else if (o.has("firstGid"))
        info.firstGid = o.getInt("firstGid", 1);
    if (o.has("columns")) info.columns = o.getInt("columns", 1);
    if (o.has("tilewidth"))
        info.tileW = o.getInt("tilewidth", 32);
    else if (o.has("tileWidth"))
        info.tileW = o.getInt("tileWidth", 32);
    if (o.has("tileheight"))
        info.tileH = o.getInt("tileheight", 32);
    else if (o.has("tileHeight"))
        info.tileH = o.getInt("tileHeight", 32);
    if (o.has("margin")) info.margin = o.getInt("margin", 0);
    if (o.has("spacing")) info.spacing = o.getInt("spacing", 0);
    if (info.columns <= 0 && o.has("imagewidth") && info.tileW > 0) {
        const int iw = o.getInt("imagewidth", 0);
        if (iw > 0) info.columns = std::max(1, (iw - info.margin) / (info.tileW + info.spacing));
    }
    if (o.has("tiles")) {
        const Json arr = o.get("tiles");
        for (size_t i = 0; arr.isArray() && i < arr.size(); ++i) {
            const Json tile    = arr.at(i);
            const int  localId = tile.isObject() && tile.has("id") ? tile.getInt("id", int(i)) : int(i);
            const int  gid     = tile.isObject() && tile.has("gid") ? tile.getInt("gid", info.firstGid + localId)
                                                                   : info.firstGid + localId;
            auto       visual  = readTileVisual(tile, gid);
            if (visual.gid > 0) info.visuals.push_back(visual);
            if (!tile.isObject()) continue;
            if (tile.has("animation")) {
                const Json                    frames = tile.get("animation");
                TileLayer::Tileset::Animation animation;
                animation.gid = gid;
                for (size_t frameIndex = 0; frames.isArray() && frameIndex < frames.size(); ++frameIndex) {
                    const Json frame = frames.at(frameIndex);
                    if (!frame.isObject()) continue;
                    const int frameLocal = frame.getInt("tileid", localId);
                    const int duration   = frame.has("duration") ? frame.getInt("duration", 100) : 100;
                    animation.frames.push_back({info.firstGid + frameLocal, std::max(1, duration)});
                }
                if (!animation.frames.empty()) info.animations.push_back(std::move(animation));
            }
            if (tile.has("terrain") && tile.has("neighborMask")) {
                info.terrainRules.push_back(
                    {gid, tile.getInt("terrain", 0), tile.getInt("neighborMask", 0) & 0xff});
            }
            if (tile.has("properties")) {
                const Json properties = tile.get("properties");
                for (size_t propertyIndex = 0; properties.isArray() && propertyIndex < properties.size();
                     ++propertyIndex) {
                    const Json property = properties.at(propertyIndex);
                    if (!property.isObject() || !property.has("name") || !property.has("value")) continue;
                    const std::string type = property.has("type") ? property.getString("type") : "string";
                    info.customData.push_back(
                        {gid, property.getString("name"), type, property.get("value").asString()});
                }
            }
        }
    }
    if (o.has("wangsets")) {
        const Json sets = o.get("wangsets");
        for (size_t setIndex = 0; sets.isArray() && setIndex < sets.size(); ++setIndex) {
            const Json set = sets.at(setIndex);
            if (!set.isObject() || !set.has("wangtiles")) continue;
            const Json wangTiles = set.get("wangtiles");
            for (size_t tileIndex = 0; wangTiles.isArray() && tileIndex < wangTiles.size(); ++tileIndex) {
                const Json wangTile = wangTiles.at(tileIndex);
                if (!wangTile.isObject() || !wangTile.has("tileid") || !wangTile.has("wangid")) continue;
                const Json wangId = wangTile.get("wangid");
                if (!wangId.isArray() || wangId.size() != 8) continue;
                const int gid = info.firstGid + wangTile.getInt("tileid", 0);
                // Tiled orders Wang positions N, NE, E, SE, S, SW, W, NW.
                // EVEngine terrain masks order NW, N, NE, E, SE, S, SW, W.
                constexpr int tiledToTerrainBit[8] = {1, 2, 3, 4, 5, 6, 7, 0};
                for (int color = 1; color <= 255; ++color) {
                    int  mask    = 0;
                    bool present = false;
                    for (int position = 0; position < 8; ++position) {
                        if (wangId.at(size_t(position)).asInt(0) != color) continue;
                        present = true;
                        mask |= 1 << tiledToTerrainBit[position];
                    }
                    if (present) info.terrainRules.push_back({gid, int(setIndex) * 256 + color, mask});
                }
            }
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

eve::json::Document readJsonDocument(eve::filesystem::Filesystem *fs, const std::string &path, std::string *error) {
    if (!fs) {
        if (error) *error = "map.import.filesystem-unavailable: " + path;
        return {};
    }
    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "map.import.external-tileset-empty: " + path;
        return {};
    }
    std::string decodeError;
    auto        document = eve::json::Document::parse(text, &decodeError);
    if (!document.valid() || !document.root().isObject()) {
        if (error)
            *error = "map.import.external-tileset-invalid-json: " + path +
                     (decodeError.empty() ? std::string{} : " (" + decodeError + ")");
        return {};
    }
    return document;
}

TilesetInfo readTsxTileset(eve::filesystem::Filesystem *fs, const std::string &path, int firstGid,
                           std::string *error) {
    TilesetInfo info;
    info.firstGid = firstGid;
    std::string text;
    if (!readImportText(fs, path, text)) {
        if (error) *error = "map.import.external-tileset-empty: " + path;
        return info;
    }
    std::string parseError;
    auto        document = eve::xml::Document::parse(text, &parseError);
    auto        root     = document.root();
    if (!document.valid() || !root || root.tagName() != "tileset") {
        if (error)
            *error = "map.import.external-tsx-invalid: " + path +
                     (parseError.empty() ? " (root is not tileset)" : " (" + parseError + ")");
        return {};
    }
    info.columns = root.getIntAttribute("columns", 1);
    info.tileW   = root.getIntAttribute("tilewidth", 32);
    info.tileH   = root.getIntAttribute("tileheight", 32);
    info.margin  = root.getIntAttribute("margin", 0);
    info.spacing = root.getIntAttribute("spacing", 0);
    const auto images = root.elementsByTag("image");
    if (!images.empty()) info.image = resolveAssetPath(path, images.front().getAttribute("source"));
    const auto tiles = root.children("tile");
    for (size_t index = 0; index < tiles.size(); ++index) {
        const auto                &tile = tiles[index];
        TileLayer::Tileset::Visual visual;
        const int                  localId = tile.getIntAttribute("id", int(index));
        visual.gid                         = firstGid + localId;
        for (const auto &property : tile.elementsByTag("property")) {
            const std::string name  = property.getAttribute("name");
            const std::string type  = property.getAttribute("type");
            const std::string value = property.getAttribute("value");
            info.customData.push_back({visual.gid, name, type.empty() ? "string" : type, value});
            if (name == "walkable")
                visual.walkable = value != "false" && value != "0";
            else if (name == "cost") {
                try {
                    visual.cost = std::max(0.001f, std::stof(value));
                } catch (...) {
                }
            } else if (name == "enterMask") {
                visual.enterMask = uint8_t(property.getIntAttribute("value", 0xff));
            } else if (name == "exitMask") {
                visual.exitMask = uint8_t(property.getIntAttribute("value", 0xff));
            } else if (name == "opaque") {
                visual.opaque = value != "false" && value != "0";
            } else if (name == "semanticFlags") {
                visual.semanticFlags = uint32_t(property.getIntAttribute("value", 0));
            }
        }
        for (const auto &object : tile.elementsByTag("object")) {
            const float x      = float(object.getIntAttribute("x", 0));
            const float y      = float(object.getIntAttribute("y", 0));
            const float width  = float(object.getIntAttribute("width", 0));
            const float height = float(object.getIntAttribute("height", 0));
            if (width > 0.f && height > 0.f) visual.collisionShapes.push_back({x, y, width, height});
        }
        if (!visual.collisionShapes.empty()) visual.walkable = false;
        info.visuals.push_back(visual);
        TileLayer::Tileset::Animation animation;
        animation.gid = visual.gid;
        for (const auto &frame : tile.elementsByTag("frame")) {
            animation.frames.push_back({firstGid + frame.getIntAttribute("tileid", localId),
                                        std::max(1, frame.getIntAttribute("duration", 100))});
        }
        if (!animation.frames.empty()) info.animations.push_back(std::move(animation));
    }
    const auto wangSets = root.elementsByTag("wangset");
    for (size_t setIndex = 0; setIndex < wangSets.size(); ++setIndex) {
        const auto &set = wangSets[setIndex];
        for (const auto &wangTile : set.elementsByTag("wangtile")) {
            std::array<int, 8> values{};
            std::stringstream  stream(wangTile.getAttribute("wangid"));
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
                        {firstGid + wangTile.getIntAttribute("tileid", 0), int(setIndex) * 256 + color, mask});
            }
        }
    }
    return info;
}

void applyTileset(TileLayer *layer, const TilesetInfo &info) {
    if (!layer) return;
    layer->setTilesetTileSize(info.tileW, info.tileH);
    graphics::Texture *tex     = tryLoadTexture(info.image);
    layer->resource()->texturePath = info.image;
    layer->setTileset(tex, info.firstGid, info.columns, info.margin, info.spacing);
    layer->tileset()->visuals      = info.visuals;
    layer->tileset()->animations   = info.animations;
    layer->tileset()->terrainRules = info.terrainRules;
    layer->tileset()->customData   = info.customData;
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

bool decodeLayerData(const Json &layerObj, size_t expectedCount, std::vector<uint32_t> &out, std::string *error) {
    if (!layerObj || !layerObj.has("data")) {
        if (error) *error = "missing data";
        return false;
    }

    const Json data = layerObj.get("data");
    if (data.isArray()) {
        out.resize(data.size());
        for (size_t i = 0; i < data.size(); ++i) out[i] = asUInt32(data.at(i), 0);
        if (expectedCount > 0 && out.size() != expectedCount) {
            if (error) *error = "gid count mismatch";
            return false;
        }
        return true;
    }

    const std::string encoding    = layerObj.has("encoding") ? layerObj.getString("encoding") : "";
    const std::string compression = layerObj.has("compression") ? layerObj.getString("compression") : "";
    if (encoding != "base64") {
        if (error) *error = "unsupported encoding";
        return false;
    }
    if (compression == "zstd") {
        if (error) *error = "zstd not supported; export as zlib";
        return false;
    }

    const std::string b64 = data.asString();
    size_t            decodedLen = 0;
    std::unique_ptr<char[]> decoded(eve::data::decode("base64", b64.data(), b64.size(), decodedLen));
    if (!decoded) {
        if (error) *error = "base64 decode failed";
        return false;
    }

    const char *bytes  = decoded.get();
    size_t      nbytes = decodedLen;
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
        bytes  = inflated.get();
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
        out[i] = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    return true;
}

void applyLayerDraw(TileLayer *layer, const Json &layerObj) {
    if (!layer || !layerObj) return;
    if (layerObj.has("visible")) layer->setVisible(layerObj.getBool("visible", true));
    if (layerObj.has("layer"))
        layer->setLayer(layerObj.getInt("layer", layer->getLayer()));
    else if (layerObj.has("id"))
        layer->setLayer(layerObj.getInt("id", layer->getLayer()));
    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(layerObj, "tint", r, g, b, a))
        layer->setTint(r, g, b, a);
    else if (layerObj.has("opacity")) {
        a = layerObj.getFloat("opacity", 1.f);
        layer->setTint(1.f, 1.f, 1.f, a);
    }
    float ox = layer->getX(), oy = layer->getY();
    if (layerObj.has("x")) ox = layerObj.getFloat("x", ox);
    if (layerObj.has("offsetx")) ox += layerObj.getFloat("offsetx", 0.f);
    if (layerObj.has("y")) oy = layerObj.getFloat("y", oy);
    if (layerObj.has("offsety")) oy += layerObj.getFloat("offsety", 0.f);
    if (layerObj.has("x") || layerObj.has("y") || layerObj.has("offsetx") || layerObj.has("offsety"))
        layer->setOrigin(ox, oy);
}

bool isTileLayerObject(const Json &o) {
    if (!o || !o.isObject()) return false;
    if (o.has("type")) {
        const std::string t = o.getString("type");
        if (!t.empty() && t != "tilelayer") return false;
    }
    return o.has("data") || o.has("chunks");
}

bool isObjectGroup(const Json &o) {
    if (!o || !o.isObject() || !o.has("type")) return false;
    return o.getString("type") == "objectgroup";
}

bool parseOrientation(const Json &root, TileLayer::Config *cfg, std::string *error) {
    if (!root || !cfg) return false;
    if (!root.has("orientation")) return true;
    const std::string o = root.getString("orientation");
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
    if (root.has("staggeraxis")) {
        const std::string a = root.getString("staggeraxis");
        cfg->staggerAxis    = (a == "x") ? StaggerAxis::X : StaggerAxis::Y;
    }
    if (root.has("staggerindex")) {
        const std::string i = root.getString("staggerindex");
        cfg->staggerIndex   = (i == "even") ? StaggerIndex::Even : StaggerIndex::Odd;
    }
    if (root.has("hexsidelength")) cfg->hexSideLength = root.getFloat("hexsidelength", 0.f);
    return true;
}

bool applyMapGlobals(TileLayer *layer, const Json &root, std::string *error) {
    if (!layer || !root) return false;

    int   mapW  = layer->getMapWidth();
    int   mapH  = layer->getMapHeight();
    float tileW = layer->getTileWidth();
    float tileH = layer->getTileHeight();

    if (root.has("width")) mapW = root.getInt("width", mapW);
    if (root.has("height")) mapH = root.getInt("height", mapH);
    if (root.has("tilewidth"))
        tileW = root.getFloat("tilewidth", tileW);
    else if (root.has("tileWidth"))
        tileW = root.getFloat("tileWidth", tileW);
    if (root.has("tileheight"))
        tileH = root.getFloat("tileheight", tileH);
    else if (root.has("tileHeight"))
        tileH = root.getFloat("tileHeight", tileH);

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
            if (root.has("cellGapX")) gapX = root.getFloat("cellGapX", gapX);
            if (root.has("cellGapY")) gapY = root.getFloat("cellGapY", gapY);
            if (root.has("cellGapX") || root.has("cellGapY")) layer->setCellGap(gapX, gapY);
        }
    }

    if (!parseOrientation(root, &(*layer->config()), error)) return false;

    float ox = layer->getX(), oy = layer->getY();
    if (readVec2(root, "origin", ox, oy))
        layer->setOrigin(ox, oy);
    else {
        if (root.has("x")) ox = root.getFloat("x", ox);
        if (root.has("y")) oy = root.getFloat("y", oy);
        if (root.has("x") || root.has("y")) layer->setOrigin(ox, oy);
    }

    if (root.has("layer")) layer->setLayer(root.getInt("layer", layer->getLayer()));
    if (root.has("visible")) layer->setVisible(root.getBool("visible", layer->isVisible()));
    if (root.has("autoReload")) layer->resource()->autoReload = root.getBool("autoReload", true);

    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(root, "tint", r, g, b, a)) layer->setTint(r, g, b, a);

    if (root.has("tileset") && !root.get("tileset").isArray()) {
        const Json tileset = root.get("tileset");
        if (tileset.isObject() && !tileset.has("source")) applyTileset(layer, readTilesetObject(tileset));
    } else if (root.has("tilesets")) {
        const Json arr = root.get("tilesets");
        for (size_t i = 0; arr.isArray() && i < arr.size(); ++i) {
            const Json o = arr.at(i);
            if (!o.isObject() || o.has("source")) continue;
            applyTileset(layer, readTilesetObject(o));
            break;
        }
    } else if (root.has("image") || root.has("texture")) {
        applyTileset(layer, readTilesetObject(root));
    }
    return true;
}

bool applyFlatLayerData(TileLayer *layer, const Json &root, std::string *error) {
    const int    mapW = layer->getMapWidth();
    const int    mapH = layer->getMapHeight();
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

bool applyOneLayerObject(TileLayer *layer, const Json &layerObj, int mapW, int mapH, std::string *error) {
    if (!layer || !layerObj) return false;
    if (layerObj.has("chunks")) {
        const Json chunks = layerObj.get("chunks");
        if (!chunks.isArray() || chunks.size() == 0) return false;
        int  minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool first = true;
        for (size_t i = 0; i < chunks.size(); ++i) {
            const Json chunk = chunks.at(i);
            if (!chunk.isObject()) continue;
            const int x = chunk.getInt("x", 0), y = chunk.getInt("y", 0);
            const int w = chunk.getInt("width", 0), h = chunk.getInt("height", 0);
            if (w <= 0 || h <= 0) continue;
            if (first) {
                minX = x;
                minY = y;
                maxX = x + w;
                maxY = y + h;
                first = false;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x + w);
                maxY = std::max(maxY, y + h);
            }
        }
        if (first) return false;
        float shiftX = 0.f, shiftY = 0.f;
        layer->tileToWorld(minX, minY, shiftX, shiftY);
        layer->resize(maxX - minX, maxY - minY);
        layer->setOrigin(shiftX, shiftY);
        auto &gids = layer->tiles()->gids;
        for (size_t i = 0; i < chunks.size(); ++i) {
            const Json chunk = chunks.at(i);
            if (!chunk.isObject()) continue;
            const int x = chunk.getInt("x", 0), y = chunk.getInt("y", 0);
            const int w = chunk.getInt("width", 0), h = chunk.getInt("height", 0);
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
    }
    int w = mapW, h = mapH;
    if (layerObj.has("width")) w = layerObj.getInt("width", w);
    if (layerObj.has("height")) h = layerObj.getInt("height", h);
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

void parseObjectGroup(const Json &group, std::vector<MapObject> &out) {
    if (!group || !group.has("objects")) return;
    const Json arr = group.get("objects");
    if (!arr.isArray()) return;
    for (size_t i = 0; i < arr.size(); ++i) {
        const Json o = arr.at(i);
        if (!o.isObject()) continue;
        MapObject mo;
        if (o.has("name")) mo.name = o.getString("name");
        if (o.has("type"))
            mo.type = o.getString("type");
        else if (o.has("class"))
            mo.type = o.getString("class");
        mo.x      = o.has("x") ? o.getFloat("x", 0.f) : 0.f;
        mo.y      = o.has("y") ? o.getFloat("y", 0.f) : 0.f;
        mo.width  = o.has("width") ? o.getFloat("width", 0.f) : 0.f;
        mo.height = o.has("height") ? o.getFloat("height", 0.f) : 0.f;
        if (o.has("gid")) mo.gid = uint32_t(o.getInt("gid", 0));
        if (o.has("properties")) {
            const Json properties = o.get("properties");
            for (size_t propertyIndex = 0; properties.isArray() && propertyIndex < properties.size();
                 ++propertyIndex) {
                const Json property = properties.at(propertyIndex);
                if (!property.isObject() || !property.has("name") || !property.has("value")) continue;
                const std::string name = property.getString("name");
                if (!name.empty()) mo.properties.emplace(name, property.get("value").asString());
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
    Json  object;
    float offsetX = 0.f;
    float offsetY = 0.f;
    float opacity = 1.f;
    bool  visible = true;
};

void flattenLayers(const Json &source, std::vector<LayerEntry> &out, float parentX = 0.f, float parentY = 0.f,
                   float parentOpacity = 1.f, bool parentVisible = true) {
    for (size_t index = 0; source.isArray() && index < source.size(); ++index) {
        const Json object = source.at(index);
        if (!object.isObject()) continue;
        const float offsetX =
            parentX + (object.has("offsetx") ? object.getFloat("offsetx", 0.f) : 0.f);
        const float offsetY =
            parentY + (object.has("offsety") ? object.getFloat("offsety", 0.f) : 0.f);
        const float opacity =
            parentOpacity * (object.has("opacity") ? object.getFloat("opacity", 1.f) : 1.f);
        const bool visible =
            parentVisible && (!object.has("visible") || object.getBool("visible", true));
        if (object.has("type") && object.getString("type") == "group" && object.has("layers")) {
            flattenLayers(object.get("layers"), out, offsetX, offsetY, opacity, visible);
            continue;
        }
        out.push_back({object, offsetX, offsetY, opacity, visible});
    }
}

std::vector<TilesetInfo> readTilesets(const Json &root, const std::string &mapPath,
                                      eve::filesystem::Filesystem *fs, std::string *error) {
    std::vector<TilesetInfo> result;
    // Keep external JSON documents alive: TilesetInfo reads image paths as
    // strings (copied), but we still need the Document while reading fields.
    std::vector<eve::json::Document> ownedDocs;
    if (root.has("tilesets")) {
        const Json arr = root.get("tilesets");
        if (!arr.isArray()) {
            if (error) *error = "map.import.tilesets-invalid";
            return {};
        }
        for (size_t i = 0; i < arr.size(); ++i) {
            const Json o = arr.at(i);
            if (!o.isObject()) {
                if (error) *error = "map.import.tileset-entry-invalid: index=" + std::to_string(i);
                return {};
            }
            const int firstGid = o.has("firstgid") ? o.getInt("firstgid", 1) : 1;
            if (o.has("source")) {
                const std::string source = resolveAssetPath(mapPath, o.getString("source"));
                if (std::filesystem::path(source).extension() == ".tsx") {
                    TilesetInfo info = readTsxTileset(fs, source, firstGid, error);
                    if (error && !error->empty()) return {};
                    info.sourcePath = source;
                    result.push_back(std::move(info));
                    continue;
                }
                ownedDocs.push_back(readJsonDocument(fs, source, error));
                if (!ownedDocs.back().valid()) return {};
                TilesetInfo info = readTilesetObject(ownedDocs.back().root());
                info.firstGid    = firstGid;
                info.sourcePath  = source;
                info.image       = resolveAssetPath(source, info.image);
                result.push_back(std::move(info));
                continue;
            }
            TilesetInfo info = readTilesetObject(o);
            info.image       = resolveAssetPath(mapPath, info.image);
            result.push_back(std::move(info));
        }
    } else if (root.has("tileset") && !root.get("tileset").isArray()) {
        const Json o = root.get("tileset");
        if (o.isObject() && !o.has("source")) {
            TilesetInfo info = readTilesetObject(o);
            info.image       = resolveAssetPath(mapPath, info.image);
            result.push_back(std::move(info));
        }
    } else if (root.has("image") || root.has("texture")) {
        TilesetInfo info = readTilesetObject(root);
        info.image       = resolveAssetPath(mapPath, info.image);
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(),
              [](const TilesetInfo &left, const TilesetInfo &right) { return left.firstGid < right.firstGid; });
    return result;
}

std::vector<TileLayer *> loadMapObject(const Json &root, const std::string &path, eve::filesystem::Filesystem *fs,
                                       std::vector<MapObject> *objects, std::string *error) {
    std::vector<TileLayer *> out;
    if (!root || !root.isObject()) {
        if (error) *error = "config root must be object";
        return out;
    }

    int   mapW  = 10, mapH = 10;
    float tileW = 32.f, tileH = 32.f;
    if (root.has("width")) mapW = root.getInt("width", mapW);
    if (root.has("height")) mapH = root.getInt("height", mapH);
    if (root.has("tilewidth"))
        tileW = root.getFloat("tilewidth", tileW);
    else if (root.has("tileWidth"))
        tileW = root.getFloat("tileWidth", tileW);
    if (root.has("tileheight"))
        tileH = root.getFloat("tileheight", tileH);
    else if (root.has("tileHeight"))
        tileH = root.getFloat("tileHeight", tileH);

    TileLayer::Config orientCheck;
    if (!parseOrientation(root, &orientCheck, error)) return out;

    std::vector<TilesetInfo> tilesets = readTilesets(root, path, fs, error);
    if (root.has("tilesets") && tilesets.empty() && error && !error->empty()) return out;
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
        if (root.has("autoReload")) res->autoReload = root.getBool("autoReload", true);
    };

    if (root.has("layers")) {
        const Json arr = root.get("layers");
        if (arr.isArray()) {
            std::vector<LayerEntry> entries;
            flattenLayers(arr, entries);
            int sort = 0;
            for (const LayerEntry &entry : entries) {
                const Json &lo = entry.object;
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
                if (!lo.has("layer") && !lo.has("id")) layer->setLayer(sort);
                layer->setOrigin(layer->getX() + entry.offsetX, layer->getY() + entry.offsetY);
                layer->draw()->visible = layer->draw()->visible && entry.visible;
                layer->draw()->tint.a *= entry.opacity;
                ++sort;
                bindResource(layer);
                out.push_back(layer);
            }
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
    if (root.has("data")) {
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

bool applyConfigDocument(TileLayer *layer, eve::json::Value root) {
    if (!layer || !root || !root.isObject()) return false;

    std::string err;
    if (!applyMapGlobals(layer, root, &err)) return false;

    if (root.has("layers")) {
        const Json arr = root.get("layers");
        for (size_t i = 0; arr.isArray() && i < arr.size(); ++i) {
            const Json lo = arr.at(i);
            if (!isTileLayerObject(lo)) continue;
            return applyOneLayerObject(layer, lo, layer->getMapWidth(), layer->getMapHeight(), nullptr);
        }
    }

    if (root.has("data")) return applyFlatLayerData(layer, root, nullptr);
    return true;
}

bool applyConfigText(TileLayer *layer, const std::string &json, std::string *error) {
    std::string err;
    auto        doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return false;
    }
    const Json root = doc.root();
    if (!root.isObject()) {
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
    if (root.has("layers")) {
        const Json arr = root.get("layers");
        for (size_t i = 0; arr.isArray() && i < arr.size(); ++i) {
            const Json lo = arr.at(i);
            if (!isTileLayerObject(lo)) continue;
            if (applyOneLayerObject(layer, lo, layer->getMapWidth(), layer->getMapHeight(), error)) return true;
            rollback();
            return false;
        }
    }
    if (root.has("data")) {
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

    std::string decodeError;
    auto        document = eve::json::Document::parse(text, &decodeError);
    if (!document.valid() || !document.root().isObject()) {
        if (error) *error = decodeError.empty() ? "map.import.invalid-json" : decodeError;
        return false;
    }
    const Json               root     = document.root();
    std::vector<TilesetInfo> tilesets = readTilesets(root, path, fs, error);
    if (root.has("tilesets") && tilesets.empty() && error && !error->empty()) return false;

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
    std::string       decodeError;
    auto              doc = eve::json::Document::parse(text, &decodeError);
    if (!doc.valid() || !doc.root().isObject()) {
        if (error) *error = decodeError.empty() ? "invalid tileset manifest" : decodeError;
        return false;
    }
    Json root = doc.root();
    if (root.has("tileset")) {
        root = root.get("tileset");
        if (!root.isObject()) {
            if (error) *error = "tileset must be an object";
            return false;
        }
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
    std::string err;
    auto        doc = eve::json::Document::parse(json, &err);
    if (!doc.valid() || !doc.root().isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return {};
    }
    return loadMapObject(doc.root(), {}, nullptr, objects, error);
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
    std::string err;
    auto        doc = eve::json::Document::parse(text, &err);
    if (!doc.valid() || !doc.root().isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return {};
    }
    return loadMapObject(doc.root(), path, fs, objects, error);
}

std::vector<TileLayer *> loadMapFile(const std::string &path, std::string *error) {
    return loadMapFile(path, nullptr, error);
}

#include "map/RpgMakerTileImporter.inl"

}  // namespace eve::map
