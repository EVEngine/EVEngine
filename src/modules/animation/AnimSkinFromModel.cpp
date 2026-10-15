#include "animation/AnimSkin.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"
#include "model3d/ModelData.h"

#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace eve::animation {

namespace {

Mat4 fromAiMatrix(const aiMatrix4x4& m) {
    // Assimp is row-major; convert to column-major Mat4.
    Mat4 out;
    out.m[0]  = m.a1;
    out.m[1]  = m.b1;
    out.m[2]  = m.c1;
    out.m[3]  = m.d1;
    out.m[4]  = m.a2;
    out.m[5]  = m.b2;
    out.m[6]  = m.c2;
    out.m[7]  = m.d2;
    out.m[8]  = m.a3;
    out.m[9]  = m.b3;
    out.m[10] = m.c3;
    out.m[11] = m.d3;
    out.m[12] = m.a4;
    out.m[13] = m.b4;
    out.m[14] = m.c4;
    out.m[15] = m.d4;
    return out;
}

}  // namespace

AnimSkin* AnimSkin::fromModel(const model3d::ModelData* model, int meshIndex, const AnimSkeleton* skeleton) {
    if (!model || !skeleton) {
        throw Exception("AnimSkin.fromModel: null model/skeleton");
    }
    const aiMesh* mesh = model->getMesh(meshIndex);
    if (!mesh) {
        throw Exception("AnimSkin.fromModel: invalid mesh index %d", meshIndex);
    }
    if (!mesh->HasBones() || mesh->mNumBones == 0) {
        throw Exception("AnimSkin.fromModel: mesh %d has no bones", meshIndex);
    }
    if (mesh->mNumVertices == 0 || !mesh->mVertices) {
        throw Exception("AnimSkin.fromModel: mesh %d has no vertices", meshIndex);
    }

    auto* skin         = new AnimSkin();
    skin->vertexCount_ = static_cast<int>(mesh->mNumVertices);
    skin->bindPos_.resize(static_cast<size_t>(skin->vertexCount_) * 3u);
    for (int v = 0; v < skin->vertexCount_; ++v) {
        const aiVector3D& p                             = mesh->mVertices[v];
        skin->bindPos_[static_cast<size_t>(v) * 3u + 0] = p.x;
        skin->bindPos_[static_cast<size_t>(v) * 3u + 1] = p.y;
        skin->bindPos_[static_cast<size_t>(v) * 3u + 2] = p.z;
    }
    if (mesh->HasNormals() && mesh->mNormals) {
        skin->bindNrm_.resize(static_cast<size_t>(skin->vertexCount_) * 3u);
        for (int v = 0; v < skin->vertexCount_; ++v) {
            const aiVector3D& n                             = mesh->mNormals[v];
            skin->bindNrm_[static_cast<size_t>(v) * 3u + 0] = n.x;
            skin->bindNrm_[static_cast<size_t>(v) * 3u + 1] = n.y;
            skin->bindNrm_[static_cast<size_t>(v) * 3u + 2] = n.z;
        }
    }

    // Gather skin joints that map onto the skeleton by name.
    std::vector<int> meshBoneToSkin(mesh->mNumBones, -1);
    int              matched = 0;
    for (unsigned b = 0; b < mesh->mNumBones; ++b) {
        const aiBone* bone = mesh->mBones[b];
        if (!bone) continue;
        const std::string name   = bone->mName.C_Str();
        const int         skBone = skeleton->findBone(name);
        if (skBone < 0) continue;
        meshBoneToSkin[b] = static_cast<int>(skin->skeletonBone_.size());
        skin->skeletonBone_.push_back(skBone);
        skin->skinBoneNames_.push_back(name);
        skin->inverseBind_.push_back(fromAiMatrix(bone->mOffsetMatrix));
        ++matched;
    }
    if (matched == 0) {
        delete skin;
        throw Exception("AnimSkin.fromModel: no mesh bones matched skeleton names");
    }

    // Accumulate all influences then keep the top-k per vertex (renormalize).
    struct Acc {
        int   bone   = -1;
        float weight = 0.f;
    };
    std::vector<std::vector<Acc>> acc(static_cast<size_t>(skin->vertexCount_));

    for (unsigned b = 0; b < mesh->mNumBones; ++b) {
        const int skinBone = meshBoneToSkin[b];
        if (skinBone < 0) continue;
        const aiBone* bone = mesh->mBones[b];
        if (!bone) continue;
        for (unsigned w = 0; w < bone->mNumWeights; ++w) {
            const aiVertexWeight& vw = bone->mWeights[w];
            if (vw.mVertexId >= mesh->mNumVertices) continue;
            if (vw.mWeight <= 0.f) continue;
            acc[vw.mVertexId].push_back(Acc{skinBone, vw.mWeight});
        }
    }

    skin->influences_.assign(static_cast<size_t>(skin->vertexCount_) * kMaxInfluences, Influence{});
    for (int v = 0; v < skin->vertexCount_; ++v) {
        auto& list = acc[static_cast<size_t>(v)];
        std::sort(list.begin(), list.end(), [](const Acc& a, const Acc& b) { return a.weight > b.weight; });
        // Merge duplicate bone entries.
        std::unordered_map<int, float> merged;
        for (const Acc& a : list) {
            if (a.bone < 0) continue;
            merged[a.bone] += a.weight;
        }
        list.clear();
        for (const auto& kv : merged) list.push_back(Acc{kv.first, kv.second});
        std::sort(list.begin(), list.end(), [](const Acc& a, const Acc& b) { return a.weight > b.weight; });

        float     sum  = 0.f;
        const int take = std::min(kMaxInfluences, static_cast<int>(list.size()));
        for (int i = 0; i < take; ++i) sum += list[static_cast<size_t>(i)].weight;
        const float inv = (sum > 1e-8f) ? (1.f / sum) : 0.f;
        for (int i = 0; i < take; ++i) {
            Influence& dst = skin->influences_[static_cast<size_t>(v) * kMaxInfluences + i];
            dst.bone       = list[static_cast<size_t>(i)].bone;
            dst.weight     = list[static_cast<size_t>(i)].weight * inv;
        }
        // Vertices with no weights stay at bind (weight sum 0 → identity path in skin).
    }

    return skin;
}

}  // namespace eve::animation
