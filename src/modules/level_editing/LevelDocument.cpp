#include "level_editing/LevelDocument.h"

#include "common/Exception.h"

#include <algorithm>

namespace eve::level_editing {

LevelDocument::LevelDocument(int width, int height, float tileWidth, float tileHeight)
    : width_(width), height_(height), tileWidth_(tileWidth), tileHeight_(tileHeight) {
    if (width < 1 || height < 1 || tileWidth <= 0 || tileHeight <= 0)
        throw Exception("LevelDocument: dimensions and tile size must be positive");
}

std::string LevelDocument::nextId(const char* prefix) { return std::string(prefix) + std::to_string(nextId_++); }

void LevelDocument::resize(int width, int height) {
    if (width < 1 || height < 1) throw Exception("LevelDocument::resize: dimensions must be positive");
    width_  = width;
    height_ = height;
    for (auto& l : layers_)
        if (l.tiles) l.tiles->resize(width, height);
}

void LevelDocument::setTileSize(float width, float height) {
    if (width <= 0 || height <= 0) throw Exception("LevelDocument::setTileSize: size must be positive");
    tileWidth_  = width;
    tileHeight_ = height;
}

void LevelDocument::setOrientation(const std::string& value) {
    if (value != "orthogonal" && value != "isometric" && value != "staggered" && value != "hexagonal")
        throw Exception("LevelDocument::setOrientation: unsupported orientation");
    orientation_ = value;
}

int LevelDocument::addTileLayer(const std::string& name) {
    LevelLayer l;
    l.id    = nextId("layer-");
    l.name  = name;
    l.kind  = LevelLayer::Kind::Tiles;
    l.tiles = std::make_unique<TileBuffer>(width_, height_);
    layers_.push_back(std::move(l));
    return int(layers_.size()) - 1;
}

int LevelDocument::addObjectLayer(const std::string& name) {
    LevelLayer l;
    l.id   = nextId("layer-");
    l.name = name;
    l.kind = LevelLayer::Kind::Objects;
    layers_.push_back(std::move(l));
    return int(layers_.size()) - 1;
}

LevelChange LevelDocument::removeLayer(int i) {
    if (!layer(i)) return LevelChange::Unchanged;
    layers_.erase(layers_.begin() + i);
    return LevelChange::Changed;
}

LevelChange LevelDocument::moveLayer(int from, int to) {
    if (!layer(from) || to < 0 || to >= int(layers_.size()) || from == to) return LevelChange::Unchanged;
    auto moving = std::move(layers_[from]);
    layers_.erase(layers_.begin() + from);
    layers_.insert(layers_.begin() + to, std::move(moving));
    return LevelChange::Changed;
}

eve::OptionalRef<LevelLayer> LevelDocument::layer(int i) {
    return i < 0 || i >= int(layers_.size()) ? eve::OptionalRef<LevelLayer>{}
                                             : eve::OptionalRef<LevelLayer>{std::ref(layers_[i])};
}
eve::OptionalRef<const LevelLayer> LevelDocument::layer(int i) const {
    return i < 0 || i >= int(layers_.size()) ? eve::OptionalRef<const LevelLayer>{}
                                             : eve::OptionalRef<const LevelLayer>{std::cref(layers_[i])};
}

const std::string& LevelDocument::getLayerName(int i) const {
    auto l = layer(i);
    if (!l) throw Exception("LevelDocument: layer index out of bounds");
    return l->get().name;
}
void LevelDocument::setLayerName(int i, const std::string& name) {
    auto l = layer(i);
    if (!l) throw Exception("LevelDocument: layer index out of bounds");
    l->get().name = name;
}
const std::string LevelDocument::getLayerKind(int i) const {
    auto l = layer(i);
    if (!l) return {};
    return l->get().kind == LevelLayer::Kind::Tiles ? "tiles" : "objects";
}
eve::OptionalRef<TileBuffer> LevelDocument::getTileLayer(int i) {
    auto l = layer(i);
    return l && l->get().tiles ? eve::OptionalRef<TileBuffer>{std::ref(*l->get().tiles)}
                               : eve::OptionalRef<TileBuffer>{};
}
eve::OptionalRef<const TileBuffer> LevelDocument::getTileLayer(int i) const {
    auto l = layer(i);
    return l && l->get().tiles ? eve::OptionalRef<const TileBuffer>{std::cref(*l->get().tiles)}
                               : eve::OptionalRef<const TileBuffer>{};
}

int LevelDocument::addObject(int li, const std::string& type, float x, float y) {
    auto l = layer(li);
    if (!l || l->get().kind != LevelLayer::Kind::Objects) return -1;
    LevelObject o;
    o.id   = nextId("object-");
    o.type = type;
    o.x    = x;
    o.y    = y;
    l->get().objects.push_back(std::move(o));
    return int(l->get().objects.size()) - 1;
}
int LevelDocument::getObjectCount(int li) const {
    auto l = layer(li);
    return l ? int(l->get().objects.size()) : 0;
}
eve::OptionalRef<LevelObject> LevelDocument::object(int li, int oi) {
    auto l = layer(li);
    return !l || oi < 0 || oi >= int(l->get().objects.size())
               ? eve::OptionalRef<LevelObject>{}
               : eve::OptionalRef<LevelObject>{std::ref(l->get().objects[oi])};
}
eve::OptionalRef<const LevelObject> LevelDocument::object(int li, int oi) const {
    auto l = layer(li);
    return !l || oi < 0 || oi >= int(l->get().objects.size())
               ? eve::OptionalRef<const LevelObject>{}
               : eve::OptionalRef<const LevelObject>{std::cref(l->get().objects[oi])};
}
LevelChange LevelDocument::removeObject(int li, int oi) {
    auto l = layer(li);
    if (!l || oi < 0 || oi >= int(l->get().objects.size())) return LevelChange::Unchanged;
    l->get().objects.erase(l->get().objects.begin() + oi);
    return LevelChange::Changed;
}

void        LevelDocument::setProperty(const std::string& key, const std::string& value) { properties_[key] = value; }
std::string LevelDocument::getProperty(const std::string& key, const std::string& fallback) const {
    auto it = properties_.find(key);
    return it == properties_.end() ? fallback : it->second;
}

}  // namespace eve::level_editing
