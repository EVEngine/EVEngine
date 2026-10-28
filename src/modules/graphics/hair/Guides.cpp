#include "graphics/hair/Guides.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
#include <limits>
#include <utility>
#include "common/Diagnostic.h"
#include "graphics/hair/Simulation.h"

// Guides.cpp
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
    const size_t i0    = i1 - 1;
    const float  u0    = pts[i0].u;
    const float  u1    = pts[i1].u;
    float        alpha = 0.f;
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
        case InterpolationMode::Rigid: return 4.f;  // strongly prefer nearest guide
        case InterpolationMode::Smooth: return 1.f;
        case InterpolationMode::Offset:
        default: return 2.f;
    }
}

}  // namespace

Result<StrandsDatas> extractGuides(const StrandsDatas &strands, float guideFraction) {
    auto ok = strands.validate();
    if (!ok.ok()) return Result<StrandsDatas>::failure(ok.status());
    if (strands.curveCount() == 0) {
        return Result<StrandsDatas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "extractGuides: empty strands", "hair.guides"));
    }

    const float  frac = std::clamp(guideFraction, 0.01f, 1.f);
    const size_t keep = std::max<size_t>(1, size_t(std::ceil(double(strands.curveCount()) * double(frac))));
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
        curve.pointCount  = uint32_t(pts.size());
        float len         = 0.f;
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            points.push_back(pts[pi]);
            if (pi > 0) len += glm::length(pts[pi].position - pts[pi - 1].position);
        }
        curve.length = len;
        curves.push_back(curve);
    }

    if (curves.empty()) {
        return Result<StrandsDatas>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "extractGuides: no usable curves", "hair.guides"));
    }

    StrandsDatas out;
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto valid = out.validate();
    if (!valid.ok()) return Result<StrandsDatas>::failure(valid.status());
    return Result<StrandsDatas>::success(std::move(out));
}

Result<std::vector<StrandGuideWeights>> buildGuideWeights(const StrandsDatas &strands, const StrandsDatas &guides,
                                                          int maxInfluences, InterpolationMode mode) {
    auto sOk = strands.validate();
    if (!sOk.ok()) return Result<std::vector<StrandGuideWeights>>::failure(sOk.status());
    auto gOk = guides.validate();
    if (!gOk.ok()) return Result<std::vector<StrandGuideWeights>>::failure(gOk.status());
    if (guides.curveCount() == 0) {
        return Result<std::vector<StrandGuideWeights>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "buildGuideWeights: no guides", "hair.guides"));
    }

    const int                       k   = std::clamp(maxInfluences, 1, StrandGuideWeights::kMaxInfluences);
    const float                     exp = weightExponent(mode);
    std::vector<StrandGuideWeights> out(strands.curveCount());

    for (size_t si = 0; si < strands.curveCount(); ++si) {
        const glm::vec3 root = rootOf(strands, si);
        struct Cand {
            uint32_t index;
            float    dist2;
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
        float              wSum = 0.f;
        for (size_t i = 0; i < take; ++i) {
            const float dist           = std::sqrt(std::max(cands[i].dist2, 0.f));
            const float w              = 1.f / std::pow(std::max(dist, 1e-4f), exp);
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

Result<StrandsDatas> interpolateStrands(const StrandsDatas &strandsRest, const StrandsDatas &guidesRest,
                                        const StrandsDatas                    &guidesDeformed,
                                        const std::vector<StrandGuideWeights> &weights, InterpolationMode mode) {
    auto sOk = strandsRest.validate();
    if (!sOk.ok()) return Result<StrandsDatas>::failure(sOk.status());
    auto g0 = guidesRest.validate();
    if (!g0.ok()) return Result<StrandsDatas>::failure(g0.status());
    auto g1 = guidesDeformed.validate();
    if (!g1.ok()) return Result<StrandsDatas>::failure(g1.status());
    if (guidesRest.curveCount() != guidesDeformed.curveCount()) {
        return Result<StrandsDatas>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation,
                              "interpolateStrands: guide rest/deformed curve count mismatch", "hair.guides"));
    }
    if (weights.size() != strandsRest.curveCount()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "interpolateStrands: weights size mismatch", "hair.guides.weights"));
    }

    std::vector<StrandPoint> points(strandsRest.points().begin(), strandsRest.points().end());
    std::vector<StrandCurve> curves(strandsRest.curves().begin(), strandsRest.curves().end());

    for (size_t si = 0; si < strandsRest.curveCount(); ++si) {
        const StrandCurve        &curve = curves[si];
        const StrandGuideWeights &row   = weights[si];
        if (curve.pointCount == 0 || row.count <= 0) continue;

        if (mode == InterpolationMode::Rigid) {
            glm::vec3 delta(0.f);
            for (int i = 0; i < row.count; ++i) {
                const auto     &inf  = row.influencers[i];
                const glm::vec3 rest = rootOf(guidesRest, inf.guideIndex);
                const glm::vec3 def  = rootOf(guidesDeformed, inf.guideIndex);
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
            glm::vec3    delta(0.f);
            for (int i = 0; i < row.count; ++i) {
                const auto     &inf   = row.influencers[i];
                const glm::vec3 restP = sampleCurveAtU(guidesRest, inf.guideIndex, pt.u);
                const glm::vec3 defP  = sampleCurveAtU(guidesDeformed, inf.guideIndex, pt.u);
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

// Simulation.cpp
namespace eve::graphics::hair {
namespace {

constexpr float kEpsilon = 1e-8f;

}  // namespace

Result<void> GuideSimulator::reset(const StrandsDatas &guides, const GuideSimParams &params) {
    auto ok = guides.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());
    if (guides.curveCount() == 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GuideSimulator::reset: empty guides", "hair.sim.guides"));
    }

    params_            = params;
    params_.damping    = std::clamp(params_.damping, 0.f, 1.f);
    params_.iterations = std::clamp(params_.iterations, 1, 16);
    params_.compliance = std::max(0.f, params_.compliance);
    params_.maxDt      = std::max(1e-4f, params_.maxDt);

    restPoints_.assign(guides.points().begin(), guides.points().end());
    curves_.clear();
    curves_.reserve(guides.curveCount());
    segments_.clear();
    pos_.clear();
    prev_.clear();
    pinned_.clear();

    pos_.reserve(guides.pointCount());
    prev_.reserve(guides.pointCount());
    pinned_.reserve(guides.pointCount());

    for (size_t ci = 0; ci < guides.curveCount(); ++ci) {
        const auto pts = guides.curvePoints(ci);
        if (pts.size() < 2) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "GuideSimulator::reset: guide curve needs >= 2 points",
                                                           "hair.sim.curve"));
        }
        CurveSpan span;
        span.pointOffset = uint32_t(pos_.size());
        span.pointCount  = uint32_t(pts.size());
        float len        = 0.f;
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            const glm::vec3 p = pts[pi].position;
            pos_.push_back(p);
            prev_.push_back(p);
            pinned_.push_back(pi == 0 ? uint8_t(1) : uint8_t(0));
            if (pi > 0) {
                const float seg = glm::length(pts[pi].position - pts[pi - 1].position);
                len += seg;
                Segment s;
                s.i0         = span.pointOffset + uint32_t(pi - 1);
                s.i1         = span.pointOffset + uint32_t(pi);
                s.restLength = std::max(seg, kEpsilon);
                segments_.push_back(s);
            }
        }
        span.length = len;
        curves_.push_back(span);
    }

    return Result<void>::success();
}

void GuideSimulator::clear() {
    restPoints_.clear();
    curves_.clear();
    segments_.clear();
    pos_.clear();
    prev_.clear();
    pinned_.clear();
}

void GuideSimulator::setParams(const GuideSimParams &params) {
    params_            = params;
    params_.damping    = std::clamp(params_.damping, 0.f, 1.f);
    params_.iterations = std::clamp(params_.iterations, 1, 16);
    params_.compliance = std::max(0.f, params_.compliance);
    params_.maxDt      = std::max(1e-4f, params_.maxDt);
}

Result<void> GuideSimulator::setPinnedRoots(const float *rootPositionsXYZ, int rootCount) {
    if (!isReady()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GuideSimulator::setPinnedRoots: not ready", "hair.sim"));
    }
    if (!rootPositionsXYZ || rootCount != int(curves_.size())) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "GuideSimulator::setPinnedRoots: rootCount must equal curveCount", "hair.sim.roots"));
    }
    for (size_t ci = 0; ci < curves_.size(); ++ci) {
        const uint32_t  idx = curves_[ci].pointOffset;
        const glm::vec3 p{rootPositionsXYZ[ci * 3 + 0], rootPositionsXYZ[ci * 3 + 1], rootPositionsXYZ[ci * 3 + 2]};
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "GuideSimulator::setPinnedRoots: non-finite", "hair.sim.roots"));
        }
        const glm::vec3 delta = p - pos_[idx];
        pos_[idx]             = p;
        prev_[idx]            = p;
        for (uint32_t pi = 1; pi < curves_[ci].pointCount; ++pi) {
            pos_[idx + pi] += delta;
            prev_[idx + pi] += delta;
        }
    }
    return Result<void>::success();
}

void GuideSimulator::solveDistances(float dt) {
    const float dt2   = dt * dt;
    const float alpha = params_.compliance / std::max(dt2, kEpsilon);

    for (int it = 0; it < params_.iterations; ++it) {
        for (const Segment &seg : segments_) {
            glm::vec3  &p0   = pos_[seg.i0];
            glm::vec3  &p1   = pos_[seg.i1];
            const float w0   = pinned_[seg.i0] ? 0.f : 1.f;
            const float w1   = pinned_[seg.i1] ? 0.f : 1.f;
            const float wSum = w0 + w1;
            if (wSum < kEpsilon) continue;

            glm::vec3 delta = p1 - p0;
            float     len   = glm::length(delta);
            if (len < kEpsilon) {
                delta = glm::vec3(0.f, kEpsilon, 0.f);
                len   = kEpsilon;
            }
            const glm::vec3 n    = delta / len;
            const float     C    = len - seg.restLength;
            const float     corr = C / (wSum + alpha);
            p0 += n * (corr * w0);
            p1 -= n * (corr * w1);
        }
    }
}

void GuideSimulator::resolveCollisions() {
    const float yPlane = params_.collisionY;
    if (yPlane < -1.0e5f) return;
    for (size_t i = 0; i < pos_.size(); ++i) {
        if (pinned_[i]) continue;
        if (pos_[i].y < yPlane) {
            pos_[i].y = yPlane;
            if (prev_[i].y < yPlane) prev_[i].y = yPlane;
        }
    }
}

Result<void> GuideSimulator::step(float dt) {
    if (!isReady()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "GuideSimulator::step: not ready", "hair.sim"));
    }
    if (!std::isfinite(dt) || dt <= 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GuideSimulator::step: dt must be positive finite", "hair.sim.dt"));
    }

    const float     stepDt = std::min(dt, params_.maxDt);
    const float     damp   = std::pow(std::clamp(params_.damping, 0.f, 1.f), stepDt);
    const glm::vec3 accel  = params_.gravity + params_.wind;

    for (size_t i = 0; i < pos_.size(); ++i) {
        if (pinned_[i]) {
            prev_[i] = pos_[i];
            continue;
        }
        const glm::vec3 cur = pos_[i];
        const glm::vec3 vel = (cur - prev_[i]) * damp;
        pos_[i]             = cur + vel + accel * (stepDt * stepDt);
        prev_[i]            = cur;
    }

    solveDistances(stepDt);
    resolveCollisions();

    for (size_t i = 0; i < pos_.size(); ++i) {
        if (pinned_[i]) pos_[i] = prev_[i];
    }
    return Result<void>::success();
}

Result<StrandsDatas> GuideSimulator::snapshot() const {
    if (!isReady()) {
        return Result<StrandsDatas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "GuideSimulator::snapshot: not ready", "hair.sim"));
    }
    if (restPoints_.size() != pos_.size()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                               "GuideSimulator::snapshot: size mismatch", "hair.sim"));
    }

    std::vector<StrandPoint> points = restPoints_;
    for (size_t i = 0; i < pos_.size(); ++i) {
        points[i].position = pos_[i];
    }
    std::vector<StrandCurve> curves;
    curves.reserve(curves_.size());
    for (const CurveSpan &span : curves_) {
        StrandCurve curve;
        curve.pointOffset = span.pointOffset;
        curve.pointCount  = span.pointCount;
        float len         = 0.f;
        for (uint32_t pi = 1; pi < span.pointCount; ++pi) {
            len += glm::length(pos_[span.pointOffset + pi] - pos_[span.pointOffset + pi - 1]);
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
