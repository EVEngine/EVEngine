#include "map/TileConfig.h"
#include "map/TileOrientation.h"

#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "filesystem/HotReload.h"
#include "graphics/Graphics.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <memory>

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
    int firstGid = 1;
    int columns = 1;
    int tileW = 32;
    int tileH = 32;
    int margin = 0;
    int spacing = 0;
    std::vector<TileLayer::Tileset::Visual> visuals;
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
                    auto visual = readTileVisual(tile, info.firstGid + int(i));
                    if (visual.gid > 0 && visual.width > 0 && visual.height > 0)
                        info.visuals.push_back(visual);
                }
            }
        } catch (...) {
        }
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
            for (size_t i = 0; i < arr->size(); ++i)
                out[i] = uint32_t(asInt(arr->get(static_cast<unsigned int>(i)), 0));
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
    return o->has("data");
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
    return true;
}

bool applyOneLayerObject(TileLayer *layer, Poco::JSON::Object::Ptr layerObj, int mapW, int mapH,
                         std::string *error) {
    if (!layer || !layerObj) return false;
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

TilesetInfo readDefaultTileset(Poco::JSON::Object::Ptr root) {
    TilesetInfo defaultTs;
    if (root->has("tilesets")) {
        try {
            auto arr = root->getArray("tilesets");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    try {
                        auto o = arr->getObject(static_cast<unsigned int>(i));
                        if (o && o->has("source")) continue;
                        return readTilesetObject(o);
                    } catch (...) {
                    }
                }
            }
        } catch (...) {
        }
    } else if (root->has("tileset") && !root->isArray("tileset")) {
        try {
            auto o = root->getObject("tileset");
            if (o && !o->has("source")) return readTilesetObject(o);
        } catch (...) {
        }
    } else if (root->has("image") || root->has("texture")) {
        return readTilesetObject(root);
    }
    return defaultTs;
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

    TilesetInfo defaultTs = readDefaultTileset(root);
    if (objects) objects->clear();

    auto bindResource = [&](TileLayer *layer) {
        auto res = layer->resource();
        res->path = path;
        if (!path.empty()) {
            res->modtime = fileModtime(path);
            if (fs) {
                fs->watch(path);
                if (auto *hot =
                        eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
                    hot->bind(path, "tilemap");
            }
        }
        if (root->has("autoReload"))
            res->autoReload = asBool(root->get("autoReload"), true);
    };

    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                int sort = 0;
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr lo;
                    try {
                        lo = arr->getObject(static_cast<unsigned int>(i));
                    } catch (...) {
                        continue;
                    }
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
                    applyTileset(layer, defaultTs);
                    if (!applyOneLayerObject(layer, lo, mapW, mapH, error)) {
                        abandonLayers(out);
                        layer->clear();
                        layer->setVisible(false);
                        return {};
                    }
                    if (!lo->has("layer") && !lo->has("id")) layer->setLayer(sort);
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
    applyTileset(layer, defaultTs);
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
    if (!applyMapGlobals(layer, root, error)) return false;
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
                                               layer->getMapHeight(), error);
                }
            }
        } catch (...) {
        }
    }
    if (root->has("data")) return applyFlatLayerData(layer, root, error);
    return true;
}

bool loadConfigFile(TileLayer *layer, const std::string &path, std::string *error) {
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

    std::string text(static_cast<const char *>(data->getData()), data->getSize());
    if (!applyConfigText(layer, text, error)) return false;

    auto res = layer->resource();
    res->path = path;
    res->modtime = fileModtime(path);
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
    if (path.empty()) {
        if (error) *error = "empty path";
        return {};
    }

    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return {};
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return {};
    }

    std::string text(static_cast<const char *>(data->getData()), data->getSize());
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

}  // namespace eve::map
