#include "graphics/hair/ClusterGrid.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <glm/glm.hpp>

namespace eve::graphics::hair {
namespace {

struct FrustumPlanes {
    float planes[6][4]{};

    static FrustumPlanes fromViewProjColumnMajor(const float *m16) {
        FrustumPlanes f;
        auto m = [&](int row, int col) -> float { return m16[col * 4 + row]; };
        auto set = [&](int i, float a, float b, float c, float d) {
            const float len = std::sqrt(a * a + b * b + c * c);
            if (len > 1e-8f) {
                f.planes[i][0] = a / len;
                f.planes[i][1] = b / len;
                f.planes[i][2] = c / len;
                f.planes[i][3] = d / len;
            } else {
                f.planes[i][0] = a;
                f.planes[i][1] = b;
                f.planes[i][2] = c;
                f.planes[i][3] = d;
            }
        };
        set(0, m(3, 0) + m(0, 0), m(3, 1) + m(0, 1), m(3, 2) + m(0, 2), m(3, 3) + m(0, 3));
        set(1, m(3, 0) - m(0, 0), m(3, 1) - m(0, 1), m(3, 2) - m(0, 2), m(3, 3) - m(0, 3));
        set(2, m(3, 0) + m(1, 0), m(3, 1) + m(1, 1), m(3, 2) + m(1, 2), m(3, 3) + m(1, 3));
        set(3, m(3, 0) - m(1, 0), m(3, 1) - m(1, 1), m(3, 2) - m(1, 2), m(3, 3) - m(1, 3));
        set(4, m(3, 0) + m(2, 0), m(3, 1) + m(2, 1), m(3, 2) + m(2, 2), m(3, 3) + m(2, 3));
        set(5, m(3, 0) - m(2, 0), m(3, 1) - m(2, 1), m(3, 2) - m(2, 2), m(3, 3) - m(2, 3));
        return f;
    }

    bool intersectsAABB(const glm::vec3 &bmin, const glm::vec3 &bmax) const {
        for (int i = 0; i < 6; ++i) {
            const float *p = planes[i];
            const float x = p[0] >= 0.f ? bmax.x : bmin.x;
            const float y = p[1] >= 0.f ? bmax.y : bmin.y;
            const float z = p[2] >= 0.f ? bmax.z : bmin.z;
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.f) return false;
        }
        return true;
    }
};

struct CellKey {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator==(const CellKey &o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellKeyHash {
    size_t operator()(const CellKey &k) const {
        const size_t hx = size_t(uint32_t(k.x)) * 73856093u;
        const size_t hy = size_t(uint32_t(k.y)) * 19349663u;
        const size_t hz = size_t(uint32_t(k.z)) * 83492791u;
        return hx ^ hy ^ hz;
    }
};

}  // namespace

Result<void> ClusterGrid::build(const StrandsDatas &strands, float cellSize) {
    clear();
    auto ok = strands.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());
    if (!(cellSize > 0.f) || !std::isfinite(cellSize)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ClusterGrid::build: cellSize must be positive",
            "hair.cluster.cellSize"));
    }

    std::unordered_map<CellKey, size_t, CellKeyHash> cellToCluster;
    cellToCluster.reserve(strands.curveCount());

    for (size_t ci = 0; ci < strands.curveCount(); ++ci) {
        const auto pts = strands.curvePoints(ci);
        if (pts.empty()) continue;
        const glm::vec3 &root = pts[0].position;
        CellKey key;
        key.x = int(std::floor(root.x / cellSize));
        key.y = int(std::floor(root.y / cellSize));
        key.z = int(std::floor(root.z / cellSize));

        size_t clusterIndex = 0;
        auto it = cellToCluster.find(key);
        if (it == cellToCluster.end()) {
            clusterIndex = clusters_.size();
            HairCluster cluster;
            cluster.aabbMin = glm::vec3(std::numeric_limits<float>::max());
            cluster.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
            clusters_.push_back(std::move(cluster));
            cellToCluster.emplace(key, clusterIndex);
        } else {
            clusterIndex = it->second;
        }

        HairCluster &cluster = clusters_[clusterIndex];
        cluster.curveIndices.push_back(uint32_t(ci));
        for (const StrandPoint &p : pts) {
            cluster.aabbMin = glm::min(cluster.aabbMin, p.position);
            cluster.aabbMax = glm::max(cluster.aabbMax, p.position);
        }
    }

    if (clusters_.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "ClusterGrid::build: no clusters produced", "hair.cluster"));
    }
    return Result<void>::success();
}

void ClusterGrid::clear() { clusters_.clear(); }

const HairCluster *ClusterGrid::clusterAt(size_t index) const {
    if (index >= clusters_.size()) return nullptr;
    return &clusters_[index];
}

Result<std::vector<uint32_t>> ClusterGrid::cullClusters(const float *viewProj16) const {
    if (!viewProj16) {
        return Result<std::vector<uint32_t>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ClusterGrid::cullClusters: null viewProj",
            "hair.cluster.viewProj"));
    }
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(viewProj16[i])) {
            return Result<std::vector<uint32_t>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "ClusterGrid::cullClusters: non-finite matrix",
                "hair.cluster.viewProj"));
        }
    }

    const FrustumPlanes frustum = FrustumPlanes::fromViewProjColumnMajor(viewProj16);
    std::vector<uint32_t> visible;
    visible.reserve(clusters_.size());
    for (size_t i = 0; i < clusters_.size(); ++i) {
        const HairCluster &c = clusters_[i];
        if (frustum.intersectsAABB(c.aabbMin, c.aabbMax)) {
            visible.push_back(uint32_t(i));
        }
    }
    return Result<std::vector<uint32_t>>::success(std::move(visible));
}

Result<std::vector<uint32_t>>
ClusterGrid::collectCurveIndices(const std::vector<uint32_t> &clusterIndices) const {
    std::vector<uint32_t> curves;
    for (uint32_t ci : clusterIndices) {
        if (ci >= clusters_.size()) {
            return Result<std::vector<uint32_t>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "ClusterGrid::collectCurveIndices: cluster index out of range",
                "hair.cluster.index"));
        }
        const auto &src = clusters_[ci].curveIndices;
        curves.insert(curves.end(), src.begin(), src.end());
    }
    std::sort(curves.begin(), curves.end());
    curves.erase(std::unique(curves.begin(), curves.end()), curves.end());
    return Result<std::vector<uint32_t>>::success(std::move(curves));
}

void ClusterGrid::computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
    if (clusters_.empty()) {
        outMin = outMax = glm::vec3(0.f);
        return;
    }
    outMin = clusters_[0].aabbMin;
    outMax = clusters_[0].aabbMax;
    for (size_t i = 1; i < clusters_.size(); ++i) {
        outMin = glm::min(outMin, clusters_[i].aabbMin);
        outMax = glm::max(outMax, clusters_[i].aabbMax);
    }
}

Result<StrandsDatas> filterStrandsByCurves(const StrandsDatas &src,
                                           const std::vector<uint32_t> &curveIndices) {
    auto ok = src.validate();
    if (!ok.ok()) return Result<StrandsDatas>::failure(ok.status());

    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    points.reserve(src.pointCount());
    curves.reserve(curveIndices.size());

    for (uint32_t ci : curveIndices) {
        if (ci >= src.curveCount()) {
            return Result<StrandsDatas>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "filterStrandsByCurves: curve index out of range",
                "hair.filter.curve"));
        }
        const auto pts = src.curvePoints(ci);
        if (pts.size() < 2) continue;
        StrandCurve curve;
        curve.pointOffset = uint32_t(points.size());
        curve.pointCount = uint32_t(pts.size());
        float len = 0.f;
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            points.push_back(pts[pi]);
            if (pi > 0) len += glm::length(pts[pi].position - pts[pi - 1].position);
        }
        curve.length = len;
        curves.push_back(curve);
    }

    StrandsDatas out;
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto valid = out.validate();
    if (!valid.ok()) return Result<StrandsDatas>::failure(valid.status());
    return Result<StrandsDatas>::success(std::move(out));
}

}  // namespace eve::graphics::hair
