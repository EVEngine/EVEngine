#include "devtools/SceneInspect.hpp"

#include "common/ECS.h"
#include "common/Module.h"

#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"

#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eve::dev {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float degToRad(float d) { return d * kPi / 180.f; }

std::string mcpStringify(const Poco::Dynamic::Var& v) {
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(v, oss, 0, 0);
    return oss.str();
}

glm::vec3 jsonVec3(Poco::JSON::Object::Ptr obj, const char* key, const glm::vec3& def) {
    if (!obj || !obj->has(key)) return def;
    try {
        auto arr = obj->getArray(key);
        if (!arr || arr->size() < 3) return def;
        return glm::vec3(static_cast<float>(arr->get(0).convert<double>()),
                         static_cast<float>(arr->get(1).convert<double>()),
                         static_cast<float>(arr->get(2).convert<double>()));
    } catch (...) {
        return def;
    }
}

bool saveImageToPng(image::ImageData* img, const std::string& path) {
    if (!img) return false;
    eve::image::Image::create();
    filesystem::FileData* png = nullptr;
    try {
        png = img->encode(medialoader::FormatHandler::ENCODED_PNG, "buf.png", false);
    } catch (...) {
        return false;
    }
    if (!png) return false;
    const std::filesystem::path outPath(path);
    std::error_code             ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary);
    if (!out.good()) {
        delete png;
        return false;
    }
    out.write(static_cast<const char*>(png->getData()),
              static_cast<std::streamsize>(png->getSize()));
    const bool ok = out.good();
    delete png;
    return ok;
}

/** 收集当前可见 3D 场景实体为逐像素 ID 绘制，渲染并读回为 ImageData。 */
image::ImageData* buildIdMask(eve::graphics::Graphics* gfx, std::string* outJson) {
    if (!gfx) return nullptr;
    auto* cam = SceneInspect::instance().findOrCreateCamera();
    if (!cam) return nullptr;
    auto d = cam->data();
    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
    const glm::vec3 up(d->upX, d->upY, d->upZ);
    const float     fovRad = degToRad(d->fovYDeg);

    const int vw = gfx->getPixelWidth() > 0 ? gfx->getPixelWidth() : gfx->getWidth();
    const int vh = gfx->getPixelHeight() > 0 ? gfx->getPixelHeight() : gfx->getHeight();
    if (vw <= 0 || vh <= 0) return nullptr;

    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const glm::mat4 projM = eve::graphics::perspectiveVulkanRH_ZO(
        fovRad, float(vw) / float(vh), d->nearZ, d->farZ);
    const glm::mat4 viewProj = projM * viewM;

    Poco::JSON::Object::Ptr root(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    root->set("camera", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    root->getArray("camera")->add(eye.x);
    root->getArray("camera")->add(eye.y);
    root->getArray("camera")->add(eye.z);
    root->set("viewportWidth", vw);
    root->set("viewportHeight", vh);
    Poco::JSON::Array::Ptr entities(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));

    std::vector<eve::graphics::Graphics::EntityIdDraw> draws;
    int                                                id = 1;

    eve::scene::TransformSystem::updateAll();
    if (ecs::current()->getManager<eve::scene::SceneHost>() != nullptr) {
        auto hostView = ecs::View<eve::scene::SceneHost, eve::scene::SceneHost::Meta,
                                  eve::scene::SceneHost::Tree>();
        for (auto it = hostView.begin(); it != hostView.end(); ++it) {
            auto [meta, tree] = *it;
            if (!meta || !meta->entity) continue;
            auto* host = meta->entity;
            if (!tree) continue;
            host->walkDepthFirst([&](eve::scene::SceneHost*, int,
                                     eve::scene::SceneNode& node) {
                if (!node.visible || node.space != "3d") return;
                if (node.linkKind != "renderable3d" || !node.linkTarget) return;
                auto* r = static_cast<eve::graphics::Renderable3D*>(node.linkTarget);
                auto  mr = r->meshRenderer();
                graphics::Mesh* mesh = mr->mesh;
                if (!mesh || !mesh->gpuHandle) return;

                eve::graphics::Graphics::EntityIdDraw draw;
                draw.mesh  = mesh;
                draw.model = node.world;
                const uint32_t packed = uint32_t(id);
                draw.idColor = glm::vec4((packed & 255u) / 255.f,
                                         ((packed >> 8) & 255u) / 255.f,
                                         ((packed >> 16) & 255u) / 255.f, 1.f);
                draws.push_back(draw);

                Poco::JSON::Object::Ptr eo(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
                eo->set("id", id);
                eo->set("asset", node.name.empty() ? node.id : node.name);
                eo->set("node", node.id.empty() ? node.name : node.id);
                entities->add(eo);
                ++id;
            });
        }
    }
    root->set("entities", entities);
    root->set("count", id - 1);

    image::ImageData* img = gfx->renderEntityIdMask(draws, viewProj, vw, vh);
    if (!img) return nullptr;
    if (outJson) *outJson = mcpStringify(Poco::Dynamic::Var(root));
    return img;
}

}  // namespace

SceneInspect& SceneInspect::instance() {
    static SceneInspect inst;
    return inst;
}

graphics::Graphics* SceneInspect::graphics() {
    return eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
}

graphics::Camera3D* SceneInspect::findOrCreateCamera() {
    // 与 RenderSystem3D::render 的默认机位发现逻辑保持一致。
    if (ecs::current()->getManager<eve::graphics::Camera3D>() != nullptr) {
        auto view = ecs::View<eve::graphics::Camera3D, eve::graphics::Camera3D::Data>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [data] = *it;
            if (data->active && data->entity) return data->entity;
        }
    }
    return eve::graphics::Camera3D::createCamera();
}

std::vector<InspectView> SceneInspect::generateViews(const glm::vec3& center, float fov) {
    std::vector<InspectView> out;
    const float              base = fov > 0.f ? fov : 60.f;

    // 沿路平视：低机位贴近街道，视线水平略前倾看向目标。
    {
        InspectView v;
        v.name    = "along_road";
        v.kind    = "沿路平视";
        v.eye     = center + glm::vec3(0.f, 1.8f, -6.f);
        v.target  = center + glm::vec3(0.f, 1.2f, 0.f);
        v.fovYDeg = base;
        out.push_back(v);
    }
    // 高空俯拍：目标正上方垂直往下。
    {
        InspectView v;
        v.name    = "bird_eye";
        v.kind    = "高空俯拍";
        v.eye     = center + glm::vec3(0.f, 30.f, 0.f);
        v.target  = center;
        v.fovYDeg = std::min(base * 1.2f, 100.f);
        out.push_back(v);
    }
    // 拐角特写：贴近目标、斜向特写，窄 FOV 突出局部。
    {
        InspectView v;
        v.name    = "corner_close";
        v.kind    = "拐角特写";
        v.eye     = center + glm::vec3(2.4f, 1.2f, 2.4f);
        v.target  = center + glm::vec3(0.f, 0.8f, 0.f);
        v.fovYDeg = std::max(base * 0.8f, 25.f);
        out.push_back(v);
    }
    // 远景视角：抬高拉远，广角俯览整片区域。
    {
        InspectView v;
        v.name    = "vista";
        v.kind    = "远景视角";
        v.eye     = center + glm::vec3(0.f, 14.f, -22.f);
        v.target  = center;
        v.fovYDeg = std::max(base * 0.85f, 35.f);
        out.push_back(v);
    }
    return out;
}

bool SceneInspect::setCameraView(const InspectView& v) {
    auto* cam = findOrCreateCamera();
    if (!cam) return false;
    cam->setActive(true);
    cam->setEye(v.eye.x, v.eye.y, v.eye.z);
    cam->setTarget(v.target.x, v.target.y, v.target.z);
    if (v.fovYDeg > 0.f) cam->setFov(v.fovYDeg);
    return true;
}

bool SceneInspect::setCameraPose(const glm::vec3& eye, const glm::vec3& rotYawPitch,
                                 float fov) {
    auto* cam = findOrCreateCamera();
    if (!cam) return false;
    const float yaw   = degToRad(rotYawPitch.x);
    const float pitch = degToRad(rotYawPitch.y);
    // 与 CameraController::desired() 的 firstperson 朝向约定一致（Y-up）。
    const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw));

    cam->setActive(true);
    cam->setEye(eye.x, eye.y, eye.z);
    cam->setTarget(eye.x + dir.x * 100.f, eye.y + dir.y * 100.f, eye.z + dir.z * 100.f);
    if (fov > 0.f) cam->setFov(fov);
    return true;
}

std::string SceneInspect::defaultCacheDir() const {
    std::string root;
    if (auto* mcp = eve::ModuleManager::find("McpServer"); mcp) {
        // 优先复用 MCP 的 gameRoot 作为缓存根。
        // 这里通过 McpServer 静态方法不可行（无静态 gameRoot），直接回退到 cwd。
        (void)mcp;
    }
    root = std::filesystem::current_path().string();
    for (char& c : root)
        if (c == '\\') c = '/';
    if (root.back() == '/') root.pop_back();
    return root + "/eve_inspect_cache";
}

bool SceneInspect::savePng(graphics::Graphics* gfx, const std::string& path, std::string& err) {
    eve::image::Image::create();
    image::ImageData* frame = nullptr;
    try {
        frame = gfx->newImageData();
    } catch (const std::exception& e) {
        err = std::string("no presented frame: ") + e.what();
        return false;
    } catch (...) {
        err = "no presented frame";
        return false;
    }
    if (!frame) {
        err = "no presented frame (enable readback and render a frame first)";
        return false;
    }

    filesystem::FileData* png = nullptr;
    try {
        png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, "frame.png", false);
    } catch (const std::exception& e) {
        delete frame;
        err = std::string("png encode failed: ") + e.what();
        return false;
    }
    if (!png) {
        delete frame;
        err = "png encode returned null";
        return false;
    }

    const std::filesystem::path outPath(path);
    std::error_code            ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary);
    if (!out.good()) {
        delete png;
        delete frame;
        err = "cannot open output file: " + path;
        return false;
    }
    out.write(static_cast<const char*>(png->getData()),
              static_cast<std::streamsize>(png->getSize()));
    const bool ok = out.good();
    delete png;
    delete frame;
    if (!ok) {
        err = "failed writing: " + path;
        return false;
    }
    return true;
}

InspectCapture SceneInspect::capture(const std::string& outDir, const std::string& tag,
                                     const std::vector<std::string>& buffers) {
    InspectCapture result;
    auto*          gfx = graphics();
    if (!gfx) {
        result.error = "Graphics module not available";
        return result;
    }

    // 解析请求的缓冲区。
    bool wantColor = buffers.empty();
    bool wantDepth = false, wantNormal = false, wantId = false, wantShadow = false;
    for (const auto& b : buffers) {
        if (b == "color") wantColor = true;
        else if (b == "depth") wantDepth = true;
        else if (b == "normal") wantNormal = true;
        else if (b == "id") wantId = true;
        else if (b == "shadow") wantShadow = true;
    }
    if (!wantColor) wantColor = true;  // 帧始终作为主 PNG 输出

    // 锁定当前相机 → 抓帧。先开启读回（对后续帧生效），并尝试读回上一帧。
    gfx->setScreenReadbackEnabled(true);

    std::string dir = outDir;
    if (dir.empty()) {
        dir = cacheDir();
        if (dir.empty()) dir = defaultCacheDir();
    }
    for (char& c : dir)
        if (c == '\\') c = '/';

    const std::string safeTag = tag.empty() ? "frame" : tag;
    const std::string pngPath = dir + "/" + safeTag + ".png";
    const std::string jsonPath = dir + "/" + safeTag + ".json";
    const std::string depthPngPath = dir + "/" + safeTag + "_depth.png";
    const std::string normalPngPath = dir + "/" + safeTag + "_normal.png";
    const std::string idPngPath = dir + "/" + safeTag + "_id.png";
    const std::string idJsonPath = dir + "/" + safeTag + "_id.json";

    std::string err;
    if (!savePng(gfx, pngPath, err)) {
        result.error = err;
        return result;
    }
    result.width  = gfx->getPixelWidth();
    result.height = gfx->getPixelHeight();

    // 立刻导出配套几何 JSON（同一相机位姿，保证同步）。
    const std::string geoJson = visibleEntitiesJson();
    {
        const std::filesystem::path jp(jsonPath);
        std::error_code             ec;
        std::filesystem::create_directories(jp.parent_path(), ec);
        std::ofstream jout(jp, std::ios::binary);
        if (jout.good()) {
            jout.write(geoJson.data(), static_cast<std::streamsize>(geoJson.size()));
            jout.flush();
            result.entityCount = 0;
            const auto pos = geoJson.find("\"count\":");
            if (pos != std::string::npos) {
                try {
                    result.entityCount = std::atoi(geoJson.c_str() + pos + 9);
                } catch (...) {
                }
            }
        }
    }

    // ---- depth / normal（读回 GBuffer） ----
    auto* gb = gfx->getRenderControl() ? gfx->getRenderControl()->getGBuffer() : nullptr;
    auto saveBuffer = [&](const std::string& bufName, const std::string& path,
                          const std::string& displayName) {
        image::ImageData* img = gfx->readGBufferToImageData(bufName);
        if (!img) {
            result.unsupported.push_back(displayName);
            return;
        }
        saveImageToPng(img, path);
        if (bufName == "depth") result.depthPngPath = path;
        else if (bufName == "normal") result.normalPngPath = path;
        delete img;
    };
    if (wantDepth) {
        if (gb && gb->isValid())
            saveBuffer("depth", depthPngPath, "depth");
        else
            result.unsupported.push_back("depth");
    }
    if (wantNormal) {
        if (gb && gb->isValid())
            saveBuffer("normal", normalPngPath, "normal");
        else
            result.unsupported.push_back("normal");
    }

    // ---- shadow：后端未暴露便携式阴影深度纹理 ----
    if (wantShadow) result.unsupported.push_back("shadow");

    // ---- 渲染 ID mask（逐像素实体 ID） ----
    if (wantId) {
        std::string idJson;
        image::ImageData* idImg = buildIdMask(gfx, &idJson);
        if (!idImg) {
            result.unsupported.push_back("id");
        } else {
            saveImageToPng(idImg, idPngPath);
            delete idImg;
            const std::filesystem::path jp(idJsonPath);
            std::error_code             ec;
            std::filesystem::create_directories(jp.parent_path(), ec);
            std::ofstream jout(jp, std::ios::binary);
            if (jout.good()) jout.write(idJson.data(), static_cast<std::streamsize>(idJson.size()));
            result.idPngPath = idPngPath;
            result.idJsonPath = idJsonPath;
        }
    }

    result.ok       = true;
    result.pngPath  = pngPath;
    result.jsonPath = jsonPath;
    lastPngPath_    = pngPath;
    lastJsonPath_   = jsonPath;
    return result;
}

std::string SceneInspect::currentPoseJson() {
    Poco::JSON::Object::Ptr o   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto*                   gfx = graphics();
    auto*                   cam = findOrCreateCamera();
    if (!cam) {
        o->set("error", "no camera");
        return mcpStringify(Poco::Dynamic::Var(o));
    }
    auto d = cam->data();
    Poco::JSON::Array::Ptr eye(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    eye->add(d->eyeX);
    eye->add(d->eyeY);
    eye->add(d->eyeZ);
    Poco::JSON::Array::Ptr tgt(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    tgt->add(d->targetX);
    tgt->add(d->targetY);
    tgt->add(d->targetZ);
    o->set("eye", eye);
    o->set("target", tgt);
    o->set("up", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    o->getArray("up")->add(d->upX);
    o->getArray("up")->add(d->upY);
    o->getArray("up")->add(d->upZ);
    o->set("fov", static_cast<double>(d->fovYDeg));
    o->set("near", static_cast<double>(d->nearZ));
    o->set("far", static_cast<double>(d->farZ));
    if (gfx) {
        o->set("viewportWidth", gfx->getPixelWidth());
        o->set("viewportHeight", gfx->getPixelHeight());
    }
    return mcpStringify(Poco::Dynamic::Var(o));
}

std::string SceneInspect::visibleEntitiesJson(const glm::vec3* eye, const glm::vec3* target,
                                              float fov) {
    Poco::JSON::Object::Ptr root(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    auto*                   gfx = graphics();
    auto*                   cam = findOrCreateCamera();

    glm::vec3 camEye(0.f, 1.8f, 3.f);
    glm::vec3 camTarget(0.f, 1.2f, 0.f);
    float     camFov = 60.f;
    float     camNear = 0.1f, camFar = 100.f;
    if (cam) {
        auto d = cam->data();
        camEye    = glm::vec3(d->eyeX, d->eyeY, d->eyeZ);
        camTarget = glm::vec3(d->targetX, d->targetY, d->targetZ);
        camFov    = d->fovYDeg;
        camNear   = d->nearZ;
        camFar    = d->farZ;
    }
    if (eye && target) {
        camEye    = *eye;
        camTarget = *target;
    }
    if (fov > 0.f) camFov = fov;

    const int vw = gfx ? gfx->getPixelWidth() : 1280;
    const int vh = gfx ? gfx->getPixelHeight() : 720;

    // 相机参数（供 Agent 核对几何快照与图像的一致性）。
    Poco::JSON::Array::Ptr eyeArr(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    eyeArr->add(camEye.x);
    eyeArr->add(camEye.y);
    eyeArr->add(camEye.z);
    Poco::JSON::Array::Ptr tgtArr(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    tgtArr->add(camTarget.x);
    tgtArr->add(camTarget.y);
    tgtArr->add(camTarget.z);
    root->set("camera", eyeArr);
    root->set("cameraTarget", tgtArr);
    root->set("fov", static_cast<double>(camFov));
    root->set("viewportWidth", vw);
    root->set("viewportHeight", vh);

    const glm::vec3 up(0.f, 1.f, 0.f);
    const glm::mat4 view = glm::lookAtRH(camEye, camTarget, up);
    const glm::mat4 proj =
        eve::graphics::perspectiveVulkanRH_ZO(degToRad(camFov), float(vw) / float(vh),
                                              camNear, camFar);
    const glm::mat4 viewProj = proj * view;

    // 单位立方体（局部空间）8 个角点；经 node.world 变换得世界 AABB。
    static const glm::vec3 kLocalCorners[8] = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, -0.5f},  {0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f},   {0.5f, 0.5f, 0.5f}};

    Poco::JSON::Array::Ptr entities(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    int                     count = 0;

    eve::scene::TransformSystem::updateAll();  // 保证 node.world 最新

    if (ecs::current()->getManager<eve::scene::SceneHost>() != nullptr) {
        auto hostView = ecs::View<eve::scene::SceneHost, eve::scene::SceneHost::Meta,
                                  eve::scene::SceneHost::Tree>();
        for (auto it = hostView.begin(); it != hostView.end(); ++it) {
            auto [meta, tree] = *it;
            if (!meta || !meta->entity) continue;
            auto* host = meta->entity;
            if (!tree) continue;

            host->walkDepthFirst([&](eve::scene::SceneHost*, int, eve::scene::SceneNode& node) {
                if (!node.visible) return;
                if (node.space != "3d") return;

                // 世界 AABB。
                float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
                float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
                float scMinX = 1e30f, scMinY = 1e30f;
                float scMaxX = -1e30f, scMaxY = -1e30f;
                bool  anyInFront = false;
                for (const auto& c : kLocalCorners) {
                    const glm::vec4 w = node.world * glm::vec4(c, 1.f);
                    minX = std::min(minX, w.x);
                    minY = std::min(minY, w.y);
                    minZ = std::min(minZ, w.z);
                    maxX = std::max(maxX, w.x);
                    maxY = std::max(maxY, w.y);
                    maxZ = std::max(maxZ, w.z);
                    const glm::vec4 clip = viewProj * w;
                    if (clip.w <= 1e-6f) continue;  // 相机后方
                    anyInFront = true;
                    const float ndcX = clip.x / clip.w;
                    const float ndcY = clip.y / clip.w;
                    const float sx   = (ndcX * 0.5f + 0.5f) * float(vw);
                    const float sy   = (ndcY * 0.5f + 0.5f) * float(vh);
                    scMinX = std::min(scMinX, sx);
                    scMinY = std::min(scMinY, sy);
                    scMaxX = std::max(scMaxX, sx);
                    scMaxY = std::max(scMaxY, sy);
                }
                if (!anyInFront) return;
                // 屏幕包围盒与视口相交（含部分可见）才算“视锥内”。
                if (scMaxX < 0.f || scMinX > float(vw) || scMaxY < 0.f || scMinY > float(vh))
                    return;

                Poco::JSON::Object::Ptr eo(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
                eo->set("id", node.id.empty() ? node.name : node.id);
                eo->set("asset", node.name.empty() ? node.id : node.name);

                Poco::JSON::Object::Ptr wabb(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
                Poco::JSON::Array::Ptr wmin(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
                wmin->add(minX);
                wmin->add(minY);
                wmin->add(minZ);
                Poco::JSON::Array::Ptr wmax(Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
                wmax->add(maxX);
                wmax->add(maxY);
                wmax->add(maxZ);
                wabb->set("min", wmin);
                wabb->set("max", wmax);
                eo->set("world_aabb", wabb);

                Poco::JSON::Object::Ptr sbb(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
                sbb->set("x", scMinX);
                sbb->set("y", scMinY);
                sbb->set("w", scMaxX - scMinX);
                sbb->set("h", scMaxY - scMinY);
                eo->set("screen_bbox", sbb);

                entities->add(eo);
                ++count;
            });
        }
    }

    root->set("entities", entities);
    root->set("count", count);
    return mcpStringify(Poco::Dynamic::Var(root));
}

}  // namespace eve::dev
