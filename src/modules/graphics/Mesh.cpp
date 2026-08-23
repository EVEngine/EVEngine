#include "graphics/Mesh.h"

#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {

void Mesh::computeBounds(const float *posXYZ, int vertexCount) {
    boundsCx     = 0.f;
    boundsCy     = 0.f;
    boundsCz     = 0.f;
    boundsRadius = 0.f;
    if (!posXYZ || vertexCount <= 0) return;

    const size_t n = size_t(vertexCount);
    glm::vec3    c(0.f);
    for (size_t i = 0; i < n; ++i) {
        c.x += posXYZ[i * 3u + 0u];
        c.y += posXYZ[i * 3u + 1u];
        c.z += posXYZ[i * 3u + 2u];
    }
    c /= float(n);
    boundsCx = c.x;
    boundsCy = c.y;
    boundsCz = c.z;

    float r = 0.f;
    for (size_t i = 0; i < n; ++i) {
        const float dx = posXYZ[i * 3u + 0u] - c.x;
        const float dy = posXYZ[i * 3u + 1u] - c.y;
        const float dz = posXYZ[i * 3u + 2u] - c.z;
        const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > r) r = d;
    }
    // Degenerate (single-point) meshes still get a tiny non-zero sphere so
    // hasBounds() stays meaningful and the culler never drops point geometry.
    boundsRadius = r > 0.f ? r : 1e-4f;
}

void Mesh::drawOcclusion(Graphics *gfx, const glm::mat4 &matrix) const {
    if (!gfx || !getCastOcclusion()) return;
    const float x = matrix[3][0];
    const float y = matrix[3][1];
    const float w = std::sqrt(matrix[0][0] * matrix[0][0] + matrix[0][1] * matrix[0][1]);
    const float h = std::sqrt(matrix[1][0] * matrix[1][0] + matrix[1][1] * matrix[1][1]);
    if (w <= 0.f || h <= 0.f) return;
    gfx->drawOcclusionSolid(x, y, w, h);
}

void Mesh::clearMorphData() {
    basePos_.clear();
    baseNrm_.clear();
    baseUv_.clear();
    morphs_.clear();
    morphWeights_.clear();
    morphDirty_ = false;
}

void Mesh::initMorphBase(int vertexCount, const float *posXYZ, const float *nrmXYZ,
                         const float *uvST) {
    clearMorphData();
    if (vertexCount <= 0 || !posXYZ) return;
    const size_t n = size_t(vertexCount);
    basePos_.assign(posXYZ, posXYZ + n * 3);
    if (nrmXYZ)
        baseNrm_.assign(nrmXYZ, nrmXYZ + n * 3);
    else
        baseNrm_.assign(n * 3, 0.f);
    if (uvST)
        baseUv_.assign(uvST, uvST + n * 2);
    else
        baseUv_.assign(n * 2, 0.f);
}

bool Mesh::addMorphTarget(const std::string &name, const float *deltaPosXYZ) {
    if (name.empty() || !deltaPosXYZ || basePos_.empty()) return false;
    if (hasMorph(name)) return false;
    MorphTarget t;
    t.name = name;
    t.deltaPos.assign(deltaPosXYZ, deltaPosXYZ + basePos_.size());
    morphs_.push_back(std::move(t));
    morphWeights_[name] = 0.f;
    morphDirty_ = true;
    return true;
}

bool Mesh::addMorphTargetAbsolute(const std::string &name, const float *absPosXYZ) {
    if (name.empty() || !absPosXYZ || basePos_.empty()) return false;
    if (hasMorph(name)) return false;
    MorphTarget t;
    t.name = name;
    t.deltaPos.resize(basePos_.size());
    for (size_t i = 0; i < basePos_.size(); ++i)
        t.deltaPos[i] = absPosXYZ[i] - basePos_[i];
    morphs_.push_back(std::move(t));
    morphWeights_[name] = 0.f;
    morphDirty_ = true;
    return true;
}

int Mesh::getVertexCount() const {
    if (!basePos_.empty()) return int(basePos_.size() / 3);
    return gpuVertexCount;
}

int Mesh::getMorphCount() const { return int(morphs_.size()); }

std::string Mesh::getMorphName(int index) const {
    if (index < 0 || size_t(index) >= morphs_.size()) return {};
    return morphs_[size_t(index)].name;
}

bool Mesh::hasMorph(const std::string &name) const {
    return morphWeights_.find(name) != morphWeights_.end();
}

bool Mesh::setMorphWeight(const std::string &name, float weight) {
    auto it = morphWeights_.find(name);
    if (it == morphWeights_.end()) return false;
    if (weight < 0.f) weight = 0.f;
    if (weight > 1.f) weight = 1.f;
    if (std::fabs(it->second - weight) > 1e-6f) {
        it->second = weight;
        morphDirty_ = true;
    }
    return true;
}

float Mesh::getMorphWeight(const std::string &name) const {
    auto it = morphWeights_.find(name);
    return it == morphWeights_.end() ? 0.f : it->second;
}

void Mesh::clearMorphWeights() {
    for (auto &kv : morphWeights_) {
        if (kv.second != 0.f) {
            kv.second = 0.f;
            morphDirty_ = true;
        }
    }
}

void Mesh::computeMorphedPositions(std::vector<float> &outPos, std::vector<float> &outNrm) const {
    outPos = basePos_;
    outNrm = baseNrm_;
    if (outPos.empty()) return;
    for (const MorphTarget &t : morphs_) {
        auto it = morphWeights_.find(t.name);
        const float w = (it == morphWeights_.end()) ? 0.f : it->second;
        if (std::fabs(w) < 1e-8f) continue;
        for (size_t i = 0; i < outPos.size() && i < t.deltaPos.size(); ++i)
            outPos[i] += t.deltaPos[i] * w;
    }
}

}  // namespace eve::graphics
