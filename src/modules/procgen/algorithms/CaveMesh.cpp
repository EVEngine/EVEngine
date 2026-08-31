#include "procgen/algorithms/CaveMesh.h"

#include "procgen/algorithms/CaveAbrasion.h"
#include "procgen/algorithms/CaveBiogenicCorrosion.h"
#include "procgen/algorithms/CaveBoundary.h"
#include "procgen/algorithms/CaveBreakdown.h"
#include "procgen/algorithms/CaveCondensation.h"
#include "procgen/algorithms/CaveConstrictionScour.h"
#include "procgen/algorithms/CaveDepositAnchoring.h"
#include "procgen/algorithms/CaveDifferentialErosion.h"
#include "procgen/algorithms/CaveFacets.h"
#include "procgen/algorithms/CaveFieldSampling.h"
#include "procgen/algorithms/CaveFractureChannelization.h"
#include "procgen/algorithms/CaveFractures.h"
#include "procgen/algorithms/CaveHydrology.h"
#include "procgen/algorithms/CaveKarren.h"
#include "procgen/algorithms/CaveKnickpoint.h"
#include "procgen/algorithms/CaveLithology.h"
#include "procgen/algorithms/CaveMeshInternal.h"
#include "procgen/algorithms/CaveMicrostructure.h"
#include "procgen/algorithms/CaveMineralArmoring.h"
#include "procgen/algorithms/CaveMixingCorrosion.h"
#include "procgen/algorithms/CaveNormals.h"
#include "procgen/algorithms/CaveObstacleScour.h"
#include "procgen/algorithms/CavePlucking.h"
#include "procgen/algorithms/CavePotholes.h"
#include "procgen/algorithms/CaveReactivePatchiness.h"
#include "procgen/algorithms/CaveRoughness.h"
#include "procgen/algorithms/CaveRoughnessTransfer.h"
#include "procgen/algorithms/CaveScallopHistory.h"
#include "procgen/algorithms/CaveScallops.h"
#include "procgen/algorithms/CaveSediment.h"
#include "procgen/algorithms/CaveSupport.h"
#include "procgen/algorithms/CaveSurfaceEvolution.h"
#include "procgen/algorithms/CaveSurfaceReactivity.h"
#include "procgen/algorithms/CaveWaterTable.h"
#include "procgen/algorithms/CaveWetness.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {
namespace {

using Vec3         = CaveHydrologyVec3;
using PassagePoint = CaveHydrologyPoint;

struct Chamber {
    Vec3  center;
    Vec3  radii;
    float irregularity = 0.f;
    float phase        = 0.f;
};

struct RisingConduit {
    Vec3  start;
    Vec3  end;
    float radius = 0.05f;
};

struct Dripstone {
    Vec3  start;
    Vec3  end;
    float startRadius      = 0.05f;
    float endRadius        = 0.01f;
    float profileAmplitude = 0.f;
    float profileFrequency = 2.f;
    float profilePhase     = 0.f;
};

struct Flowstone {
    Vec3  center;
    Vec3  normal;
    Vec3  tangent;
    float halfWidth       = 0.08f;
    float halfHeight      = 0.12f;
    float thickness       = 0.02f;
    float rippleAmplitude = 0.01f;
    float rippleFrequency = 5.f;
    float phase           = 0.f;
};

struct Curtain {
    Vec3  anchor;
    Vec3  normal;
    Vec3  tangent;
    float halfWidth     = 0.08f;
    float length        = 0.18f;
    float thickness     = 0.018f;
    float waveAmplitude = 0.018f;
    float waveFrequency = 3.f;
    float phase         = 0.f;
};

struct QuantizedPoint {
    int x     = 0;
    int y     = 0;
    int z     = 0;
    int group = 0;

    bool operator==(const QuantizedPoint& other) const {
        return x == other.x && y == other.y && z == other.z && group == other.group;
    }
};

struct EdgeKey {
    int ax = 0, ay = 0, az = 0;
    int bx = 0, by = 0, bz = 0;

    bool operator==(const EdgeKey& other) const {
        return ax == other.ax && ay == other.ay && az == other.az && bx == other.bx && by == other.by && bz == other.bz;
    }
};

struct EdgeProjection {
    Vec3 projected;
    bool split = false;
};

uint32_t hash(uint32_t value);

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& key) const {
        size_t value = size_t(hash(uint32_t(key.ax)));
        value ^= size_t(hash(uint32_t(key.ay))) << 1u;
        value ^= size_t(hash(uint32_t(key.az))) << 2u;
        value ^= size_t(hash(uint32_t(key.bx))) << 3u;
        value ^= size_t(hash(uint32_t(key.by))) << 4u;
        value ^= size_t(hash(uint32_t(key.bz))) << 5u;
        return value;
    }
};

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3  sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3  add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3  mul(Vec3 a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }

EdgeKey makeEdgeKey(Vec3 a, Vec3 b) {
    auto quantize = [](Vec3 p) {
        return QuantizedPoint{int(std::lround(p.x * 1000000.f)), int(std::lround(p.y * 1000000.f)),
                              int(std::lround(p.z * 1000000.f)), 0};
    };
    QuantizedPoint qa = quantize(a), qb = quantize(b);
    const bool     swap = qa.x > qb.x || (qa.x == qb.x && qa.y > qb.y) || (qa.x == qb.x && qa.y == qb.y && qa.z > qb.z);
    if (swap) std::swap(qa, qb);
    return {qa.x, qa.y, qa.z, qb.x, qb.y, qb.z};
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float smoothMaximum(float a, float b, float blendRadius) {
    if (blendRadius <= 0.f) return std::max(a, b);
    const float h = std::clamp(0.5f + 0.5f * (a - b) / blendRadius, 0.f, 1.f);
    return b + (a - b) * h + blendRadius * h * (1.f - h);
}

float distanceToSegment(Vec3 point, Vec3 a, Vec3 b) {
    const Vec3  segment = sub(b, a);
    const float length2 = dot(segment, segment);
    const float t       = length2 > 1e-8f ? std::clamp(dot(sub(point, a), segment) / length2, 0.f, 1.f) : 0.f;
    const Vec3  delta   = sub(point, add(a, mul(segment, t)));
    return std::sqrt(dot(delta, delta));
}

float ellipsoidDistance(Vec3 point, const Chamber& shape) {
    const Vec3  q           = sub(point, shape.center);
    const float normalizedX = q.x / shape.radii.x;
    const float normalizedY = q.y / shape.radii.y;
    const float normalizedZ = q.z / shape.radii.z;
    const float base =
        (std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY + normalizedZ * normalizedZ) - 1.f) *
        std::min({shape.radii.x, shape.radii.y, shape.radii.z});
    if (shape.irregularity <= 0.f) return base;
    const float azimuth = std::atan2(normalizedZ, normalizedX);
    const float lobes   = std::sin(azimuth * 3.f + shape.phase) * 0.58f +
                          std::sin(azimuth * 2.f - normalizedY * 2.4f + shape.phase * 0.7f) * 0.42f;
    return base - lobes * shape.irregularity * std::min({shape.radii.x, shape.radii.y, shape.radii.z}) * 0.13f;
}

float taperedSegmentDistance(Vec3 point, const Dripstone& shape, float horizontalX = 1.f, float horizontalZ = 1.f) {
    const Vec3  axis    = sub(shape.end, shape.start);
    const float length2 = dot(axis, axis);
    const float t       = length2 > 1e-8f ? std::clamp(dot(sub(point, shape.start), axis) / length2, 0.f, 1.f) : 0.f;
    Vec3        delta   = sub(point, add(shape.start, mul(axis, t)));
    delta.x *= horizontalX;
    delta.z *= horizontalZ;
    float       radius   = shape.startRadius + (shape.endRadius - shape.startRadius) * t;
    const float envelope = std::sin(t * 3.1415926535f);
    radius *= 1.f + shape.profileAmplitude * envelope *
                        std::sin(t * shape.profileFrequency * 6.283185307f + shape.profilePhase);
    return std::sqrt(dot(delta, delta)) - radius;
}

float flowstoneDistance(Vec3 point, const Flowstone& shape) {
    const Vec3  q      = sub(point, shape.center);
    const float u      = dot(q, shape.tangent);
    const float v      = q.y;
    const float ripple = std::sin((v / shape.halfHeight) * shape.rippleFrequency + shape.phase) * shape.rippleAmplitude;
    const float n      = dot(q, shape.normal) - ripple;
    const float ellipsoid =
        std::sqrt((u * u) / (shape.halfWidth * shape.halfWidth) + (v * v) / (shape.halfHeight * shape.halfHeight) +
                  (n * n) / (shape.thickness * shape.thickness)) -
        1.f;
    return ellipsoid * std::min({shape.halfWidth, shape.halfHeight, shape.thickness});
}

float curtainDistance(Vec3 point, const Curtain& shape) {
    const Vec3  q        = sub(point, shape.anchor);
    const float u        = dot(q, shape.tangent);
    const float downward = -q.y;
    const float wave     = std::sin((u / shape.halfWidth) * shape.waveFrequency + shape.phase) * shape.waveAmplitude;
    const float n        = dot(q, shape.normal) - wave;
    const float side     = std::fabs(u) - shape.halfWidth;
    const float vertical = std::max(-downward, downward - shape.length);
    const float sheet    = std::fabs(n) - shape.thickness;
    return std::max({side, vertical, sheet});
}

uint32_t hash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float latticeNoise(float x, float y, float z, uint32_t seed) {
    const int   ix     = int(std::floor(x));
    const int   iy     = int(std::floor(y));
    const int   iz     = int(std::floor(z));
    const float fx     = x - float(ix);
    const float fy     = y - float(iy);
    const float fz     = z - float(iz);
    auto        smooth = [](float t) { return t * t * (3.f - 2.f * t); };
    auto        sample = [seed](int sx, int sy, int sz) {
        uint32_t h = hash(uint32_t(sx) * 73856093u ^ uint32_t(sy) * 19349663u ^ uint32_t(sz) * 83492791u ^ seed);
        return float(h & 0xffffu) / 32767.5f - 1.f;
    };
    const float ux = smooth(fx), uy = smooth(fy), uz = smooth(fz);
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00  = lerp(sample(ix, iy, iz), sample(ix + 1, iy, iz), ux);
    const float x10  = lerp(sample(ix, iy + 1, iz), sample(ix + 1, iy + 1, iz), ux);
    const float x01  = lerp(sample(ix, iy, iz + 1), sample(ix + 1, iy, iz + 1), ux);
    const float x11  = lerp(sample(ix, iy + 1, iz + 1), sample(ix + 1, iy + 1, iz + 1), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

Vec3 projectToDensitySurface(Vec3 meshPoint, const std::vector<float>& density, int nx, int ny, int nz) {
    const CaveFieldPoint point =
        projectToCaveDensitySurface({meshPoint.x, meshPoint.y, meshPoint.z}, density, nx, ny, nz);
    return {point.x, point.y, point.z};
}

void addFacetedTriangle(MeshBuild& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 referenceNormal) {
    const Vec3 ab = sub(b, a), ac = sub(c, a);
    Vec3       normal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    if (dot(normal, referenceNormal) < 0.f) {
        std::swap(b, c);
        normal = mul(normal, -1.f);
    }
    const float length = std::sqrt(dot(normal, normal));
    if (length > 1e-8f) normal = mul(normal, 1.f / length);
    const uint32_t base = uint32_t(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, a.x + 0.5f, a.y + 0.5f);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, b.x + 0.5f, b.y + 0.5f);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, c.x + 0.5f, c.y + 0.5f);
    mesh.addTriangle(base, base + 1, base + 2);
}

bool validStyle(const std::string& style) {
    return style == "cavern" || style == "tunnels" || style == "vertical" || style == "labyrinth" || style == "mixed";
}

bool validGenesis(const std::string& genesis) {
    return genesis == "epigene" || genesis == "hypogene" || genesis == "mixed";
}

bool generateCaveMesh(const Params& params, MeshBuild& out, std::string& error) {
    const std::string style                       = params.getString("style", "mixed");
    const std::string genesis                     = params.getString("genesis", "epigene");
    const int         resolution                  = params.getInt("resolution", 40);
    const int         nx                          = params.getInt("nx", resolution);
    const int         ny                          = params.getInt("ny", std::max(12, resolution * 3 / 5));
    const int         nz                          = params.getInt("nz", resolution);
    const int         chamberCount                = params.getInt("chambers", 7);
    const int         branchCount                 = params.getInt("branches", 4);
    const float       tunnelRadius                = params.getFloat("tunnelRadius", 0.16f);
    const float       chamberScale                = params.getFloat("chamberScale", 1.f);
    const float       chamberHierarchy            = params.getFloat("chamberHierarchy", 0.f);
    const float       passageVariation            = params.getFloat("passageVariation", 0.f);
    const float       chamberIrregularity         = params.getFloat("chamberIrregularity", 0.f);
    const float       roughness                   = params.getFloat("roughness", 0.12f);
    const float       multiscaleRoughness         = params.getFloat("multiscaleRoughness", 0.f);
    const float       roughnessFlowCoupling       = params.getFloat("roughnessFlowCoupling", 0.f);
    const float       erosion                     = params.getFloat("erosion", 0.55f);
    const float       bedding                     = params.getFloat("bedding", 0.6f);
    const float       fractureDissolution         = params.getFloat("fractureDissolution", 0.55f);
    const float       fractureApertureVariability = params.getFloat("fractureApertureVariability", 0.f);
    const float       fractureStressControl       = params.getFloat("fractureStressControl", 0.f);
    const float       fractureFlowFeedback        = params.getFloat("fractureFlowFeedback", 0.f);
    const float       vadoseIncision              = params.getFloat("vadoseIncision", 0.35f);
    const float       waterTableCorrosion         = params.getFloat("waterTableCorrosion", 0.f);
    const float       waterTableLevel             = params.getFloat("waterTableLevel", 0.18f);
    const int         waterTableStages            = params.getInt("waterTableStages", 1);
    const float       waterTableDrop              = params.getFloat("waterTableDrop", 0.18f);
    const float       waterTableFluctuation       = params.getFloat("waterTableFluctuation", 0.35f);
    const float       scallopErosion              = params.getFloat("scallopErosion", 0.45f);
    const float       scallopScale                = params.getFloat("scallopScale", 0.12f);
    const float       scallopHydraulicScaling     = params.getFloat("scallopHydraulicScaling", 0.f);
    const float       scallopMaturity             = params.getFloat("scallopMaturity", 0.f);
    const float       scallopScaleVariability     = params.getFloat("scallopScaleVariability", 0.f);
    const float       scallopFlowSeparation       = params.getFloat("scallopFlowSeparation", 0.f);
    const float       scallopFlowHistory          = params.getFloat("scallopFlowHistory", 0.f);
    const float       bendUndercut                = params.getFloat("bendUndercut", 0.f);
    const float       fragmentDetachment          = params.getFloat("fragmentDetachment", 0.f);
    const float       curvatureDissolution        = params.getFloat("curvatureDissolution", 0.f);
    const float       reactiveSurfaceCoupling     = params.getFloat("reactiveSurfaceCoupling", 0.f);
    const float       surfaceSlopeReactivity      = params.getFloat("surfaceSlopeReactivity", 0.f);
    const float       reactivePatchiness          = params.getFloat("reactivePatchiness", 0.f);
    const float       hydraulicErosion            = params.getFloat("hydraulicErosion", 0.f);
    const float       mixingCorrosion             = params.getFloat("mixingCorrosion", 0.f);
    const float       lithologicHeterogeneity     = params.getFloat("lithologicHeterogeneity", 0.f);
    const float       floodAbrasion               = params.getFloat("floodAbrasion", 0.f);
    const float       sedimentLoad                = params.getFloat("sedimentLoad", 0.55f);
    const float       floodPlucking               = params.getFloat("floodPlucking", 0.f);
    const float       pluckingBlockScale          = params.getFloat("pluckingBlockScale", 0.1f);
    const float       constrictionScour           = params.getFloat("constrictionScour", 0.f);
    const float       knickpointErosion           = params.getFloat("knickpointErosion", 0.f);
    const float       streamBedKarren             = params.getFloat("streamBedKarren", 0.f);
    const float       eddyPotholes                = params.getFloat("eddyPotholes", 0.f);
    const float       potholeGravelSize           = params.getFloat("potholeGravelSize", 0.5f);
    const float       breakdownScour              = params.getFloat("breakdownScour", 0.f);
    const float       hydraulicGradient           = params.getFloat("hydraulicGradient", 0.35f);
    const float       recharge                    = params.getFloat("recharge", 0.65f);
    const float       flowFocusing                = params.getFloat("flowFocusing", 0.7f);
    const float       damkohler                   = params.getFloat("damkohler", 0.002f);
    const float       transportG                  = params.getFloat("transportG", 1.f);
    const float       microstructure              = params.getFloat("microstructure", 0.f);
    const float       microporosityAccess         = params.getFloat("microporosityAccess", 0.55f);
    const float       permeabilityContrast        = params.getFloat("permeabilityContrast", 0.65f);
    const int         fractureCount               = params.getInt("fractureCount", 5);
    const int         cupolaCount                 = params.getInt("cupolas", 6);
    const int         feederCount                 = params.getInt("feeders", 4);
    const int         dripstoneCount              = params.getInt("dripstones", 12);
    const float       dripstoneScale              = params.getFloat("dripstoneScale", 0.7f);
    const std::string stalagmiteShape             = params.getString("stalagmiteShape", "mixed");
    const float       normalSmoothing             = params.getFloat("normalSmoothing", 0.78f);
    const std::string surfaceNormalMode           = params.getString("surfaceNormalMode", "faceAverage");
    const int         wetnessRefinement           = params.getInt("wetnessRefinement", 0);
    const float       boundaryClosure             = params.getFloat("boundaryClosure", 0.f);
    const float       condensationCorrosion       = params.getFloat("condensationCorrosion", 0.f);
    const float       biogenicCorrosion           = params.getFloat("biogenicCorrosion", 0.f);
    const float       mineralArmoring             = params.getFloat("mineralArmoring", 0.f);
    const float       condensationFaceting        = params.getFloat("condensationFaceting", 0.f);
    const float       differentialVeinErosion     = params.getFloat("differentialVeinErosion", 0.f);
    const float       breakdown                   = params.getFloat("breakdown", 0.f);
    const int         breakdownEvents             = params.getInt("breakdownEvents", 4);
    const float       sedimentDeposition          = params.getFloat("sedimentDeposition", 0.f);
    const float       paragenesis                 = params.getFloat("paragenesis", 0.f);
    const int         sedimentBars                = params.getInt("sedimentBars", 4);
    const int         flowstoneCount              = params.getInt("flowstones", 7);
    const int         curtainCount                = params.getInt("curtains", 5);
    const float       flowstoneScale              = params.getFloat("flowstoneScale", 0.75f);
    const int         surfaceRefinement           = params.getInt("surfaceRefinement", 0);
    const int         isosurfaceSampling          = params.getInt("isosurfaceSampling", 1);
    const float       refinementThreshold         = params.getFloat("refinementThreshold", 0.0015f);
    const float       width                       = params.getFloat("width", 30.f);
    const float       height                      = params.getFloat("height", 12.f);
    const float       depth                       = params.getFloat("depth", 24.f);
    if (!validStyle(style)) {
        error = "mesh.cave: unknown style '" + style + "' (use cavern|tunnels|vertical|labyrinth|mixed)";
        return false;
    }
    if (!validGenesis(genesis)) {
        error = "mesh.cave: unknown genesis '" + genesis + "' (use epigene|hypogene|mixed)";
        return false;
    }
    if (stalagmiteShape != "conical" && stalagmiteShape != "columnar" && stalagmiteShape != "flatTop" &&
        stalagmiteShape != "mixed") {
        error = "mesh.cave: unknown stalagmiteShape '" + stalagmiteShape + "' (use conical|columnar|flatTop|mixed)";
        return false;
    }
    if (surfaceNormalMode != "faceAverage" && surfaceNormalMode != "densityGradient") {
        error = "mesh.cave: unknown surfaceNormalMode '" + surfaceNormalMode + "' (use faceAverage|densityGradient)";
        return false;
    }
    if (nx < 8 || ny < 8 || nz < 8 || nx > 128 || ny > 128 || nz > 128) {
        error = "mesh.cave: each resolution axis must be in [8, 128]";
        return false;
    }
    if (chamberCount < 1 || chamberCount > 64 || branchCount < 0 || branchCount > 32) {
        error = "mesh.cave: chambers must be in [1, 64] and branches in [0, 32]";
        return false;
    }
    if (!(tunnelRadius >= 0.04f && tunnelRadius <= 0.4f) || chamberScale < 0.35f || chamberScale > 2.5f ||
        chamberHierarchy < 0.f || chamberHierarchy > 1.f || passageVariation < 0.f || passageVariation > 1.f ||
        chamberIrregularity < 0.f || chamberIrregularity > 1.f || roughness < 0.f || roughness > 0.45f ||
        multiscaleRoughness < 0.f || multiscaleRoughness > 1.f || roughnessFlowCoupling < 0.f ||
        roughnessFlowCoupling > 1.f || erosion < 0.f || erosion > 1.f || bedding < 0.f || bedding > 1.f ||
        fractureDissolution < 0.f || fractureDissolution > 1.f || fractureApertureVariability < 0.f ||
        fractureApertureVariability > 1.f || fractureStressControl < 0.f || fractureStressControl > 1.f ||
        fractureFlowFeedback < 0.f || fractureFlowFeedback > 1.f || vadoseIncision < 0.f || vadoseIncision > 1.f ||
        waterTableCorrosion < 0.f || waterTableCorrosion > 1.f || waterTableLevel < -0.8f || waterTableLevel > 0.8f ||
        waterTableStages < 1 || waterTableStages > 4 || waterTableDrop < 0.05f || waterTableDrop > 0.5f ||
        waterTableFluctuation < 0.f || waterTableFluctuation > 1.f || scallopErosion < 0.f || scallopErosion > 1.f ||
        scallopScale < 0.04f || scallopScale > 0.32f || scallopHydraulicScaling < 0.f ||
        scallopHydraulicScaling > 1.f || scallopMaturity < 0.f || scallopMaturity > 1.f ||
        scallopScaleVariability < 0.f || scallopScaleVariability > 1.f || scallopFlowSeparation < 0.f ||
        scallopFlowSeparation > 1.f || scallopFlowHistory < 0.f || scallopFlowHistory > 1.f || bendUndercut < 0.f ||
        bendUndercut > 1.f || fragmentDetachment < 0.f || fragmentDetachment > 1.f || curvatureDissolution < 0.f ||
        curvatureDissolution > 1.f || reactiveSurfaceCoupling < 0.f || reactiveSurfaceCoupling > 1.f ||
        surfaceSlopeReactivity < 0.f || surfaceSlopeReactivity > 1.f || reactivePatchiness < 0.f ||
        reactivePatchiness > 1.f || hydraulicErosion < 0.f || hydraulicErosion > 1.f || mixingCorrosion < 0.f ||
        mixingCorrosion > 1.f || lithologicHeterogeneity < 0.f || lithologicHeterogeneity > 1.f ||
        floodAbrasion < 0.f || floodAbrasion > 1.f || sedimentLoad < 0.f || sedimentLoad > 1.f || floodPlucking < 0.f ||
        floodPlucking > 1.f || pluckingBlockScale < 0.04f || pluckingBlockScale > 0.2f || constrictionScour < 0.f ||
        constrictionScour > 1.f || knickpointErosion < 0.f || knickpointErosion > 1.f || streamBedKarren < 0.f ||
        streamBedKarren > 1.f || eddyPotholes < 0.f || eddyPotholes > 1.f || potholeGravelSize < 0.f ||
        potholeGravelSize > 1.f || breakdownScour < 0.f || breakdownScour > 1.f || hydraulicGradient < 0.01f ||
        hydraulicGradient > 2.f || recharge < 0.f || recharge > 1.f || flowFocusing < 0.f || flowFocusing > 1.f ||
        damkohler < 0.00005f || damkohler > 0.05f || transportG < 0.1f || transportG > 5.f || cupolaCount < 0 ||
        cupolaCount > 32 || microstructure < 0.f || microstructure > 1.f || microporosityAccess < 0.f ||
        microporosityAccess > 1.f || permeabilityContrast < 0.f || permeabilityContrast > 1.f || feederCount < 0 ||
        feederCount > 24 || fractureCount < 0 || fractureCount > 24 || dripstoneCount < 0 || dripstoneCount > 64 ||
        dripstoneScale < 0.25f || dripstoneScale > 1.5f || normalSmoothing < 0.f || normalSmoothing > 1.f ||
        flowstoneCount < 0 || flowstoneCount > 32 || curtainCount < 0 || curtainCount > 32 || flowstoneScale < 0.25f ||
        flowstoneScale > 1.5f || surfaceRefinement < 0 || surfaceRefinement > 2 || isosurfaceSampling < 1 ||
        isosurfaceSampling > 2 || wetnessRefinement < 0 || wetnessRefinement > 1 || boundaryClosure < 0.f ||
        boundaryClosure > 1.f || condensationCorrosion < 0.f || condensationCorrosion > 1.f ||
        biogenicCorrosion < 0.f || biogenicCorrosion > 1.f || mineralArmoring < 0.f || mineralArmoring > 1.f ||
        condensationFaceting < 0.f || condensationFaceting > 1.f || differentialVeinErosion < 0.f ||
        differentialVeinErosion > 1.f || breakdown < 0.f || breakdown > 1.f || breakdownEvents < 0 ||
        breakdownEvents > 16 || refinementThreshold < 0.0001f || refinementThreshold > 0.02f ||
        sedimentDeposition < 0.f || sedimentDeposition > 1.f || paragenesis < 0.f || paragenesis > 1.f ||
        sedimentBars < 0 || sedimentBars > 16 || width <= 0.f || height <= 0.f || depth <= 0.f) {
        error = "mesh.cave: invalid radius, chamber scale, roughness, or world dimensions";
        return false;
    }

    std::mt19937                          rng(params.getSeed());
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    std::uniform_real_distribution<float> positive(0.f, 1.f);
    std::vector<PassagePoint>             spine;
    const int                             spinePoints = std::max(8, chamberCount * 2);
    spine.reserve(size_t(spinePoints));
    for (int i = 0; i < spinePoints; ++i) {
        const float t = float(i) / float(spinePoints - 1);
        float       y = unit(rng) * 0.16f;
        float       z = std::sin(t * 9.f + unit(rng)) * 0.22f + unit(rng) * 0.1f;
        if (style == "vertical") y = (t - 0.5f) * 1.25f + unit(rng) * 0.12f;
        if (style == "labyrinth") z += std::sin(t * 22.f) * 0.24f;
        const float coherentRadius = 1.f + passageVariation * (std::sin(t * 4.f * 3.1415926535f + 0.7f) * 0.24f +
                                                               std::sin(t * 9.f * 3.1415926535f + 1.9f) * 0.10f);
        const float randomRadius   = 0.78f + positive(rng) * 0.55f;
        spine.push_back({{-0.72f + t * 1.44f, std::clamp(y, -0.72f, 0.72f), std::clamp(z, -0.68f, 0.68f)},
                         tunnelRadius * randomRadius * std::clamp(coherentRadius, 0.68f, 1.38f)});
    }

    std::vector<CaveHydrologyBranch> branches;
    branches.reserve(size_t(branchCount));
    for (int branch = 0; branch < branchCount; ++branch) {
        const int                 anchor = 1 + int(rng() % uint32_t(std::max(1, spinePoints - 2)));
        const int                 points = 3 + int(rng() % 4u);
        std::vector<PassagePoint> path;
        path.reserve(size_t(points + 1));
        path.push_back(spine[size_t(anchor)]);
        Vec3 cursor = path.front().position;
        Vec3 direction{unit(rng) * 0.4f, unit(rng) * (style == "vertical" ? 0.5f : 0.22f),
                       (unit(rng) < 0.f ? -1.f : 1.f) * (0.15f + positive(rng) * 0.2f)};
        for (int step = 0; step < points; ++step) {
            direction.x = std::clamp(direction.x + unit(rng) * 0.13f, -0.35f, 0.35f);
            direction.y = std::clamp(direction.y + unit(rng) * 0.1f, -0.32f, 0.32f);
            direction.z = std::clamp(direction.z + unit(rng) * 0.13f, -0.4f, 0.4f);
            cursor      = add(cursor, direction);
            cursor.x    = std::clamp(cursor.x, -0.76f, 0.76f);
            cursor.y    = std::clamp(cursor.y, -0.72f, 0.72f);
            cursor.z    = std::clamp(cursor.z, -0.74f, 0.74f);
            path.push_back({cursor, tunnelRadius * (0.58f + positive(rng) * 0.45f)});
        }
        branches.push_back({anchor, std::move(path)});
    }

    const CaveHydrologyWeights hydrology = buildCaveHydrology(spine, branches, hydraulicErosion, hydraulicGradient,
                                                              recharge, flowFocusing, damkohler, transportG);
    const std::vector<CaveMixingSite>            mixingSites = createCaveMixingSites(spine, branches, params.getSeed());
    const std::vector<CaveConstrictionScourSite> constrictionScourSites =
        createCaveConstrictionScourSites(spine, branches, hydrology);
    const std::vector<CaveKnickpointSite> knickpointSites =
        createCaveKnickpointSites(spine, hydrology.trunk, sedimentLoad);

    std::vector<Chamber> chambers;
    chambers.reserve(size_t(chamberCount));
    const int primaryChamber               = chamberCount / 2;
    float     primaryChamberVerticalRadius = 0.f;
    for (int i = 0; i < chamberCount; ++i) {
        const float evenT  = float(i) / float(std::max(1, chamberCount - 1));
        const float jitter = chamberHierarchy > 0.f && i > 0 && i + 1 < chamberCount
                                 ? unit(rng) * chamberHierarchy * 0.32f / float(std::max(1, chamberCount - 1))
                                 : 0.f;
        const float anchoredT =
            i == primaryChamber && chamberHierarchy > 0.f ? 0.52f : std::clamp(evenT + jitter, 0.f, 1.f);
        const PassagePoint& anchor     = spine[size_t(std::lround(anchoredT * float(spinePoints - 1)))];
        const float         styleScale = style == "cavern" ? 1.35f : (style == "tunnels" ? 0.72f : 1.f);
        const bool          primary    = i == primaryChamber;
        const float         hierarchyScale =
            chamberHierarchy <= 0.f
                ? 1.f
                : (primary ? 1.f + 0.95f * chamberHierarchy : 1.f - chamberHierarchy * (0.12f + positive(rng) * 0.20f));
        const float radius = tunnelRadius * chamberScale * styleScale * (1.5f + positive(rng) * 1.25f) * hierarchyScale;
        const float verticalScale = primary ? 1.f + chamberHierarchy * 0.58f : 1.f;
        chambers.push_back({add(anchor.position, {unit(rng) * 0.08f, unit(rng) * 0.08f, unit(rng) * 0.08f}),
                            {std::min(0.82f, radius * (1.1f + positive(rng) * 0.8f)),
                             std::min(0.84f, radius * (0.75f + positive(rng) * 0.65f) * verticalScale),
                             std::min(0.82f, radius * (1.0f + positive(rng) * 0.9f))},
                            chamberIrregularity * (primary ? 1.f : 0.65f),
                            positive(rng) * 6.283185307f});
        if (primary) primaryChamberVerticalRadius = chambers.back().radii.y;
    }
    std::vector<CaveBreakdownChamber> breakdownChambers;
    breakdownChambers.reserve(chambers.size());
    for (const Chamber& chamber : chambers)
        breakdownChambers.push_back(
            {chamber.center.x, chamber.center.y, chamber.center.z, chamber.radii.x, chamber.radii.y, chamber.radii.z});
    const CaveBreakdownSet breakdownSet =
        createCaveBreakdown(breakdownChambers, breakdownEvents, breakdown, params.getSeed());
    const std::vector<CaveObstacleScourSite> obstacleScourSites =
        createCaveObstacleScourSites(breakdownSet, spine, hydrology.trunk, sedimentLoad, multiscaleRoughness);
    std::vector<CaveSedimentPathPoint> sedimentPath;
    sedimentPath.reserve(spine.size());
    for (const PassagePoint& point : spine)
        sedimentPath.push_back({point.position.x, point.position.y, point.position.z, point.radius});
    const CaveSedimentSet sedimentSet =
        createCaveSediment(sedimentPath, sedimentBars, sedimentDeposition, paragenesis, params.getSeed());

    // Natural carbonate caves preferentially enlarge pre-existing joints.  A sparse set of
    // deterministic vertical fracture planes approximates that structural control without
    // requiring a full reactive-transport solve at recipe-build time.
    std::vector<CaveFracture> fractures;
    fractures.reserve(size_t(fractureCount));
    for (int i = 0; i < fractureCount; ++i) {
        const float angle = positive(rng) * 3.1415926535f;
        fractures.push_back({std::cos(angle), std::sin(angle), unit(rng) * 0.58f, 0.012f + positive(rng) * 0.025f});
    }
    const std::vector<CavePotholeSite> potholeSites =
        createCavePotholeSites(spine, hydrology.trunk, fractures, fractureApertureVariability, fractureStressControl,
                               sedimentLoad, potholeGravelSize, params.getSeed());

    std::vector<Chamber>       cupolas;
    std::vector<RisingConduit> feeders;
    std::vector<RisingConduit> ceilingChannels;
    if (genesis != "epigene") {
        cupolas.reserve(size_t(cupolaCount));
        feeders.reserve(size_t(feederCount));
        ceilingChannels.reserve(size_t(feederCount));
        for (int i = 0; i < cupolaCount; ++i) {
            const Chamber& host = chambers[size_t(rng() % uint32_t(chambers.size()))];
            const float    rx   = 0.045f + positive(rng) * 0.065f;
            const float    rz   = 0.045f + positive(rng) * 0.065f;
            const float    ry   = 0.08f + positive(rng) * 0.15f;
            cupolas.push_back(
                {{host.center.x + unit(rng) * host.radii.x * 0.45f, host.center.y + host.radii.y * 0.82f + ry * 0.45f,
                  host.center.z + unit(rng) * host.radii.z * 0.45f},
                 {rx, ry, rz}});
        }
        for (int i = 0; i < feederCount; ++i) {
            const Chamber& host = chambers[size_t(rng() % uint32_t(chambers.size()))];
            const Vec3     top{host.center.x + unit(rng) * host.radii.x * 0.32f, host.center.y - host.radii.y * 0.35f,
                               host.center.z + unit(rng) * host.radii.z * 0.32f};
            const Vec3     bottom{top.x + unit(rng) * 0.06f, std::max(-0.96f, top.y - (0.20f + positive(rng) * 0.35f)),
                                  top.z + unit(rng) * 0.06f};
            const float    radius = 0.035f + positive(rng) * 0.035f;
            feeders.push_back({bottom, top, radius});
            if (!cupolas.empty()) {
                const Chamber& outlet = cupolas[size_t(i) % cupolas.size()];
                const Vec3     ceilingStart{top.x, host.center.y + host.radii.y * 0.72f, top.z};
                ceilingChannels.push_back({ceilingStart, outlet.center, radius * 0.62f});
            }
        }
    }

    // Each drip site creates a slender ceiling deposit and a broader floor deposit.  The
    // latter samples the conical, columnar and flat-topped steady forms described by the
    // reaction/advection (Damkohler) family, rather than mirroring the stalactite.
    std::vector<Dripstone> dripstones;
    dripstones.reserve(size_t(dripstoneCount) * 2u);
    int columnCount = 0;
    for (int i = 0; i < dripstoneCount; ++i) {
        const Chamber& chamber    = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    dripAngle  = positive(rng) * 2.f * 3.1415926535f;
        const float    dripRadius = 0.34f + positive(rng) * 0.30f;
        const float    dx         = std::cos(dripAngle) * chamber.radii.x * dripRadius;
        const float    dz         = std::sin(dripAngle) * chamber.radii.z * dripRadius;
        const float    radial     = std::min(
            0.86f, (dx * dx) / (chamber.radii.x * chamber.radii.x) + (dz * dz) / (chamber.radii.z * chamber.radii.z));
        const float yExtent = chamber.radii.y * std::sqrt(1.f - radial);
        const Vec3  ceiling{chamber.center.x + dx, chamber.center.y + yExtent, chamber.center.z + dz};
        const Vec3  floor{ceiling.x, chamber.center.y - yExtent, ceiling.z};
        const float gap           = std::max(0.08f, ceiling.y - floor.y);
        const bool  column        = positive(rng) < 0.12f * dripstoneScale;
        const float ceilingLength = gap * (column ? 0.54f : (0.18f + positive(rng) * 0.28f)) * dripstoneScale;
        const float floorLength   = gap * (column ? 0.54f : (0.14f + positive(rng) * 0.23f)) * dripstoneScale;
        const float ceilingRadius = std::clamp(ceilingLength * (0.14f + positive(rng) * 0.08f), 0.025f, 0.075f);
        dripstones.push_back({add(ceiling, {0.f, 0.018f, 0.f}), add(ceiling, {0.f, -ceilingLength, 0.f}), ceilingRadius,
                              column ? ceilingRadius * 0.72f : ceilingRadius * 0.13f});

        std::string shape = stalagmiteShape;
        if (shape == "mixed") {
            const float selector = positive(rng);
            shape                = selector < 0.42f ? "conical" : (selector < 0.78f ? "columnar" : "flatTop");
        }
        const float floorRadius = std::clamp(floorLength * (shape == "conical"    ? 0.38f
                                                            : shape == "columnar" ? 0.28f
                                                                                  : 0.45f),
                                             0.035f, 0.12f);
        const float tipRatio    = column ? 0.72f : (shape == "conical" ? 0.12f : shape == "columnar" ? 0.68f : 0.88f);
        dripstones.push_back({add(floor, {0.f, -0.018f, 0.f}), add(floor, {0.f, floorLength, 0.f}), floorRadius,
                              floorRadius * tipRatio});
        if (column) ++columnCount;
    }

    std::vector<Flowstone> flowstones;
    flowstones.reserve(size_t(flowstoneCount));
    for (int i = 0; i < flowstoneCount; ++i) {
        const Chamber& chamber = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    angle   = positive(rng) * 2.f * 3.1415926535f;
        const Vec3     normal{std::cos(angle), 0.f, std::sin(angle)};
        const Vec3     tangent{-normal.z, 0.f, normal.x};
        // Keep the deposit centred on the parent chamber wall.  Moving it farther
        // inward creates a detached calcite lens after voxelisation.
        const Vec3 wall{chamber.center.x + normal.x * chamber.radii.x * 0.98f,
                        chamber.center.y + unit(rng) * chamber.radii.y * 0.22f,
                        chamber.center.z + normal.z * chamber.radii.z * 0.98f};
        flowstones.push_back({wall, normal, tangent, (0.055f + positive(rng) * 0.065f) * flowstoneScale,
                              (0.09f + positive(rng) * 0.13f) * flowstoneScale,
                              (0.026f + positive(rng) * 0.018f) * flowstoneScale,
                              (0.005f + positive(rng) * 0.009f) * flowstoneScale, 4.f + positive(rng) * 4.f,
                              positive(rng) * 2.f * 3.1415926535f});
    }

    std::vector<Curtain> curtains;
    curtains.reserve(size_t(curtainCount));
    for (int i = 0; i < curtainCount; ++i) {
        const Chamber& chamber = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    angle   = positive(rng) * 2.f * 3.1415926535f;
        const float    radial  = 0.38f + positive(rng) * 0.32f;
        const float    dx      = std::cos(angle) * chamber.radii.x * radial;
        const float    dz      = std::sin(angle) * chamber.radii.z * radial;
        const float    radial2 = std::min(
            0.9f, (dx * dx) / (chamber.radii.x * chamber.radii.x) + (dz * dz) / (chamber.radii.z * chamber.radii.z));
        const Vec3 anchor{chamber.center.x + dx, chamber.center.y + chamber.radii.y * std::sqrt(1.f - radial2) + 0.018f,
                          chamber.center.z + dz};
        const Vec3 normal{std::cos(angle), 0.f, std::sin(angle)};
        const Vec3 tangent{-normal.z, 0.f, normal.x};
        curtains.push_back({anchor, normal, tangent, (0.045f + positive(rng) * 0.07f) * flowstoneScale,
                            (0.10f + positive(rng) * 0.18f) * flowstoneScale,
                            (0.024f + positive(rng) * 0.016f) * flowstoneScale,
                            (0.008f + positive(rng) * 0.014f) * flowstoneScale, 2.5f + positive(rng) * 2.5f,
                            positive(rng) * 2.f * 3.1415926535f});
    }

    std::vector<float>             density(size_t(nx) * size_t(ny) * size_t(nz), 1.f);
    std::vector<float>             reactiveRate(density.size(), 1.f);
    std::vector<float>             hydraulicExposure(density.size(), 1.f);
    std::vector<CaveHydrologyVec3> flowDirection(density.size(), {1.f, 0.f, 0.f});
    int                            biogenicAffectedVoxels                   = 0;
    float                          minimumScallopRetention                  = 1.f;
    float                          maximumBiogenicErosion                   = 0.f;
    float                          totalBiogenicErosion                     = 0.f;
    int                            facetAffectedVoxels                      = 0;
    float                          maximumFacetRetreat                      = 0.f;
    int                            differentialVeinAffectedVoxels           = 0;
    float                          maximumDifferentialVeinRetreat           = 0.f;
    float                          maximumVeinProtection                    = 0.f;
    float                          minimumFractureAperture                  = 1.f;
    float                          maximumFractureAperture                  = 1.f;
    float                          minimumFractureBranchOpenness            = 1.f;
    int                            fractureChannelAffectedVoxels            = 0;
    float                          maximumFractureChannelRetreat            = 0.f;
    float                          maximumFractureFlowConcentration         = 0.f;
    float                          maximumFractureIntersectionAmplification = 0.f;
    float                          minimumFractureReactantAccess            = 1.4f;
    float                          minimumWallRelief                        = 1.f;
    float                          maximumWallRelief                        = -1.f;
    int                            roughnessTransferAffectedVoxels          = 0;
    float                          minimumRoughnessTransfer                 = 1.f;
    float                          maximumRoughnessTransfer                 = 1.f;
    float                          maximumRidgeExposure                     = 0.f;
    float                          maximumRecessShelter                     = 0.f;
    int                            waterTableAffectedVoxels                 = 0;
    float                          maximumWaterTableRetreat                 = 0.f;
    int                            mixingCorrosionAffectedVoxels            = 0;
    float                          maximumMixingCorrosionRetreat            = 0.f;
    int                            lithologyAffectedVoxels                  = 0;
    int                            styloliteAffectedVoxels                  = 0;
    float                          minimumBedResistance                     = 1.f;
    float                          maximumLithologyRetreat                  = 0.f;
    int                            abrasionAffectedVoxels                   = 0;
    float                          maximumAbrasionRetreat                   = 0.f;
    float                          maximumAbrasionVortex                    = 0.f;
    int                            pluckingAffectedVoxels                   = 0;
    float                          maximumPluckingRetreat                   = 0.f;
    float                          maximumPluckingPredisposition            = 0.f;
    int                            constrictionScourAffectedVoxels          = 0;
    float                          maximumConstrictionScourRetreat          = 0.f;
    float                          maximumConstrictionRatio                 = 0.f;
    float                          maximumPlungingEfficiency                = 0.f;
    int                            knickpointAffectedVoxels                 = 0;
    float                          maximumKnickpointRetreat                 = 0.f;
    float                          maximumKnickpointSlopeBreak              = 0.f;
    float                          maximumKnickpointDrop                    = 0.f;
    int                            streamBedKarrenAffectedVoxels            = 0;
    float                          maximumStreamBedKarrenRetreat            = 0.f;
    float                          maximumKarrenFractureGuidance            = 0.f;
    float                          maximumKarrenIntersectionPocket          = 0.f;
    int                            potholeAffectedVoxels                    = 0;
    float                          maximumPotholeRetreat                    = 0.f;
    float                          maximumPotholeSecondaryErosion           = 0.f;
    float                          maximumPotholeFractureIntersection       = 0.f;
    int                            obstacleScourAffectedVoxels              = 0;
    float                          maximumObstacleScourRetreat              = 0.f;
    float                          maximumHorseshoeScour                    = 0.f;
    float                          maximumWakeScour                         = 0.f;
    float                          minimumObstacleRoughnessRetention        = 1.f;
    int                            mineralArmoringAffectedVoxels            = 0;
    float                          maximumMineralCoatingCoverage            = 0.f;
    float                          maximumMineralHydraulicRetention         = 0.f;
    float                          minimumArmoredDissolutionRetention       = 1.f;
    int                            scallopHistoryAffectedVoxels             = 0;
    float                          maximumYoungerScallopErosion             = 0.f;
    float                          maximumYoungerScallopCoverage            = 0.f;
    float                          maximumScallopReversalMask               = 0.f;
    float                          minimumSecondaryScallopScaleRatio        = 1.f;
    auto                           carvePath = [](Vec3 p, const std::vector<PassagePoint>& path, float current) {
        for (size_t i = 1; i < path.size(); ++i) {
            const float radius = 0.5f * (path[i - 1].radius + path[i].radius);
            current = std::min(current, distanceToSegment(p, path[i - 1].position, path[i].position) - radius);
        }
        return current;
    };
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const Vec3 p{float(x) / float(nx - 1) * 2.f - 1.f, float(y) / float(ny - 1) * 2.f - 1.f,
                             float(z) / float(nz - 1) * 2.f - 1.f};
                float      d = carvePath(p, spine, 1.f);
                for (const auto& branch : branches) d = carvePath(p, branch.points, d);
                CavePassageFrame passage = nearestCavePassageFrame(p, spine, hydrology.trunk);
                for (size_t branch = 0; branch < branches.size(); ++branch) {
                    const CavePassageFrame candidate =
                        nearestCavePassageFrame(p, branches[branch].points, hydrology.branches[branch]);
                    if (candidate.distance < passage.distance) passage = candidate;
                }

                // After a phreatic tube drains, gravity-driven streams incise a narrower
                // canyon into its floor.  This second connected carve preserves traversability.
                const float effectiveVadose = genesis == "hypogene" ? 0.f : vadoseIncision;
                if (effectiveVadose > 0.f) {
                    const Vec3 incisedPoint{p.x, p.y + tunnelRadius * (0.55f + 0.55f * vadoseIncision), p.z};
                    float      incision = 1.f;
                    for (size_t i = 1; i < spine.size(); ++i) {
                        const float radius = 0.5f * (spine[i - 1].radius + spine[i].radius) *
                                             (0.38f + 0.22f * vadoseIncision) * std::sqrt(hydrology.trunk[i - 1]);
                        incision           = std::min(
                            incision,
                            distanceToSegment(incisedPoint, spine[i - 1].position, spine[i].position) - radius);
                    }
                    for (size_t branch = 0; branch < branches.size(); ++branch) {
                        const auto& path = branches[branch].points;
                        for (size_t i = 1; i < path.size(); ++i) {
                            const float radius = 0.5f * (path[i - 1].radius + path[i].radius) *
                                                 (0.30f + 0.18f * vadoseIncision) *
                                                 std::sqrt(hydrology.branches[branch][i - 1]);
                            incision           = std::min(
                                incision,
                                distanceToSegment(incisedPoint, path[i - 1].position, path[i].position) - radius);
                        }
                    }
                    d = std::min(d, incision);
                }
                for (const Chamber& chamber : chambers) {
                    d = std::min(d, ellipsoidDistance(p, chamber));
                }
                for (const Chamber& cupola : cupolas) d = std::min(d, ellipsoidDistance(p, cupola));
                for (const RisingConduit& feeder : feeders)
                    d = std::min(d, distanceToSegment(p, feeder.start, feeder.end) - feeder.radius);
                for (const RisingConduit& channel : ceilingChannels)
                    d = std::min(d, distanceToSegment(p, channel.start, channel.end) - channel.radius);
                const float legacyStrata =
                    latticeNoise((p.x + 2.f) * 5.5f, (p.y + 3.f) * 3.2f, (p.z + 5.f) * 5.5f, params.getSeed());
                float strata = legacyStrata;
                if (multiscaleRoughness > 0.f) {
                    const CaveRoughnessSample spectrum = sampleCaveWallRoughness({p.x, p.y, p.z, params.getSeed()});
                    strata = legacyStrata * (1.f - multiscaleRoughness) + spectrum.relief * multiscaleRoughness;
                }
                minimumWallRelief = std::min(minimumWallRelief, strata);
                maximumWallRelief = std::max(maximumWallRelief, strata);
                d += strata * roughness * 0.32f;
                d = carveCaveBreakdownScars(p.x, p.y, p.z, d, breakdownSet);
                d = carveCaveParagenesis(p.x, p.y, p.z, d, sedimentSet);

                // Apply dissolution only in a shell around the connected void.  Bedding bands
                // produce horizontal notches, joints produce tall slots, and positive fine
                // noise creates solution pockets/scallops instead of uniformly inflating rock.
                const float shell = 1.f - smoothstep(0.025f, 0.22f, std::fabs(d));
                const float beddingWarp =
                    latticeNoise(p.x * 2.3f, p.y * 1.1f, p.z * 2.3f, params.getSeed() ^ 0x51a7u) * 0.12f;
                const float beddingWave               = std::fabs(std::sin((p.y + beddingWarp) * 8.f * 3.1415926535f));
                const float beddingMask               = 1.f - smoothstep(0.05f, 0.42f, beddingWave);
                float       fractureMask              = 0.f;
                float       secondaryFractureMask     = 0.f;
                float       fractureAperture          = 1.f;
                float       secondaryFractureAperture = 1.f;
                for (const CaveFracture& fracture : fractures) {
                    const CaveFractureSample sample =
                        sampleCaveFracture({p.x, p.y, p.z, fracture, fractureApertureVariability, fractureStressControl,
                                            params.getSeed()});
                    if (sample.mask > fractureMask) {
                        secondaryFractureMask     = fractureMask;
                        secondaryFractureAperture = fractureAperture;
                        fractureMask              = sample.mask;
                        fractureAperture          = sample.apertureMultiplier;
                    } else {
                        if (sample.mask > secondaryFractureMask) {
                            secondaryFractureMask     = sample.mask;
                            secondaryFractureAperture = sample.apertureMultiplier;
                        }
                    }
                    minimumFractureAperture       = std::min(minimumFractureAperture, sample.apertureMultiplier);
                    maximumFractureAperture       = std::max(maximumFractureAperture, sample.apertureMultiplier);
                    minimumFractureBranchOpenness = std::min(minimumFractureBranchOpenness, sample.branchOpenness);
                }
                const float pockets =
                    std::max(0.f, latticeNoise(p.x * 15.f, p.y * 11.f, p.z * 15.f, params.getSeed() ^ 0xb5297a4du));
                // Fowler's boundary-layer model motivates travelling, cusp-edged scallops.
                // Local flow and passage curvature add hydraulic scale and outer-bank bias.
                const CaveScallopHistorySample scallops = sampleCaveScallopHistory(
                    {{passage.along, passage.angle, passage.distance, passage.radius, passage.hydraulicIntensity,
                      scallopScale, scallopHydraulicScaling, scallopMaturity, bendUndercut, passage.bendStrength,
                      passage.outerBankAngle, scallopScaleVariability, scallopFlowSeparation, params.getSeed()},
                     scallopFlowHistory});
                if (scallops.youngerErosion > 1e-6f) {
                    ++scallopHistoryAffectedVoxels;
                    maximumYoungerScallopErosion  = std::max(maximumYoungerScallopErosion, scallops.youngerErosion);
                    maximumYoungerScallopCoverage = std::max(maximumYoungerScallopCoverage, scallops.youngerCoverage);
                    maximumScallopReversalMask    = std::max(maximumScallopReversalMask, scallops.reversalMask);
                    minimumSecondaryScallopScaleRatio =
                        std::min(minimumSecondaryScallopScaleRatio, scallops.secondaryScaleRatio);
                }
                const CaveBiogenicCorrosionSample biogenic =
                    sampleCaveBiogenicCorrosion({passage.along, passage.angle, passage.distance, passage.radius,
                                                 passage.hydraulicIntensity, biogenicCorrosion, params.getSeed()});
                const CaveFacetSample facets =
                    sampleCaveCondensationFacets({passage.along, passage.angle, passage.distance, passage.radius,
                                                  condensationFaceting, params.getSeed()});
                const CaveDifferentialErosionSample differentialVeins = sampleCaveDifferentialVeinErosion(
                    {p.x, p.y, p.z, passage.distance, passage.radius, differentialVeinErosion, params.getSeed()});
                const float mineralSupply = genesis == "hypogene" ? 1.f : (genesis == "mixed" ? 0.72f : 0.28f);
                CaveMineralArmoringSample armoring;
                const bool                chemicalArmoringPotential =
                    erosion > 0.f || biogenicCorrosion > 0.f || waterTableCorrosion > 0.f || mixingCorrosion > 0.f ||
                    lithologicHeterogeneity > 0.f || condensationFaceting > 0.f || differentialVeinErosion > 0.f;
                if (mineralArmoring > 0.f && chemicalArmoringPotential) {
                    armoring = sampleCaveMineralArmoring(
                        {passage.along, passage.angle, passage.hydraulicIntensity, mineralSupply, params.getSeed()});
                }
                const float chemicalRetention = 1.f - mineralArmoring * (1.f - armoring.dissolutionRetention);
                CaveRoughnessTransferSample roughnessTransfer;
                if (roughnessFlowCoupling > 0.f && multiscaleRoughness > 0.f && roughness > 0.f &&
                    chemicalArmoringPotential) {
                    roughnessTransfer =
                        sampleCaveRoughnessTransfer({strata, passage.hydraulicIntensity, roughnessFlowCoupling});
                }
                const float chemicalMassTransfer = roughnessTransfer.massTransferMultiplier;
                if (shell > 1e-4f && std::fabs(chemicalMassTransfer - 1.f) > 1e-6f) {
                    ++roughnessTransferAffectedVoxels;
                    minimumRoughnessTransfer = std::min(minimumRoughnessTransfer, chemicalMassTransfer);
                    maximumRoughnessTransfer = std::max(maximumRoughnessTransfer, chemicalMassTransfer);
                    maximumRidgeExposure     = std::max(maximumRidgeExposure, roughnessTransfer.ridgeExposure);
                    maximumRecessShelter     = std::max(maximumRecessShelter, roughnessTransfer.recessShelter);
                }
                CaveWaterTableSample waterTable;
                if (waterTableCorrosion > 0.f && genesis != "hypogene") {
                    waterTable = sampleCaveWaterTableCorrosion({p.y, passage.angle, passage.along, waterTableLevel,
                                                                waterTableDrop, waterTableFluctuation, waterTableStages,
                                                                params.getSeed()});
                }
                CaveMixingCorrosionSample mixing;
                if (mixingCorrosion > 0.f) mixing = sampleCaveMixingCorrosion(p, mixingSites);
                const CaveLithologySample lithology =
                    sampleCaveLithology({p.x, p.y, p.z, passage.along, passage.hydraulicIntensity,
                                         lithologicHeterogeneity, params.getSeed()});
                CaveAbrasionSample abrasion;
                if (floodAbrasion > 0.f && genesis != "hypogene") {
                    abrasion = sampleCaveFloodAbrasion({passage.along, passage.angle, passage.hydraulicIntensity,
                                                        passage.bendStrength, passage.outerBankAngle, sedimentLoad,
                                                        params.getSeed()});
                }
                CavePluckingSample plucking;
                if (floodPlucking > 0.f && genesis != "hypogene" && fractures.size() >= 2) {
                    plucking =
                        sampleCaveFloodPlucking({p.x, p.y, p.z, passage.angle, passage.hydraulicIntensity, fractureMask,
                                                 secondaryFractureMask, pluckingBlockScale, params.getSeed()});
                }
                CaveConstrictionScourSample constriction;
                if (constrictionScour > 0.f && genesis != "hypogene") {
                    constriction = sampleCaveConstrictionScour(p, constrictionScourSites);
                }
                CaveKnickpointSample knickpoint;
                if (knickpointErosion > 0.f && genesis != "hypogene") {
                    knickpoint = sampleCaveKnickpointErosion(p, knickpointSites);
                }
                CaveKarrenSample karren;
                if (streamBedKarren > 0.f && genesis != "hypogene" && fractures.size() >= 2) {
                    karren = sampleCaveStreamKarren(
                        {passage.angle, passage.hydraulicIntensity, fractureMask, secondaryFractureMask});
                }
                CavePotholeSample pothole;
                if (eddyPotholes > 0.f && genesis != "hypogene") {
                    pothole = sampleCavePotholeErosion(p, potholeSites);
                }
                CaveObstacleScourSample obstacleScour;
                if (breakdownScour > 0.f && genesis != "hypogene") {
                    obstacleScour = sampleCaveObstacleScour(p, obstacleScourSites);
                }
                CaveFractureChannelizationSample fractureChannel;
                if (fractureFlowFeedback > 0.f && fractureApertureVariability > 0.f) {
                    fractureChannel = sampleCaveFractureChannelization(
                        {fractureMask, secondaryFractureMask, fractureAperture, secondaryFractureAperture,
                         passage.hydraulicIntensity, passage.along, hydrology.reactantPenetration});
                }
                if (facets.retreat > 1e-6f) {
                    ++facetAffectedVoxels;
                    maximumFacetRetreat = std::max(maximumFacetRetreat, facets.retreat);
                }
                if (biogenic.erosion > 1e-6f) {
                    ++biogenicAffectedVoxels;
                    minimumScallopRetention = std::min(minimumScallopRetention, biogenic.fluvialScallopRetention);
                    maximumBiogenicErosion  = std::max(maximumBiogenicErosion, biogenic.erosion);
                    totalBiogenicErosion += biogenic.erosion;
                }
                if (differentialVeins.hostRetreat > 1e-6f) {
                    ++differentialVeinAffectedVoxels;
                    maximumDifferentialVeinRetreat =
                        std::max(maximumDifferentialVeinRetreat, differentialVeins.hostRetreat);
                    maximumVeinProtection = std::max(maximumVeinProtection, differentialVeins.veinProtection);
                }
                const float dissolution = bedding * beddingMask * 0.055f + fractureDissolution * fractureMask * 0.075f +
                                          pockets * 0.032f +
                                          scallopErosion * scallops.erosion * biogenic.fluvialScallopRetention *
                                              0.045f * (genesis == "hypogene" ? 0.35f : 1.f);
                const CaveMicrostructureSample rock = sampleCaveMicrostructure(
                    p.x, p.y, p.z, params.getSeed(), microstructure, microporosityAccess, permeabilityContrast);
                // Preferential paths transport fresher undersaturated water. Their local
                // permeability co-evolves with accessible reactive surface instead of
                // treating every limestone voxel as chemically identical.
                const float flowWeightedDissolution =
                    dissolution * rock.reactiveSurface *
                    std::clamp(passage.hydraulicIntensity * rock.permeability, 0.2f, 1.7f);
                const float biogenicDissolution = biogenic.erosion * rock.reactiveSurface * 0.052f;
                d -= shell * chemicalRetention * chemicalMassTransfer *
                     (erosion * flowWeightedDissolution + biogenicDissolution);
                if (shell > 1e-4f && chemicalRetention < 0.999999f) {
                    ++mineralArmoringAffectedVoxels;
                    maximumMineralCoatingCoverage = std::max(maximumMineralCoatingCoverage, armoring.coatingCoverage);
                    maximumMineralHydraulicRetention =
                        std::max(maximumMineralHydraulicRetention, armoring.hydraulicRetention);
                    minimumArmoredDissolutionRetention =
                        std::min(minimumArmoredDissolutionRetention, chemicalRetention);
                }
                const float fractureChannelRetreat = shell * chemicalMassTransfer * erosion * fractureDissolution *
                                                     fractureFlowFeedback * fractureChannel.erosion * 0.062f;
                d -= fractureChannelRetreat;
                if (fractureChannelRetreat > 1e-6f) {
                    ++fractureChannelAffectedVoxels;
                    maximumFractureChannelRetreat = std::max(maximumFractureChannelRetreat, fractureChannelRetreat);
                    maximumFractureFlowConcentration =
                        std::max(maximumFractureFlowConcentration, fractureChannel.flowConcentration);
                    maximumFractureIntersectionAmplification =
                        std::max(maximumFractureIntersectionAmplification, fractureChannel.intersectionAmplification);
                    minimumFractureReactantAccess =
                        std::min(minimumFractureReactantAccess, fractureChannel.reactantAccess);
                }
                const float waterTableRetreat = shell * chemicalRetention * chemicalMassTransfer * waterTableCorrosion *
                                                waterTable.erosion * 0.065f;
                d -= waterTableRetreat;
                if (waterTableRetreat > 1e-6f) {
                    ++waterTableAffectedVoxels;
                    maximumWaterTableRetreat = std::max(maximumWaterTableRetreat, waterTableRetreat);
                }
                const float mixingRetreat =
                    shell * chemicalRetention * chemicalMassTransfer * mixingCorrosion * mixing.erosion * 0.07f;
                d -= mixingRetreat;
                if (mixingRetreat > 1e-6f) {
                    ++mixingCorrosionAffectedVoxels;
                    maximumMixingCorrosionRetreat = std::max(maximumMixingCorrosionRetreat, mixingRetreat);
                }
                const float lithologyRetreat =
                    shell * chemicalRetention * chemicalMassTransfer * bedding * lithology.retreat * 0.052f;
                d -= lithologyRetreat;
                if (lithologyRetreat > 1e-6f) {
                    ++lithologyAffectedVoxels;
                    if (lithology.styloliteMask > 0.05f) ++styloliteAffectedVoxels;
                    minimumBedResistance    = std::min(minimumBedResistance, lithology.bedResistance);
                    maximumLithologyRetreat = std::max(maximumLithologyRetreat, lithologyRetreat);
                }
                const float abrasionRetreat = shell * floodAbrasion * abrasion.erosion * 0.058f;
                d -= abrasionRetreat;
                if (abrasionRetreat > 1e-6f) {
                    ++abrasionAffectedVoxels;
                    maximumAbrasionRetreat = std::max(maximumAbrasionRetreat, abrasionRetreat);
                    maximumAbrasionVortex  = std::max(maximumAbrasionVortex, abrasion.vortexMask);
                }
                const float pluckingRetreat = shell * floodPlucking * plucking.erosion * 0.082f;
                d -= pluckingRetreat;
                if (pluckingRetreat > 1e-6f) {
                    ++pluckingAffectedVoxels;
                    maximumPluckingRetreat = std::max(maximumPluckingRetreat, pluckingRetreat);
                    maximumPluckingPredisposition =
                        std::max(maximumPluckingPredisposition, plucking.fracturePredisposition);
                }
                const float constrictionRetreat = shell * constrictionScour * constriction.erosion * 0.115f;
                d -= constrictionRetreat;
                if (constrictionRetreat > 1e-6f) {
                    ++constrictionScourAffectedVoxels;
                    maximumConstrictionScourRetreat = std::max(maximumConstrictionScourRetreat, constrictionRetreat);
                    maximumConstrictionRatio  = std::max(maximumConstrictionRatio, constriction.constrictionRatio);
                    maximumPlungingEfficiency = std::max(maximumPlungingEfficiency, constriction.plungingEfficiency);
                }
                const float knickpointRetreat = shell * knickpointErosion * knickpoint.erosion * 0.12f;
                d -= knickpointRetreat;
                if (knickpointRetreat > 1e-6f) {
                    ++knickpointAffectedVoxels;
                    maximumKnickpointRetreat    = std::max(maximumKnickpointRetreat, knickpointRetreat);
                    maximumKnickpointSlopeBreak = std::max(maximumKnickpointSlopeBreak, knickpoint.slopeBreak);
                    maximumKnickpointDrop =
                        std::max(maximumKnickpointDrop, knickpointSites[size_t(knickpoint.siteIndex)].drop);
                }
                const float karrenRetreat = shell * streamBedKarren * karren.erosion * 0.052f;
                d -= karrenRetreat;
                if (karrenRetreat > 1e-6f) {
                    ++streamBedKarrenAffectedVoxels;
                    maximumStreamBedKarrenRetreat = std::max(maximumStreamBedKarrenRetreat, karrenRetreat);
                    maximumKarrenFractureGuidance = std::max(maximumKarrenFractureGuidance, karren.fractureGuidance);
                    maximumKarrenIntersectionPocket =
                        std::max(maximumKarrenIntersectionPocket, karren.intersectionPocket);
                }
                const float potholeRetreat = shell * eddyPotholes * pothole.erosion * 0.09f;
                d -= potholeRetreat;
                if (potholeRetreat > 1e-6f) {
                    ++potholeAffectedVoxels;
                    maximumPotholeRetreat          = std::max(maximumPotholeRetreat, potholeRetreat);
                    maximumPotholeSecondaryErosion = std::max(maximumPotholeSecondaryErosion, pothole.secondaryPothole);
                    maximumPotholeFractureIntersection =
                        std::max(maximumPotholeFractureIntersection,
                                 potholeSites[size_t(pothole.siteIndex)].fractureIntersection);
                }
                const float obstacleScourRetreat = shell * breakdownScour * obstacleScour.erosion * 0.082f;
                d -= obstacleScourRetreat;
                if (obstacleScourRetreat > 1e-6f) {
                    ++obstacleScourAffectedVoxels;
                    maximumObstacleScourRetreat = std::max(maximumObstacleScourRetreat, obstacleScourRetreat);
                    maximumHorseshoeScour       = std::max(maximumHorseshoeScour, obstacleScour.horseshoeScour);
                    maximumWakeScour            = std::max(maximumWakeScour, obstacleScour.wakeScour);
                    minimumObstacleRoughnessRetention =
                        std::min(minimumObstacleRoughnessRetention, obstacleScour.roughnessRetention);
                }
                d -= shell * chemicalRetention * chemicalMassTransfer * facets.retreat;
                d -= shell * chemicalRetention * chemicalMassTransfer * differentialVeins.hostRetreat;
                const size_t voxel = size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
                density[voxel]     = d;
                const float exposedRate =
                    rock.reactiveSurface * std::clamp(passage.hydraulicIntensity * rock.permeability, 0.2f, 1.7f);
                reactiveRate[voxel]      = std::clamp(1.f + reactiveSurfaceCoupling * (exposedRate - 1.f), 0.25f, 2.5f);
                hydraulicExposure[voxel] = passage.hydraulicIntensity;
                flowDirection[voxel]     = passage.tangent;
            }
        }
    }

    // The resolved wall geometry now feeds back into intrinsic surface reactivity.
    // Normal dispersion is orientation-independent and is recomputed after each retreat.
    const CaveSurfaceReactivityResult surfaceReactivity =
        evolveCaveSurfaceByReactivity(density, reactiveRate, nx, ny, nz, surfaceSlopeReactivity);

    // Mineral-scale reactivity is spatially heterogeneous rather than voxel-white noise.
    // A seeded two-band spectrum creates coherent etch patches and feeds each retreat
    // iteration back into the current zero isosurface.
    const CaveReactivePatchinessResult reactivePatchEvolution = evolveCaveSurfaceByCorrelatedReactivity(
        density, reactiveRate, flowDirection, nx, ny, nz, reactivePatchiness, params.getSeed());

    // Humid, weakly flushed ceiling sectors receive a separate late-stage corrosion
    // overprint before curvature retreat and secondary calcite deposition.
    const CaveCondensationResult condensation =
        erodeCaveByCondensation(density, hydraulicExposure, nx, ny, nz, condensationCorrosion, params.getSeed());

    // Carbonate wall retreat precedes secondary calcite deposition. Curvature-driven
    // dissolution rounds exposed convex asperities without blurring protected recesses.
    const CaveSurfaceEvolutionResult surfaceEvolution =
        evolveCaveSurfaceByCurvature(density, reactiveRate, nx, ny, nz, curvatureDissolution);

    // Deposits are generated from stable seeded candidates, but attach only after all
    // wall-retreat stages have produced the final host surface. This prevents the old
    // analytic chamber boundary from leaving calcite floating in evolved cave air.
    const float            voxelSize              = 2.f / float(std::min({nx - 1, ny - 1, nz - 1}));
    const float            embedDepth             = voxelSize * 1.15f;
    const float            minimumFeatureRadius   = voxelSize * 0.82f;
    int                    rejectedDripstonePairs = 0;
    int                    anchoredDripstonePairs = 0;
    std::vector<Dripstone> anchoredDripstones;
    anchoredDripstones.reserve(dripstones.size());
    for (size_t i = 0; i + 1 < dripstones.size(); i += 2) {
        const Dripstone& ceilingCandidate = dripstones[i];
        const Dripstone& floorCandidate   = dripstones[i + 1];
        const float      preferredY       = (ceilingCandidate.start.y + floorCandidate.start.y) * 0.5f;
        const auto       span =
            findCaveVerticalSpan(density, nx, ny, nz, ceilingCandidate.start.x, ceilingCandidate.start.z, preferredY);
        if (!span) {
            ++rejectedDripstonePairs;
            continue;
        }
        const float gap    = span->ceiling.position.y - span->floor.position.y;
        bool        column = ceilingCandidate.endRadius > ceilingCandidate.startRadius * 0.5f &&
                             floorCandidate.endRadius > floorCandidate.startRadius * 0.5f;
        if (column) {
            const float clearanceRadius = std::max(ceilingCandidate.startRadius, floorCandidate.startRadius) * 1.18f;
            for (int sampleIndex = 1; sampleIndex < 12 && column; ++sampleIndex) {
                const float sampleY = span->floor.position.y + gap * float(sampleIndex) / 12.f;
                for (int angleIndex = 0; angleIndex < 8; ++angleIndex) {
                    const float          angle = float(angleIndex) * 0.7853981634f;
                    const CaveFieldPoint clearancePoint{ceilingCandidate.start.x + std::cos(angle) * clearanceRadius,
                                                        sampleY,
                                                        ceilingCandidate.start.z + std::sin(angle) * clearanceRadius};
                    if (sampleCaveDensity(density, nx, ny, nz, clearancePoint) > -voxelSize * 0.35f) {
                        column = false;
                        break;
                    }
                }
            }
        }
        const float maximumLength = gap * (column ? 0.515f : 0.42f);
        const float ceilingLength =
            column ? maximumLength
                   : std::min(std::abs(ceilingCandidate.end.y - ceilingCandidate.start.y), maximumLength);
        const float floorLength =
            column ? maximumLength : std::min(std::abs(floorCandidate.end.y - floorCandidate.start.y), maximumLength);
        if (ceilingLength < embedDepth * 1.5f || floorLength < embedDepth * 1.5f) {
            ++rejectedDripstonePairs;
            continue;
        }
        const Vec3 ceiling{span->ceiling.position.x, span->ceiling.position.y, span->ceiling.position.z};
        const Vec3 floor{span->floor.position.x, span->floor.position.y, span->floor.position.z};
        if (column) {
            // A mature column is one continuous implicit body. Two overlapping
            // tapered segments produced a false waist, discontinuous normals and
            // a self-shadowing ring even when their endpoints overlapped.
            anchoredDripstones.push_back({add(ceiling, {0.f, embedDepth, 0.f}), add(floor, {0.f, -embedDepth, 0.f}),
                                          std::max(ceilingCandidate.startRadius, minimumFeatureRadius),
                                          std::max(floorCandidate.startRadius, minimumFeatureRadius)});
            anchoredDripstones.back().profileAmplitude = 0.055f;
            anchoredDripstones.back().profileFrequency = 2.5f;
            anchoredDripstones.back().profilePhase =
                std::fmod(std::abs(ceiling.x * 19.7f + ceiling.z * 31.1f), 6.283185307f);
            ++anchoredDripstonePairs;
            continue;
        }
        anchoredDripstones.push_back({add(ceiling, {0.f, embedDepth, 0.f}), add(ceiling, {0.f, -ceilingLength, 0.f}),
                                      std::max(ceilingCandidate.startRadius, minimumFeatureRadius),
                                      std::max(ceilingCandidate.endRadius, minimumFeatureRadius * 0.24f)});
        anchoredDripstones.back().profileAmplitude = 0.08f;
        anchoredDripstones.back().profileFrequency = 1.5f;
        anchoredDripstones.back().profilePhase =
            std::fmod(std::abs(ceiling.x * 17.3f + ceiling.z * 23.9f), 6.283185307f);
        anchoredDripstones.push_back({add(floor, {0.f, -embedDepth, 0.f}), add(floor, {0.f, floorLength, 0.f}),
                                      std::max(floorCandidate.startRadius, minimumFeatureRadius),
                                      std::max(floorCandidate.endRadius, minimumFeatureRadius * 0.30f)});
        anchoredDripstones.back().profileAmplitude = 0.11f;
        anchoredDripstones.back().profileFrequency = 2.f;
        anchoredDripstones.back().profilePhase = std::fmod(std::abs(floor.x * 29.3f + floor.z * 13.7f), 6.283185307f);
        ++anchoredDripstonePairs;
    }
    dripstones = std::move(anchoredDripstones);

    int                    rejectedFlowstones = 0;
    std::vector<Flowstone> anchoredFlowstones;
    anchoredFlowstones.reserve(flowstones.size());
    for (Flowstone flowstone : flowstones) {
        const auto anchor = projectToFinalCaveSurface(
            density, nx, ny, nz, {flowstone.center.x, flowstone.center.y, flowstone.center.z}, 0.24f);
        if (!anchor || std::abs(anchor->rockNormal.y) > 0.82f) {
            ++rejectedFlowstones;
            continue;
        }
        flowstone.normal = {anchor->rockNormal.x, anchor->rockNormal.y, anchor->rockNormal.z};
        Vec3        tangent{-flowstone.normal.z, 0.f, flowstone.normal.x};
        const float tangentLength = std::sqrt(dot(tangent, tangent));
        if (tangentLength < 1e-4f) {
            ++rejectedFlowstones;
            continue;
        }
        flowstone.tangent = mul(tangent, 1.f / tangentLength);
        flowstone.center =
            add({anchor->position.x, anchor->position.y, anchor->position.z}, mul(flowstone.normal, embedDepth));
        flowstone.thickness = std::max(flowstone.thickness, minimumFeatureRadius);
        anchoredFlowstones.push_back(flowstone);
    }
    flowstones = std::move(anchoredFlowstones);

    int                  rejectedCurtains = 0;
    std::vector<Curtain> anchoredCurtains;
    anchoredCurtains.reserve(curtains.size());
    for (Curtain curtain : curtains) {
        const auto span =
            findCaveVerticalSpan(density, nx, ny, nz, curtain.anchor.x, curtain.anchor.z, curtain.anchor.y);
        if (!span || span->ceiling.rockNormal.y < 0.35f) {
            ++rejectedCurtains;
            continue;
        }
        curtain.anchor    = {span->ceiling.position.x, span->ceiling.position.y + embedDepth, span->ceiling.position.z};
        curtain.length    = std::min(curtain.length, (span->ceiling.position.y - span->floor.position.y) * 0.36f);
        curtain.thickness = std::max(curtain.thickness, minimumFeatureRadius);
        if (curtain.length < embedDepth * 2.f) {
            ++rejectedCurtains;
            continue;
        }
        anchoredCurtains.push_back(curtain);
    }
    curtains = std::move(anchoredCurtains);
    std::vector<float> depositDelta(density.size(), 0.f);
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const Vec3  p{float(x) / float(nx - 1) * 2.f - 1.f, float(y) / float(ny - 1) * 2.f - 1.f,
                              float(z) / float(nz - 1) * 2.f - 1.f};
                float&      d = density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)];
                const float densityBeforeDeposits = d;
                for (const Dripstone& dripstone : dripstones)
                    d = smoothMaximum(d, -taperedSegmentDistance(p, dripstone, width / height, depth / height), 0.010f);
                for (const Flowstone& flowstone : flowstones)
                    d = smoothMaximum(d, -flowstoneDistance(p, flowstone), 0.008f);
                for (const Curtain& curtain : curtains) d = smoothMaximum(d, -curtainDistance(p, curtain), 0.006f);
                depositDelta[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)] =
                    std::max(0.f, d - densityBeforeDeposits);
                d = addCaveBreakdownBlocks(p.x, p.y, p.z, d, breakdownSet);
                d = addCaveSediment(p.x, p.y, p.z, d, sedimentSet);
            }
        }
    }

    const CaveDetachmentResult detachment = detachUnsupportedCaveFragments(density, nx, ny, nz, fragmentDetachment);
    const CaveBoundaryClosure  boundary =
        closeCaveDensityBoundary(density, nx, ny, nz, boundaryClosure, params.getSeed());

    CaveResampledField        resampled;
    const std::vector<float>* extractionDensity = &density;
    int                       extractionNx = nx, extractionNy = ny, extractionNz = nz;
    if (isosurfaceSampling > 1) {
        resampled         = resampleCaveDensity(density, nx, ny, nz, isosurfaceSampling);
        extractionDensity = &resampled.density;
        extractionNx      = resampled.nx;
        extractionNy      = resampled.ny;
        extractionNz      = resampled.nz;
    }
    if (!marchingCubes(extractionDensity->data(), extractionNx, extractionNy, extractionNz, 0.f, out, &error) ||
        out.empty()) {
        if (error.empty()) error = "mesh.cave: generated an empty cave";
        return false;
    }
    std::vector<int> caveWallGroups(size_t(out.getIndexCount() / 3), 0);
    const float      groupTolerance = 1.5f / float(std::min({extractionNx - 1, extractionNy - 1, extractionNz - 1}));
    for (int triangle = 0; triangle < out.getIndexCount() / 3; ++triangle) {
        Vec3 center{};
        for (int corner = 0; corner < 3; ++corner) {
            const int index = out.indices()[size_t(triangle * 3 + corner)];
            // marchingCubes emits a cube centered at the origin in [-0.5, 0.5].
            center.x += out.getPositionX(index) * 2.f;
            center.y += out.getPositionY(index) * 2.f;
            center.z += out.getPositionZ(index) * 2.f;
        }
        center = mul(center, 1.f / 3.f);
        const bool createdByDeposition =
            sampleCaveDensity(depositDelta, nx, ny, nz, {center.x, center.y, center.z}) > voxelSize * 0.01f;
        if (createdByDeposition) {
            for (const Dripstone& dripstone : dripstones) {
                if (std::fabs(taperedSegmentDistance(center, dripstone, width / height, depth / height)) <=
                    groupTolerance) {
                    caveWallGroups[size_t(triangle)] = 1;
                    break;
                }
            }
            if (caveWallGroups[size_t(triangle)] == 0) {
                for (const Flowstone& flowstone : flowstones) {
                    if (std::fabs(flowstoneDistance(center, flowstone)) <= groupTolerance) {
                        caveWallGroups[size_t(triangle)] = 1;
                        break;
                    }
                }
            }
            if (caveWallGroups[size_t(triangle)] == 0) {
                for (const Curtain& curtain : curtains) {
                    if (std::fabs(curtainDistance(center, curtain)) <= groupTolerance) {
                        caveWallGroups[size_t(triangle)] = 1;
                        break;
                    }
                }
            }
        }
        if (isCaveBreakdownBlockSurface(center.x, center.y, center.z, groupTolerance, breakdownSet))
            caveWallGroups[size_t(triangle)] = 3;
        if (isCaveSedimentSurface(center.x, center.y, center.z, groupTolerance, sedimentSet))
            caveWallGroups[size_t(triangle)] = 4;
    }
    auto groupResult = out.restoreGroupData({"caveWalls", "speleothems", "wetWalls", "breakdown", "sediment"},
                                            std::move(caveWallGroups), 0);
    if (!groupResult) {
        const auto* diagnostic = groupResult.error();
        error                  = "mesh.cave: failed to assign cave wall group";
        if (diagnostic) error += ": " + diagnostic->message();
        return false;
    }
    int refinedSourceTriangles = 0;
    int adaptiveSplitEdges     = 0;
    if (surfaceRefinement > 0) {
        MeshBuild refined;
        refined.reserve(out.getVertexCount() * 4, out.getIndexCount() * 4);
        std::unordered_map<EdgeKey, EdgeProjection, EdgeKeyHash> edgeProjections;
        edgeProjections.reserve(size_t(out.getIndexCount()));
        auto edgeProjection = [&](Vec3 a, Vec3 b) -> EdgeProjection {
            const EdgeKey key   = makeEdgeKey(a, b);
            auto          found = edgeProjections.find(key);
            if (found != edgeProjections.end()) return found->second;
            const Vec3 midpoint = mul(add(a, b), 0.5f);
            Vec3       projected =
                projectToDensitySurface(midpoint, *extractionDensity, extractionNx, extractionNy, extractionNz);
            Vec3        error             = sub(projected, midpoint);
            const float errorLength       = std::sqrt(dot(error, error));
            const float edgeLength        = std::sqrt(dot(sub(b, a), sub(b, a)));
            const float maximumProjection = edgeLength * (surfaceRefinement == 1 ? 0.35f : 0.10f);
            if (errorLength > maximumProjection && errorLength > 1e-8f) {
                error     = mul(error, maximumProjection / errorLength);
                projected = add(midpoint, error);
            }
            const bool split = surfaceRefinement == 1 || std::sqrt(dot(error, error)) >= refinementThreshold;
            if (split) ++adaptiveSplitEdges;
            return edgeProjections.emplace(key, EdgeProjection{projected, split}).first->second;
        };
        for (int triangle = 0; triangle < out.getIndexCount() / 3; ++triangle) {
            const int group = out.getTriangleGroup(triangle);
            refined.setActiveGroup(out.getGroupName(group));
            Vec3 corners[3];
            for (int corner = 0; corner < 3; ++corner) {
                const int index = out.getIndex(triangle * 3 + corner);
                corners[corner] = {out.getPositionX(index), out.getPositionY(index), out.getPositionZ(index)};
            }
            const EdgeProjection e01 = edgeProjection(corners[0], corners[1]);
            const EdgeProjection e12 = edgeProjection(corners[1], corners[2]);
            const EdgeProjection e20 = edgeProjection(corners[2], corners[0]);
            const Vec3           ab  = sub(corners[1], corners[0]);
            const Vec3           ac  = sub(corners[2], corners[0]);
            const Vec3 referenceNormal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
            if (surfaceRefinement == 1) {
                ++refinedSourceTriangles;
                addFacetedTriangle(refined, corners[0], e01.projected, e20.projected, referenceNormal);
                addFacetedTriangle(refined, e01.projected, corners[1], e12.projected, referenceNormal);
                addFacetedTriangle(refined, e20.projected, e12.projected, corners[2], referenceNormal);
                addFacetedTriangle(refined, e01.projected, e12.projected, e20.projected, referenceNormal);
                continue;
            }
            const int splitMask = (e01.split ? 1 : 0) | (e12.split ? 2 : 0) | (e20.split ? 4 : 0);
            if (splitMask != 0) ++refinedSourceTriangles;
            auto emit = [&](Vec3 a, Vec3 b, Vec3 c) { addFacetedTriangle(refined, a, b, c, referenceNormal); };
            switch (splitMask) {
                case 0: emit(corners[0], corners[1], corners[2]); break;
                case 1:
                    emit(corners[0], e01.projected, corners[2]);
                    emit(e01.projected, corners[1], corners[2]);
                    break;
                case 2:
                    emit(corners[1], e12.projected, corners[0]);
                    emit(e12.projected, corners[2], corners[0]);
                    break;
                case 3:
                    emit(corners[1], e12.projected, e01.projected);
                    emit(corners[0], e01.projected, corners[2]);
                    emit(e01.projected, e12.projected, corners[2]);
                    break;
                case 4:
                    emit(corners[2], e20.projected, corners[1]);
                    emit(e20.projected, corners[0], corners[1]);
                    break;
                case 5:
                    emit(corners[0], e01.projected, e20.projected);
                    emit(corners[2], e20.projected, corners[1]);
                    emit(e20.projected, e01.projected, corners[1]);
                    break;
                case 6:
                    emit(corners[2], e20.projected, e12.projected);
                    emit(corners[1], e12.projected, corners[0]);
                    emit(e12.projected, e20.projected, corners[0]);
                    break;
                case 7:
                    emit(corners[0], e01.projected, e20.projected);
                    emit(e01.projected, corners[1], e12.projected);
                    emit(e20.projected, e12.projected, corners[2]);
                    emit(e01.projected, e12.projected, e20.projected);
                    break;
            }
        }
        out = std::move(refined);
    }
    const CaveWetnessRefinement wetness =
        refineCaveWetnessBoundary(out, spine, tunnelRadius, params.getSeed(), wetnessRefinement > 0);
    auto& positions = out.positions();
    // marchingCubes already normalizes grid coordinates to [-0.5, 0.5], so world
    // dimensions are direct axis scales.  Applying another voxel-size division would
    // collapse the cave into a tiny patch at the negative world bound.
    const float sx = width;
    const float sy = height;
    const float sz = depth;
    if (applyCaveSurfaceNormals(out, *extractionDensity, extractionNx, extractionNy, extractionNz, width, height, depth,
                                surfaceNormalMode, normalSmoothing, error) != CaveNormalStatus::applied)
        return false;
    for (size_t i = 0; i < positions.size(); i += 3) {
        positions[i] *= sx;
        positions[i + 1] *= sy;
        positions[i + 2] *= sz;
    }
    out.setMeta("algorithm", "mesh.cave");
    out.setMeta("style", style);
    out.setMeta("genesis", genesis);
    out.setMeta("seed", std::to_string(params.getSeed()));
    out.setMeta("chambers", std::to_string(chamberCount));
    out.setMeta("branches", std::to_string(branchCount));
    out.setMeta("chamberHierarchy", std::to_string(chamberHierarchy));
    out.setMeta("passageVariation", std::to_string(passageVariation));
    out.setMeta("chamberIrregularity", std::to_string(chamberIrregularity));
    out.setMeta("macroMorphologyModel",
                chamberHierarchy > 0.f ? "hierarchical-primary-hall-v2" : "uniform-chambers-v1");
    out.setMeta("primaryChamberVerticalRadius", std::to_string(primaryChamberVerticalRadius));
    out.setMeta("cupolas", std::to_string(cupolas.size()));
    out.setMeta("feeders", std::to_string(feeders.size()));
    out.setMeta("risingFlowModel", genesis == "epigene" ? "none" : "feeder-half-tube-cupola-v1");
    out.setMeta(
        "erosionModel",
        biogenicCorrosion > 0.f ? "karst-biogenic-aero-overprint-v10"
        : scallopMaturity > 0.f ? "karst-reactive-coarsening-v9"
        : bendUndercut > 0.f    ? "karst-reactive-curvature-v8"
        : microstructure > 0.f && scallopHydraulicScaling > 0.f
            ? "karst-reactive-microstructure-scallop-v7"
            : (microstructure > 0.f ? "karst-reactive-microstructure-v6"
                                    : (scallopHydraulicScaling > 0.f
                                           ? "karst-hydraulic-scallop-v7"
                                           : (hydraulicErosion > 0.f ? "karst-reactive-network-da-v5"
                                                                     : "karst-fracture-bedding-vadose-scallop-v2"))));
    out.setMeta("erosion", std::to_string(erosion));
    out.setMeta("waterTableCorrosion", std::to_string(waterTableCorrosion));
    out.setMeta("waterTableCorrosionModel", waterTableCorrosion <= 0.f ? "disabled"
                                            : genesis == "hypogene"    ? "inactive-hypogene"
                                                                       : "descending-epiphreatic-belts-v1");
    out.setMeta("waterTableLevel", std::to_string(waterTableLevel));
    out.setMeta("waterTableStages", std::to_string(waterTableStages));
    out.setMeta("waterTableDrop", std::to_string(waterTableDrop));
    out.setMeta("waterTableAffectedVoxels", std::to_string(waterTableAffectedVoxels));
    out.setMeta("maximumWaterTableRetreat", std::to_string(maximumWaterTableRetreat));
    out.setMeta("mixingCorrosion", std::to_string(mixingCorrosion));
    out.setMeta("mixingCorrosionModel", mixingCorrosion <= 0.f ? "disabled"
                                        : mixingSites.empty()  ? "inactive-no-confluence"
                                                               : "chemistry-weighted-confluence-v1");
    out.setMeta("mixingCorrosionSites", std::to_string(mixingSites.size()));
    out.setMeta("mixingCorrosionAffectedVoxels", std::to_string(mixingCorrosionAffectedVoxels));
    out.setMeta("maximumMixingCorrosionRetreat", std::to_string(maximumMixingCorrosionRetreat));
    out.setMeta("lithologicHeterogeneity", std::to_string(lithologicHeterogeneity));
    out.setMeta("lithologyErosionModel",
                lithologicHeterogeneity > 0.f && bedding > 0.f ? "flow-accessible-stylolite-beds-v1" : "disabled");
    out.setMeta("lithologyAffectedVoxels", std::to_string(lithologyAffectedVoxels));
    out.setMeta("styloliteAffectedVoxels", std::to_string(styloliteAffectedVoxels));
    out.setMeta("minimumBedResistance", std::to_string(minimumBedResistance));
    out.setMeta("maximumLithologyRetreat", std::to_string(maximumLithologyRetreat));
    out.setMeta("floodAbrasion", std::to_string(floodAbrasion));
    out.setMeta("sedimentLoad", std::to_string(sedimentLoad));
    out.setMeta("floodAbrasionModel", floodAbrasion <= 0.f    ? "disabled"
                                      : genesis == "hypogene" ? "inactive-hypogene"
                                      : sedimentLoad <= 0.f   ? "inactive-no-tools"
                                                              : "near-bed-tools-cover-vortex-v1");
    out.setMeta("abrasionAffectedVoxels", std::to_string(abrasionAffectedVoxels));
    out.setMeta("maximumAbrasionRetreat", std::to_string(maximumAbrasionRetreat));
    out.setMeta("maximumAbrasionVortex", std::to_string(maximumAbrasionVortex));
    out.setMeta("floodPlucking", std::to_string(floodPlucking));
    out.setMeta("pluckingBlockScale", std::to_string(pluckingBlockScale));
    out.setMeta("floodPluckingModel", floodPlucking <= 0.f    ? "disabled"
                                      : genesis == "hypogene" ? "inactive-hypogene"
                                      : fractures.size() < 2  ? "inactive-no-fracture-network"
                                                              : "thresholded-fracture-block-release-v1");
    out.setMeta("pluckingAffectedVoxels", std::to_string(pluckingAffectedVoxels));
    out.setMeta("maximumPluckingRetreat", std::to_string(maximumPluckingRetreat));
    out.setMeta("maximumPluckingPredisposition", std::to_string(maximumPluckingPredisposition));
    out.setMeta("constrictionScour", std::to_string(constrictionScour));
    out.setMeta("constrictionScourModel", constrictionScour <= 0.f         ? "disabled"
                                          : genesis == "hypogene"          ? "inactive-hypogene"
                                          : constrictionScourSites.empty() ? "inactive-no-constriction"
                                                                           : "discharge-optimal-plunging-flow-cpw-v2");
    out.setMeta("constrictionScourSites", std::to_string(constrictionScourSites.size()));
    out.setMeta("constrictionScourAffectedVoxels", std::to_string(constrictionScourAffectedVoxels));
    out.setMeta("maximumConstrictionScourRetreat", std::to_string(maximumConstrictionScourRetreat));
    out.setMeta("maximumConstrictionRatio", std::to_string(maximumConstrictionRatio));
    out.setMeta("maximumPlungingEfficiency", std::to_string(maximumPlungingEfficiency));
    out.setMeta("knickpointErosion", std::to_string(knickpointErosion));
    out.setMeta("knickpointErosionModel", knickpointErosion <= 0.f  ? "disabled"
                                          : genesis == "hypogene"   ? "inactive-hypogene"
                                          : sedimentLoad <= 0.f     ? "inactive-no-tools"
                                          : knickpointSites.empty() ? "inactive-no-slope-break"
                                                                    : "sediment-driven-headward-plunge-pool-v1");
    out.setMeta("knickpointSites", std::to_string(knickpointSites.size()));
    out.setMeta("knickpointAffectedVoxels", std::to_string(knickpointAffectedVoxels));
    out.setMeta("maximumKnickpointRetreat", std::to_string(maximumKnickpointRetreat));
    out.setMeta("maximumKnickpointSlopeBreak", std::to_string(maximumKnickpointSlopeBreak));
    out.setMeta("maximumKnickpointDrop", std::to_string(maximumKnickpointDrop));
    out.setMeta("streamBedKarren", std::to_string(streamBedKarren));
    out.setMeta("streamBedKarrenModel", streamBedKarren <= 0.f  ? "disabled"
                                        : genesis == "hypogene" ? "inactive-hypogene"
                                        : fractures.size() < 2  ? "inactive-no-crossing-fractures"
                                                                : "lidar-constrained-fracture-guided-bed-karren-v1");
    out.setMeta("streamBedKarrenAffectedVoxels", std::to_string(streamBedKarrenAffectedVoxels));
    out.setMeta("maximumStreamBedKarrenRetreat", std::to_string(maximumStreamBedKarrenRetreat));
    out.setMeta("maximumKarrenFractureGuidance", std::to_string(maximumKarrenFractureGuidance));
    out.setMeta("maximumKarrenIntersectionPocket", std::to_string(maximumKarrenIntersectionPocket));
    out.setMeta("eddyPotholes", std::to_string(eddyPotholes));
    out.setMeta("potholeGravelSize", std::to_string(potholeGravelSize));
    out.setMeta("eddyPotholeModel", eddyPotholes <= 0.f     ? "disabled"
                                    : genesis == "hypogene" ? "inactive-hypogene"
                                    : sedimentLoad <= 0.f   ? "inactive-no-tools"
                                    : fractures.size() < 2  ? "inactive-no-crossing-fractures"
                                    : potholeSites.empty()  ? "inactive-no-fracture-seeded-vortex"
                                                            : "gravel-size-dependent-compound-eddy-pothole-v1");
    out.setMeta("eddyPotholeSites", std::to_string(potholeSites.size()));
    out.setMeta("eddyPotholeAffectedVoxels", std::to_string(potholeAffectedVoxels));
    out.setMeta("maximumPotholeRetreat", std::to_string(maximumPotholeRetreat));
    out.setMeta("maximumPotholeSecondaryErosion", std::to_string(maximumPotholeSecondaryErosion));
    out.setMeta("maximumPotholeFractureIntersection", std::to_string(maximumPotholeFractureIntersection));
    out.setMeta("breakdownScour", std::to_string(breakdownScour));
    out.setMeta("breakdownScourModel", breakdownScour <= 0.f          ? "disabled"
                                       : genesis == "hypogene"        ? "inactive-hypogene"
                                       : sedimentLoad <= 0.f          ? "inactive-no-tools"
                                       : breakdownSet.blockCount <= 0 ? "inactive-no-breakdown-blocks"
                                       : obstacleScourSites.empty()   ? "inactive-no-stream-obstacle"
                                                                      : "roughness-damped-horseshoe-wake-scour-v1");
    out.setMeta("breakdownScourSites", std::to_string(obstacleScourSites.size()));
    out.setMeta("breakdownScourAffectedVoxels", std::to_string(obstacleScourAffectedVoxels));
    out.setMeta("maximumBreakdownScourRetreat", std::to_string(maximumObstacleScourRetreat));
    out.setMeta("maximumHorseshoeScour", std::to_string(maximumHorseshoeScour));
    out.setMeta("maximumWakeScour", std::to_string(maximumWakeScour));
    out.setMeta("minimumObstacleRoughnessRetention", std::to_string(minimumObstacleRoughnessRetention));
    out.setMeta("mineralArmoring", std::to_string(mineralArmoring));
    out.setMeta("mineralArmoringModel", mineralArmoring <= 0.f ? "disabled"
                                        : erosion <= 0.f && biogenicCorrosion <= 0.f && waterTableCorrosion <= 0.f &&
                                                mixingCorrosion <= 0.f && lithologicHeterogeneity <= 0.f &&
                                                condensationFaceting <= 0.f && differentialVeinErosion <= 0.f
                                            ? "inactive-no-chemical-retreat"
                                            : "genesis-supplied-hydraulic-stripping-shield-v1");
    out.setMeta("mineralArmoringAffectedVoxels", std::to_string(mineralArmoringAffectedVoxels));
    out.setMeta("maximumMineralCoatingCoverage", std::to_string(maximumMineralCoatingCoverage));
    out.setMeta("maximumMineralHydraulicRetention", std::to_string(maximumMineralHydraulicRetention));
    out.setMeta("minimumArmoredDissolutionRetention",
                std::to_string(mineralArmoringAffectedVoxels > 0 ? minimumArmoredDissolutionRetention : 1.f));
    out.setMeta("multiscaleRoughness", std::to_string(multiscaleRoughness));
    out.setMeta("wallRoughnessSpectrum",
                multiscaleRoughness > 0.f ? "band-limited-three-scale-v1" : "legacy-single-band");
    out.setMeta("minimumWallRelief", std::to_string(minimumWallRelief));
    out.setMeta("maximumWallRelief", std::to_string(maximumWallRelief));
    out.setMeta("roughnessFlowCoupling", std::to_string(roughnessFlowCoupling));
    out.setMeta("roughnessMassTransferModel",
                roughnessFlowCoupling <= 0.f                     ? "disabled"
                : multiscaleRoughness <= 0.f || roughness <= 0.f ? "inactive-no-resolved-relief"
                : erosion <= 0.f && biogenicCorrosion <= 0.f && waterTableCorrosion <= 0.f && mixingCorrosion <= 0.f &&
                        lithologicHeterogeneity <= 0.f && condensationFaceting <= 0.f && differentialVeinErosion <= 0.f
                    ? "inactive-no-chemical-retreat"
                    : "ridge-exposure-recess-shelter-v1");
    out.setMeta("roughnessTransferAffectedVoxels", std::to_string(roughnessTransferAffectedVoxels));
    out.setMeta("minimumRoughnessTransferMultiplier", std::to_string(minimumRoughnessTransfer));
    out.setMeta("maximumRoughnessTransferMultiplier", std::to_string(maximumRoughnessTransfer));
    out.setMeta("maximumRidgeExposure", std::to_string(maximumRidgeExposure));
    out.setMeta("maximumRecessShelter", std::to_string(maximumRecessShelter));
    out.setMeta("fractureApertureVariability", std::to_string(fractureApertureVariability));
    out.setMeta("fractureApertureDistribution",
                fractureApertureVariability > 0.f ? "correlated-lognormal-proxy-v1" : "uniform");
    out.setMeta("fractureStressControl", std::to_string(fractureStressControl));
    out.setMeta("fractureDissolutionFront",
                fractureStressControl > 0.f ? "stress-split-branching-v1" : "uniform-plane");
    out.setMeta("fractureApertureGeometricStdDev", std::to_string(std::exp(0.55f * fractureApertureVariability)));
    out.setMeta("minimumFractureApertureMultiplier", std::to_string(minimumFractureAperture));
    out.setMeta("maximumFractureApertureMultiplier", std::to_string(maximumFractureAperture));
    out.setMeta("minimumFractureBranchOpenness", std::to_string(minimumFractureBranchOpenness));
    out.setMeta("fractureFlowFeedback", std::to_string(fractureFlowFeedback));
    out.setMeta("fractureFlowFeedbackModel",
                fractureFlowFeedback <= 0.f                    ? "disabled"
                : fractureDissolution <= 0.f || erosion <= 0.f ? "inactive-no-chemical-retreat"
                : fractures.empty()                            ? "inactive-no-fracture-network"
                : fractureApertureVariability <= 0.f           ? "inactive-no-aperture-contrast"
                                                               : "cubic-aperture-reactive-channelization-v1");
    out.setMeta("fractureChannelAffectedVoxels", std::to_string(fractureChannelAffectedVoxels));
    out.setMeta("maximumFractureChannelRetreat", std::to_string(maximumFractureChannelRetreat));
    out.setMeta("maximumFractureFlowConcentration", std::to_string(maximumFractureFlowConcentration));
    out.setMeta("maximumFractureIntersectionAmplification", std::to_string(maximumFractureIntersectionAmplification));
    out.setMeta("minimumFractureReactantAccess",
                std::to_string(fractureChannelAffectedVoxels > 0 ? minimumFractureReactantAccess : 0.f));
    out.setMeta("scallopErosion", std::to_string(scallopErosion));
    out.setMeta("scallopScale", std::to_string(scallopScale));
    out.setMeta("scallopHydraulicScaling", std::to_string(scallopHydraulicScaling));
    out.setMeta("scallopMaturity", std::to_string(scallopMaturity));
    out.setMeta("scallopScaleVariability", std::to_string(scallopScaleVariability));
    out.setMeta("scallopScaleDistribution",
                scallopScaleVariability > 0.f ? "correlated-lognormal-proxy-v1" : "uniform");
    out.setMeta("scallopFlowSeparation", std::to_string(scallopFlowSeparation));
    out.setMeta("scallopFlowProfile", scallopFlowSeparation > 0.f ? "slope-separated-travelling-wave-v2" : "legacy");
    out.setMeta("scallopFlowHistory", std::to_string(scallopFlowHistory));
    out.setMeta("scallopFlowHistoryModel",
                scallopFlowHistory <= 0.f ? "disabled" : "partitioned-base-flood-reversal-overprint-v1");
    out.setMeta("scallopHistoryAffectedVoxels", std::to_string(scallopHistoryAffectedVoxels));
    out.setMeta("maximumYoungerScallopErosion", std::to_string(maximumYoungerScallopErosion));
    out.setMeta("maximumYoungerScallopCoverage", std::to_string(maximumYoungerScallopCoverage));
    out.setMeta("maximumScallopReversalMask", std::to_string(maximumScallopReversalMask));
    out.setMeta("minimumSecondaryScallopScaleRatio",
                std::to_string(scallopHistoryAffectedVoxels > 0 ? minimumSecondaryScallopScaleRatio : 1.f));
    out.setMeta("scallopEvolutionModel", scallopMaturity > 0.f ? "normal-ablation-coarsening-v1" : "stationary-wave");
    out.setMeta("bendUndercut", std::to_string(bendUndercut));
    out.setMeta("bendErosionModel", bendUndercut > 0.f ? "curvature-outer-bank-v1" : "disabled");
    out.setMeta("fragmentDetachment", std::to_string(fragmentDetachment));
    out.setMeta("detachmentModel", fragmentDetachment > 0.f ? "host-rock-connectivity-v1" : "disabled");
    out.setMeta("unsupportedVoxels", std::to_string(detachment.unsupportedVoxels));
    out.setMeta("detachedVoxels", std::to_string(detachment.detachedVoxels));
    out.setMeta("curvatureDissolution", std::to_string(curvatureDissolution));
    out.setMeta("surfaceEvolutionModel", curvatureDissolution > 0.f ? "convex-normal-retreat-v1" : "disabled");
    out.setMeta("depositionAnchoringModel",
                dripstoneCount + flowstoneCount + curtainCount > 0 ? "post-erosion-zero-crossing-v1" : "disabled");
    out.setMeta("anchoredDripstonePairs", std::to_string(anchoredDripstonePairs));
    out.setMeta("rejectedDripstonePairs", std::to_string(rejectedDripstonePairs));
    out.setMeta("anchoredFlowstones", std::to_string(flowstones.size()));
    out.setMeta("rejectedFlowstones", std::to_string(rejectedFlowstones));
    out.setMeta("anchoredCurtains", std::to_string(curtains.size()));
    out.setMeta("rejectedCurtains", std::to_string(rejectedCurtains));
    out.setMeta("curvatureAffectedVoxels", std::to_string(surfaceEvolution.affectedVoxels));
    out.setMeta("maximumCurvatureRetreat", std::to_string(surfaceEvolution.maximumRetreat));
    out.setMeta("totalCurvatureRetreat", std::to_string(surfaceEvolution.totalRetreat));
    out.setMeta("reactiveSurfaceCoupling", std::to_string(reactiveSurfaceCoupling));
    out.setMeta("surfaceSlopeReactivity", std::to_string(surfaceSlopeReactivity));
    out.setMeta("surfaceReactivityModel",
                surfaceSlopeReactivity > 0.f ? "rotation-invariant-normal-dispersion-v1" : "disabled");
    out.setMeta("surfaceReactivityAffectedVoxels", std::to_string(surfaceReactivity.affectedVoxels));
    out.setMeta("maximumSurfaceReactivityRetreat", std::to_string(surfaceReactivity.maximumRetreat));
    out.setMeta("totalSurfaceReactivityRetreat", std::to_string(surfaceReactivity.totalRetreat));
    out.setMeta("maximumSurfaceNormalDispersion", std::to_string(surfaceReactivity.maximumNormalDispersion));
    out.setMeta("reactivePatchiness", std::to_string(reactivePatchiness));
    out.setMeta("reactivePatchModel", reactivePatchiness > 0.f ? "flow-aligned-correlated-psd-v2" : "disabled");
    out.setMeta("reactivePatchAffectedVoxels", std::to_string(reactivePatchEvolution.affectedVoxels));
    out.setMeta("maximumReactivePatchRetreat", std::to_string(reactivePatchEvolution.maximumRetreat));
    out.setMeta("totalReactivePatchRetreat", std::to_string(reactivePatchEvolution.totalRetreat));
    out.setMeta("minimumReactivePatchRate", std::to_string(reactivePatchEvolution.minimumPatchRate));
    out.setMeta("maximumReactivePatchRate", std::to_string(reactivePatchEvolution.maximumPatchRate));
    out.setMeta("reactivePatchNeighborCoherence", std::to_string(reactivePatchEvolution.meanNeighborCoherence));
    out.setMeta("reactivePatchFlowCoherence", std::to_string(reactivePatchEvolution.meanFlowCoherence));
    out.setMeta("reactivePatchTransverseCoherence", std::to_string(reactivePatchEvolution.meanTransverseCoherence));
    out.setMeta("reactivePatchChannelAnisotropy", std::to_string(reactivePatchEvolution.channelAnisotropy));
    out.setMeta("condensationCorrosion", std::to_string(condensationCorrosion));
    out.setMeta("condensationCorrosionModel",
                condensationCorrosion > 0.f ? "cool-wall-co2-film-pitting-v1" : "disabled");
    out.setMeta("condensationAffectedVoxels", std::to_string(condensation.affectedVoxels));
    out.setMeta("maximumCondensationRetreat", std::to_string(condensation.maximumRetreat));
    out.setMeta("totalCondensationRetreat", std::to_string(condensation.totalRetreat));
    out.setMeta("biogenicCorrosion", std::to_string(biogenicCorrosion));
    out.setMeta("biogenicCorrosionModel",
                biogenicCorrosion > 0.f ? "ammonia-nitrification-aero-speleogen-v1" : "disabled");
    out.setMeta("biogenicTransportModel",
                biogenicCorrosion > 0.f ? "passage-airflow-wet-film-protection-v1" : "disabled");
    out.setMeta("biogenicAffectedVoxels", std::to_string(biogenicAffectedVoxels));
    out.setMeta("minimumFluvialScallopRetention", std::to_string(minimumScallopRetention));
    out.setMeta("maximumBiogenicErosion", std::to_string(maximumBiogenicErosion));
    out.setMeta("totalBiogenicErosion", std::to_string(totalBiogenicErosion));
    out.setMeta("condensationFaceting", std::to_string(condensationFaceting));
    out.setMeta("condensationFacetModel",
                condensationFaceting > 0.f ? "local-convection-planar-envelope-v1" : "disabled");
    out.setMeta("condensationFacetCount",
                condensationFaceting > 0.f ? std::to_string(5 + int(params.getSeed() % 3u)) : "0");
    out.setMeta("facetAffectedVoxels", std::to_string(facetAffectedVoxels));
    out.setMeta("maximumFacetRetreat", std::to_string(maximumFacetRetreat));
    out.setMeta("differentialVeinErosion", std::to_string(differentialVeinErosion));
    out.setMeta("differentialVeinModel", differentialVeinErosion > 0.f ? "resistant-vein-host-retreat-v1" : "disabled");
    out.setMeta("differentialVeinAffectedVoxels", std::to_string(differentialVeinAffectedVoxels));
    out.setMeta("maximumDifferentialVeinRetreat", std::to_string(maximumDifferentialVeinRetreat));
    out.setMeta("maximumVeinProtection", std::to_string(maximumVeinProtection));
    out.setMeta("breakdown", std::to_string(breakdown));
    out.setMeta("breakdownModel", breakdown > 0.f ? "paired-ceiling-spall-talus-v1" : "disabled");
    out.setMeta("breakdownEvents", std::to_string(breakdownSet.events.size()));
    out.setMeta("breakdownBlocks", std::to_string(breakdownSet.blockCount));
    out.setMeta("breakdownDetachedVolume", std::to_string(breakdownSet.detachedVolume));
    out.setMeta("breakdownDepositedVolume", std::to_string(breakdownSet.depositedVolume));
    out.setMeta("sedimentDeposition", std::to_string(sedimentDeposition));
    out.setMeta("sedimentModel", sedimentDeposition > 0.f ? "longitudinal-bar-imbrication-v1" : "disabled");
    out.setMeta("sedimentBars", std::to_string(sedimentSet.bars.size()));
    out.setMeta("sedimentClasts", std::to_string(sedimentSet.clastCount));
    out.setMeta("sedimentVolume", std::to_string(sedimentSet.depositedVolume));
    out.setMeta("meanImbricationDegrees", std::to_string(sedimentSet.meanImbricationDegrees));
    out.setMeta("paragenesis", std::to_string(paragenesis));
    out.setMeta("paragenesisModel", paragenesis > 0.f ? "alluvial-notch-ceiling-half-tube-v2" : "disabled");
    out.setMeta("paragenesisStatus",
                paragenesis <= 0.f ? "disabled" : (sedimentSet.bars.empty() ? "inactive-no-sediment" : "applied"));
    out.setMeta("parageneticChannels", std::to_string(sedimentSet.parageneticChannels));
    out.setMeta("maximumParageneticLift", std::to_string(sedimentSet.maximumCeilingLift));
    out.setMeta("meanParageneticWidth", std::to_string(sedimentSet.meanParageneticWidth));
    out.setMeta("meanPalaeofillRatio", std::to_string(sedimentSet.meanPalaeofillRatio));
    out.setMeta("maximumAlluvialNotchRetreat", std::to_string(sedimentSet.maximumNotchRetreat));
    out.setMeta("meanAlluvialNotchThickness", std::to_string(sedimentSet.meanNotchThickness));
    out.setMeta("surfaceRateModel", reactiveSurfaceCoupling > 0.f ? "microstructure-hydraulic-access-v1" : "uniform");
    out.setMeta("minimumSurfaceRate", std::to_string(surfaceEvolution.minimumRateMultiplier));
    out.setMeta("maximumSurfaceRate", std::to_string(surfaceEvolution.maximumRateMultiplier));
    const float minimumScallopScale = scallopScale *
                                      (1.f + scallopHydraulicScaling * (1.f / std::sqrt(hydrology.maximum) - 1.f)) *
                                      std::exp(-0.38f * scallopScaleVariability);
    const float maximumScallopScale = scallopScale *
                                      (1.f + scallopHydraulicScaling * (1.f / std::sqrt(hydrology.minimum) - 1.f)) *
                                      std::exp(0.38f * scallopScaleVariability);
    out.setMeta("scallopGeometricStdDev", std::to_string(std::exp(0.38f * scallopScaleVariability)));
    out.setMeta("minimumScallopScale", std::to_string(minimumScallopScale));
    out.setMeta("maximumScallopScale", std::to_string(maximumScallopScale));
    out.setMeta("hydraulicErosion", std::to_string(hydraulicErosion));
    out.setMeta("hydraulicGradient", std::to_string(hydraulicGradient));
    out.setMeta("recharge", std::to_string(recharge));
    out.setMeta("flowFocusing", std::to_string(flowFocusing));
    out.setMeta("effectiveDamkohler", std::to_string(damkohler));
    out.setMeta("transportG", std::to_string(transportG));
    out.setMeta("dissolutionRegime", hydrology.dissolutionRegime);
    out.setMeta("reactantPenetration", std::to_string(hydrology.reactantPenetration));
    out.setMeta("microstructure", std::to_string(microstructure));
    out.setMeta("microporosityAccess", std::to_string(microporosityAccess));
    out.setMeta("permeabilityContrast", std::to_string(permeabilityContrast));
    out.setMeta("microstructureModel", microstructure > 0.f ? "dual-scale-accessibility-permeability-v1" : "disabled");
    out.setMeta("hydraulicNetwork", hydraulicErosion > 0.f ? "tributary-confluence-feedback-v1" : "disabled");
    out.setMeta("hydraulicConfluences", std::to_string(branches.size()));
    out.setMeta("minimumFlowWeight", std::to_string(hydrology.minimum));
    out.setMeta("maximumFlowWeight", std::to_string(hydrology.maximum));
    out.setMeta("surfaceRefinement", std::to_string(surfaceRefinement));
    out.setMeta("isosurfaceSampling", std::to_string(isosurfaceSampling));
    out.setMeta("isosurfaceReconstruction", isosurfaceSampling > 1 ? "trilinear-supersample-v1" : "native-grid");
    out.setMeta("extractionResolution",
                std::to_string(extractionNx) + "x" + std::to_string(extractionNy) + "x" + std::to_string(extractionNz));
    out.setMeta("extractionVoxels", std::to_string(extractionDensity->size()));
    out.setMeta("surfaceProjection", surfaceRefinement > 0 ? "trilinear-newton-v1" : "none");
    out.setMeta("refinementTriangulation", surfaceRefinement == 2
                                               ? "conforming-edge-mask-v2"
                                               : (surfaceRefinement == 1 ? "symmetric-four-way-v1" : "none"));
    out.setMeta("refinementThreshold", std::to_string(refinementThreshold));
    out.setMeta("refinedSourceTriangles", std::to_string(refinedSourceTriangles));
    out.setMeta("adaptiveSplitEdges", std::to_string(adaptiveSplitEdges));
    out.setMeta("dripstones", std::to_string(dripstoneCount));
    out.setMeta("columns", std::to_string(columnCount));
    out.setMeta("flowstones", std::to_string(flowstoneCount));
    out.setMeta("curtains", std::to_string(curtainCount));
    out.setMeta("depositionModel", "damkohler-thin-film-ripple-v2");
    out.setMeta("normalSmoothing", std::to_string(normalSmoothing));
    out.setMeta("surfaceNormalMode", surfaceNormalMode);
    out.setMeta("wetnessRefinement", std::to_string(wetnessRefinement));
    out.setMeta("wetnessModel", wetnessRefinement > 0 ? "gravity-drainage-contour-v2" : "drainage-proximity-v1");
    out.setMeta("wetnessBoundaryTriangles", std::to_string(wetness.boundaryTriangles));
    out.setMeta("wetnessAddedTriangles", std::to_string(wetness.addedTriangles));
    out.setMeta("boundaryClosure", std::to_string(boundaryClosure));
    out.setMeta("boundaryClosureModel", boundaryClosure > 0.f ? "rough-host-envelope-v1" : "open-domain");
    out.setMeta("boundaryAirSamplesBefore", std::to_string(boundary.airSamplesBefore));
    out.setMeta("boundaryAirSamplesAfter", std::to_string(boundary.airSamplesAfter));
    out.setMeta("boundaryClosureChangedVoxels", std::to_string(boundary.changedVoxels));
    out.setMeta("determinism", "bit-exact-cpu");
    return true;
}

}  // namespace

MeshRecipeFn caveMeshGenerator() { return generateCaveMesh; }

}  // namespace eve::procgen
