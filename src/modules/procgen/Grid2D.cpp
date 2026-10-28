#include "procgen/Grid2D.h"

#include "procgen/Semantic.h"

#include <algorithm>

namespace eve::procgen {

bool Grid2D::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

void Grid2D::resize(int width, int height) {
    width_  = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    cells_.assign(size_t(width_ * height_), Semantic::Empty);
    detail_.assign(size_t(width_ * height_), 0);
}

int Grid2D::getWidth() const { return width_; }
int Grid2D::getHeight() const { return height_; }

void Grid2D::setCell(int x, int y, int semantic) {
    if (!inBounds(x, y)) return;
    cells_[size_t(y * width_ + x)] = uint32_t(semantic < 0 ? 0 : semantic);
}

int Grid2D::getCell(int x, int y) const {
    if (!inBounds(x, y)) return int(Semantic::Empty);
    return int(cells_[size_t(y * width_ + x)]);
}

void Grid2D::fill(int semantic) {
    std::fill(cells_.begin(), cells_.end(), uint32_t(semantic < 0 ? 0 : semantic));
}

void Grid2D::setDetail(int x, int y, int value) {
    if (!inBounds(x, y)) return;
    detail_[size_t(y * width_ + x)] = uint8_t(value < 0 ? 0 : (value > 255 ? 255 : value));
}

int Grid2D::getDetail(int x, int y) const {
    if (!inBounds(x, y)) return 0;
    return int(detail_[size_t(y * width_ + x)]);
}

void Grid2D::setMeta(const std::string &key, const std::string &value) { meta_[key] = value; }

std::string Grid2D::getMeta(const std::string &key, const std::string &defaultValue) const {
    auto it = meta_.find(key);
    return it == meta_.end() ? defaultValue : it->second;
}

void Grid2D::clearObjects() {
    objects_.clear();
}

void Grid2D::addObjectAt(const std::string &name, const std::string &type, float x, float y) {
    addObject(name, type, x, y, 0.f, 0.f, 0);
}

void Grid2D::addObject(const std::string &name, const std::string &type, float x, float y,
                       float width, float height, int gid) {
    GridObject o;
    o.name   = name;
    o.type   = type;
    o.x      = x;
    o.y      = y;
    o.width  = width;
    o.height = height;
    o.gid    = uint32_t(gid < 0 ? 0 : gid);
    objects_.push_back(std::move(o));
}

void Grid2D::addAssetObject(const std::string &name, const std::string &role,
                            const std::string &asset, float x, float y, float width, float height,
                            float rotationDegrees, int flags) {
    addObject(name, role, x, y, width, height, 0);
    objects_.back().asset = asset;
    objects_.back().rotationDegrees = rotationDegrees;
    objects_.back().placementFlags = uint32_t(flags < 0 ? 0 : flags);
}

int Grid2D::getObjectCount() const { return int(objects_.size()); }

std::string Grid2D::getObjectName(int i) const {
    if (i < 0 || i >= int(objects_.size())) return {};
    return objects_[size_t(i)].name;
}
std::string Grid2D::getObjectType(int i) const {
    if (i < 0 || i >= int(objects_.size())) return {};
    return objects_[size_t(i)].type;
}
float Grid2D::getObjectX(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].x;
}
float Grid2D::getObjectY(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].y;
}
float Grid2D::getObjectWidth(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].width;
}
float Grid2D::getObjectHeight(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].height;
}
int Grid2D::getObjectGid(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0;
    return int(objects_[size_t(i)].gid);
}
std::string Grid2D::getObjectAsset(int i) const {
    if (i < 0 || i >= int(objects_.size())) return {};
    return objects_[size_t(i)].asset;
}
float Grid2D::getObjectRotation(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].rotationDegrees;
}
int Grid2D::getObjectFlags(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0;
    return int(objects_[size_t(i)].placementFlags);
}

}  // namespace eve::procgen
