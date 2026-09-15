#include "graphics/hair/Procedural.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics::hair {
namespace {

struct Triangle {
    uint32_t i0 = 0, i1 = 0, i2 = 0;
    float area = 0.f;
    glm::vec3 n{0.f, 1.f, 0.f};
};

glm::vec3 readPos(const float *pos, int i) {
    return glm::vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
}

glm::vec3 readNrm(const float *nrm, int i) {
    if (!nrm) return glm::vec3(0.f, 1.f, 0.f);
    return glm::vec3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
}

uint32_t mixSeed(uint32_t seed, uint32_t i) {
    seed ^= 0x9e3779b9u + (i << 6) + (i >> 2);
    seed *= 0x85ebca6bu;
    return seed ^ (seed >> 13);
}

float hash01(uint32_t s) {
    s ^= s >> 16;
    s *= 0x7feb352du;
    s ^= s >> 15;
    s *= 0x846ca68bu;
    s ^= s >> 16;
    return float(s >> 8) * (1.f / 16777215.f);
}

bool buildTriangles(const float *posXYZ, const float *nrmXYZ, int vertexCount, const uint32_t *indices,
                    int indexCount, float minSlopeDot, std::vector<Triangle> &tris,
                    std::vector<float> &cdf) {
    tris.clear();
    cdf.clear();
    if (!posXYZ || !indices || vertexCount < 3 || indexCount < 3 || (indexCount % 3) != 0) return false;

    float total = 0.f;
    for (int t = 0; t + 2 < indexCount; t += 3) {
        const uint32_t i0 = indices[t];
        const uint32_t i1 = indices[t + 1];
        const uint32_t i2 = indices[t + 2];
        if (int(i0) >= vertexCount || int(i1) >= vertexCount || int(i2) >= vertexCount) continue;
        const glm::vec3 a = readPos(posXYZ, int(i0));
        const glm::vec3 b = readPos(posXYZ, int(i1));
        const glm::vec3 c = readPos(posXYZ, int(i2));
        glm::vec3 n = glm::cross(b - a, c - a);
        const float twice = glm::length(n);
        if (twice < 1e-10f) continue;
        n /= twice;
        if (nrmXYZ) {
            glm::vec3 ns =
                readNrm(nrmXYZ, int(i0)) + readNrm(nrmXYZ, int(i1)) + readNrm(nrmXYZ, int(i2));
            if (glm::dot(ns, ns) > 1e-8f) n = glm::normalize(ns);
        }
        if (n.y < minSlopeDot) continue;
        Triangle tri;
        tri.i0 = i0;
        tri.i1 = i1;
        tri.i2 = i2;
        tri.area = 0.5f * twice;
        tri.n = n;
        total += tri.area;
        tris.push_back(tri);
        cdf.push_back(total);
    }
    return total > 1e-12f && !tris.empty();
}

int pickTriangle(const std::vector<float> &cdf, float u) {
    const float target = u * cdf.back();
    auto it = std::lower_bound(cdf.begin(), cdf.end(), target);
    int idx = int(it - cdf.begin());
    if (idx >= int(cdf.size())) idx = int(cdf.size()) - 1;
    return idx;
}

glm::vec3 sampleOnTriangle(const float *posXYZ, const Triangle &tri, float u, float v) {
    if (u + v > 1.f) {
        u = 1.f - u;
        v = 1.f - v;
    }
    const glm::vec3 a = readPos(posXYZ, int(tri.i0));
    const glm::vec3 b = readPos(posXYZ, int(tri.i1));
    const glm::vec3 c = readPos(posXYZ, int(tri.i2));
    return a + u * (b - a) + v * (c - a);
}

}  // namespace

Result<StrandsDatas> generateOnMesh(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                                    const uint32_t *indices, int indexCount,
                                    const ProceduralParams &params) {
    if (!posXYZ || !indices) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generateOnMesh: null mesh buffers", "hair.procedural"));
    }
    if (params.strandCount <= 0 || params.pointsPerStrand < 2) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generateOnMesh: strandCount/pointsPerStrand invalid",
            "hair.procedural"));
    }
    if (!(params.length > 0.f) || !(params.rootRadius >= 0.f) || !(params.tipRadius >= 0.f)) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generateOnMesh: non-positive length/radius",
            "hair.procedural"));
    }

    std::vector<Triangle> tris;
    std::vector<float> cdf;
    if (!buildTriangles(posXYZ, nrmXYZ, vertexCount, indices, indexCount, params.minSlopeDot, tris,
                        cdf)) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generateOnMesh: no usable triangles",
            "hair.procedural.mesh"));
    }

    StrandsDatas out;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    points.reserve(size_t(params.strandCount) * size_t(params.pointsPerStrand));
    curves.reserve(size_t(params.strandCount));

    for (int s = 0; s < params.strandCount; ++s) {
        const uint32_t seed = mixSeed(params.seed, uint32_t(s));
        const float uPick = hash01(seed);
        const float uBary = hash01(mixSeed(seed, 1u));
        const float vBary = hash01(mixSeed(seed, 2u));
        const Triangle &tri = tris[size_t(pickTriangle(cdf, uPick))];

        float bu = uBary;
        float bv = vBary;
        if (bu + bv > 1.f) {
            bu = 1.f - bu;
            bv = 1.f - bv;
        }
        const float bw = 1.f - bu - bv;
        const glm::vec3 root = sampleOnTriangle(posXYZ, tri, bu, bv);

        glm::vec3 n = tri.n;
        if (nrmXYZ) {
            n = readNrm(nrmXYZ, int(tri.i0)) * bw + readNrm(nrmXYZ, int(tri.i1)) * bu +
                readNrm(nrmXYZ, int(tri.i2)) * bv;
            if (glm::dot(n, n) > 1e-8f)
                n = glm::normalize(n);
            else
                n = tri.n;
        }

        glm::vec3 tangent = glm::cross(n, glm::vec3(0.f, 0.f, 1.f));
        if (glm::dot(tangent, tangent) < 1e-8f) tangent = glm::cross(n, glm::vec3(1.f, 0.f, 0.f));
        tangent = glm::normalize(tangent);
        const glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

        const float len =
            params.length + (hash01(mixSeed(seed, 3u)) * 2.f - 1.f) * params.lengthJitter;
        const float safeLen = std::max(len, params.length * 0.25f);
        const float curlPhase = hash01(mixSeed(seed, 4u)) * 6.2831853f;
        const float curlAmp = params.curlStrength * (0.5f + hash01(mixSeed(seed, 5u)));

        StrandCurve curve;
        curve.pointOffset = uint32_t(points.size());
        curve.pointCount = uint32_t(params.pointsPerStrand);
        float accum = 0.f;
        glm::vec3 prev = root;
        for (int p = 0; p < params.pointsPerStrand; ++p) {
            const float t = float(p) / float(params.pointsPerStrand - 1);
            const float along = t * safeLen;
            const float curl = curlAmp * std::sin(curlPhase + t * 6.2831853f * 1.5f);
            const glm::vec3 pos =
                root + n * along + tangent * (curl * t) + bitangent * (curl * 0.35f * t);
            if (p > 0) accum += glm::length(pos - prev);
            prev = pos;
            StrandPoint sp;
            sp.position = pos;
            sp.u = t;
            sp.radius = params.rootRadius * (1.f - t) + params.tipRadius * t;
            points.push_back(sp);
        }
        curve.length = accum;
        curves.push_back(curve);
    }

    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto check = out.validate();
    if (!check.ok()) return Result<StrandsDatas>::failure(check.status());
    return Result<StrandsDatas>::success(std::move(out));
}

Result<StrandsDatas> generateOnPlane(float sizeX, float sizeZ, const ProceduralParams &params) {
    if (!(sizeX > 0.f) || !(sizeZ > 0.f)) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generateOnPlane: non-positive size",
            "hair.procedural.plane"));
    }
    const float hx = sizeX * 0.5f;
    const float hz = sizeZ * 0.5f;
    const float pos[12] = {-hx, 0.f, -hz, hx, 0.f, -hz, hx, 0.f, hz, -hx, 0.f, hz};
    const float nrm[12] = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    return generateOnMesh(pos, nrm, 4, idx, 6, params);
}

}  // namespace eve::graphics::hair
