#include "crowd/CrowdField.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace eve::crowd {
namespace {

constexpr float kSqrt2 = 1.41421356237f;

bool finiteCost(float c) { return c < CrowdField::kUnreachable && c >= 0.f; }

}  // namespace

void CrowdField::invalidate() {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Crowd navigation revision exhausted");
    ++revision_;
}
void CrowdField::invalidateInputs() {
    if (inputRevision_ == std::numeric_limits<std::uint64_t>::max() ||
        revision_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Crowd navigation input revision exhausted");
    ++inputRevision_;
    invalidate();
}

void CrowdField::resize(int width, int height, float cellSize, float originX, float originY) {
    if (!std::isfinite(cellSize) || !std::isfinite(originX) || !std::isfinite(originY))
        throw std::invalid_argument("Crowd field geometry must be finite");
    const int   nextWidth = std::max(width, 0), nextHeight = std::max(height, 0);
    const float nextCell = std::max(cellSize, 0.f);
    const auto  count    = std::int64_t(nextWidth) * nextHeight;
    if (count > std::numeric_limits<int>::max()) throw std::length_error("Crowd field dimensions exceed index range");
    if (nextWidth == width_ && nextHeight == height_ && nextCell == cellSize_ && originX == originX_ &&
        originY == originY_)
        return;
    const bool         dimsChanged = nextWidth != width_ || nextHeight != height_;
    const auto         size        = static_cast<std::size_t>(count);
    std::vector<float> costs;
    if (dimsChanged) costs.assign(size, 1.f);
    std::vector<float> integration(size, kUnreachable), flowX(size, 0.f), flowY(size, 0.f);
    // Allocate everything before changing the authoritative geometry or revision.
    invalidateInputs();
    navigationCost_.clear();
    navigationDiagonal_   = true;
    destinationEntryCost_ = false;
    if (dimsChanged) {
        cost_.swap(costs);
        goals_.clear();
    }
    integ_.swap(integration);
    flowX_.swap(flowX);
    flowY_.swap(flowY);
    width_    = nextWidth;
    height_   = nextHeight;
    cellSize_ = nextCell;
    originX_  = originX;
    originY_  = originY;
    built_ = false;
}

void CrowdField::clear() {
    invalidateInputs();
    navigationCost_.clear();
    navigationDiagonal_   = true;
    destinationEntryCost_ = false;
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
    const float value = blocked ? 0.f : 1.f;
    if (cost_[size_t(index(cx, cy))] == value) return;
    invalidateInputs();
    navigationCost_.clear();
    navigationDiagonal_          = true;
    destinationEntryCost_        = false;
    cost_[size_t(index(cx, cy))] = blocked ? 0.f : 1.f;
}

bool CrowdField::isBlocked(int cx, int cy) const {
    if (!inBounds(cx, cy)) return true;
    return cost_[size_t(index(cx, cy))] <= 0.f;
}

void CrowdField::setCellCost(int cx, int cy, float cost) {
    if (!inBounds(cx, cy)) return;
    const float value = cost > 0.f ? cost : 0.f;
    if (cost_[size_t(index(cx, cy))] == value) return;
    invalidateInputs();
    navigationCost_.clear();
    navigationDiagonal_          = true;
    destinationEntryCost_        = false;
    cost_[size_t(index(cx, cy))] = cost > 0.f ? cost : 0.f;
}

float CrowdField::getCellCost(int cx, int cy) const {
    if (!inBounds(cx, cy)) return 0.f;
    return cost_[size_t(index(cx, cy))];
}

void CrowdField::setAllCellCost(float cost) {
    std::vector<float> costs(size_t(width_ * height_), cost > 0.f ? cost : 1.f);
    invalidateInputs();
    navigationCost_.clear();
    navigationDiagonal_   = true;
    destinationEntryCost_ = false;
    cost_.swap(costs);
}

void CrowdField::setGoal(int gx, int gy) {
    std::vector<int> replacement;
    if (inBounds(gx, gy)) replacement = {gx, gy};
    if (goals_ == replacement) return;
    invalidate();
    goals_.swap(replacement);
}

void CrowdField::addGoal(int gx, int gy) {
    if (!inBounds(gx, gy)) return;
    for (std::size_t index = 0; index + 1 < goals_.size(); index += 2)
        if (goals_[index] == gx && goals_[index + 1] == gy) return;
    goals_.reserve(goals_.size() + 2);
    invalidate();
    goals_.push_back(gx);
    goals_.push_back(gy);
}

void CrowdField::clearGoals() {
    if (!goals_.empty()) {
        invalidate();
        goals_.clear();
    }
}

void CrowdField::worldToCell(float wx, float wy, float &fx, float &fy) const {
    fx = (wx - originX_) / cellSize_ - 0.5f;
    fy = (wy - originY_) / cellSize_ - 0.5f;
}

bool CrowdField::navigationBlocked(int cx, int cy) const {
    return !inBounds(cx, cy) || navigationCost(index(cx, cy)) <= 0.f;
}
float CrowdField::navigationCost(int i) const {
    return navigationCost_.empty() ? cost_[static_cast<std::size_t>(i)] : navigationCost_[static_cast<std::size_t>(i)];
}

void CrowdField::build() {
    CrowdField staged(*this);
    staged.buildWorking();
    *this = std::move(staged);
}

void CrowdField::buildWorking() {
    beginBuildWorking();
    while (!isBuilt()) advanceBuildWorking(1000000);
}

void CrowdField::beginBuildWorking() {
    const int n = width_ * height_;
    integ_.assign(size_t(n), kUnreachable);
    flowX_.assign(size_t(n), 0.f);
    flowY_.assign(size_t(n), 0.f);
    built_      = false;
    flowCursor_ = 0;
    buildHeap_.clear();
    if (!valid()) {
        built_         = true;
        builtRevision_ = revision_;
        return;
    }
    using HeapNode = std::pair<float, int>;
    for (size_t i = 0; i + 1 < goals_.size(); i += 2) {
        const int gx = goals_[i];
        const int gy = goals_[i + 1];
        if (!navigationBlocked(gx, gy)) {
            const int gi = index(gx, gy);
            if (integ_[size_t(gi)] > 0.f) {
                integ_[size_t(gi)] = 0.f;
                buildHeap_.emplace_back(0.f, gi);
                std::push_heap(buildHeap_.begin(), buildHeap_.end(), std::greater<HeapNode>{});
            }
        }
    }
}

std::pair<int, int> CrowdField::advanceBuildWorking(int maxWork) {
    using HeapNode          = std::pair<float, int>;
    int                pops = 0, cells = 0;
    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kMoveCost[8] = {1.f, 1.f, 1.f, 1.f, kSqrt2, kSqrt2, kSqrt2, kSqrt2};

    while (!buildHeap_.empty() && pops < maxWork) {
        std::pop_heap(buildHeap_.begin(), buildHeap_.end(), std::greater<HeapNode>{});
        const HeapNode cur = buildHeap_.back();
        buildHeap_.pop_back();
        ++pops;
        const float dist = cur.first;
        if (dist > integ_[size_t(cur.second)]) continue;
        const int cx = cur.second % width_;
        const int cy = cur.second / width_;

        for (int i = 0; i < 8; ++i) {
            if (!navigationDiagonal_ && kDx[i] != 0 && kDy[i] != 0) continue;
            const int nx = cx + kDx[i];
            const int ny = cy + kDy[i];
            if (!inBounds(nx, ny) || navigationBlocked(nx, ny)) continue;
            if (kDx[i] != 0 && kDy[i] != 0) {
                // 禁止穿过对角两侧都阻挡的"墙角"。
                if (navigationBlocked(cx + kDx[i], cy) || navigationBlocked(cx, cy + kDy[i])) continue;
            }
            const int ni = index(nx, ny);
            const float tentative = dist + kMoveCost[i] * navigationCost(destinationEntryCost_ ? cur.second : ni);
            if (tentative >= integ_[size_t(ni)]) continue;
            integ_[size_t(ni)] = tentative;
            buildHeap_.emplace_back(tentative, ni);
            std::push_heap(buildHeap_.begin(), buildHeap_.end(), std::greater<HeapNode>{});
        }
    }
    if (buildHeap_.empty()) {
        const int count = width_ * height_;
        while (flowCursor_ < count && pops + cells < maxWork) {
            rebuildFlowCell(flowCursor_++);
            ++cells;
        }
        if (flowCursor_ == count) {
            built_         = true;
            builtRevision_ = revision_;
        }
    }
    return {pops, cells};
}

void CrowdField::rebuildFlowCell(int i) {
    const int   cx = i % width_, cy = i / width_;
    const float c = integ_[size_t(i)];
    if (!finiteCost(c) || c == 0.f) return;  // 不可达或目标格无方向

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

    const float len = std::hypot(gx, gy);
    if (len > 1e-6f) {
        flowX_[size_t(i)] = gx / len;
        flowY_[size_t(i)] = gy / len;
    }
}

float CrowdField::costAtCell(int cx, int cy) const {
    if (!isBuilt() || !inBounds(cx, cy)) return kUnreachable;
    return integ_[size_t(index(cx, cy))];
}

bool CrowdField::isReachable(int cx, int cy) const {
    return finiteCost(costAtCell(cx, cy));
}

void CrowdField::flowAtCell(int cx, int cy, float &dx, float &dy) const {
    dx = dy = 0.f;
    if (!isBuilt() || !inBounds(cx, cy)) return;
    const int i = index(cx, cy);
    dx = flowX_[size_t(i)];
    dy = flowY_[size_t(i)];
}

float CrowdField::costAtWorld(float wx, float wy) const {
    if (!valid() || !isBuilt() || !std::isfinite(wx) || !std::isfinite(wy)) return kUnreachable;
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
    if (!valid() || !isBuilt() || !std::isfinite(wx) || !std::isfinite(wy)) return;
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
    const int   cx0    = std::max(0, int(std::floor((wx - radius - originX_) / cs)));
    const int   cx1    = std::min(width_ - 1, int(std::floor((wx + radius - originX_) / cs)));
    const int   cy0    = std::max(0, int(std::floor((wy - radius - originY_) / cs)));
    const int   cy1    = std::min(height_ - 1, int(std::floor((wy + radius - originY_) / cs)));
    bool pushed = false;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (!isBlocked(cx, cy)) continue;
            const float loX = originX_ + float(cx) * cs, hiX = loX + cs;
            const float loY = originY_ + float(cy) * cs, hiY = loY + cs;
            const float dx = wx - std::clamp(wx, loX, hiX);
            const float dy = wy - std::clamp(wy, loY, hiY);
            const float d2 = dx * dx + dy * dy;
            if (d2 >= radius * radius) continue;
            if (d2 > 1e-12f) {
                const float distance = std::sqrt(d2);
                const float scale    = (radius - distance) / distance;
                wx += dx * scale;
                wy += dy * scale;
            } else {
                // Inside the box: choose the nearest expanded face, including radius.
                const float left = wx - loX, right = hiX - wx;
                const float down = wy - loY, up = hiY - wy;
                const float nearest = std::min({left, right, down, up});
                if (nearest == left)
                    wx = loX - radius;
                else if (nearest == right)
                    wx = hiX + radius;
                else if (nearest == down)
                    wy = loY - radius;
                else
                    wy = hiY + radius;
            }
            pushed = true;
        }
    }
    return pushed;
}

}  // namespace eve::crowd
