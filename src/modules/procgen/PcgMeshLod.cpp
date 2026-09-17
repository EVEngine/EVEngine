#include "procgen/PcgMeshLod.h"

#include <cmath>
#include <limits>
#include <unordered_map>

namespace eve::procgen {
namespace {
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "procgen.pcgMeshLod"));
}
}

PcgMeshTransform::PcgMeshTransform() noexcept {
    matrix_[0] = matrix_[5] = matrix_[10] = matrix_[15] = 1.F;
}
Result<void> PcgMeshTransform::setElement(int row, int column, float value) {
    if (row < 0 || row >= 4 || column < 0 || column >= 4 || !std::isfinite(value))
        return invalid<void>("Pcg mesh transform element is invalid");
    matrix_[static_cast<std::size_t>(row * 4 + column)] = value;
    return Result<void>::success();
}
float PcgMeshTransform::getElement(int row, int column) const noexcept {
    return row >= 0 && row < 4 && column >= 0 && column < 4
               ? matrix_[static_cast<std::size_t>(row * 4 + column)] : 0.F;
}

Result<void> PcgMeshCombinePlan::appendSource(const MeshBuild& mesh, const PcgMeshTransform& transform,
                                               const std::string& defaultMaterialId) {
    if (mesh.empty() || sources_.size() >= 4096 || defaultMaterialId.empty())
        return invalid<void>("Pcg mesh combine source, material identity or source budget is invalid");
    const int vertices = mesh.getVertexCount(), indices = mesh.getIndexCount();
    if (vertices <= 0 || indices <= 0 || indices % 3 != 0 ||
        mesh.positions().size() != static_cast<std::size_t>(vertices) * 3U ||
        mesh.normals().size() != static_cast<std::size_t>(vertices) * 3U ||
        mesh.uvs().size() != static_cast<std::size_t>(vertices) * 2U ||
        (mesh.hasVertexColors() && mesh.colors().size() != static_cast<std::size_t>(vertices) * 4U))
        return invalid<void>("Pcg mesh combine source streams are invalid");
    for (float value : transform.matrix_) if (!std::isfinite(value))
        return invalid<void>("Pcg mesh combine transform is non-finite");
    for (std::uint32_t index : mesh.indices()) if (index >= static_cast<std::uint32_t>(vertices))
        return invalid<void>("Pcg mesh combine source index is out of range");
    sources_.push_back(Source{mesh, transform, defaultMaterialId});
    return Result<void>::success();
}
void PcgMeshCombinePlan::clear() noexcept { sources_.clear(); }
int PcgMeshCombinePlan::getSourceCount() const noexcept { return static_cast<int>(sources_.size()); }

Result<int> combinePcgStaticMeshesInto(MeshBuild& output, const PcgMeshCombinePlan& plan) {
    if (plan.sources_.empty()) return invalid<int>("Pcg static mesh combine plan is empty");
    std::uint64_t vertexCount = 0, indexCount = 0;
    bool anyColors = false;
    for (const auto& source : plan.sources_) {
        vertexCount += static_cast<std::uint64_t>(source.mesh.getVertexCount());
        indexCount += static_cast<std::uint64_t>(source.mesh.getIndexCount());
        anyColors = anyColors || source.mesh.hasVertexColors();
    }
    if (vertexCount > 8U * 1024U * 1024U || indexCount > 48U * 1024U * 1024U ||
        vertexCount > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
        indexCount > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return invalid<int>("Pcg static mesh combine output exceeds native mesh budgets");
    MeshBuild candidate;
    candidate.reserve(static_cast<int>(vertexCount), static_cast<int>(indexCount));
    std::vector<float> colors;
    if (anyColors) colors.reserve(static_cast<std::size_t>(vertexCount) * 4U);
    struct Bucket { std::string material; std::vector<std::uint32_t> indices; };
    std::vector<Bucket> buckets;
    std::unordered_map<std::string, std::size_t> materialMap;
    std::uint32_t vertexOffset = 0;
    for (const auto& source : plan.sources_) {
        const auto& mesh = source.mesh;
        const auto& m = source.transform.matrix_;
        for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
            const float x=mesh.getPositionX(vertex),y=mesh.getPositionY(vertex),z=mesh.getPositionZ(vertex);
            const float nx=mesh.getNormalX(vertex),ny=mesh.getNormalY(vertex),nz=mesh.getNormalZ(vertex);
            const float px=m[0]*x+m[1]*y+m[2]*z+m[3],py=m[4]*x+m[5]*y+m[6]*z+m[7];
            const float pz=m[8]*x+m[9]*y+m[10]*z+m[11];
            const float tx=m[0]*nx+m[1]*ny+m[2]*nz,ty=m[4]*nx+m[5]*ny+m[6]*nz;
            const float tz=m[8]*nx+m[9]*ny+m[10]*nz;
            if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(pz)||!std::isfinite(tx)||
               !std::isfinite(ty)||!std::isfinite(tz))return invalid<int>("Pcg static mesh transform overflowed");
            candidate.addVertex(px,py,pz,tx,ty,tz,mesh.getUvU(vertex),mesh.getUvV(vertex));
            if(anyColors)for(int component=0;component<4;++component)
                colors.push_back(mesh.hasVertexColors()?mesh.getColor(vertex,component):1.F);
        }
        for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
            const int group = mesh.getTriangleGroup(triangle);
            std::string material = group >= 0 ? mesh.getGroupName(group) : source.defaultMaterialId;
            if(material.empty())return invalid<int>("Pcg static mesh triangle has no material identity");
            auto [found, inserted] = materialMap.emplace(material,buckets.size());
            if(inserted)buckets.push_back(Bucket{material,{}});
            auto& indices=buckets[found->second].indices;
            for(int corner=0;corner<3;++corner)
                indices.push_back(vertexOffset+static_cast<std::uint32_t>(mesh.getIndex(triangle*3+corner)));
        }
        vertexOffset += static_cast<std::uint32_t>(mesh.getVertexCount());
    }
    for(auto& bucket:buckets){candidate.setActiveGroup(bucket.material);
        for(std::size_t i=0;i<bucket.indices.size();i+=3)
            candidate.addTriangle(bucket.indices[i],bucket.indices[i+1],bucket.indices[i+2]);}
    if(anyColors){auto set=candidate.setVertexColors(std::move(colors));if(!set.ok())return Result<int>::failure(set.status());}
    candidate.setMeta("kind","pcg.mesh.combined");
    candidate.setMeta("sourceCount",std::to_string(plan.sources_.size()));
    output=std::move(candidate);
    return Result<int>::success(static_cast<int>(indexCount/3U));
}
Result<void> PcgMeshLodProfile::appendLevel(float transition, float fade, float quality,
                                             bool combineMeshes, bool combineSubMeshes) {
    if (levels_.size() >= 4 || !std::isfinite(transition) || transition < 0.F || transition > 1.F ||
        !std::isfinite(fade) || fade < 0.F || fade > 1.F || !std::isfinite(quality) || quality < 0.F ||
        quality > 1.F || (combineSubMeshes && !combineMeshes))
        return invalid<void>("Pcg mesh LOD level values are invalid");
    if (!levels_.empty() && transition >= levels_.back().screenRelativeTransitionHeight)
        return invalid<void>("Pcg mesh LOD transition heights must be strictly descending");
    PcgMeshLodLevel level;
    level.screenRelativeTransitionHeight = transition;
    level.fadeTransitionWidth = fade;
    level.quality = quality;
    level.combineMeshes = combineMeshes;
    level.combineSubMeshes = combineSubMeshes;
    level.simplification.preserveBorderEdges = false;
    level.simplification.vertexLinkDistance = std::numeric_limits<double>::denorm_min();
    levels_.push_back(level);
    return Result<void>::success();
}

Result<void> PcgMeshLodProfile::setLevelRendererState(int index, int skinQuality, int shadowCastingMode,
                                                       bool receiveShadows, int motionVectorMode,
                                                       bool skinnedMotionVectors, int lightProbeUsage,
                                                       int reflectionProbeUsage) {
    const bool validSkinQuality = skinQuality == 0 || skinQuality == 1 || skinQuality == 2 || skinQuality == 4;
    const bool validLightProbe = lightProbeUsage == 0 || lightProbeUsage == 1 || lightProbeUsage == 2 ||
                                 lightProbeUsage == 4;
    if (index < 0 || index >= static_cast<int>(levels_.size()) || !validSkinQuality ||
        shadowCastingMode < 0 || shadowCastingMode > 3 || motionVectorMode < 0 || motionVectorMode > 2 ||
        !validLightProbe || reflectionProbeUsage < 0 || reflectionProbeUsage > 2)
        return invalid<void>("Pcg mesh LOD renderer state is invalid");
    PcgMeshLodRendererState state;
    state.skinQuality = skinQuality;
    state.shadowCastingMode = shadowCastingMode;
    state.receiveShadows = receiveShadows;
    state.motionVectorMode = motionVectorMode;
    state.skinnedMotionVectors = skinnedMotionVectors;
    state.lightProbeUsage = lightProbeUsage;
    state.reflectionProbeUsage = reflectionProbeUsage;
    levels_[static_cast<std::size_t>(index)].renderer = state;
    return Result<void>::success();
}

int PcgMeshLodProfile::getLevelRendererState(int index, int field) const noexcept {
    const auto* level = levelAt(index);
    if (!level || field < 0 || field > 6) return -1;
    const auto& state = level->renderer;
    switch (field) {
        case 0: return state.skinQuality;
        case 1: return state.shadowCastingMode;
        case 2: return state.receiveShadows ? 1 : 0;
        case 3: return state.motionVectorMode;
        case 4: return state.skinnedMotionVectors ? 1 : 0;
        case 5: return state.lightProbeUsage;
        case 6: return state.reflectionProbeUsage;
        default: return -1;
    }
}

Result<void> PcgMeshLodProfile::setFadePolicy(int fadeMode, bool animateCrossFading,
                                               float animationDuration) {
    if ((fadeMode < 0 || fadeMode > 2) || !std::isfinite(animationDuration) || animationDuration <= 0.F ||
        (animateCrossFading && fadeMode == 0))
        return invalid<void>("Pcg mesh LOD fade policy is invalid");
    fadeMode_ = fadeMode;
    animateCrossFading_ = animateCrossFading;
    crossFadeAnimationDuration_ = animationDuration;
    return Result<void>::success();
}

const PcgMeshLodLevel* PcgMeshLodProfile::levelAt(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(levels_.size()) ? &levels_[static_cast<std::size_t>(index)] : nullptr;
}

float PcgMeshLodSet::getTransitionHeight(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(levels_.size())
               ? levels_[static_cast<std::size_t>(index)].screenRelativeTransitionHeight : -1.F;
}
float PcgMeshLodSet::getFadeWidth(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(levels_.size())
               ? levels_[static_cast<std::size_t>(index)].fadeTransitionWidth : -1.F;
}
float PcgMeshLodSet::getQuality(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(levels_.size())
               ? levels_[static_cast<std::size_t>(index)].quality : -1.F;
}
const MeshBuild* PcgMeshLodSet::meshAt(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(meshes_.size()) ? &meshes_[static_cast<std::size_t>(index)] : nullptr;
}
const PcgMeshLodRendererState* PcgMeshLodSet::rendererStateAt(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(levels_.size())
               ? &levels_[static_cast<std::size_t>(index)].renderer : nullptr;
}
int PcgMeshLodSet::selectLevel(float relativeHeight) const noexcept {
    if (!std::isfinite(relativeHeight) || relativeHeight < 0.F || levels_.empty()) return -2;
    for (int i = 0; i < static_cast<int>(levels_.size()); ++i)
        if (relativeHeight >= levels_[static_cast<std::size_t>(i)].screenRelativeTransitionHeight) return i;
    return -1;
}
float PcgMeshLodSet::getSwitchDistance(int level, float diameter, float fov) const noexcept {
    if (level < 0 || level >= static_cast<int>(levels_.size()) || !std::isfinite(diameter) || diameter <= 0.F ||
        !std::isfinite(fov) || fov <= 0.F || fov >= 180.F) return -1.F;
    constexpr float radians = 0.01745329251994329577F;
    const float threshold = levels_[static_cast<std::size_t>(level)].screenRelativeTransitionHeight;
    if (threshold <= 0.F) return -1.F;
    return diameter / (2.F * std::tan(fov * 0.5F * radians) * threshold);
}

Result<void> buildPcgMeshLodsInto(PcgMeshLodSet& output, const MeshBuild& source,
                                   const PcgMeshLodProfile& profile) {
    const auto& levels = profile.levels();
    if (source.empty() || levels.empty() || levels.size() > 4)
        return invalid<void>("Pcg mesh LOD requires a non-empty mesh and one through four levels");
    PcgMeshLodSet candidate;
    candidate.levels_ = levels;
    candidate.fadeMode_ = profile.getFadeMode();
    candidate.animateCrossFading_ = profile.getAnimateCrossFading();
    candidate.crossFadeAnimationDuration_ = profile.getCrossFadeAnimationDuration();
    candidate.meshes_.reserve(levels.size());
    for (const PcgMeshLodLevel& level : levels) {
        MeshBuild mesh;
        auto simplified = simplifyGtsMesh(mesh, source, level.quality, level.simplification);
        if (!simplified.ok()) return Result<void>::failure(simplified.status());
        candidate.meshes_.push_back(std::move(mesh));
    }
    output = std::move(candidate);
    return Result<void>::success();
}

Result<void> buildPcgCombinedMeshLodsInto(PcgMeshLodSet& output, const PcgMeshCombinePlan& plan,
                                           const PcgMeshLodProfile& profile) {
    if (profile.levels().empty())
        return invalid<void>("Pcg combined mesh LOD requires at least one level");
    for (const PcgMeshLodLevel& level : profile.levels())
        if (!level.combineMeshes)
            return invalid<void>("Every Pcg combined mesh LOD level must enable mesh combination");
    MeshBuild combined;
    auto combinedResult = combinePcgStaticMeshesInto(combined, plan);
    if (!combinedResult.ok()) return Result<void>::failure(combinedResult.status());
    return buildPcgMeshLodsInto(output, combined, profile);
}

}  // namespace eve::procgen
