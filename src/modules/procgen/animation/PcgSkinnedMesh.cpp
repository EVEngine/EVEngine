#include "procgen/animation/PcgSkinnedMesh.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace eve::procgen_animation {
namespace {
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message,
                                                "procgen.animation.pcgSkinnedMesh"));
}

bool sameBind(const std::array<float, 16>& left, const std::array<float, 16>& right) {
    return std::equal(left.begin(), left.end(), right.begin());
}
}  // namespace

Result<void> PcgSkinnedMeshPlan::appendSource(const procgen::MeshBuild& mesh,
                                               const animation::AnimSkin& skin,
                                               const procgen::PcgMeshTransform& transform,
                                               const std::string& defaultMaterialId) {
    if (mesh.empty() || mesh.getVertexCount() != skin.getVertexCount() || skin.getBoneCount() <= 0 ||
        defaultMaterialId.empty() || sources_.size() >= 4096)
        return invalid<void>("Pcg skinned source mesh, skin, material or budget is invalid");
    animation::AnimSkinStreamData data;
    data.vertexCount = skin.getVertexCount();
    data.bindPositions.reserve(static_cast<std::size_t>(data.vertexCount) * 3U);
    for (int vertex = 0; vertex < data.vertexCount; ++vertex) {
        data.bindPositions.insert(data.bindPositions.end(), {skin.getBindPositionX(vertex),
                                                             skin.getBindPositionY(vertex),
                                                             skin.getBindPositionZ(vertex)});
        for (int influence = 0; influence < animation::AnimSkin::kMaxInfluences; ++influence) {
            data.vertexSkinJoints.push_back(skin.getVertexSkinJoint(vertex, influence));
            data.vertexWeights.push_back(skin.getVertexWeight(vertex, influence));
        }
    }
    data.skeletonBones.reserve(static_cast<std::size_t>(skin.getBoneCount()));
    data.boneNames.reserve(static_cast<std::size_t>(skin.getBoneCount()));
    data.inverseBindMatrices.reserve(static_cast<std::size_t>(skin.getBoneCount()));
    for (int joint = 0; joint < skin.getBoneCount(); ++joint) {
        data.skeletonBones.push_back(skin.getSkeletonBone(joint));
        data.boneNames.push_back(skin.getSkinBoneName(joint));
        std::array<float, 16> bind{};
        for (int element = 0; element < 16; ++element)
            bind[static_cast<std::size_t>(element)] = skin.getInverseBindElement(joint, element);
        data.inverseBindMatrices.push_back(bind);
    }
    sources_.push_back(Source{mesh, transform, defaultMaterialId, std::move(data)});
    return Result<void>::success();
}

Result<void> combinePcgSkinnedMeshesInto(PcgSkinnedMeshResult& output,
                                          const PcgSkinnedMeshPlan& plan) {
    if (plan.sources_.empty()) return invalid<void>("Pcg skinned combine plan is empty");
    procgen::PcgMeshCombinePlan meshPlan;
    for (const auto& source : plan.sources_) {
        auto appended = meshPlan.appendSource(source.mesh, source.transform, source.defaultMaterialId);
        if (!appended.ok()) return Result<void>::failure(appended.status());
    }
    procgen::MeshBuild combinedMesh;
    auto combined = procgen::combinePcgStaticMeshesInto(combinedMesh, meshPlan);
    if (!combined.ok()) return Result<void>::failure(combined.status());

    animation::AnimSkinStreamData streams;
    streams.vertexCount = combinedMesh.getVertexCount();
    streams.bindPositions = combinedMesh.positions();
    streams.bindNormals = combinedMesh.normals();
    streams.vertexSkinJoints.reserve(static_cast<std::size_t>(streams.vertexCount) * 4U);
    streams.vertexWeights.reserve(static_cast<std::size_t>(streams.vertexCount) * 4U);

    bool seeded = false;
    for (const auto& source : plan.sources_) {
        const auto& skin = source.skin;
        std::vector<int> remap(skin.skeletonBones.size(), -1);
        if (!seeded) {
            streams.skeletonBones = skin.skeletonBones;
            streams.boneNames = skin.boneNames;
            streams.inverseBindMatrices = skin.inverseBindMatrices;
            seeded = true;
        }
        for (std::size_t joint = 0; joint < skin.skeletonBones.size(); ++joint) {
            int firstIdentity = -1;
            for (std::size_t used = 0; used < streams.skeletonBones.size(); ++used)
                if (streams.skeletonBones[used] == skin.skeletonBones[joint]) {
                    firstIdentity = static_cast<int>(used);
                    break;
                }
            if (firstIdentity < 0 || !sameBind(skin.inverseBindMatrices[joint],
                                               streams.inverseBindMatrices[static_cast<std::size_t>(firstIdentity)])) {
                firstIdentity = static_cast<int>(streams.skeletonBones.size());
                streams.skeletonBones.push_back(skin.skeletonBones[joint]);
                streams.boneNames.push_back(skin.boneNames[joint]);
                streams.inverseBindMatrices.push_back(skin.inverseBindMatrices[joint]);
            }
            remap[joint] = firstIdentity;
        }
        for (std::size_t influence = 0; influence < skin.vertexWeights.size(); ++influence) {
            const float weight = skin.vertexWeights[influence];
            const int sourceJoint = skin.vertexSkinJoints[influence];
            if (weight > 0.F && (sourceJoint < 0 || static_cast<std::size_t>(sourceJoint) >= remap.size()))
                return invalid<void>("Pcg skinned source influence references an invalid joint");
            streams.vertexSkinJoints.push_back(weight > 0.F ? remap[static_cast<std::size_t>(sourceJoint)] : -1);
            streams.vertexWeights.push_back(weight);
        }
    }
    auto skin = animation::AnimSkin::fromStreams(std::move(streams));
    if (!skin.ok()) return Result<void>::failure(skin.status());
    PcgSkinnedMeshResult candidate;
    candidate.mesh_ = std::move(combinedMesh);
    candidate.skin_ = std::move(skin).takeValue();
    output = std::move(candidate);
    return Result<void>::success();
}

}  // namespace eve::procgen_animation
