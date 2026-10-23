#include "animation/AnimSkin.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"


#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace eve::animation {

namespace {

/**
 * Cofactor matrix (adjugate-transpose) of the upper 3x3 of a column-major Mat4.
 * (M^{-1})^T = cof(M) / det(M), and the scalar 1/det cancels under
 * normalization, so normals transform as n' = normalize(cof(M) * n).
 */
void normalMatrixCofactor(const Mat4& m, float out[9]) {
    // Row-major 3x3 linear part of the skin matrix.
    const float r0x = m.m[0], r0y = m.m[4], r0z = m.m[8];
    const float r1x = m.m[1], r1y = m.m[5], r1z = m.m[9];
    const float r2x = m.m[2], r2y = m.m[6], r2z = m.m[10];
    out[0] = r1y * r2z - r1z * r2y;
    out[1] = r1z * r2x - r1x * r2z;
    out[2] = r1x * r2y - r1y * r2x;
    out[3] = r2y * r0z - r2z * r0y;
    out[4] = r2z * r0x - r2x * r0z;
    out[5] = r2x * r0y - r2y * r0x;
    out[6] = r0y * r1z - r0z * r1y;
    out[7] = r0z * r1x - r0x * r1z;
    out[8] = r0x * r1y - r0y * r1x;
}

}  // namespace

void AnimSkin::requireVertex(int vertexIndex) const {
    if (vertexIndex < 0 || vertexIndex >= vertexCount_) {
        throw Exception("AnimSkin: invalid vertex index %d", vertexIndex);
    }
}

void AnimSkin::requireSkinBone(int skinBoneIndex) const {
    if (skinBoneIndex < 0 || skinBoneIndex >= getBoneCount()) {
        throw Exception("AnimSkin: invalid skin bone index %d", skinBoneIndex);
    }
}

Result<std::unique_ptr<AnimSkin>> AnimSkin::fromStreams(AnimSkinStreamData streams) {
    auto fail = [](const char* message) {
        return Result<std::unique_ptr<AnimSkin>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, message, "animation.skin.streams"));
    };
    const std::size_t vertices = streams.vertexCount > 0 ? static_cast<std::size_t>(streams.vertexCount) : 0U;
    const std::size_t joints = streams.skeletonBones.size();
    if (vertices == 0 || vertices > 8U * 1024U * 1024U ||
        streams.bindPositions.size() != vertices * 3U ||
        (!streams.bindNormals.empty() && streams.bindNormals.size() != vertices * 3U) ||
        joints == 0 || joints > 65535U || streams.boneNames.size() != joints ||
        streams.inverseBindMatrices.size() != joints ||
        streams.vertexSkinJoints.size() != vertices * kMaxInfluences ||
        streams.vertexWeights.size() != vertices * kMaxInfluences)
        return fail("AnimSkin native stream sizes are invalid");
    for (float value : streams.bindPositions) if (!std::isfinite(value)) return fail("AnimSkin position is non-finite");
    for (float value : streams.bindNormals) if (!std::isfinite(value)) return fail("AnimSkin normal is non-finite");
    for (std::size_t joint = 0; joint < joints; ++joint) {
        if (streams.skeletonBones[joint] < 0 || streams.boneNames[joint].empty())
            return fail("AnimSkin joint identity is invalid");
        for (float value : streams.inverseBindMatrices[joint])
            if (!std::isfinite(value)) return fail("AnimSkin inverse bind is non-finite");
    }
    for (std::size_t index = 0; index < streams.vertexWeights.size(); ++index) {
        const int joint = streams.vertexSkinJoints[index];
        const float weight = streams.vertexWeights[index];
        if (!std::isfinite(weight) || weight < 0.F ||
            (weight > 0.F && (joint < 0 || static_cast<std::size_t>(joint) >= joints)) ||
            (weight == 0.F && joint >= static_cast<int>(joints)))
            return fail("AnimSkin influence is invalid");
    }
    auto skin = std::make_unique<AnimSkin>();
    skin->vertexCount_ = streams.vertexCount;
    skin->bindPos_ = std::move(streams.bindPositions);
    skin->bindNrm_ = std::move(streams.bindNormals);
    skin->skeletonBone_ = std::move(streams.skeletonBones);
    skin->skinBoneNames_ = std::move(streams.boneNames);
    skin->inverseBind_.resize(joints);
    for (std::size_t joint = 0; joint < joints; ++joint)
        std::copy(streams.inverseBindMatrices[joint].begin(), streams.inverseBindMatrices[joint].end(),
                  skin->inverseBind_[joint].m);
    skin->influences_.resize(vertices * kMaxInfluences);
    for (std::size_t index = 0; index < skin->influences_.size(); ++index) {
        skin->influences_[index].bone = streams.vertexWeights[index] > 0.F ? streams.vertexSkinJoints[index] : -1;
        skin->influences_[index].weight = streams.vertexWeights[index];
    }
    return Result<std::unique_ptr<AnimSkin>>::success(std::move(skin));
}

int AnimSkin::getSkeletonBone(int skinBoneIndex) const {
    requireSkinBone(skinBoneIndex);
    return skeletonBone_[static_cast<size_t>(skinBoneIndex)];
}

std::string AnimSkin::getSkinBoneName(int skinBoneIndex) const {
    requireSkinBone(skinBoneIndex);
    return skinBoneNames_[static_cast<size_t>(skinBoneIndex)];
}

float AnimSkin::getInverseBindElement(int skinBoneIndex, int elementIndex) const {
    requireSkinBone(skinBoneIndex);
    if (elementIndex < 0 || elementIndex > 15) {
        throw Exception("AnimSkin.getInverseBindElement: elementIndex must be 0..15");
    }
    return inverseBind_[static_cast<size_t>(skinBoneIndex)].m[elementIndex];
}

bool AnimSkin::updateMatrixPalette(const AnimPose* pose) const {
    matrixPaletteValid_ = false;
    if (!pose) return false;
    skinMatrices_.resize(inverseBind_.size());
    for (size_t j = 0; j < inverseBind_.size(); ++j) {
        const int  skelBone = skeletonBone_[j];
        const Mat4 world    = Mat4::fromTRS(pose->world(skelBone));
        skinMatrices_[j]    = Mat4::mul(world, inverseBind_[j]);
    }
    matrixPaletteValid_ = true;
    return true;
}

float AnimSkin::getMatrixPaletteElement(int skinBoneIndex, int elementIndex) const {
    requireSkinBone(skinBoneIndex);
    if (!matrixPaletteValid_) throw Exception("AnimSkin.getMatrixPaletteElement: call updateMatrixPalette first");
    if (elementIndex < 0 || elementIndex >= 16) {
        throw Exception("AnimSkin.getMatrixPaletteElement: elementIndex must be 0..15");
    }
    return skinMatrices_[static_cast<size_t>(skinBoneIndex)].m[elementIndex];
}

bool AnimSkin::bindGpuMesh(graphics::Graphics* gfx, graphics::Mesh* mesh) {
    if (!gfx || !mesh || mesh->getVertexCount() != vertexCount_) return false;
    // The uploaded joint stream uses unsigned 16-bit indices.
    if (getBoneCount() > 65536) return false;
    std::vector<uint16_t> joints(static_cast<size_t>(vertexCount_) * kMaxInfluences, 0);
    std::vector<float>    weights(static_cast<size_t>(vertexCount_) * kMaxInfluences, 0.f);
    for (int v = 0; v < vertexCount_; ++v) {
        for (int i = 0; i < kMaxInfluences; ++i) {
            const size_t idx = static_cast<size_t>(v) * kMaxInfluences + i;
            const auto&  inf = influences_[idx];
            if (inf.bone >= 0) {
                joints[idx] = static_cast<uint16_t>(inf.bone);
                weights[idx] = inf.weight;
            }
        }
    }
    return gfx->setMeshSkinningData(mesh, joints.data(), weights.data(), vertexCount_);
}

bool AnimSkin::updateGpuMesh(graphics::Mesh* mesh, const AnimPose* pose) const {
    if (!mesh || !mesh->hasGpuSkinning() || !updateMatrixPalette(pose) || skinMatrices_.empty()) return false;
    return mesh->setSkinPalette(skinMatrices_.front().m, static_cast<int>(skinMatrices_.size())).ok();
}

float AnimSkin::getBindPositionX(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 0];
}
float AnimSkin::getBindPositionY(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 1];
}
float AnimSkin::getBindPositionZ(int vertexIndex) const {
    requireVertex(vertexIndex);
    return bindPos_[static_cast<size_t>(vertexIndex) * 3u + 2];
}

int AnimSkin::getVertexBone(int vertexIndex, int influenceIndex) const {
    requireVertex(vertexIndex);
    if (influenceIndex < 0 || influenceIndex >= kMaxInfluences) {
        throw Exception("AnimSkin.getVertexBone: influenceIndex must be 0..%d", kMaxInfluences - 1);
    }
    const Influence& inf = influences_[static_cast<size_t>(vertexIndex) * kMaxInfluences + influenceIndex];
    if (inf.bone < 0) return -1;
    return skeletonBone_[static_cast<size_t>(inf.bone)];
}

int AnimSkin::getVertexSkinJoint(int vertexIndex, int influenceIndex) const {
    requireVertex(vertexIndex);
    if (influenceIndex < 0 || influenceIndex >= kMaxInfluences)
        throw Exception("AnimSkin.getVertexSkinJoint: influenceIndex must be 0..%d", kMaxInfluences - 1);
    return influences_[static_cast<size_t>(vertexIndex) * kMaxInfluences + influenceIndex].bone;
}

float AnimSkin::getVertexWeight(int vertexIndex, int influenceIndex) const {
    requireVertex(vertexIndex);
    if (influenceIndex < 0 || influenceIndex >= kMaxInfluences) {
        throw Exception("AnimSkin.getVertexWeight: influenceIndex must be 0..%d", kMaxInfluences - 1);
    }
    return influences_[static_cast<size_t>(vertexIndex) * kMaxInfluences + influenceIndex].weight;
}

void AnimSkin::skinPositions(const AnimPose* pose, float* outPosXYZ) const {
    if (!pose) throw Exception("AnimSkin.skinPositions: pose is null");
    if (!outPosXYZ) throw Exception("AnimSkin.skinPositions: outPosXYZ is null");
    if (vertexCount_ <= 0) return;

    updateMatrixPalette(pose);

    for (int v = 0; v < vertexCount_; ++v) {
        const float bx = bindPos_[static_cast<size_t>(v) * 3u + 0];
        const float by = bindPos_[static_cast<size_t>(v) * 3u + 1];
        const float bz = bindPos_[static_cast<size_t>(v) * 3u + 2];
        float       ox = 0.f, oy = 0.f, oz = 0.f;
        float       wsum = 0.f;
        for (int i = 0; i < kMaxInfluences; ++i) {
            const Influence& inf = influences_[static_cast<size_t>(v) * kMaxInfluences + i];
            if (inf.bone < 0 || inf.weight <= 0.f) continue;
            float px, py, pz;
            skinMatrices_[static_cast<size_t>(inf.bone)].transformPoint(bx, by, bz, px, py, pz);
            ox += px * inf.weight;
            oy += py * inf.weight;
            oz += pz * inf.weight;
            wsum += inf.weight;
        }
        if (wsum <= 1e-8f) {
            ox = bx;
            oy = by;
            oz = bz;
        }
        outPosXYZ[static_cast<size_t>(v) * 3u + 0] = ox;
        outPosXYZ[static_cast<size_t>(v) * 3u + 1] = oy;
        outPosXYZ[static_cast<size_t>(v) * 3u + 2] = oz;
    }
}

bool AnimSkin::skinPositionsTo(const AnimPose* pose, std::vector<float>& outPosXYZ) const {
    if (!pose || vertexCount_ <= 0) return false;
    outPosXYZ.resize(static_cast<size_t>(vertexCount_) * 3u);
    skinPositions(pose, outPosXYZ.data());
    return true;
}

bool AnimSkin::updateSkinnedPositions(const AnimPose* pose) {
    if (!pose || vertexCount_ <= 0) {
        skinnedValid_ = false;
        return false;
    }
    skinnedPos_.resize(static_cast<size_t>(vertexCount_) * 3u);
    skinPositions(pose, skinnedPos_.data());
    skinnedValid_ = true;
    return true;
}

float AnimSkin::getSkinnedPositionX(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!skinnedValid_) throw Exception("AnimSkin.getSkinnedPositionX: call updateSkinnedPositions first");
    return skinnedPos_[static_cast<size_t>(vertexIndex) * 3u + 0];
}
float AnimSkin::getSkinnedPositionY(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!skinnedValid_) throw Exception("AnimSkin.getSkinnedPositionY: call updateSkinnedPositions first");
    return skinnedPos_[static_cast<size_t>(vertexIndex) * 3u + 1];
}
float AnimSkin::getSkinnedPositionZ(int vertexIndex) const {
    requireVertex(vertexIndex);
    if (!skinnedValid_) throw Exception("AnimSkin.getSkinnedPositionZ: call updateSkinnedPositions first");
    return skinnedPos_[static_cast<size_t>(vertexIndex) * 3u + 2];
}

std::vector<float> AnimSkin::getSkinnedPositions() const {
    if (!skinnedValid_) return {};
    return skinnedPos_;
}

bool AnimSkin::updateSkinnedNormals(const AnimPose* pose) {
    skinnedNrmValid_ = false;
    if (!pose || bindNrm_.empty() || vertexCount_ <= 0) return false;

    // n' = sum_i w_i * (M_i^{-1})^T * n, M_i = boneWorld * inverseBind.
    normalMatrices_.resize(inverseBind_.size() * 9u);
    for (size_t j = 0; j < inverseBind_.size(); ++j) {
        const int  skelBone = skeletonBone_[j];
        const Mat4 world    = Mat4::fromTRS(pose->world(skelBone));
        const Mat4 skinMat  = Mat4::mul(world, inverseBind_[j]);
        normalMatrixCofactor(skinMat, normalMatrices_.data() + j * 9u);
    }

    skinnedNrm_.resize(static_cast<size_t>(vertexCount_) * 3u);
    for (int v = 0; v < vertexCount_; ++v) {
        const size_t base = static_cast<size_t>(v) * 3u;
        const float  bx   = bindNrm_[base + 0];
        const float  by   = bindNrm_[base + 1];
        const float  bz   = bindNrm_[base + 2];
        float        nx = 0.f, ny = 0.f, nz = 0.f;
        float        wsum = 0.f;
        for (int i = 0; i < kMaxInfluences; ++i) {
            const Influence& inf = influences_[static_cast<size_t>(v) * kMaxInfluences + i];
            if (inf.bone < 0 || inf.weight <= 0.f) continue;
            const float* c  = normalMatrices_.data() + static_cast<size_t>(inf.bone) * 9u;
            const float  tx = c[0] * bx + c[1] * by + c[2] * bz;
            const float  ty = c[3] * bx + c[4] * by + c[5] * bz;
            const float  tz = c[6] * bx + c[7] * by + c[8] * bz;
            nx += tx * inf.weight;
            ny += ty * inf.weight;
            nz += tz * inf.weight;
            wsum += inf.weight;
        }
        if (wsum <= 1e-8f) {
            nx = bx;
            ny = by;
            nz = bz;
        } else {
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = bx;
                ny = by;
                nz = bz;
            }
        }
        skinnedNrm_[base + 0] = nx;
        skinnedNrm_[base + 1] = ny;
        skinnedNrm_[base + 2] = nz;
    }
    skinnedNrmValid_ = true;
    return true;
}

std::vector<float> AnimSkin::getSkinnedNormals() const {
    if (!skinnedNrmValid_) return {};
    return skinnedNrm_;
}

bool AnimSkin::applyToMesh(graphics::Graphics* gfx, graphics::Mesh* mesh, const AnimPose* pose) {
    if (!gfx || !mesh || !mesh->gpuHandle || !pose || vertexCount_ <= 0) return false;
    if (mesh->getVertexCount() != vertexCount_) return false;

    if (!updateSkinnedPositions(pose)) return false;
    updateSkinnedNormals(pose);
    const auto& importedUv = mesh->importedUv(0);
    const float* uv = importedUv.size() == static_cast<size_t>(vertexCount_) * 2u
                          ? importedUv.data()
                          : nullptr;
    return gfx->updateMeshVertices(mesh, skinnedPos_.data(),
                                   skinnedNrmValid_ ? skinnedNrm_.data() : nullptr, uv,
                                   vertexCount_, nullptr, 0);
}

}  // namespace eve::animation
