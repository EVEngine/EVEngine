#include "graphics/hair/Guides.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

namespace eve::graphics::hair {
namespace {

glm::vec3 rootOf(const StrandsDatas &datas, size_t curveIndex) {
    const auto pts = datas.curvePoints(curveIndex);
    return pts.empty() ? glm::vec3(0.f) : pts[0].position;
}

glm::vec3 sampleCurveAtU(const StrandsDatas &datas, size_t curveIndex, float u) {
    const auto pts = datas.curvePoints(curveIndex);
    if (pts.empty()) return {};
    if (pts.size() == 1) return pts[0].position;
    const float t = std::clamp(u, 0.f, 1.f);
    // Prefer explicit per-point u; fall back to index parameterization.
    size_t i1 = 1;
    while (i1 + 1 < pts.size() && pts[i1].u < t) ++i1;
    const size_t i0 = i1 - 1;
    const float u0 = pts[i0].u;
    const float u1 = pts[i1].u;
    float alpha = 0.f;
    if (std::fabs(u1 - u0) > 1e-8f) {
        alpha = (t - u0) / (u1 - u0);
    } else {
        alpha = float(i1 - i0) > 0.f ? 1.f : 0.f;
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    return pts[i0].position * (1.f - alpha) + pts[i1].position * alpha;
}

float weightExponent(InterpolationMode mode) {
    switch (mode) {
        case InterpolationMode::Rigid:
            return 4.f;  // strongly prefer nearest guide
        case InterpolationMode::Smooth:
            return 1.f;
        case InterpolationMode::Offset:
        default:
            return 2.f;
    }
}

}  // namespace

Result<StrandsDatas> extractGuides(const StrandsDatas &strands, float guideFraction) {
    auto ok = strands.validate();
    if (!ok.ok()) return Result<StrandsDatas>::failure(ok.status());
    if (strands.curveCount() == 0) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "extractGuides: empty strands", "hair.guides"));
    }

    const float frac = std::clamp(guideFraction, 0.01f, 1.f);
    const size_t keep =
        std::max<size_t>(1, size_t(std::ceil(double(strands.curveCount()) * double(frac))));
    const size_t step = std::max<size_t>(1, strands.curveCount() / keep);

    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    points.reserve(strands.pointCount());
    curves.reserve(keep);

    for (size_t ci = 0; ci < strands.curveCount() && curves.size() < keep; ci += step) {
        const auto pts = strands.curvePoints(ci);
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

    if (curves.empty()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "extractGuides: no usable curves", "hair.guides"));
    }

    StrandsDatas out;
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto valid = out.validate();
    if (!valid.ok()) return Result<StrandsDatas>::failure(valid.status());
    return Result<StrandsDatas>::success(std::move(out));
}

Result<std::vector<StrandGuideWeights>>
buildGuideWeights(const StrandsDatas &strands, const StrandsDatas &guides, int maxInfluences,
                  InterpolationMode mode) {
    auto sOk = strands.validate();
    if (!sOk.ok()) return Result<std::vector<StrandGuideWeights>>::failure(sOk.status());
    auto gOk = guides.validate();
    if (!gOk.ok()) return Result<std::vector<StrandGuideWeights>>::failure(gOk.status());
    if (guides.curveCount() == 0) {
        return Result<std::vector<StrandGuideWeights>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "buildGuideWeights: no guides", "hair.guides"));
    }

    const int k = std::clamp(maxInfluences, 1, StrandGuideWeights::kMaxInfluences);
    const float exp = weightExponent(mode);
    std::vector<StrandGuideWeights> out(strands.curveCount());

    for (size_t si = 0; si < strands.curveCount(); ++si) {
        const glm::vec3 root = rootOf(strands, si);
        struct Cand {
            uint32_t index;
            float dist2;
        };
        std::vector<Cand> cands;
        cands.reserve(guides.curveCount());
        for (size_t gi = 0; gi < guides.curveCount(); ++gi) {
            const glm::vec3 d = rootOf(guides, gi) - root;
            cands.push_back({uint32_t(gi), glm::dot(d, d)});
        }
        const size_t take = std::min(size_t(k), cands.size());
        std::partial_sort(cands.begin(), cands.begin() + std::ptrdiff_t(take), cands.end(),
                          [](const Cand &a, const Cand &b) { return a.dist2 < b.dist2; });

        StrandGuideWeights row;
        float wSum = 0.f;
        for (size_t i = 0; i < take; ++i) {
            const float dist = std::sqrt(std::max(cands[i].dist2, 0.f));
            const float w = 1.f / std::pow(std::max(dist, 1e-4f), exp);
            row.influencers[row.count] = {cands[i].index, w};
            wSum += w;
            ++row.count;
        }
        if (wSum > 0.f) {
            for (int i = 0; i < row.count; ++i) row.influencers[i].weight /= wSum;
        } else if (row.count > 0) {
            row.influencers[0].weight = 1.f;
            for (int i = 1; i < row.count; ++i) row.influencers[i].weight = 0.f;
        }
        out[si] = row;
    }
    return Result<std::vector<StrandGuideWeights>>::success(std::move(out));
}

Result<StrandsDatas> interpolateStrands(const StrandsDatas &strandsRest,
                                        const StrandsDatas &guidesRest,
                                        const StrandsDatas &guidesDeformed,
                                        const std::vector<StrandGuideWeights> &weights,
                                        InterpolationMode mode) {
    auto sOk = strandsRest.validate();
    if (!sOk.ok()) return Result<StrandsDatas>::failure(sOk.status());
    auto g0 = guidesRest.validate();
    if (!g0.ok()) return Result<StrandsDatas>::failure(g0.status());
    auto g1 = guidesDeformed.validate();
    if (!g1.ok()) return Result<StrandsDatas>::failure(g1.status());
    if (guidesRest.curveCount() != guidesDeformed.curveCount()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation,
            "interpolateStrands: guide rest/deformed curve count mismatch", "hair.guides"));
    }
    if (weights.size() != strandsRest.curveCount()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation,
            "interpolateStrands: weights size mismatch", "hair.guides.weights"));
    }

    std::vector<StrandPoint> points(strandsRest.points().begin(), strandsRest.points().end());
    std::vector<StrandCurve> curves(strandsRest.curves().begin(), strandsRest.curves().end());

    for (size_t si = 0; si < strandsRest.curveCount(); ++si) {
        const StrandCurve &curve = curves[si];
        const StrandGuideWeights &row = weights[si];
        if (curve.pointCount == 0 || row.count <= 0) continue;

        if (mode == InterpolationMode::Rigid) {
            glm::vec3 delta(0.f);
            for (int i = 0; i < row.count; ++i) {
                const auto &inf = row.influencers[i];
                const glm::vec3 rest = rootOf(guidesRest, inf.guideIndex);
                const glm::vec3 def = rootOf(guidesDeformed, inf.guideIndex);
                delta += (def - rest) * inf.weight;
            }
            for (uint32_t pi = 0; pi < curve.pointCount; ++pi) {
                points[curve.pointOffset + pi].position += delta;
            }
            continue;
        }

        // Offset / Smooth: per-point guide deltas at matching u.
        for (uint32_t pi = 0; pi < curve.pointCount; ++pi) {
            StrandPoint &pt = points[curve.pointOffset + pi];
            glm::vec3 delta(0.f);
            for (int i = 0; i < row.count; ++i) {
                const auto &inf = row.influencers[i];
                const glm::vec3 restP = sampleCurveAtU(guidesRest, inf.guideIndex, pt.u);
                const glm::vec3 defP = sampleCurveAtU(guidesDeformed, inf.guideIndex, pt.u);
                delta += (defP - restP) * inf.weight;
            }
            pt.position += delta;
        }
    }

    StrandsDatas out;
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto valid = out.validate();
    if (!valid.ok()) return Result<StrandsDatas>::failure(valid.status());
    return Result<StrandsDatas>::success(std::move(out));
}

}  // namespace eve::graphics::hair
