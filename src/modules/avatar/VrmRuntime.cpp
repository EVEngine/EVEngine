#include "avatar/VrmRuntime.h"
#include <assimp/scene.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmSurface.h"
#include "common/Module.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

namespace eve::avatar {
VrmRuntime::VrmRuntime() = default;
VrmRuntime::~VrmRuntime() {
    for (auto& part : parts) {
        if (auto* entity = ecs::try_get(part.entity)) ecs::DestroyEntity(entity);
        if (auto* entity = ecs::try_get(part.outline)) ecs::DestroyEntity(entity);
    }
    if (auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics"); gfx && !providerLifetime.expired()) {
        for (auto& part : parts)
            if (gfx->releaseMesh(part.mesh)) delete part.mesh;
        surfaces.clear();
        for (auto* texture : textures)
            if (gfx->releaseTexture(texture)) delete texture;
    }
}
eve::Result<std::unique_ptr<VrmRuntime>> VrmRuntime::prepare(std::string_view path) {
    using R = eve::Result<std::unique_ptr<VrmRuntime>>;
    try {
        auto*                                 fs = filesystem::Filesystem::create();
        std::unique_ptr<filesystem::FileData> file(fs->read(std::string(path)));
        if (!file) throw std::runtime_error("VRM file could not be read");
        auto parsed = parseVrm({static_cast<const std::byte*>(file->getData()), file->getSize()});
        if (!parsed.ok()) return R::failure(parsed.status());
        auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
        if (!gfx || gfx->getBackendName() != "vulkan")
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                     "VRM import requires an initialized graphics provider",
                                                     std::string(path)));
        auto candidate              = std::make_unique<VrmRuntime>();
        candidate->document         = std::move(parsed).takeValue();
        candidate->providerLifetime = gfx->resourceLifetime();
        auto& d                     = candidate->document;
        // Uncached owning model: replacing/reloading a cache entry cannot invalidate this import.
        candidate->model.reset(model3d::Model3D::create()->newModelData(file.get(), ".glb"));
        candidate->skeleton.reset(animation::AnimImporter::loadSkeletonFromModel(candidate->model.get()));
        candidate->player = std::make_unique<animation::AnimPlayer>(candidate->skeleton.get());
        candidate->nodeBones.resize(d.nodes.size(), -1);
        for (size_t n = 0; n < d.nodes.size(); ++n) {
            if (!d.nodes[n].empty()) candidate->nodeBones[n] = candidate->skeleton->findBone(d.nodes[n]);
        }
        auto requireNode = [&](int node) {
            if (node >= 0 && candidate->nodeBones[node] < 0)
                throw std::runtime_error("Cannot resolve VRM node: " + d.nodes[node]);
        };
        for (auto& [semantic, node] : d.humanoid) requireNode(node);
        for (auto& c : d.colliders) requireNode(c.node);
        for (auto& s : d.springs) {
            requireNode(s.center);
            for (auto& j : s.joints) requireNode(j.node);
        }
        std::vector<VrmAtlasRect> rectangles;
        auto*                     atlas = &buildVrmAtlas(*gfx, d, rectangles);
        candidate->textures.push_back(atlas);
        for (auto& m : d.materials) candidate->surfaces.push_back(buildVrmSurface(*gfx, m, *atlas, rectangles));
        const auto* scene = candidate->model->getScene();
        for (size_t n = 0; n < d.nodes.size(); ++n) {
            if (d.nodeMeshes[n] < 0) continue;
            const auto* node = scene->mRootNode->FindNode(d.nodes[n].c_str());
            if (!node) throw std::runtime_error("Cannot resolve VRM mesh node: " + d.nodes[n]);
            const auto& slots = d.meshMaterials[d.nodeMeshes[n]];
            if (node->mNumMeshes != slots.size())
                throw std::runtime_error("VRM primitive mapping changed during decode");
            for (unsigned p = 0; p < node->mNumMeshes; ++p) {
                Part part;
                part.sourceMesh = int(node->mMeshes[p]);
                part.node       = int(n);
                part.material   = slots[p];
                if (part.material < 0) throw std::runtime_error("VRM primitive is missing its material");
                candidate->parts.push_back(std::move(part));
                auto& owned = candidate->parts.back();
                owned.mesh  = gfx->newMeshFromAssimp(*scene->mMeshes[owned.sourceMesh]);
                if (!owned.mesh) throw std::runtime_error("Mesh upload failed");
                if (candidate->model->hasBones(owned.sourceMesh)) {
                    owned.skin.reset(animation::AnimSkin::fromModel(candidate->model.get(), owned.sourceMesh,
                                                                    candidate->skeleton.get()));
                    if (!owned.skin->bindGpuMesh(gfx, owned.mesh))
                        throw std::runtime_error("VRM GPU skin upload failed");
                }
                auto* entity = graphics::Renderable3D::create();
                owned.entity = ecs::handle_of(entity);
                entity->setVisible(false);
                entity->setMesh(owned.mesh);
                entity->setMaterial(candidate->surfaces[owned.material]->material.get());
                if (auto* material = candidate->surfaces[owned.material]->outlineMaterial.get()) {
                    auto* outline = graphics::Renderable3D::create();
                    owned.outline = ecs::handle_of(outline);
                    outline->setVisible(false);
                    outline->setMesh(owned.mesh);
                    outline->setMaterial(material);
                }
            }
        }
        if (candidate->parts.empty()) throw std::runtime_error("VRM contains no renderable primitives");
        candidate->particles_.resize(d.springs.size());
        for (size_t i = 0; i < d.springs.size(); ++i) candidate->particles_[i].resize(d.springs[i].joints.size());
        return R::success(std::move(candidate));
    } catch (const std::exception& e) {
        return R::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, e.what(), std::string(path), {}, "avatar.vrm.import"));
    }
}
void VrmRuntime::sync(AvatarInstance&, const glm::mat4& matrix, bool visible) {
    visible = visible && !providerLifetime.expired();
    world   = matrix;
    for (auto& part : parts) {
        glm::mat4 partWorld = matrix;
        if (!part.skin && nodeBones[part.node] >= 0) {
            glm::mat4 nodeMatrix(1);
            player->getPose()->getWorldMatrix(nodeBones[part.node], &nodeMatrix[0][0]);
            partWorld *= nodeMatrix;
        }
        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose(partWorld, scale, rotation, translation, skew, perspective);
        const glm::vec3 angles = glm::eulerAngles(rotation);
        for (auto handle : {part.entity, part.outline})
            if (auto* entity = dynamic_cast<graphics::Renderable3D*>(ecs::try_get(handle))) {
                entity->setPosition(translation.x, translation.y, translation.z);
                entity->setRotation(angles.y, angles.x, angles.z);
                entity->setScale(scale.x, scale.y, scale.z);
                entity->setVisible(visible);
            }
    }
}
void VrmRuntime::animate(AvatarInstance&, animation::AnimPose& pose, float dt) {
    if (providerLifetime.expired()) return;
    time += dt;
    springs(pose, dt);
    for (auto& part : parts)
        if (part.skin && !part.skin->updateGpuMesh(part.mesh, &pose))
            throw std::runtime_error("VRM skin palette no longer matches imported skeleton");
}
void VrmRuntime::morphs(AvatarInstance& avatar) {
    if (providerLifetime.expired()) return;
    std::vector<float> weights;
    float              blink = 0, mouth = 0, look = 0;
    auto               overrideValue = [](const std::string& mode, float weight) {
        return mode == "block" && weight > 0 ? 1.f : mode == "blend" ? weight : 0.f;
    };
    for (const auto& e : document.expressions) {
        float w = std::clamp(avatar.getParameter(e.name) + (gazeWeights_.contains(e.name) ? gazeWeights_[e.name] : 0.f),
                             0.f, 1.f);
        if (e.binary) w = w > .5f ? 1.f : 0.f;
        weights.push_back(w);
        blink += overrideValue(e.overrideBlink, w);
        mouth += overrideValue(e.overrideMouth, w);
        look += overrideValue(e.overrideLookAt, w);
    }
    for (auto& s : surfaces) {
        s->current  = s->base;
        s->uvScale  = {1, 1};
        s->uvOffset = {0, 0};
    }
    for (auto& part : parts) {
        part.mesh->clearMorphWeights();
        for (int i = 0; i < part.mesh->getMorphCount(); ++i) {
            auto name = part.mesh->getMorphName(i);
            if (avatar.hasParameter(name)) part.mesh->setMorphWeight(name, avatar.getParameter(name));
        }
    }
    for (size_t i = 0; i < document.expressions.size(); ++i) {
        const auto& e           = document.expressions[i];
        float       suppression = 0;
        if (e.name == "blink" || e.name == "blinkLeft" || e.name == "blinkRight") suppression = blink;
        if (e.name == "aa" || e.name == "ih" || e.name == "ou" || e.name == "ee" || e.name == "oh") suppression = mouth;
        if (e.name == "lookUp" || e.name == "lookDown" || e.name == "lookLeft" || e.name == "lookRight")
            suppression = look;
        float w = weights[i] * (1 - std::clamp(suppression, 0.f, 1.f));
        if (e.binary && suppression > 0) w = 0;
        for (const auto& bind : e.morphs)
            for (auto& part : parts)
                if (part.node == bind.node) {
                    auto name = part.mesh->getMorphName(bind.index);
                    part.mesh->setMorphWeight(name, part.mesh->getMorphWeight(name) + bind.weight * w);
                }
        for (const auto& b : e.colors) {
            auto& s   = *surfaces[b.material];
            auto  add = [&](auto& out, const auto& base) {
                for (size_t c = 0; c < out.size(); ++c) out[c] += (b.target[c] - base[c]) * w;
            };
            if (b.type == "color")
                add(s.current.color, s.base.color);
            else if (b.type == "emissionColor")
                add(s.current.emission, s.base.emission);
            else if (b.type == "shadeColor")
                add(s.current.shade, s.base.shade);
            else if (b.type == "matcapColor")
                add(s.current.matcap, s.base.matcap);
            else if (b.type == "rimColor")
                add(s.current.rim, s.base.rim);
            else if (b.type == "outlineColor")
                add(s.current.outline, s.base.outline);
        }
        for (const auto& b : e.uvs)
            for (int c = 0; c < 2; ++c) {
                surfaces[b.material]->uvScale[c] += (b.scale[c] - 1) * w;
                surfaces[b.material]->uvOffset[c] += b.offset[c] * w;
            }
    }
    auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (gfx)
        for (auto& part : parts)
            if (part.mesh->isMorphDirty()) gfx->bakeMeshMorph(part.mesh);
    for (auto& surface : surfaces) surface->update(time);
}
}  // namespace eve::avatar
