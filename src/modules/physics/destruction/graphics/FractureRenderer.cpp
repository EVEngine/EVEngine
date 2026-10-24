#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "common/Assert.h"
#include "common/Exception.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/FractureGeometry.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"
namespace eve::physics {
eve::Result<void> GeometryCollectionRenderer::setGroupExteriorColor(int groupId, float r, float g, float b, float a) {
    if (groupId < 0 || groupId > 4095 || !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
        !std::isfinite(a) || r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1 || a < 0 || a > 1)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "group/color out of range", "palette"));
    groupColors_[groupId] = {r, g, b, a};
    return eve::Result<void>::success();
}

void GeometryCollectionRenderer::releaseFragmentMeshes(graphics::Graphics* graphics) {
    if (graphics)
        for (auto& batch : fragmentBatches_)
            if (batch.mesh) {
                const bool released = graphics->releaseMesh(batch.mesh);
                EV_ASSERT(released, "prepared mesh ownership changed");
                if (released) delete batch.mesh;
            }
    fragmentBatches_ = {};
    preparedGeometry_.reset();
    poseScratch_.clear();
    boneMatrices_.clear();
}
eve::Result<void> GeometryCollectionRenderer::prepareAnimation(const CollapseAnimation& animation,
                                                               graphics::Graphics*      graphics) {
    std::vector<int> groups;
    for (int bone = 0; bone < animation.boneCount(); ++bone) {
        auto pose = animation.sample(0.f, bone);
        if (!pose) return eve::Result<void>::failure(pose.status());
        groups.push_back(pose.value().clusterId);
    }
    return prepareGeometry(animation.fragmentGeometry(), graphics, groups);
}
eve::Result<void> GeometryCollectionRenderer::prepareInstance(graphics::Graphics* graphics) {
    if (!instance_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "no instance to prepare", "instance"));
    std::vector<int> groups;
    for (int bone = 0; bone < instance_->boneCount(); ++bone) {
        auto pose = instance_->bonePresentation(bone);
        if (!pose) return eve::Result<void>::failure(pose.status());
        groups.push_back(pose.value().clusterId);
    }
    return prepareGeometry(instance_->fragmentGeometry(), graphics, groups);
}
eve::Result<void> GeometryCollectionRenderer::prepareGeometry(std::shared_ptr<const FractureGeometry> geometry,
                                                              graphics::Graphics*                     graphics,
                                                              const std::vector<int>&                 groups) {
    if (!graphics)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "graphics required", "graphics"));
    if (preparedGeometry_ == geometry && meshOwner_ == graphics) return eve::Result<void>::success();
    if (geometry && groups.size() != geometry->fragments.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "one structural group per fragment required", "groups"));
    std::vector<FragmentBatch>                  candidate;
    std::map<std::pair<int, bool>, std::size_t> batches;
    auto                                        cleanup = [&] {
        for (auto& batch : candidate)
            if (batch.mesh) {
                const bool released = graphics->releaseMesh(batch.mesh);
                EV_ASSERT(released, "candidate mesh ownership changed");
                if (released) delete batch.mesh;
                batch.mesh = nullptr;
            }
    };
    try {
        if (geometry)
            for (std::size_t bone = 0; bone < geometry->fragments.size(); ++bone) {
                const auto& f = geometry->fragments[bone];
                for (std::size_t t = 0; t < f.materials.size(); ++t) {
                    const bool interior = f.materials[t] != 0;
                    const int  group    = interior ? -1 : groups[bone];
                    const auto key      = std::make_pair(group, interior);
                    auto       found    = batches.find(key);
                    if (found == batches.end()) {
                        const auto index = candidate.size();
                        candidate.emplace_back();
                        candidate.back().groupId  = group;
                        candidate.back().interior = interior;
                        found                     = batches.emplace(key, index).first;
                    }
                    auto&     batch = candidate[found->second];
                    glm::vec3 points[3];
                    for (int k = 0; k < 3; ++k) {
                        auto id   = f.indices[t * 3 + static_cast<std::size_t>(k)];
                        points[k] = {f.positions[3 * id], f.positions[3 * id + 1], f.positions[3 * id + 2]};
                    }
                    const auto n = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
                    for (auto p : points) {
                        batch.indices.push_back(static_cast<std::uint32_t>(batch.restPositions.size() / 3));
                        batch.bones.push_back(static_cast<std::uint32_t>(bone));
                        batch.restPositions.insert(batch.restPositions.end(), {p.x, p.y, p.z});
                        batch.restNormals.insert(batch.restNormals.end(), {n.x, n.y, n.z});
                        batch.uv.insert(batch.uv.end(), {p.x, p.z});
                    }
                }
            }
        for (auto& batch : candidate) {
            if (batch.indices.empty()) continue;
            batch.positions = batch.restPositions;
            batch.normals   = batch.restNormals;
            batch.mesh      = graphics->newMeshFromArrays(batch.positions.data(), batch.normals.data(), batch.uv.data(),
                                                          static_cast<int>(batch.positions.size() / 3), batch.indices.data(),
                                                          static_cast<int>(batch.indices.size()));
            if (!batch.mesh) {
                cleanup();
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, "fragment batch allocation failed", "geometry"));
            }
        }
    } catch (const std::exception& error) {
        cleanup();
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "geometry"));
    }
    // Reserve CPU scratch before publishing GPU ownership so allocation failure is transactional.
    std::vector<BonePresentation>      poses;
    std::vector<std::array<float, 16>> matrices;
    try {
        if (geometry) {
            poses.reserve(geometry->fragments.size());
            matrices.resize(geometry->fragments.size());
        }
    } catch (const std::exception& error) {
        cleanup();
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "geometry"));
    }
    std::uint64_t token       = forwardToken_;
    std::uint64_t shadowToken = shadowToken_;
    try {
        if (geometry && !token)
            token = graphics::RenderSystem3D::addForwardExtraDrawer(
                [this](graphics::Graphics& target, const graphics::Camera3D::Data&, const glm::mat4&, float) {
                    if (!submitted_ || &target != meshOwner_) return;
                    submitted_ = false;
                    auto drawn = drawGeometry(target);
                    if (!drawn) throw eve::Exception("%s", drawn.status().describe().c_str());
                });
        if (geometry && !shadowToken)
            shadowToken = graphics::RenderSystem3D::addShadowExtraDrawer(
                [this](graphics::Graphics& target, const glm::mat4& lightVP, const graphics::Camera3D::Data&) {
                    if (!submitted_ || &target != meshOwner_) return;
                    for (const auto& batch : fragmentBatches_)
                        if (batch.mesh) target.drawMeshShadow(batch.mesh, lightVP, false);
                });
    } catch (const std::exception& error) {
        if (token != forwardToken_) graphics::RenderSystem3D::removeForwardExtraDrawer(token);
        if (shadowToken != shadowToken_) graphics::RenderSystem3D::removeShadowExtraDrawer(shadowToken);
        cleanup();
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "forwardContributor"));
    }
    if (geometry && (!token || !shadowToken)) {
        if (token != forwardToken_) graphics::RenderSystem3D::removeForwardExtraDrawer(token);
        if (shadowToken != shadowToken_) graphics::RenderSystem3D::removeShadowExtraDrawer(shadowToken);
        cleanup();
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "forward contributor registration failed", "geometry"));
    }
    forwardToken_ = token;
    shadowToken_  = shadowToken;
    if (meshOwner_ != graphics)
        releaseOwnedMeshes(meshOwner_);
    else
        releaseFragmentMeshes(graphics);
    meshOwner_        = graphics;
    preparedGeometry_ = std::move(geometry);
    fragmentBatches_  = std::move(candidate);
    poseScratch_      = std::move(poses);
    boneMatrices_     = std::move(matrices);
    return eve::Result<void>::success();
}
eve::Result<void> GeometryCollectionRenderer::submitAnimation(const CollapseAnimation& animation, float seconds,
                                                              float x, float y, float z) {
    const auto geometry = animation.fragmentGeometry();
    if (!geometry)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "scene submission requires embedded mesh geometry; legacy boxes use drawAnimation inside an active 3D pass",
            "geometry"));
    if (geometry != preparedGeometry_ || !forwardToken_ ||
        static_cast<std::size_t>(animation.boneCount()) != boneMatrices_.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "prepareAnimation is required before scene submission",
                                                                 "geometry"));
    if (!std::isfinite(seconds) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "submission values must be finite", "time"));
    auto depth = registerDepthContribution();
    if (!depth) return depth;
    poseScratch_.clear();
    for (int i = 0; i < animation.boneCount(); ++i) {
        auto pose = animation.sample(seconds, i);
        if (!pose) return eve::Result<void>::failure(pose.status());
        auto p = pose.value();
        p.worldX += x;
        p.worldY += y;
        p.worldZ += z;
        poseScratch_.push_back(p);
    }
    submitted_   = false;
    auto updated = updateGeometry(*meshOwner_);
    if (!updated) return updated;
    submitted_ = true;
    return eve::Result<void>::success();
}
eve::Result<void> GeometryCollectionRenderer::updateGeometry(graphics::Graphics& graphics) {
    if (poseScratch_.size() != boneMatrices_.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "prepared pose count mismatch", "geometry"));
    for (std::size_t i = 0; i < poseScratch_.size(); ++i) {
        const auto&     b = poseScratch_[i];
        const glm::quat q(b.rotW, b.rotX, b.rotY, b.rotZ);
        const auto      m =
            glm::translate(glm::mat4(1.f), glm::vec3(b.worldX, b.worldY, b.worldZ)) * glm::mat4_cast(glm::normalize(q));
        std::copy_n(glm::value_ptr(m), 16, boneMatrices_[i].data());
    }
    for (auto& batch : fragmentBatches_) {
        if (!batch.mesh) continue;
        for (std::size_t i = 0; i < batch.bones.size(); ++i) {
            const auto& m      = boneMatrices_[batch.bones[i]];
            const auto  offset = 3 * i;
            const float x = batch.restPositions[offset], y = batch.restPositions[offset + 1],
                        z  = batch.restPositions[offset + 2];
            const float nx = batch.restNormals[offset], ny = batch.restNormals[offset + 1],
                        nz = batch.restNormals[offset + 2];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                batch.positions[offset + axis] = m[axis] * x + m[4 + axis] * y + m[8 + axis] * z + m[12 + axis];
                batch.normals[offset + axis]   = m[axis] * nx + m[4 + axis] * ny + m[8 + axis] * nz;
            }
        }
        if (!graphics.updateMeshVertices(batch.mesh, batch.positions.data(), batch.normals.data(), batch.uv.data(),
                                         static_cast<int>(batch.positions.size() / 3), batch.indices.data(),
                                         static_cast<int>(batch.indices.size())))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "fragment batch upload failed", "geometry"));
    }
    return eve::Result<void>::success();
}
eve::Result<void> GeometryCollectionRenderer::drawGeometry(graphics::Graphics& graphics) {
    graphics.setMesh3DMaterial(0.f, .85f);
    graphics.setMesh3DShadowReceive(true);
    for (const auto& batch : fragmentBatches_) {
        if (!batch.mesh) continue;
        // Group palette lookups are typed and bounded by prepared material groups.
        graphics::Color color = batch.interior ? graphics::Color(interiorR_, interiorG_, interiorB_, interiorA_)
                                               : graphics::Color(exteriorR_, exteriorG_, exteriorB_, exteriorA_);
        if (!batch.interior) {
            auto found = groupColors_.find(batch.groupId);
            if (found != groupColors_.end()) {
                const auto& c = found->second;
                color         = graphics::Color(c[0], c[1], c[2], c[3]);
            }
        }
        graphics.drawMesh(batch.mesh, glm::mat4(1.f), nullptr, color);
    }
    return eve::Result<void>::success();
}
}  // namespace eve::physics
