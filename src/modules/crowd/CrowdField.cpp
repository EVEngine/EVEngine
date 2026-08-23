#include "crowd/CrowdField.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace eve::crowd {
namespace {

constexpr float kSqrt2 = 1.41421356237f;

bool finiteCost(float c) { return c < CrowdField::kUnreachable && c >= 0.f; }

}  // namespace

void CrowdField::resize(int width, int height, float cellSize, float originX, float originY) {
    const bool dimsChanged = width != width_ || height != height_;
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    cellSize_ = cellSize > 0.f ? cellSize : 0.f;
    originX_ = originX;
    originY_ = originY;

    const int n = width_ * height_;
    if (dimsChanged) {
        cost_.assign(size_t(n), 1.f);
        goals_.clear();
    }
    integ_.assign(size_t(n), kUnreachable);
    flowX_.assign(size_t(n), 0.f);
    flowY_.assign(size_t(n), 0.f);
    built_ = false;
}

void CrowdField::clear() {
    width_ = height_ = 0;
    cellSize_ = 0.f;
    originX_ = originY_ = 0.f;
    cost_.clear();
    integ_.clear();
    flowX_.clear();
    flowY_.clear();
    goals_.clear();
    built_ = false;
}

void CrowdField::setBlocked(int cx, int cy, bool blocked) {
    if (!inBounds(cx, cy)) return;
    cost_[size_t(index(cx, cy))] = blocked ? 0.f : 1.f;
}

bool CrowdField::isBlocked(int cx, int cy) const {
    if (!inBounds(cx, cy)) return true;
    return cost_[size_t(index(cx, cy))] <= 0.f;
}

void CrowdField::setCellCost(int cx, int cy, float cost) {
    if (!inBounds(cx, cy)) return;
    cost_[size_t(index(cx, cy))] = cost > 0.f ? cost : 0.f;
}

float CrowdField::getCellCost(int cx, int cy) const {
    if (!inBounds(cx, cy)) return 0.f;
    return cost_[size_t(index(cx, cy))];
}

void CrowdField::setAllCellCost(float cost) {
    cost_.assign(size_t(width_ * height_), cost > 0.f ? cost : 1.f);
}

void CrowdField::setGoal(int gx, int gy) {
    goals_.clear();
    addGoal(gx, gy);
}

void CrowdField::addGoal(int gx, int gy) {
    if (!inBounds(gx, gy)) return;
    goals_.push_back(gx);
    goals_.push_back(gy);
}

void CrowdField::clearGoals() { goals_.clear(); }

void CrowdField::worldToCell(float wx, float wy, float &fx, float &fy) const {
    fx = (wx - originX_) / cellSize_ - 0.5f;
    fy = (wy - originY_) / cellSize_ - 0.5f;
}

void CrowdField::build() {
    const int n = width_ * height_;
    integ_.assign(size_t(n), kUnreachable);
    flowX_.assign(size_t(n), 0.f);
    flowY_.assign(size_t(n), 0.f);
    built_ = true;

    if (!valid() || goals_.empty()) return;

    using HeapNode = std::pair<float, int>;  // (cost, idx)
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;
    for (size_t i = 0; i + 1 < goals_.size(); i += 2) {
        const int gx = goals_[i];
        const int gy = goals_[i + 1];
        if (!isBlocked(gx, gy)) {
            const int gi = index(gx, gy);
            if (integ_[size_t(gi)] > 0.f) {
                integ_[size_t(gi)] = 0.f;
                open.emplace(0.f, gi);
            }
        }
    }

    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kMoveCost[8] = {1.f, 1.f, 1.f, 1.f, kSqrt2, kSqrt2, kSqrt2, kSqrt2};

    while (!open.empty()) {
        const HeapNode cur = open.top();
        open.pop();
        const float dist = cur.first;
        if (dist > integ_[size_t(cur.second)]) continue;
        const int cx = cur.second % width_;
        const int cy = cur.second / width_;

        for (int i = 0; i < 8; ++i) {
            const int nx = cx + kDx[i];
            const int ny = cy + kDy[i];
            if (!inBounds(nx, ny) || isBlocked(nx, ny)) continue;
            if (kDx[i] != 0 && kDy[i] != 0) {
                // 禁止穿过对角两侧都阻挡的"墙角"。
                if (isBlocked(cx + kDx[i], cy) || isBlocked(cx, cy + kDy[i])) continue;
            }
            const int ni = index(nx, ny);
            const float tentative = dist + kMoveCost[i] * cost_[size_t(ni)];
            if (tentative >= integ_[size_t(ni)]) continue;
            integ_[size_t(ni)] = tentative;
            open.emplace(tentative, ni);
        }
    }
    rebuildFlow();
}

void CrowdField::rebuildFlow() {
    for (int cy = 0; cy < height_; ++cy) {
        for (int cx = 0; cx < width_; ++cx) {
            const int i = index(cx, cy);
            const float c = integ_[size_t(i)];
            if (!finiteCost(c) || c == 0.f) continue;  // 不可达或目标格无方向

            // 中心差分近似 -∇integ（指向代价下降方向）；缺失/不可达的邻格
            // 用本格值代替，形成单侧差分，保证边缘格方向正确。
            float gx = 0.f;
            if (cx > 0 && finiteCost(integ_[size_t(i - 1)]))
                gx += integ_[size_t(i - 1)];
            else
                gx += c;
            if (cx + 1 < width_ && finiteCost(integ_[size_t(i + 1)]))
                gx -= integ_[size_t(i + 1)];
            else
                gx -= c;

            float gy = 0.f;
            if (cy > 0 && finiteCost(integ_[size_t(i - width_)]))
                gy += integ_[size_t(i - width_)];
            else
                gy += c;
            if (cy + 1 < height_ && finiteCost(integ_[size_t(i + width_)]))
                gy -= integ_[size_t(i + width_)];
            else
                gy -= c;

            const float len = std::sqrt(gx * gx + gy * gy);
            if (len > 1e-6f) {
                flowX_[size_t(i)] = gx / len;
                flowY_[size_t(i)] = gy / len;
            }
        }
    }
}

float CrowdField::costAtCell(int cx, int cy) const {
    if (!inBounds(cx, cy)) return kUnreachable;
    return integ_[size_t(index(cx, cy))];
}

bool CrowdField::isReachable(int cx, int cy) const {
    return finiteCost(costAtCell(cx, cy));
}

void CrowdField::flowAtCell(int cx, int cy, float &dx, float &dy) const {
    dx = dy = 0.f;
    if (!inBounds(cx, cy)) return;
    const int i = index(cx, cy);
    dx = flowX_[size_t(i)];
    dy = flowY_[size_t(i)];
}

float CrowdField::costAtWorld(float wx, float wy) const {
    if (!valid()) return kUnreachable;
    float fx = 0.f, fy = 0.f;
    worldToCell(wx, wy, fx, fy);
    if (fx < -0.5f || fy < -0.5f || fx > float(width_) - 0.5f || fy > float(height_) - 0.5f)
        return kUnreachable;

    const int x0 = std::clamp(int(std::floor(fx)), 0, width_ - 1);
    const int y0 = std::clamp(int(std::floor(fy)), 0, height_ - 1);
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const float tx = std::clamp(fx - float(x0), 0.f, 1.f);
    const float ty = std::clamp(fy - float(y0), 0.f, 1.f);

    const float c00 = integ_[size_t(index(x0, y0))];
    const float c10 = integ_[size_t(index(x1, y0))];
    const float c01 = integ_[size_t(index(x0, y1))];
    const float c11 = integ_[size_t(index(x1, y1))];

    // 只对有限角落插值；全不可达才返回 kUnreachable，避免把单位"吓停"。
    float sum = 0.f;
    float wsum = 0.f;
    const auto acc = [&](float c, float w) {
        if (finiteCost(c)) {
            sum += c * w;
            wsum += w;
        }
    };
    acc(c00, (1.f - tx) * (1.f - ty));
    acc(c10, tx * (1.f - ty));
    acc(c01, (1.f - tx) * ty);
    acc(c11, tx * ty);
    return wsum > 0.f ? sum / wsum : kUnreachable;
}

void CrowdField::flowAtWorld(float wx, float wy, float &dx, float &dy) const {
    dx = dy = 0.f;
    if (!valid()) return;
    float fx = 0.f, fy = 0.f;
    worldToCell(wx, wy, fx, fy);
    if (fx < -0.5f || fy < -0.5f || fx > float(width_) - 0.5f || fy > float(height_) - 0.5f)
        return;

    const int x0 = std::clamp(int(std::floor(fx)), 0, width_ - 1);
    const int y0 = std::clamp(int(std::floor(fy)), 0, height_ - 1);
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const float tx = std::clamp(fx - float(x0), 0.f, 1.f);
    const float ty = std::clamp(fy - float(y0), 0.f, 1.f);

    const auto sample = [&](int x, int y, float &sx, float &sy) {
        const int i = index(x, y);
        sx = finiteCost(integ_[size_t(i)]) ? flowX_[size_t(i)] : 0.f;
        sy = finiteCost(integ_[size_t(i)]) ? flowY_[size_t(i)] : 0.f;
    };
    float f00x = 0.f, f00y = 0.f, f10x = 0.f, f10y = 0.f;
    float f01x = 0.f, f01y = 0.f, f11x = 0.f, f11y = 0.f;
    sample(x0, y0, f00x, f00y);
    sample(x1, y0, f10x, f10y);
    sample(x0, y1, f01x, f01y);
    sample(x1, y1, f11x, f11y);

    dx = (1.f - ty) * ((1.f - tx) * f00x + tx * f10x) + ty * ((1.f - tx) * f01x + tx * f11x);
    dy = (1.f - ty) * ((1.f - tx) * f00y + tx * f10y) + ty * ((1.f - tx) * f01y + tx * f11y);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-6f) {
        dx /= len;
        dy /= len;
    } else {
        dx = dy = 0.f;
    }
}

bool CrowdField::resolvePenetration(float &wx, float &wy, float radius) const {
    if (!valid() || radius <= 0.f) return false;
    const float cs = cellSize_;
    const float minX = wx - radius;
    const float maxX = wx + radius;
    const float minY = wy - radius;
    const float maxY = wy + radius;

    const auto cellAt = [this](float w) {
        return int(std::floor((w - originX_) / cellSize_));
    };
    const int cx0 = cellAt(minX);
    const int cx1 = cellAt(maxX);
    const int cy0 = cellAt(minY);
    const int cy1 = cellAt(maxY);

    bool pushed = false;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (!inBounds(cx, cy) || !isBlocked(cx, cy)) continue;
            const float bminX = originX_ + float(cx) * cs;
            const float bmaxX = bminX + cs;
            const float bminY = originY_ + float(cy) * cs;
            const float bmaxY = bminY + cs;

            const float overlapX = std::min(maxX, bmaxX) - std::max(minX, bminX);
            const float overlapY = std::min(maxY, bmaxY) - std::max(minY, bminY);
            if (overlapX <= 0.f || overlapY <= 0.f) continue;

            if (overlapX < overlapY) {
                if (wx < bminX)
                    wx = bminX - radius;
                else
                    wx = bmaxX + radius;
            } else {
                if (wy < bminY)
                    wy = bminY - radius;
                else
                    wy = bmaxY + radius;
            }
            pushed = true;
        }
    }
    return pushed;
}

}  // namespace eve::crowd
