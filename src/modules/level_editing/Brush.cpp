#include "level_editing/Brush.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace eve::level_editing {

Brush::Brush() = default;

void Brush::setTool(const std::string &tool) {
    if (tool != "paint" && tool != "erase" && tool != "fill" && tool != "line" && tool != "rect" &&
        tool != "stamp") {
        throw Exception("Brush::setTool: expected paint|erase|fill|line|rect|stamp");
    }
    tool_ = tool;
}

void Brush::setSize(int size) {
    if (size < 1) throw Exception("Brush::setSize: size must be >= 1");
    size_ = size;
}

void Brush::setShape(const std::string &shape) {
    if (shape != "square" && shape != "circle") {
        throw Exception("Brush::setShape: expected square|circle");
    }
    shape_ = shape;
}

void Brush::setTile(int gid) { tile_ = gid; }

void Brush::setEraseTile(int gid) { eraseTile_ = gid; }

void Brush::setStampSize(int width, int height) {
    if (width < 0 || height < 0) throw Exception("Brush::setStampSize: size must be >= 0");
    stampW_ = width;
    stampH_ = height;
    stamp_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
}

void Brush::setStampTile(int lx, int ly, int gid) {
    if (lx < 0 || ly < 0 || lx >= stampW_ || ly >= stampH_) {
        throw Exception("Brush::setStampTile: out of stamp bounds");
    }
    stamp_[static_cast<size_t>(ly * stampW_ + lx)] = gid;
}

int Brush::getStampTile(int lx, int ly) const {
    if (lx < 0 || ly < 0 || lx >= stampW_ || ly >= stampH_) {
        throw Exception("Brush::getStampTile: out of stamp bounds");
    }
    return stamp_[static_cast<size_t>(ly * stampW_ + lx)];
}

void Brush::clearStamp() {
    stampW_ = 0;
    stampH_ = 0;
    stamp_.clear();
}

void Brush::clearChanges() { changes_.clear(); }

void Brush::clearPreview() { preview_.clear(); }

void Brush::collectBrushCells(int tx, int ty, std::vector<std::pair<int, int>> &out) const {
    out.clear();
    int r = size_ / 2;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (shape_ == "circle") {
                float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (d > static_cast<float>(r) + 0.01f) continue;
            }
            out.emplace_back(tx + dx, ty + dy);
        }
    }
}

void Brush::collectLineCells(int x0, int y0, int x1, int y1,
                             std::vector<std::pair<int, int>> &out) const {
    out.clear();
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (true) {
        std::vector<std::pair<int, int>> brush;
        collectBrushCells(x, y, brush);
        out.insert(out.end(), brush.begin(), brush.end());
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void Brush::collectRectCells(int x0, int y0, int x1, int y1, bool filled,
                             std::vector<std::pair<int, int>> &out) const {
    out.clear();
    int minX = std::min(x0, x1), maxX = std::max(x0, x1);
    int minY = std::min(y0, y1), maxY = std::max(y0, y1);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!filled && x != minX && x != maxX && y != minY && y != maxY) continue;
            out.emplace_back(x, y);
        }
    }
}

int Brush::applyCells(TileBuffer *buffer, const std::vector<std::pair<int, int>> &cells, int gid) {
    if (!buffer) throw Exception("Brush: null TileBuffer");
    clearChanges();
    // Deduplicate while preserving first occurrence
    std::vector<std::pair<int, int>> unique;
    unique.reserve(cells.size());
    for (const auto &c : cells) {
        bool found = false;
        for (const auto &u : unique) {
            if (u.first == c.first && u.second == c.second) {
                found = true;
                break;
            }
        }
        if (!found) unique.push_back(c);
    }
    for (const auto &c : unique) {
        if (!buffer->containsCell(c.first, c.second)) continue;
        int old = buffer->getGid(c.first, c.second);
        if (old == gid) continue;
        buffer->setGid(c.first, c.second, gid);
        Cell ch;
        ch.x = c.first;
        ch.y = c.second;
        ch.oldGid = old;
        ch.gid = gid;
        changes_.push_back(ch);
    }
    return static_cast<int>(changes_.size());
}

int Brush::applyStamp(TileBuffer *buffer, int tx, int ty) {
    if (!buffer) throw Exception("Brush: null TileBuffer");
    if (stampW_ <= 0 || stampH_ <= 0) return 0;
    clearChanges();
    int ox = tx - stampW_ / 2;
    int oy = ty - stampH_ / 2;
    for (int ly = 0; ly < stampH_; ++ly) {
        for (int lx = 0; lx < stampW_; ++lx) {
            int gid = stamp_[static_cast<size_t>(ly * stampW_ + lx)];
            int x = ox + lx;
            int y = oy + ly;
            if (!buffer->containsCell(x, y)) continue;
            int old = buffer->getGid(x, y);
            if (old == gid) continue;
            buffer->setGid(x, y, gid);
            Cell ch;
            ch.x = x;
            ch.y = y;
            ch.oldGid = old;
            ch.gid = gid;
            changes_.push_back(ch);
        }
    }
    return static_cast<int>(changes_.size());
}

int Brush::previewCells(TileBuffer *buffer, const std::vector<std::pair<int, int>> &cells, int gid) {
    if (!buffer) throw Exception("Brush: null TileBuffer");
    clearPreview();
    for (const auto &c : cells) {
        if (!buffer->containsCell(c.first, c.second)) continue;
        Cell p;
        p.x = c.first;
        p.y = c.second;
        p.gid = gid;
        p.oldGid = buffer->getGid(c.first, c.second);
        preview_.push_back(p);
    }
    return static_cast<int>(preview_.size());
}

int Brush::paintAt(TileBuffer *buffer, int tx, int ty) {
    if (tool_ == "stamp") return applyStamp(buffer, tx, ty);
    if (tool_ == "erase") return eraseAt(buffer, tx, ty);
    if (tool_ == "fill") return floodFill(buffer, tx, ty);
    std::vector<std::pair<int, int>> cells;
    collectBrushCells(tx, ty, cells);
    return applyCells(buffer, cells, tile_);
}

int Brush::eraseAt(TileBuffer *buffer, int tx, int ty) {
    std::vector<std::pair<int, int>> cells;
    collectBrushCells(tx, ty, cells);
    return applyCells(buffer, cells, eraseTile_);
}

int Brush::floodFill(TileBuffer *buffer, int tx, int ty) {
    if (!buffer) throw Exception("Brush: null TileBuffer");
    clearChanges();
    if (!buffer->containsCell(tx, ty)) return 0;
    int target = buffer->getGid(tx, ty);
    if (target == tile_) return 0;
    std::vector<char> seen(static_cast<size_t>(buffer->getWidth() * buffer->getHeight()), 0);
    std::queue<std::pair<int, int>> q;
    q.push({tx, ty});
    auto idx = [&](int x, int y) { return y * buffer->getWidth() + x; };
    seen[static_cast<size_t>(idx(tx, ty))] = 1;
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        int old = buffer->getGid(x, y);
        buffer->setGid(x, y, tile_);
        Cell ch;
        ch.x = x;
        ch.y = y;
        ch.oldGid = old;
        ch.gid = tile_;
        changes_.push_back(ch);
        for (auto &d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (!buffer->containsCell(nx, ny)) continue;
            size_t i = static_cast<size_t>(idx(nx, ny));
            if (seen[i]) continue;
            if (buffer->getGid(nx, ny) != target) continue;
            seen[i] = 1;
            q.push({nx, ny});
        }
    }
    return static_cast<int>(changes_.size());
}

int Brush::paintLine(TileBuffer *buffer, int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> cells;
    collectLineCells(x0, y0, x1, y1, cells);
    int gid = (tool_ == "erase") ? eraseTile_ : tile_;
    return applyCells(buffer, cells, gid);
}

int Brush::paintRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled) {
    std::vector<std::pair<int, int>> cells;
    collectRectCells(x0, y0, x1, y1, filled, cells);
    int gid = (tool_ == "erase") ? eraseTile_ : tile_;
    return applyCells(buffer, cells, gid);
}

int Brush::previewAt(TileBuffer *buffer, int tx, int ty) {
    if (tool_ == "stamp") {
        clearPreview();
        if (stampW_ <= 0 || stampH_ <= 0 || !buffer) return 0;
        int ox = tx - stampW_ / 2;
        int oy = ty - stampH_ / 2;
        for (int ly = 0; ly < stampH_; ++ly) {
            for (int lx = 0; lx < stampW_; ++lx) {
                int x = ox + lx, y = oy + ly;
                if (!buffer->containsCell(x, y)) continue;
                Cell p;
                p.x = x;
                p.y = y;
                p.gid = stamp_[static_cast<size_t>(ly * stampW_ + lx)];
                p.oldGid = buffer->getGid(x, y);
                preview_.push_back(p);
            }
        }
        return static_cast<int>(preview_.size());
    }
    std::vector<std::pair<int, int>> cells;
    collectBrushCells(tx, ty, cells);
    int gid = (tool_ == "erase") ? eraseTile_ : tile_;
    return previewCells(buffer, cells, gid);
}

int Brush::previewLine(TileBuffer *buffer, int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> cells;
    collectLineCells(x0, y0, x1, y1, cells);
    int gid = (tool_ == "erase") ? eraseTile_ : tile_;
    return previewCells(buffer, cells, gid);
}

int Brush::previewRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled) {
    std::vector<std::pair<int, int>> cells;
    collectRectCells(x0, y0, x1, y1, filled, cells);
    int gid = (tool_ == "erase") ? eraseTile_ : tile_;
    return previewCells(buffer, cells, gid);
}

bool Brush::validChange(int index) const {
    return index >= 0 && index < static_cast<int>(changes_.size());
}
bool Brush::validPreview(int index) const {
    return index >= 0 && index < static_cast<int>(preview_.size());
}

int Brush::getPreviewX(int index) const {
    if (!validPreview(index)) throw Exception("Brush::getPreviewX: bad index");
    return preview_[index].x;
}
int Brush::getPreviewY(int index) const {
    if (!validPreview(index)) throw Exception("Brush::getPreviewY: bad index");
    return preview_[index].y;
}
int Brush::getPreviewGid(int index) const {
    if (!validPreview(index)) throw Exception("Brush::getPreviewGid: bad index");
    return preview_[index].gid;
}
int Brush::getChangeX(int index) const {
    if (!validChange(index)) throw Exception("Brush::getChangeX: bad index");
    return changes_[index].x;
}
int Brush::getChangeY(int index) const {
    if (!validChange(index)) throw Exception("Brush::getChangeY: bad index");
    return changes_[index].y;
}
int Brush::getChangeOldGid(int index) const {
    if (!validChange(index)) throw Exception("Brush::getChangeOldGid: bad index");
    return changes_[index].oldGid;
}
int Brush::getChangeNewGid(int index) const {
    if (!validChange(index)) throw Exception("Brush::getChangeNewGid: bad index");
    return changes_[index].gid;
}

}  // namespace eve::level_editing
