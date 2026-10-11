#include "physics/destruction/graphics/GeometryCollectionRenderer.h"

#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/destruction/GeometryCollectionInstance.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::physics {
namespace {

void appendBox(std::vector<float>& positions, std::vector<float>& normals, std::vector<float>& uv,
               std::vector<std::uint32_t>& indices, const glm::mat4& model, float hx, float hy, float hz) {
    struct Face {
        glm::vec3 n;
        glm::vec3 a, b, c, d;
    };
    const Face faces[] = {
        {{0, 0, 1}, {-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}},
        {{0, 0, -1}, {hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz}},
        {{0, 1, 0}, {-hx, hy, hz}, {hx, hy, hz}, {hx, hy, -hz}, {-hx, hy, -hz}},
        {{0, -1, 0}, {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}, {-hx, -hy, hz}},
        {{1, 0, 0}, {hx, -hy, hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}},
        {{-1, 0, 0}, {-hx, -hy, -hz}, {-hx, -hy, hz}, {-hx, hy, hz}, {-hx, hy, -hz}},
    };
    const float tex[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    for (const auto& face : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size() / 3);
        const glm::vec3 corners[4] = {face.a, face.b, face.c, face.d};
        const glm::vec3 worldN = glm::normalize(glm::mat3(model) * face.n);
        for (int i = 0; i < 4; ++i) {
            const glm::vec4 world = model * glm::vec4(corners[i], 1.f);
            positions.insert(positions.end(), {world.x, world.y, world.z});
            normals.insert(normals.end(), {worldN.x, worldN.y, worldN.z});
            uv.insert(uv.end(), {tex[i * 2], tex[i * 2 + 1]});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

glm::mat4 boneModel(const BonePresentation& bone) {
    const glm::quat rotation(bone.rotW, bone.rotX, bone.rotY, bone.rotZ);
    glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(bone.worldX, bone.worldY, bone.worldZ));
    model *= glm::mat4_cast(glm::normalize(rotation));
    return model;
}

void clampColor(float& r, float& g, float& b, float& a) {
    r = std::clamp(r, 0.f, 1.f);
    g = std::clamp(g, 0.f, 1.f);
    b = std::clamp(b, 0.f, 1.f);
    a = std::clamp(a, 0.f, 1.f);
}

}  // namespace

GeometryCollectionRenderer::GeometryCollectionRenderer(GeometryCollectionInstance* instance) noexcept
    : instance_(instance) {}

GeometryCollectionRenderer::~GeometryCollectionRenderer() { releaseOwnedMeshes(meshOwner_); }

void GeometryCollectionRenderer::setInstance(GeometryCollectionInstance* instance) {
    instance_ = instance;
    invalidateSleepBatch(meshOwner_);
}

void GeometryCollectionRenderer::setExteriorColor(float r, float g, float b, float a) {
    exteriorR_ = r;
    exteriorG_ = g;
    exteriorB_ = b;
    exteriorA_ = a;
    clampColor(exteriorR_, exteriorG_, exteriorB_, exteriorA_);
}

void GeometryCollectionRenderer::setInteriorColor(float r, float g, float b, float a) {
    interiorR_ = r;
    interiorG_ = g;
    interiorB_ = b;
    interiorA_ = a;
    clampColor(interiorR_, interiorG_, interiorB_, interiorA_);
}

void GeometryCollectionRenderer::setSleepColor(float r, float g, float b, float a) {
    sleepR_ = r;
    sleepG_ = g;
    sleepB_ = b;
    sleepA_ = a;
    clampColor(sleepR_, sleepG_, sleepB_, sleepA_);
}

void GeometryCollectionRenderer::releaseOwnedMeshes(graphics::Graphics* graphics) {
    if (graphics) {
        if (sleepBatch_) (void)graphics->releaseMesh(sleepBatch_);
        if (unitBox_) (void)graphics->releaseMesh(unitBox_);
    }
    sleepBatch_ = nullptr;
    unitBox_ = nullptr;
    meshOwner_ = nullptr;
    sleepBatchRevision_ = 0;
    sleepBatchBoneCount_ = 0;
}

void GeometryCollectionRenderer::invalidateSleepBatch(graphics::Graphics* graphics) {
    if (graphics && sleepBatch_) (void)graphics->releaseMesh(sleepBatch_);
    sleepBatch_ = nullptr;
    sleepBatchRevision_ = 0;
    sleepBatchBoneCount_ = 0;
}

void GeometryCollectionRenderer::ensureUnitBox(graphics::Graphics& graphics) {
    if (meshOwner_ != &graphics) {
        releaseOwnedMeshes(meshOwner_);
        meshOwner_ = &graphics;
    }
    if (unitBox_) return;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv;
    std::vector<std::uint32_t> indices;
    appendBox(positions, normals, uv, indices, glm::mat4(1.f), 1.f, 1.f, 1.f);
    unitBox_ = graphics.newMeshFromArrays(positions.data(), normals.data(), uv.data(),
                                          static_cast<int>(positions.size() / 3), indices.data(),
                                          static_cast<int>(indices.size()));
}

eve::Result<void> GeometryCollectionRenderer::rebuildSleepBatch(graphics::Graphics& graphics) {
    if (!instance_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection renderer has no instance", "instance"));
    if (meshOwner_ != &graphics) {
        releaseOwnedMeshes(meshOwner_);
        meshOwner_ = &graphics;
    }
    invalidateSleepBatch(&graphics);
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv;
    std::vector<std::uint32_t> indices;
    int sleepCount = 0;
    for (int i = 0; i < instance_->boneCount(); ++i) {
        auto presentation = instance_->bonePresentation(i);
        if (!presentation) return eve::Result<void>::failure(presentation.status());
        if (presentation.value().state != BoneRuntimeState::Sleeping) continue;
        appendBox(positions, normals, uv, indices, boneModel(presentation.value()),
                  presentation.value().halfExtentX, presentation.value().halfExtentY,
                  presentation.value().halfExtentZ);
        ++sleepCount;
    }
    sleepBatchBoneCount_ = sleepCount;
    sleepBatchRevision_ = instance_->sleepBatchRevision();
    if (sleepCount == 0) return eve::Result<void>::success();
    sleepBatch_ = graphics.newMeshFromArrays(positions.data(), normals.data(), uv.data(),
                                             static_cast<int>(positions.size() / 3), indices.data(),
                                             static_cast<int>(indices.size()));
    if (!sleepBatch_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "geometry-collection sleep batch mesh allocation failed", "mesh"));
    return eve::Result<void>::success();
}

eve::Result<void> GeometryCollectionRenderer::draw(graphics::Graphics* graphics) {
    lastActiveDrawCount_ = 0;
    lastSleepBatchCount_ = 0;
    if (!graphics)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection draw requires Graphics", "graphics"));
    if (!instance_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection renderer has no instance", "instance"));
    if (!instance_->hasLiveWorld())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));

    ensureUnitBox(*graphics);
    if (!unitBox_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "geometry-collection unit box mesh allocation failed", "mesh"));

    if (sleepBatchRevision_ != instance_->sleepBatchRevision() ||
        (sleepBatch_ == nullptr && instance_->sleepBatchRevision() != 0)) {
        auto rebuilt = rebuildSleepBatch(*graphics);
        if (!rebuilt) return rebuilt;
    }

    for (int i = 0; i < instance_->boneCount(); ++i) {
        auto presentation = instance_->bonePresentation(i);
        if (!presentation) return eve::Result<void>::failure(presentation.status());
        const auto& bone = presentation.value();
        if (bone.state == BoneRuntimeState::Sleeping) continue;
        glm::mat4 model = boneModel(bone);
        model = glm::scale(model, glm::vec3(bone.halfExtentX, bone.halfExtentY, bone.halfExtentZ));
        const bool detached = bone.state == BoneRuntimeState::Detached;
        const graphics::Color tint(detached ? interiorR_ : exteriorR_, detached ? interiorG_ : exteriorG_,
                                   detached ? interiorB_ : exteriorB_, detached ? interiorA_ : exteriorA_);
        graphics->drawMesh(unitBox_, model, nullptr, tint);
        ++lastActiveDrawCount_;
    }

    if (sleepBatch_) {
        graphics->drawMesh(sleepBatch_, glm::mat4(1.f), nullptr,
                           graphics::Color(sleepR_, sleepG_, sleepB_, sleepA_));
        lastSleepBatchCount_ = sleepBatchBoneCount_;
    }
    return eve::Result<void>::success();
}

}  // namespace eve::physics
