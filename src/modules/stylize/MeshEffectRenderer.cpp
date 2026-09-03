#include "stylize/MeshEffectRenderer.h"

#include "common/Diagnostic.h"
#include "graphics/BlendMode.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/SurfaceMode.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <exception>

namespace eve::stylize {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), {}, {}, "stylize.mesh-effect-renderer"));
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace

MeshParticleRenderInputs buildMeshParticleRenderInputs(
    std::span<const MeshParticleInstance> instances, MeshEffectInstance* effect,
    graphics::Mesh* mesh, graphics::Texture* albedo, const MeshVfxBatchKey& key,
    std::uint64_t emitterStableId, const glm::vec3& cameraPosition,
    float projectionScalePixels, float maximumDistance, std::int32_t priority) {
    MeshParticleRenderInputs result;
    result.lodCandidates.reserve(instances.size());
    result.renderItems.reserve(instances.size());
    result.commands.reserve(instances.size());
    const float safeProjection = std::max(0.f, projectionScalePixels);
    const float safeMaximumDistance = std::max(0.f, maximumDistance);
    for (const auto& instance : instances) {
        const glm::vec3 position(instance.model[3]);
        const float distance = glm::length(position - cameraPosition);
        const float scale = glm::length(glm::vec3(instance.model[0]));
        const float projectedRadius = safeProjection * scale / std::max(distance, 1e-4f);
        const std::uint64_t stableId =
            (emitterStableId * 0x9e3779b97f4a7c15ull) ^ instance.stableId;
        result.lodCandidates.push_back(
            {stableId, distance, projectedRadius, priority,
             std::isfinite(distance) && distance <= safeMaximumDistance});
        result.renderItems.push_back({stableId, key, distance});
        MeshVfxRendererCommand command;
        command.stableInstanceId = stableId;
        command.kind = MeshVfxRendererCommand::Kind::Particle;
        command.effect = effect;
        command.particle = {mesh, albedo, instance};
        result.commands.push_back(std::move(command));
    }
    return result;
}

eve::Result<TrailUploadData> prepareTrailUpload(const TrailMeshSnapshot& snapshot) {
    TrailUploadData upload;
    upload.positions.reserve(snapshot.vertices.size() * 3u);
    upload.normals.assign(snapshot.vertices.size() * 3u, 0.f);
    upload.uvs.reserve(snapshot.vertices.size() * 2u);
    upload.indices = snapshot.indices;

    for (const TrailVertex& vertex : snapshot.vertices) {
        if (!finite(vertex.position) || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
            return failure<TrailUploadData>(eve::DiagnosticCode::InvalidArgument,
                                            "trail snapshot contains a non-finite vertex");
        upload.positions.insert(upload.positions.end(),
                                {vertex.position.x, vertex.position.y, vertex.position.z});
        upload.uvs.insert(upload.uvs.end(), {vertex.uv.x, vertex.uv.y});
    }
    if (upload.indices.size() % 3u != 0u)
        return failure<TrailUploadData>(eve::DiagnosticCode::InvalidArgument,
                                        "trail snapshot index count is not triangular");

    for (std::size_t i = 0; i < upload.indices.size(); i += 3u) {
        const std::uint32_t ia = upload.indices[i];
        const std::uint32_t ib = upload.indices[i + 1u];
        const std::uint32_t ic = upload.indices[i + 2u];
        if (ia >= snapshot.vertices.size() || ib >= snapshot.vertices.size() || ic >= snapshot.vertices.size())
            return failure<TrailUploadData>(eve::DiagnosticCode::InvalidArgument,
                                            "trail snapshot contains an out-of-range index");
        const glm::vec3 a = snapshot.vertices[ia].position;
        const glm::vec3 b = snapshot.vertices[ib].position;
        const glm::vec3 c = snapshot.vertices[ic].position;
        glm::vec3 normal = glm::cross(b - a, c - a);
        const float length = glm::length(normal);
        if (length > 1e-6f) normal /= length;
        for (const std::uint32_t index : {ia, ib, ic}) {
            upload.normals[index * 3u] += normal.x;
            upload.normals[index * 3u + 1u] += normal.y;
            upload.normals[index * 3u + 2u] += normal.z;
        }
    }
    for (std::size_t i = 0; i < snapshot.vertices.size(); ++i) {
        glm::vec3 normal(upload.normals[i * 3u], upload.normals[i * 3u + 1u],
                         upload.normals[i * 3u + 2u]);
        const float length = glm::length(normal);
        normal = length > 1e-6f ? normal / length : glm::vec3(0.f, 0.f, 1.f);
        upload.normals[i * 3u] = normal.x;
        upload.normals[i * 3u + 1u] = normal.y;
        upload.normals[i * 3u + 2u] = normal.z;
    }
    return eve::Result<TrailUploadData>::success(std::move(upload));
}

MeshEffectRenderer::MeshEffectRenderer(graphics::Graphics& graphics) noexcept : graphics_(graphics) {}

graphics::Shader* MeshEffectRenderer::shaderFor(MeshEffectInstance& effect) {
    const std::string style = effect.style().getStyle();
    auto found = shaders_.find(style);
    if (found == shaders_.end()) {
        graphics::Shader* shader = effect.newMeshShader(&graphics_);
        found = shaders_.emplace(style, shader).first;
    } else {
        effect.style().applyToShader(found->second);
    }
    return found->second;
}

namespace {

void requestSceneColorForRefraction(graphics::Graphics& graphics, MeshEffectInstance& effect) {
    if (!effect.style().hasParam("refractionStrength") ||
        effect.style().getFloat("refractionStrength") <= 0.f)
        return;
    const auto capture = graphics.captureMesh3DSceneColor();
    switch (capture) {
        case graphics::Graphics::Mesh3DSceneColorCaptureStatus::ExplicitOverride:
        case graphics::Graphics::Mesh3DSceneColorCaptureStatus::Scheduled:
        case graphics::Graphics::Mesh3DSceneColorCaptureStatus::Captured:
        case graphics::Graphics::Mesh3DSceneColorCaptureStatus::HistoryFallback:
        case graphics::Graphics::Mesh3DSceneColorCaptureStatus::Unavailable:
            break;
    }
}

}  // namespace

eve::Result<MeshEffectSubmitStatus> MeshEffectRenderer::submitOverlay(
    MeshEffectInstance& effect, const MeshEffectDrawSource& source) {
    if (!effect.isBound())
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::PreconditionViolation,
                                               "mesh effect has no bound target");
    if (source.target != effect.target())
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::StaleHandle,
                                               "resolved mesh source does not match the effect target");
    if (!source.mesh)
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::InvalidArgument,
                                               "resolved mesh source has no mesh");
    if (effect.intensity() <= 0.f)
        return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::SkippedInactive);

    try {
        graphics::Color tint = source.tint;
        tint.a *= effect.intensity();
        if (!batchActive_)
            graphics_.setMesh3DSurface(graphics::SurfaceMode::Transparent, graphics::BlendMode::Additive,
                                       false, true, 0.f);
        requestSceneColorForRefraction(graphics_, effect);
        graphics_.drawMeshShader(source.mesh, source.model, source.albedo, tint, shaderFor(effect));
    } catch (const std::exception& error) {
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::Failed,
                                               std::string("mesh overlay submission failed: ") + error.what());
    }
    return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::Drawn);
}

eve::Result<MeshEffectSubmitStatus> MeshEffectRenderer::submitTrail(
    MeshEffectInstance& effect, const TrailMeshSnapshot& snapshot, const glm::mat4& model,
    const graphics::Color& tint) {
    if (effect.intensity() <= 0.f)
        return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::SkippedInactive);
    if (snapshot.vertices.empty() || snapshot.indices.empty())
        return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::SkippedEmpty);

    auto prepared = prepareTrailUpload(snapshot);
    if (!prepared.ok())
        return eve::Result<MeshEffectSubmitStatus>::failure(prepared.status());
    TrailUploadData upload = std::move(prepared).takeValue();

    try {
        if (!trailMesh_) {
            trailMesh_ = graphics_.newMeshFromArrays(
                upload.positions.data(), upload.normals.data(), upload.uvs.data(),
                static_cast<int>(upload.positions.size() / 3u), upload.indices.data(),
                static_cast<int>(upload.indices.size()));
            if (!trailMesh_)
                return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::Failed,
                                                       "graphics failed to create the trail mesh");
        } else if (!graphics_.updateMeshVertices(
                       trailMesh_, upload.positions.data(), upload.normals.data(), upload.uvs.data(),
                       static_cast<int>(upload.positions.size() / 3u), upload.indices.data(),
                       static_cast<int>(upload.indices.size()))) {
            return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::Unsupported,
                                                   "graphics backend rejected the dynamic trail upload");
        }
        graphics::Color drawTint = tint;
        drawTint.a *= effect.intensity();
        if (!batchActive_)
            graphics_.setMesh3DSurface(graphics::SurfaceMode::Transparent, graphics::BlendMode::Additive,
                                       false, true, 0.f);
        requestSceneColorForRefraction(graphics_, effect);
        graphics_.drawMeshShader(trailMesh_, model, nullptr, drawTint, shaderFor(effect));
    } catch (const std::exception& error) {
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::Failed,
                                               std::string("trail submission failed: ") + error.what());
    }
    return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::Drawn);
}

eve::Result<MeshEffectSubmitStatus> MeshEffectRenderer::submitParticle(
    MeshEffectInstance& effect, const MeshParticleDrawSource& source) {
    if (!source.mesh)
        return failure<MeshEffectSubmitStatus>(eve::DiagnosticCode::InvalidArgument,
                                               "mesh particle source has no mesh");
    if (effect.intensity() <= 0.f)
        return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::SkippedInactive);
    try {
        graphics::Color tint = source.instance.color;
        tint.a *= effect.intensity();
        if (!batchActive_)
            graphics_.setMesh3DSurface(graphics::SurfaceMode::Transparent,
                                       graphics::BlendMode::Additive, false, true, 0.f);
        requestSceneColorForRefraction(graphics_, effect);
        graphics_.drawMeshShader(source.mesh, source.instance.model, source.albedo, tint,
                                 shaderFor(effect));
    } catch (const std::exception& error) {
        return failure<MeshEffectSubmitStatus>(
            eve::DiagnosticCode::Failed,
            std::string("mesh particle submission failed: ") + error.what());
    }
    return eve::Result<MeshEffectSubmitStatus>::success(MeshEffectSubmitStatus::Drawn);
}

eve::Result<MeshVfxSubmissionReport> MeshEffectRenderer::submitQueue(
    const MeshVfxRenderQueue& queue, std::span<const MeshVfxRendererCommand> commands) {
    if (batchActive_ || !activeCommands_.empty())
        return failure<MeshVfxSubmissionReport>(eve::DiagnosticCode::PreconditionViolation,
                                                "mesh VFX queue submission is reentrant");
    for (const auto& command : commands) {
        if (!command.effect)
            return failure<MeshVfxSubmissionReport>(eve::DiagnosticCode::InvalidArgument,
                                                    "mesh VFX renderer command has no effect");
        if (!activeCommands_.emplace(command.stableInstanceId, &command).second) {
            activeCommands_.clear();
            return failure<MeshVfxSubmissionReport>(eve::DiagnosticCode::InvalidArgument,
                                                    "mesh VFX renderer commands contain a duplicate stable ID");
        }
    }

    MeshVfxSubmissionReport report = MeshVfxBatchExecutor{}.submit(queue, *this);
    activeCommands_.clear();
    batchActive_ = false;
    return eve::Result<MeshVfxSubmissionReport>::success(std::move(report));
}

MeshVfxSubmitStatus MeshEffectRenderer::beginBatch(const MeshVfxBatchKey& key) {
    if (batchActive_)
        return MeshVfxSubmitStatus::Failed;
    try {
        const auto blend = key.blend == MeshVfxBatchBlend::Alpha ? graphics::BlendMode::Alpha
                                                                 : graphics::BlendMode::Additive;
        graphics_.setMesh3DSurface(graphics::SurfaceMode::Transparent, blend, false, true, 0.f);
        batchActive_ = true;
        return MeshVfxSubmitStatus::Accepted;
    } catch (const std::exception&) {
        return MeshVfxSubmitStatus::Failed;
    }
}

MeshVfxSubmitStatus MeshEffectRenderer::submitDraw(const MeshVfxBatchedDraw& draw) {
    if (!batchActive_)
        return MeshVfxSubmitStatus::Failed;
    const auto found = activeCommands_.find(draw.stableInstanceId);
    if (found == activeCommands_.end())
        return MeshVfxSubmitStatus::Skipped;
    const auto& command = *found->second;
    eve::Result<MeshEffectSubmitStatus> submitted =
        command.kind == MeshVfxRendererCommand::Kind::Trail
            ? submitTrail(*command.effect, command.trail, command.trailModel, command.trailTint)
        : command.kind == MeshVfxRendererCommand::Kind::Particle
            ? submitParticle(*command.effect, command.particle)
            : submitOverlay(*command.effect, command.overlay);
    if (!submitted.ok())
        return MeshVfxSubmitStatus::Failed;
    return submitted.value() == MeshEffectSubmitStatus::Drawn ? MeshVfxSubmitStatus::Accepted
                                                               : MeshVfxSubmitStatus::Skipped;
}

MeshVfxSubmitStatus MeshEffectRenderer::endBatch() {
    if (!batchActive_)
        return MeshVfxSubmitStatus::Failed;
    batchActive_ = false;
    return MeshVfxSubmitStatus::Accepted;
}

}  // namespace eve::stylize
