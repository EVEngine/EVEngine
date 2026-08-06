#include "map/TileConfig.h"

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
};

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
    return info;
}

void applyTileset(TileLayer *layer, const TilesetInfo &info) {
    if (!layer) return;
    layer->setTilesetTileSize(info.tileW, info.tileH);
    graphics::Texture *tex = tryLoadTexture(info.image);
    layer->resource()->texturePath = info.image;
    layer->setTileset(tex, info.firstGid, info.columns, info.margin, info.spacing);
}

bool readDataArray(Poco::JSON::Object::Ptr layerObj, std::vector<uint32_t> &out) {
    if (!layerObj || !layerObj->has("data")) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = layerObj->getArray("data");
    } catch (...) {
        return false;
    }
    if (!arr) return false;
    out.resize(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) out[i] = uint32_t(asInt(arr->get(i), 0));
    return true;
}

void applyLayerDraw(TileLayer *layer, Poco::JSON::Object::Ptr layerObj) {
    if (!layer || !layerObj) return;
    if (layerObj->has("visible"))
        layer->setVisible(asBool(layerObj->get("visible"), true));
    // Tiled uses "opacity"; EVEngine also accepts "layer" for render sort.
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

void applyMapGlobals(TileLayer *layer, Poco::JSON::Object::Ptr root) {
    if (!layer || !root) return;

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

    // Single tileset object / image shortcut.
    if (root->has("tileset") && !root->isArray("tileset")) {
        try {
            applyTileset(layer, readTilesetObject(root->getObject("tileset")));
        } catch (...) {
        }
    } else if (root->has("tilesets")) {
        try {
            auto arr = root->getArray("tilesets");
            if (arr && arr->size() > 0) {
                try {
                    applyTileset(layer, readTilesetObject(arr->getObject(0)));
                } catch (...) {
                }
            }
        } catch (...) {
        }
    } else if (root->has("image") || root->has("texture")) {
        TilesetInfo info = readTilesetObject(root);
        applyTileset(layer, info);
    }
}

bool applyFlatLayerData(TileLayer *layer, Poco::JSON::Object::Ptr root) {
    // Flat single-layer: data at root.
    std::vector<uint32_t> data;
    if (!readDataArray(root, data)) return false;
    const int mapW = layer->getMapWidth();
    const int mapH = layer->getMapHeight();
    const size_t need = size_t(std::max(0, mapW) * std::max(0, mapH));
    if (need == 0) return false;
    auto &gids = layer->tiles()->gids;
    gids.assign(need, 0u);
    const size_t n = std::min(need, data.size());
    for (size_t i = 0; i < n; ++i) gids[i] = data[i];
    return true;
}

bool applyOneLayerObject(TileLayer *layer, Poco::JSON::Object::Ptr layerObj, int mapW, int mapH) {
    if (!layer || !layerObj) return false;
    int w = mapW, h = mapH;
    if (layerObj->has("width")) w = asInt(layerObj->get("width"), w);
    if (layerObj->has("height")) h = asInt(layerObj->get("height"), h);
    if (w != layer->getMapWidth() || h != layer->getMapHeight()) layer->resize(w, h);

    std::vector<uint32_t> data;
    if (!readDataArray(layerObj, data)) return false;
    const size_t need = size_t(std::max(0, layer->getMapWidth()) * std::max(0, layer->getMapHeight()));
    auto &gids = layer->tiles()->gids;
    gids.assign(need, 0u);
    const size_t n = std::min(need, data.size());
    for (size_t i = 0; i < n; ++i) gids[i] = data[i];

    applyLayerDraw(layer, layerObj);
    return true;
}

}  // namespace

bool applyConfigDocument(TileLayer *layer, data::JsonDocument *doc) {
    if (!layer || !doc || !doc->isObject()) return false;
    auto root = doc->object();
    if (!root) return false;

    applyMapGlobals(layer, root);

    // Prefer nested layers[0]; fall back to root.data.
    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr lo;
                    try {
                        lo = arr->getObject(i);
                    } catch (...) {
                        continue;
                    }
                    if (!isTileLayerObject(lo)) continue;
                    return applyOneLayerObject(layer, lo, layer->getMapWidth(),
                                               layer->getMapHeight());
                }
            }
        } catch (...) {
        }
    }

    if (root->has("data")) return applyFlatLayerData(layer, root);

    // Globals-only config still succeeds (empty map).
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
    if (!applyConfigDocument(layer, doc.get())) {
        if (error) *error = "config root must be object";
        return false;
    }
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

std::vector<TileLayer *> loadMapFile(const std::string &path, std::string *error) {
    std::vector<TileLayer *> out;
    if (path.empty()) {
        if (error) *error = "empty path";
        return out;
    }

    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return out;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return out;
    }

    std::string text(static_cast<const char *>(data->getData()), data->getSize());
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(text, &err));
    if (!doc || !doc->isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return out;
    }
    auto root = doc->object();
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

    TilesetInfo defaultTs;
    if (root->has("tilesets")) {
        try {
            auto arr = root->getArray("tilesets");
            if (arr && arr->size() > 0) defaultTs = readTilesetObject(arr->getObject(0));
        } catch (...) {
        }
    } else if (root->has("tileset") && !root->isArray("tileset")) {
        try {
            defaultTs = readTilesetObject(root->getObject("tileset"));
        } catch (...) {
        }
    } else if (root->has("image") || root->has("texture")) {
        defaultTs = readTilesetObject(root);
    }

    auto bindResource = [&](TileLayer *layer) {
        auto res = layer->resource();
        res->path = path;
        res->modtime = fileModtime(path);
        if (root->has("autoReload"))
            res->autoReload = asBool(root->get("autoReload"), true);
        fs->watch(path);
        if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
            hot->bind(path, "tilemap");
    };

    // Multi-layer document.
    if (root->has("layers")) {
        try {
            auto arr = root->getArray("layers");
            if (arr) {
                int sort = 0;
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr lo;
                    try {
                        lo = arr->getObject(i);
                    } catch (...) {
                        continue;
                    }
                    if (!isTileLayerObject(lo)) continue;
                    TileLayer *layer = TileLayer::createLayer(mapW, mapH, tileW, tileH);
                    applyMapGlobals(layer, root);
                    applyTileset(layer, defaultTs);
                    applyOneLayerObject(layer, lo, mapW, mapH);
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

    // Flat single-layer (root.data) or empty map shell.
    TileLayer *layer = TileLayer::createLayer(mapW, mapH, tileW, tileH);
    applyMapGlobals(layer, root);
    applyTileset(layer, defaultTs);
    if (root->has("data")) applyFlatLayerData(layer, root);
    bindResource(layer);
    out.push_back(layer);
    return out;
}

}  // namespace eve::map
