#include "animation/AnimLattice.h"

#include "common/Exception.h"
#include "model3d/ModelData.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>

namespace eve::animation {

namespace {

/** @brief Cell coordinate along one axis: base index i0 and fractional fu in [0,1]. */
void computeAxis(float u, int divisions, bool clamp, int& i0, float& fu) {
    const float scaled = u * static_cast<float>(divisions - 1);
    if (clamp) {
        if (scaled <= 0.f) {
            i0 = 0;
            fu = 0.f;
            return;
        }
        if (scaled >= static_cast<float>(divisions - 1)) {
            i0 = divisions - 2;
            fu = 1.f;
            return;
        }
    }
    i0 = static_cast<int>(std::floor(scaled));
    fu = scaled - static_cast<float>(i0);
    if (i0 < 0) {
        i0 = 0;
    } else if (i0 > divisions - 2) {
        i0 = divisions - 2;
    }
}

}  // namespace

AnimLattice::AnimLattice() { setDivisions(kMinDivisions, kMinDivisions, kMinDivisions); }

AnimLattice::AnimLattice(int divX, int divY, int divZ) { setDivisions(divX, divY, divZ); }

void AnimLattice::setDivisions(int divX, int divY, int divZ) {
    if (divX < kMinDivisions || divY < kMinDivisions || divZ < kMinDivisions) {
        throw Exception("AnimLattice.setDivisions: divisions must be >= %d", kMinDivisions);
    }
    divX_ = divX;
    divY_ = divY;
    divZ_ = divZ;
    points_.assign(static_cast<size_t>(divX_) * static_cast<size_t>(divY_) * static_cast<size_t>(divZ_),
                   ControlPoint{});
    clearBind();
}

void AnimLattice::setSize(float sx, float sy, float sz) {
    if (sx <= 0.f || sy <= 0.f || sz <= 0.f) {
        throw Exception("AnimLattice.setSize: size components must be > 0");
    }
    sizeX_ = sx;
    sizeY_ = sy;
    sizeZ_ = sz;
}

void AnimLattice::setOrigin(float ox, float oy, float oz) {
    originX_ = ox;
    originY_ = oy;
    originZ_ = oz;
}

int AnimLattice::pointIndex(int ix, int iy, int iz) const { return (iz * divY_ + iy) * divX_ + ix; }

void AnimLattice::requirePoint(int ix, int iy, int iz) const {
    if (ix < 0 || ix >= divX_ || iy < 0 || iy >= divY_ || iz < 0 || iz >= divZ_) {
        throw Exception("AnimLattice: invalid control point (%d,%d,%d) for divisions (%d,%d,%d)", ix, iy, iz, divX_,
                        divY_, divZ_);
    }
}

void AnimLattice::requireVertex(int vertexIndex) const {
    if (vertexIndex < 0 || vertexIndex >= vertexCount_) {
        throw Exception("AnimLattice: invalid vertex index %d", vertexIndex);
    }
}

void AnimLattice::setPointScale(int ix, int iy, int iz, float sx, float sy, float sz) {
    requirePoint(ix, iy, iz);
    ControlPoint& p = points_[static_cast<size_t>(pointIndex(ix, iy, iz))];
    p.sx            = sx;
    p.sy            = sy;
    p.sz            = sz;
}

void AnimLattice::setPointOffset(int ix, int iy, int iz, float dx, float dy, float dz) {
    requirePoint(ix, iy, iz);
    ControlPoint& p = points_[static_cast<size_t>(pointIndex(ix, iy, iz))];
    p.ox            = dx;
    p.oy            = dy;
    p.oz            = dz;
}

void AnimLattice::setScale(float sx, float sy, float sz) {
    for (ControlPoint& p : points_) {
        p.sx = sx;
        p.sy = sy;
        p.sz = sz;
    }
}

void AnimLattice::reset() { std::fill(points_.begin(), points_.end(), ControlPoint{}); }

float AnimLattice::getPointScaleX(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].sx;
}
float AnimLattice::getPointScaleY(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].sy;
}
float AnimLattice::getPointScaleZ(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].sz;
}
float AnimLattice::getPointOffsetX(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].ox;
}
float AnimLattice::getPointOffsetY(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].oy;
}
float AnimLattice::getPointOffsetZ(int ix, int iy, int iz) const {
    requirePoint(ix, iy, iz);
    return points_[static_cast<size_t>(pointIndex(ix, iy, iz))].oz;
}

AnimLattice* AnimLattice::fromModel(const model3d::ModelData* model, int meshIndex, int divX, int divY, int divZ) {
    if (!model) {
        throw Exception("AnimLattice.fromModel: null model");
    }
    const aiMesh* mesh = model->getMesh(meshIndex);
    if (!mesh) {
        throw Exception("AnimLattice.fromModel: invalid mesh index %d", meshIndex);
    }
    if (mesh->mNumVertices == 0 || !mesh->mVertices) {
        throw Exception("AnimLattice.fromModel: mesh %d has no vertices", meshIndex);
    }
    auto* lattice = new AnimLattice(divX, divY, divZ);
    lattice->bindPositions(reinterpret_cast<const float*>(mesh->mVertices), static_cast<int>(mesh->mNumVertices));
    return lattice;
}

void AnimLattice::bindModel(const model3d::ModelData* model, int meshIndex) {
    if (!model) {
        throw Exception("AnimLattice.bindModel: null model");
    }
    const aiMesh* mesh = model->getMesh(meshIndex);
    if (!mesh) {
        throw Exception("AnimLattice.bindModel: invalid mesh index %d", meshIndex);
    }
    if (mesh->mNumVertices == 0 || !mesh->mVertices) {
        throw Exception("AnimLattice.bindModel: mesh %d has no vertices", meshIndex);
    }
    bindPositions(reinterpret_cast<const float*>(mesh->mVertices), static_cast<int>(mesh->mNumVertices));
}

void AnimLattice::bindPositions(const float* posXYZ, int count) {
    if (!posXYZ || count < 0) {
        throw Exception("AnimLattice.bindPositions: invalid position data");
    }
    vertexCount_ = count;
    bindPos_.resize(static_cast<size_t>(count) * 3u);
    if (count > 0) {
        std::copy(posXYZ, posXYZ + static_cast<size_t>(count) * 3u, bindPos_.begin());
    }
    deformedValid_    = false;
    deformedNrmValid_ = false;
}

void AnimLattice::clearBind() {
    vertexCount_ = 0;
    bindPos_.clear();
    deformedValid_    = false;
    deformedNrmValid_ = false;
}

float AnimLattice::getBindPositionX(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 0];
}
float AnimLattice::getBindPositionY(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 1];
}
float AnimLattice::getBindPositionZ(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 2];
}

void AnimLattice::computeCell(float x, float y, float z, int& i0, int& j0, int& k0, float& fu, float& fv,
                              float& fw) const {
    const float u = (x - (originX_ - sizeX_ * 0.5f)) / sizeX_;
    const float v = (y - (originY_ - sizeY_ * 0.5f)) / sizeY_;
    const float w = (z - (originZ_ - sizeZ_ * 0.5f)) / sizeZ_;
    computeAxis(u, divX_, clamp_, i0, fu);
    computeAxis(v, divY_, clamp_, j0, fv);
    computeAxis(w, divZ_, clamp_, k0, fw);
}

void AnimLattice::deformPositions(const float* inPosXYZ, float* outPosXYZ, int count) const {
    if (!inPosXYZ || !outPosXYZ) {
        throw Exception("AnimLattice.deformPositions: null position buffer");
    }
    if (count < 0) {
        throw Exception("AnimLattice.deformPositions: negative vertex count");
    }
    for (int v = 0; v < count; ++v) {
        const float x = inPosXYZ[static_cast<size_t>(v) * 3u + 0];
        const float y = inPosXYZ[static_cast<size_t>(v) * 3u + 1];
        const float z = inPosXYZ[static_cast<size_t>(v) * 3u + 2];

        int   i0, j0, k0;
        float fu, fv, fw;
        computeCell(x, y, z, i0, j0, k0, fu, fv, fw);

        float ox = 0.f, oy = 0.f, oz = 0.f;
        float sx = 0.f, sy = 0.f, sz = 0.f;
        for (int di = 0; di < 2; ++di) {
            const float wu = di ? fu : 1.f - fu;
            for (int dj = 0; dj < 2; ++dj) {
                const float wv = dj ? fv : 1.f - fv;
                for (int dk = 0; dk < 2; ++dk) {
                    const float         ww     = dk ? fw : 1.f - fw;
                    const float         weight = wu * wv * ww;
                    const ControlPoint& p      = points_[static_cast<size_t>(pointIndex(i0 + di, j0 + dj, k0 + dk))];
                    ox += p.ox * weight;
                    oy += p.oy * weight;
                    oz += p.oz * weight;
                    sx += p.sx * weight;
                    sy += p.sy * weight;
                    sz += p.sz * weight;
                }
            }
        }

        const float rx                             = x - originX_;
        const float ry                             = y - originY_;
        const float rz                             = z - originZ_;
        outPosXYZ[static_cast<size_t>(v) * 3u + 0] = originX_ + ox + sx * rx;
        outPosXYZ[static_cast<size_t>(v) * 3u + 1] = originY_ + oy + sy * ry;
        outPosXYZ[static_cast<size_t>(v) * 3u + 2] = originZ_ + oz + sz * rz;
    }
}

bool AnimLattice::deformPositionsTo(const std::vector<float>& inPosXYZ, std::vector<float>& outPosXYZ) const {
    if (inPosXYZ.empty() || inPosXYZ.size() % 3u != 0) return false;
    const int count = static_cast<int>(inPosXYZ.size() / 3u);
    outPosXYZ.resize(inPosXYZ.size());
    deformPositions(inPosXYZ.data(), outPosXYZ.data(), count);
    return true;
}

bool AnimLattice::deformNormals(const float* posXYZ, const float* inNrmXYZ, float* outNrmXYZ, int count) const {
    if (!posXYZ || !inNrmXYZ || !outNrmXYZ || count < 0) return false;
    if (vertexCount_ > 0 && count != vertexCount_) return false;
    for (int v = 0; v < count; ++v) {
        const float px = posXYZ[static_cast<size_t>(v) * 3u + 0];
        const float py = posXYZ[static_cast<size_t>(v) * 3u + 1];
        const float pz = posXYZ[static_cast<size_t>(v) * 3u + 2];

        int   i0, j0, k0;
        float fu, fv, fw;
        computeCell(px, py, pz, i0, j0, k0, fu, fv, fw);

        float sx = 0.f, sy = 0.f, sz = 0.f;
        for (int di = 0; di < 2; ++di) {
            const float wu = di ? fu : 1.f - fu;
            for (int dj = 0; dj < 2; ++dj) {
                const float wv = dj ? fv : 1.f - fv;
                for (int dk = 0; dk < 2; ++dk) {
                    const float         ww     = dk ? fw : 1.f - fw;
                    const float         weight = wu * wv * ww;
                    const ControlPoint& p      = points_[static_cast<size_t>(pointIndex(i0 + di, j0 + dj, k0 + dk))];
                    sx += p.sx * weight;
                    sy += p.sy * weight;
                    sz += p.sz * weight;
                }
            }
        }

        const float nx  = sx * inNrmXYZ[static_cast<size_t>(v) * 3u + 0];
        const float ny  = sy * inNrmXYZ[static_cast<size_t>(v) * 3u + 1];
        const float nz  = sz * inNrmXYZ[static_cast<size_t>(v) * 3u + 2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            outNrmXYZ[static_cast<size_t>(v) * 3u + 0] = nx / len;
            outNrmXYZ[static_cast<size_t>(v) * 3u + 1] = ny / len;
            outNrmXYZ[static_cast<size_t>(v) * 3u + 2] = nz / len;
        } else {
            outNrmXYZ[static_cast<size_t>(v) * 3u + 0] = inNrmXYZ[static_cast<size_t>(v) * 3u + 0];
            outNrmXYZ[static_cast<size_t>(v) * 3u + 1] = inNrmXYZ[static_cast<size_t>(v) * 3u + 1];
            outNrmXYZ[static_cast<size_t>(v) * 3u + 2] = inNrmXYZ[static_cast<size_t>(v) * 3u + 2];
        }
    }
    return true;
}

bool AnimLattice::updateDeformedPositions(const std::vector<float>& inPosXYZ) {
    if (vertexCount_ <= 0 || inPosXYZ.size() < static_cast<size_t>(vertexCount_) * 3u) {
        deformedValid_ = false;
        return false;
    }
    deformedPos_.resize(static_cast<size_t>(vertexCount_) * 3u);
    deformPositions(inPosXYZ.data(), deformedPos_.data(), vertexCount_);
    deformedValid_ = true;
    return true;
}

bool AnimLattice::updateDeformedPositions() {
    if (vertexCount_ <= 0) {
        deformedValid_ = false;
        return false;
    }
    return updateDeformedPositions(bindPos_);
}

bool AnimLattice::updateDeformedNormals(const std::vector<float>& posXYZ, const std::vector<float>& inNrmXYZ) {
    if (vertexCount_ <= 0 || posXYZ.size() < static_cast<size_t>(vertexCount_) * 3u ||
        inNrmXYZ.size() < static_cast<size_t>(vertexCount_) * 3u) {
        deformedNrmValid_ = false;
        return false;
    }
    deformedNrm_.resize(static_cast<size_t>(vertexCount_) * 3u);
    if (!deformNormals(posXYZ.data(), inNrmXYZ.data(), deformedNrm_.data(), vertexCount_)) {
        deformedNrmValid_ = false;
        return false;
    }
    deformedNrmValid_ = true;
    return true;
}

float AnimLattice::getDeformedPositionX(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!deformedValid_) {
        throw Exception("AnimLattice.getDeformedPositionX: call updateDeformedPositions first");
    }
    return deformedPos_[static_cast<size_t>(vertexIndex) * 3u + 0];
}
float AnimLattice::getDeformedPositionY(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!deformedValid_) {
        throw Exception("AnimLattice.getDeformedPositionY: call updateDeformedPositions first");
    }
    return deformedPos_[static_cast<size_t>(vertexIndex) * 3u + 1];
}
float AnimLattice::getDeformedPositionZ(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!deformedValid_) {
        throw Exception("AnimLattice.getDeformedPositionZ: call updateDeformedPositions first");
    }
    return deformedPos_[static_cast<size_t>(vertexIndex) * 3u + 2];
}

std::vector<float> AnimLattice::getDeformedPositions() const {
    return deformedValid_ ? deformedPos_ : std::vector<float>();
}

std::vector<float> AnimLattice::getDeformedNormals() const {
    return deformedNrmValid_ ? deformedNrm_ : std::vector<float>();
}

}  // namespace eve::animation
