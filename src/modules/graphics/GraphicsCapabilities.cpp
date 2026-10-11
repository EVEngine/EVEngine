// IRenderCapture capability provider.
//
// DevTools / MCP must not depend on the graphics module, so the graphics
// module registers this implementation of the common render-capture
// interface. The scene traversal for the entity-id mask and visible-entity
// snapshot lives here too (graphics -> scene is a legal downward edge).

#include "common/Capability.h"
#include "common/RenderCapture.h"
#include "common/ProcgenProbeSink.h"

#include "common/ECS.h"
#include "common/b64.h"
#include "filesystem/FileData.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/DiffuseLightProbeRegistry.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "image/ImageData.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::graphics {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float degToRad(float d) { return d * kPi / 180.f; }

std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string num(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

std::string vec3Json(const glm::vec3 &v) {
    return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]";
}

class RenderCaptureImpl final : public eve::IRenderCapture {
public:
    Graphics *gfx() { return eve::ModuleManager::getInstance<Graphics>("Graphics"); }

    Camera3D *camera() {
        if (ecs::current()->getManager<Camera3D>() != nullptr) {
            auto view = ecs::View<Camera3D, Camera3D::Data>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [data] = *it;
                if (data->active && data->entity) return data->entity;
            }
        }
        return Camera3D::createCamera();
    }

    eve::RenderStatusInfo status() const override {
        eve::RenderStatusInfo info;
        if (auto *g = const_cast<RenderCaptureImpl *>(this)->gfx()) {
            info.width = g->getWidth();
            info.height = g->getHeight();
            info.pixelWidth = g->getPixelWidth();
            info.pixelHeight = g->getPixelHeight();
            info.had3DThisFrame = g->had3DThisFrame();
            info.readbackEnabled = g->isScreenReadbackEnabled();
            info.backend = g->getBackendName();
        }
        return info;
    }

    void setReadbackEnabled(bool enabled) override {
        if (auto *g = gfx()) g->setScreenReadbackEnabled(enabled);
    }

    bool savePng(const std::string &path, int *outWidth, int *outHeight,
                 std::string *err) override {
        auto *g = gfx();
        if (!g) {
            if (err) *err = "Graphics module not available";
            return false;
        }
        g->setScreenReadbackEnabled(true);
        image::ImageData *frame = nullptr;
        try {
            frame = g->newImageData();
        } catch (...) {
            if (err)
                *err = "no presented frame yet: readback was just enabled and the "
                       "copy lands on the next present — call again after one frame "
                       "(screenshots need a running, presented game loop)";
            return false;
        }
        if (!frame) {
            if (err) *err = "no presented frame (enable readback and render a frame first)";
            return false;
        }
        filesystem::FileData *png = nullptr;
        try {
            png = frame->encode(image::ImageData::FormatHandler::ENCODED_PNG, "frame.png", false);
        } catch (...) {
            delete frame;
            if (err) *err = "png encode failed";
            return false;
        }
        if (!png) {
            delete frame;
            if (err) *err = "png encode returned null";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        if (!out.good()) {
            delete png;
            delete frame;
            if (err) *err = "cannot open output file: " + path;
            return false;
        }
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        const bool ok = out.good();
        delete png;
        delete frame;
        if (outWidth) *outWidth = g->getPixelWidth();
        if (outHeight) *outHeight = g->getPixelHeight();
        if (!ok && err) *err = "failed writing: " + path;
        return ok;
    }

    std::string capturePngDataUrl() override {
        auto *g = gfx();
        if (!g) return {};
        g->setScreenReadbackEnabled(true);
        image::ImageData *frame = nullptr;
        try {
            frame = g->newImageData();
        } catch (...) {
            return {};
        }
        if (!frame) return {};
        filesystem::FileData *png = nullptr;
        try {
            png = frame->encode(image::ImageData::FormatHandler::ENCODED_PNG, "frame.png", false);
        } catch (...) {
            delete frame;
            return {};
        }
        if (!png) {
            delete frame;
            return {};
        }
        size_t len = 0;
        char *b64 = eve::b64_encode(static_cast<const char *>(png->getData()), png->getSize(), 0, len);
        std::string url = "data:image/png;base64," + std::string(b64 ? b64 : "", len);
        delete[] b64;
        delete png;
        delete frame;
        return url;
    }

    bool ensureCamera() override { return camera() != nullptr; }

    bool setCameraPose(float ex, float ey, float ez, float tx, float ty, float tz,
                       float fovYDeg) override {
        auto *cam = camera();
        if (!cam) return false;
        cam->setActive(true);
        cam->setEye(ex, ey, ez);
        cam->setTarget(tx, ty, tz);
        if (fovYDeg > 0.f) cam->setFov(fovYDeg);
        return true;
    }

    bool setCameraPoseYawPitch(float ex, float ey, float ez, float yawDeg, float pitchDeg,
                               float fovYDeg) override {
        auto *cam = camera();
        if (!cam) return false;
        cam->setActive(true);
        const float yaw = degToRad(yawDeg);
        const float pitch = degToRad(pitchDeg);
        const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                            std::cos(pitch) * std::cos(yaw));
        cam->setEye(ex, ey, ez);
        cam->setTarget(ex + dir.x * 100.f, ey + dir.y * 100.f, ez + dir.z * 100.f);
        if (fovYDeg > 0.f) cam->setFov(fovYDeg);
        return true;
    }

    std::string cameraPoseJson() override {
        auto *cam = camera();
        if (!cam) return "{\"error\":\"no camera\"}";
        auto d = cam->data();
        std::string out = "{\"eye\":" + vec3Json(glm::vec3(d->eyeX, d->eyeY, d->eyeZ));
        out += ",\"target\":" + vec3Json(glm::vec3(d->targetX, d->targetY, d->targetZ));
        out += ",\"up\":" + vec3Json(glm::vec3(d->upX, d->upY, d->upZ));
        out += ",\"fov\":" + num(d->fovYDeg);
        out += ",\"near\":" + num(d->nearZ);
        out += ",\"far\":" + num(d->farZ);
        if (auto *g = gfx()) {
            out += ",\"viewportWidth\":" + std::to_string(g->getPixelWidth());
            out += ",\"viewportHeight\":" + std::to_string(g->getPixelHeight());
        }
        out += "}";
        return out;
    }

    eve::Result<eve::Renderable3DInfo> inspectRenderable3D(
        std::uint32_t entityId, std::uint32_t generation) override {
        ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(Renderable3D)),
                                 entityId, generation};
        auto* renderable = dynamic_cast<Renderable3D*>(ecs::try_get(handle));
        if (!renderable)
            return eve::Result<eve::Renderable3DInfo>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle,
                "Renderable3D handle is missing or stale", "entity"));
        auto transform = renderable->transform();
        auto renderer = renderable->meshRenderer();
        eve::Renderable3DInfo info;
        info.entityId = renderable->id;
        info.generation = renderable->generation;
        info.x = transform->x;
        info.y = transform->y;
        info.z = transform->z;
        info.tintR = renderer->r;
        info.tintG = renderer->g;
        info.tintB = renderer->b;
        info.tintA = renderer->a;
        info.metallic = renderer->metallic;
        info.roughness = renderer->roughness;
        info.parallaxScale = renderer->parallaxScale;
        info.visible = renderer->visible;
        info.receiveLight = renderer->receiveLight;
        info.castShadow = renderer->castShadow;
        info.receiveShadow = renderer->receiveShadow;
        info.hasPackedMaterial = renderer->material != nullptr;
        info.hasTexture = renderer->texture != nullptr || renderer->normalTexture != nullptr ||
                          renderer->heightTexture != nullptr;
        info.hasShader = renderer->shader != nullptr;
        return eve::Result<eve::Renderable3DInfo>::success(std::move(info));
    }

    std::string visibleEntitiesJson(float fovYDeg, bool *ok) override {
        return visibleAt(nullptr, nullptr, fovYDeg, ok);
    }

    std::string visibleEntitiesJsonAt(float ex, float ey, float ez, float tx, float ty, float tz,
                                      float fovYDeg, bool *ok) override {
        const glm::vec3 eye(ex, ey, ez);
        const glm::vec3 target(tx, ty, tz);
        return visibleAt(&eye, &target, fovYDeg, ok);
    }

    std::string entityIdMaskJson(int *outWidth, int *outHeight, bool *ok) override {
        auto *g = gfx();
        if (ok) *ok = false;
        if (!g) return "{\"error\":\"Graphics module not available\"}";
        auto *cam = camera();
        if (!cam) return "{\"error\":\"no camera\"}";
        auto d = cam->data();
        const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
        const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
        const glm::vec3 up(d->upX, d->upY, d->upZ);
        const int vw = g->getPixelWidth() > 0 ? g->getPixelWidth() : g->getWidth();
        const int vh = g->getPixelHeight() > 0 ? g->getPixelHeight() : g->getHeight();
        if (outWidth) *outWidth = vw;
        if (outHeight) *outHeight = vh;
        if (vw <= 0 || vh <= 0) return "{\"error\":\"empty viewport\"}";
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const glm::mat4 projM = cameraProjectionVulkanRH_ZO(
            d->orthographic, degToRad(d->fovYDeg), d->orthoHeight,
            float(vw) / float(vh), d->nearZ, d->farZ);
        const glm::mat4 viewProj = projM * viewM;

        std::vector<Graphics::EntityIdDraw> draws;
        int id = 1;
        std::string entities;
        scene::TransformSystem::updateAll();
        if (ecs::current()->getManager<scene::SceneHost>() != nullptr) {
            auto hostView = ecs::View<scene::SceneHost, scene::SceneHost::Meta,
                                      scene::SceneHost::Tree>();
            for (auto it = hostView.begin(); it != hostView.end(); ++it) {
                auto [meta, tree] = *it;
                if (!meta || !meta->entity || !tree) continue;
                auto *host = meta->entity;
                host->walkDepthFirst([&](scene::SceneHost *, int, scene::SceneNode &node) {
                    if (!node.visible || node.space != "3d") return;
                    const auto *l = host->findLink(&node, scene::findLinkKind("renderable3d"));
                    if (!l || !l->target) return;
                    auto *r = static_cast<Renderable3D *>(l->target);
                    auto mr = r->meshRenderer();
                    Mesh *mesh = mr->mesh;
                    if (!mesh || !mesh->gpuHandle) return;
                    Graphics::EntityIdDraw draw;
                    draw.mesh = mesh;
                    draw.model = node.world;
                    const uint32_t packed = uint32_t(id);
                    draw.idColor = glm::vec4((packed & 255u) / 255.f,
                                             ((packed >> 8) & 255u) / 255.f,
                                             ((packed >> 16) & 255u) / 255.f, 1.f);
                    draws.push_back(draw);
                    if (!entities.empty()) entities += ",";
                    entities += "{\"id\":" + std::to_string(id) +
                                ",\"asset\":\"" + jsonEscape(node.name.empty() ? node.id : node.name) +
                                "\",\"node\":\"" + jsonEscape(node.id.empty() ? node.name : node.id) + "\"}";
                    ++id;
                });
            }
        }
        image::ImageData *img = g->renderEntityIdMask(draws, viewProj, vw, vh);
        if (!img) return "{\"error\":\"id mask unsupported\"}";
        delete img;
        if (ok) *ok = true;
        return "{\"camera\":" + vec3Json(eye) + ",\"viewportWidth\":" + std::to_string(vw) +
               ",\"viewportHeight\":" + std::to_string(vh) + ",\"entities\":[" + entities +
               "],\"count\":" + std::to_string(id - 1) + "}";
    }

    bool entityIdMaskPng(const std::string &path, std::string *err) override {
        auto *g = gfx();
        if (!g) {
            if (err) *err = "Graphics module not available";
            return false;
        }
        auto *cam = camera();
        if (!cam) {
            if (err) *err = "no camera";
            return false;
        }
        auto d = cam->data();
        const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
        const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
        const glm::vec3 up(d->upX, d->upY, d->upZ);
        const int vw = g->getPixelWidth() > 0 ? g->getPixelWidth() : g->getWidth();
        const int vh = g->getPixelHeight() > 0 ? g->getPixelHeight() : g->getHeight();
        if (vw <= 0 || vh <= 0) {
            if (err) *err = "empty viewport";
            return false;
        }
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const glm::mat4 projM = cameraProjectionVulkanRH_ZO(
            d->orthographic, degToRad(d->fovYDeg), d->orthoHeight,
            float(vw) / float(vh), d->nearZ, d->farZ);
        const glm::mat4 viewProj = projM * viewM;
        std::vector<Graphics::EntityIdDraw> draws;
        int id = 1;
        scene::TransformSystem::updateAll();
        if (ecs::current()->getManager<scene::SceneHost>() != nullptr) {
            auto hostView = ecs::View<scene::SceneHost, scene::SceneHost::Meta,
                                      scene::SceneHost::Tree>();
            for (auto it = hostView.begin(); it != hostView.end(); ++it) {
                auto [meta, tree] = *it;
                if (!meta || !meta->entity || !tree) continue;
                auto *host = meta->entity;
                host->walkDepthFirst([&](scene::SceneHost *, int, scene::SceneNode &node) {
                    if (!node.visible || node.space != "3d") return;
                    const auto *l = host->findLink(&node, scene::findLinkKind("renderable3d"));
                    if (!l || !l->target) return;
                    auto *r = static_cast<Renderable3D *>(l->target);
                    auto mr = r->meshRenderer();
                    Mesh *mesh = mr->mesh;
                    if (!mesh || !mesh->gpuHandle) return;
                    Graphics::EntityIdDraw draw;
                    draw.mesh = mesh;
                    draw.model = node.world;
                    const uint32_t packed = uint32_t(id);
                    draw.idColor = glm::vec4((packed & 255u) / 255.f,
                                             ((packed >> 8) & 255u) / 255.f,
                                             ((packed >> 16) & 255u) / 255.f, 1.f);
                    draws.push_back(draw);
                    ++id;
                });
            }
        }
        image::ImageData *img = g->renderEntityIdMask(draws, viewProj, vw, vh);
        if (!img) {
            if (err) *err = "id mask unsupported";
            return false;
        }
        filesystem::FileData *png = nullptr;
        try {
            png = img->encode(image::ImageData::FormatHandler::ENCODED_PNG, "id.png", false);
        } catch (...) {
            delete img;
            if (err) *err = "png encode failed";
            return false;
        }
        bool ok = false;
        if (png) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
            std::ofstream out(path, std::ios::binary);
            if (out.good()) {
                out.write(static_cast<const char *>(png->getData()),
                          static_cast<std::streamsize>(png->getSize()));
                ok = out.good();
            }
        }
        delete png;
        delete img;
        if (!ok && err) *err = "failed writing id mask";
        return ok;
    }

    bool gbufferPng(const std::string &name, const std::string &path, std::string *err) override {
        auto *g = gfx();
        if (!g) {
            if (err) *err = "Graphics module not available";
            return false;
        }
        image::ImageData *img = g->readGBufferToImageData(name);
        if (!img) {
            if (err) *err = "G-buffer attachment unavailable: " + name;
            return false;
        }
        filesystem::FileData *png = nullptr;
        try {
            png = img->encode(image::ImageData::FormatHandler::ENCODED_PNG, "buf.png", false);
        } catch (...) {
            delete img;
            if (err) *err = "png encode failed";
            return false;
        }
        const bool ok = png != nullptr;
        if (ok) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
            std::ofstream out(path, std::ios::binary);
            if (out.good()) {
                out.write(static_cast<const char *>(png->getData()),
                          static_cast<std::streamsize>(png->getSize()));
            }
        }
        delete png;
        delete img;
        if (!ok && err) *err = "png encode returned null";
        return ok;
    }

private:
    std::string visibleAt(const glm::vec3 *eye, const glm::vec3 *target, float fovYDeg,
                          bool *ok) {
        auto *g = gfx();
        if (ok) *ok = false;
        auto *cam = camera();

        glm::vec3 camEye(0.f, 1.8f, 3.f);
        glm::vec3 camTarget(0.f, 1.2f, 0.f);
        float camFov = 60.f;
        float camNear = 0.1f, camFar = 100.f;
        if (cam) {
            auto d = cam->data();
            camEye = glm::vec3(d->eyeX, d->eyeY, d->eyeZ);
            camTarget = glm::vec3(d->targetX, d->targetY, d->targetZ);
            camFov = d->fovYDeg;
            camNear = d->nearZ;
            camFar = d->farZ;
        }
        if (eye && target) {
            camEye = *eye;
            camTarget = *target;
        }
        if (fovYDeg > 0.f) camFov = fovYDeg;

        const int vw = g ? g->getPixelWidth() : 1280;
        const int vh = g ? g->getPixelHeight() : 720;
        const glm::vec3 up(0.f, 1.f, 0.f);
        const glm::mat4 view = glm::lookAtRH(camEye, camTarget, up);
        const glm::mat4 proj = perspectiveVulkanRH_ZO(degToRad(camFov), float(vw) / float(vh),
                                                      camNear, camFar);
        const glm::mat4 viewProj = proj * view;

        static const glm::vec3 kLocalCorners[8] = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
            {-0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, -0.5f},  {0.5f, -0.5f, 0.5f},
            {-0.5f, 0.5f, 0.5f},   {0.5f, 0.5f, 0.5f}};

        std::string entities;
        int count = 0;
        scene::TransformSystem::updateAll();
        if (ecs::current()->getManager<scene::SceneHost>() != nullptr) {
            auto hostView = ecs::View<scene::SceneHost, scene::SceneHost::Meta,
                                      scene::SceneHost::Tree>();
            for (auto it = hostView.begin(); it != hostView.end(); ++it) {
                auto [meta, tree] = *it;
                if (!meta || !meta->entity || !tree) continue;
                auto *host = meta->entity;
                host->walkDepthFirst([&](scene::SceneHost *, int, scene::SceneNode &node) {
                    if (!node.visible || node.space != "3d") return;
                    float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
                    float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
                    float scMinX = 1e30f, scMinY = 1e30f;
                    float scMaxX = -1e30f, scMaxY = -1e30f;
                    bool anyInFront = false;
                    for (const auto &c : kLocalCorners) {
                        const glm::vec4 w = node.world * glm::vec4(c, 1.f);
                        minX = std::min(minX, w.x);
                        minY = std::min(minY, w.y);
                        minZ = std::min(minZ, w.z);
                        maxX = std::max(maxX, w.x);
                        maxY = std::max(maxY, w.y);
                        maxZ = std::max(maxZ, w.z);
                        const glm::vec4 clip = viewProj * w;
                        if (clip.w <= 1e-6f) continue;
                        anyInFront = true;
                        const float ndcX = clip.x / clip.w;
                        const float ndcY = clip.y / clip.w;
                        const float sx = (ndcX * 0.5f + 0.5f) * float(vw);
                        const float sy = (ndcY * 0.5f + 0.5f) * float(vh);
                        scMinX = std::min(scMinX, sx);
                        scMinY = std::min(scMinY, sy);
                        scMaxX = std::max(scMaxX, sx);
                        scMaxY = std::max(scMaxY, sy);
                    }
                    if (!anyInFront) return;
                    if (scMaxX < 0.f || scMinX > float(vw) || scMaxY < 0.f ||
                        scMinY > float(vh))
                        return;
                    if (!entities.empty()) entities += ",";
                    entities += "{\"id\":\"" + jsonEscape(node.id.empty() ? node.name : node.id) +
                                "\",\"asset\":\"" +
                                jsonEscape(node.name.empty() ? node.id : node.name) +
                                "\",\"world_aabb\":{\"min\":" + vec3Json(glm::vec3(minX, minY, minZ)) +
                                ",\"max\":" + vec3Json(glm::vec3(maxX, maxY, maxZ)) +
                                "},\"screen_bbox\":{\"x\":" + num(scMinX) + ",\"y\":" + num(scMinY) +
                                ",\"w\":" + num(scMaxX - scMinX) + ",\"h\":" + num(scMaxY - scMinY) +
                                "}}";
                    ++count;
                });
            }
        }
        if (ok) *ok = true;
        return "{\"camera\":" + vec3Json(camEye) + ",\"cameraTarget\":" + vec3Json(camTarget) +
               ",\"fov\":" + num(camFov) + ",\"viewportWidth\":" + std::to_string(vw) +
               ",\"viewportHeight\":" + std::to_string(vh) + ",\"entities\":[" + entities +
               "],\"count\":" + std::to_string(count) + "}";
    }
};

class ProcgenProbeSinkImpl final : public eve::IProcgenProbeSink {
public:
    struct Batch {
        std::vector<std::unique_ptr<ReflectionProbeCapture>> reflections;
        std::vector<eve::ProcgenProbeDesc> lights;
    };

    Result<int> replaceProbeBatch(const std::string& batchId,
                                  const std::vector<eve::ProcgenProbeDesc>& probes) override {
        Graphics* graphics = eve::ModuleManager::getInstance<Graphics>("Graphics");
        if (batchId.empty() || !graphics)
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                "procedural probe publication requires a batch id and graphics provider"));
        Batch candidate;
        std::unordered_set<std::uint64_t> identities;
        for (const auto& probe : probes) {
            if (!probe.sourcePointId || !identities.insert(probe.sourcePointId).second ||
                probe.resource.empty() || (probe.type != 0 && probe.type != 1) ||
                !std::isfinite(probe.x) || !std::isfinite(probe.y) || !std::isfinite(probe.z) ||
                !std::isfinite(probe.extentX) || !std::isfinite(probe.extentY) || !std::isfinite(probe.extentZ) ||
                probe.extentX <= 0 || probe.extentY <= 0 || probe.extentZ <= 0 ||
                probe.resolution < 16 || probe.resolution > 2048 ||
                !std::isfinite(probe.clipDistance) || probe.clipDistance <= 0 ||
                !std::isfinite(probe.irradianceR) || !std::isfinite(probe.irradianceG) ||
                !std::isfinite(probe.irradianceB) || probe.irradianceR < 0 || probe.irradianceG < 0 ||
                probe.irradianceB < 0)
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                    "procedural probe publication requires valid unique probe descriptors"));
            if (probe.hasSphericalHarmonics &&
                !std::all_of(probe.sphericalHarmonics.begin(), probe.sphericalHarmonics.end(),
                             [](float coefficient) { return std::isfinite(coefficient); }))
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                    "procedural light probe spherical harmonics must be finite"));
            if (probe.type == 1) {
                candidate.lights.push_back(probe);
                continue;
            }
            auto capture = std::make_unique<ReflectionProbeCapture>(graphics);
            capture->configure(probe.x, probe.y, probe.z, probe.resolution, 0.1F, probe.clipDistance);
            capture->configureInfluence(probe.extentX, probe.extentY, probe.extentZ, 1, 1, 0);
            capture->requestCapture();
            candidate.reflections.push_back(std::move(capture));
        }
        ensureRegistry();
        for (auto& probe : candidate.reflections) registry_->add(probe.get());
        auto found = batches_.find(batchId);
        if (found != batches_.end())
            for (auto& probe : found->second.reflections) registry_->remove(probe.get());
        DiffuseLightProbeRegistry::instance().replaceBatch(batchId, candidate.lights);
        batches_.insert_or_assign(batchId, std::move(candidate));
        return Result<int>::success(static_cast<int>(probes.size()));
    }

    Result<int> removeProbeBatch(const std::string& batchId) override {
        auto found = batches_.find(batchId);
        if (found == batches_.end())
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                "procedural probe batch was not published", batchId));
        const int count = static_cast<int>(found->second.reflections.size() + found->second.lights.size());
        if (registry_)
            for (auto& probe : found->second.reflections) registry_->remove(probe.get());
        DiffuseLightProbeRegistry::instance().removeBatch(batchId);
        batches_.erase(found);
        return Result<int>::success(count);
    }

    Result<int> tickProbeBatches(int faceBudget, int filterBudget, int filterSamples) override {
        if (faceBudget < 0 || filterBudget < 0 || filterSamples < 8 || filterSamples > 512)
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                "procedural probe tick requires nonnegative budgets and 8..512 filter samples"));
        ensureRegistry();
        return Result<int>::success(registry_->tick(faceBudget, filterBudget, filterSamples));
    }
    int probeCount(const std::string& batchId) const override {
        auto found = batches_.find(batchId);
        return found == batches_.end() ? 0 : static_cast<int>(found->second.reflections.size() + found->second.lights.size());
    }
    int reflectionProbeCount(const std::string& batchId) const override {
        auto found = batches_.find(batchId);
        return found == batches_.end() ? 0 : static_cast<int>(found->second.reflections.size());
    }
    int lightProbeCount(const std::string& batchId) const override {
        auto found = batches_.find(batchId);
        return found == batches_.end() ? 0 : static_cast<int>(found->second.lights.size());
    }
private:
    void ensureRegistry() {
        if (!registry_) registry_ = std::make_unique<ReflectionProbeRegistry>();
    }
    std::unique_ptr<ReflectionProbeRegistry> registry_;
    std::unordered_map<std::string, Batch> batches_;
};

}  // namespace

void registerGraphicsCapabilities() {
    static RenderCaptureImpl impl;
    static ProcgenProbeSinkImpl probeSink;
    eve::cap::provide<eve::IRenderCapture>(&impl);
    eve::cap::provide<eve::IProcgenProbeSink>(&probeSink);
}

}  // namespace eve::graphics
