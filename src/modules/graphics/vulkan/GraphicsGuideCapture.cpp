#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <unordered_map>

#include "common/ActionGuidePack.h"
#include "common/Capability.h"
#include "graphics/ClipSpace.h"
#include "graphics/RenderTransformProjection.h"
#include "graphics/shaders/primitive3d_frag_spv.inc"
#include "graphics/shaders/primitive3d_vert_spv.inc"
#include "graphics/vulkan/FrameReadback.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
namespace {

template <class T>
Result<T> guideFail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message)));
}

bool finitePosition(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool shotSelected(const std::vector<std::string>& selected, const std::string& shotId) {
    return selected.empty() || std::find(selected.begin(), selected.end(), shotId) != selected.end();
}

bool wantsAov(const ActionGuideShot& shot, std::string_view name) {
    return std::find(shot.aovs.begin(), shot.aovs.end(), name) != shot.aovs.end();
}

std::array<float, 3> boneHue(std::string_view boneId) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : boneId) {
        hash ^= c;
        hash *= 16777619u;
    }
    const float h = float(hash % 360u);
    const float s = 0.85f;
    const float v = 1.f;
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;
    float       r = 0, g = 0, b = 0;
    if (h < 60) {
        r = c;
        g = x;
    } else if (h < 120) {
        r = x;
        g = c;
    } else if (h < 180) {
        g = c;
        b = x;
    } else if (h < 240) {
        g = x;
        b = c;
    } else if (h < 300) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    return {r + m, g + m, b + m};
}

void putPixel(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int x, int y, const std::array<float, 3>& rgb) {
    if (x < 0 || y < 0 || uint32_t(x) >= w || uint32_t(y) >= h) return;
    const size_t i = (size_t(y) * w + size_t(x)) * 4;
    rgba[i + 0]    = static_cast<uint8_t>(std::lround(std::clamp(rgb[0], 0.f, 1.f) * 255.f));
    rgba[i + 1]    = static_cast<uint8_t>(std::lround(std::clamp(rgb[1], 0.f, 1.f) * 255.f));
    rgba[i + 2]    = static_cast<uint8_t>(std::lround(std::clamp(rgb[2], 0.f, 1.f) * 255.f));
    rgba[i + 3]    = 255;
}

void drawCircle(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int cx, int cy, int radius,
                const std::array<float, 3>& rgb) {
    const int r2 = radius * radius;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= r2) putPixel(rgba, w, h, cx + x, cy + y, rgb);
}

void drawLine(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int x0, int y0, int x1, int y1,
              const std::array<float, 3>& rgb) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int       err = dx + dy;
    for (;;) {
        putPixel(rgba, w, h, x0, y0, rgb);
        putPixel(rgba, w, h, x0 + 1, y0, rgb);
        putPixel(rgba, w, h, x0, y0 + 1, rgb);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

std::shared_ptr<const std::vector<uint8_t>> rasterizeBoneAov(uint32_t w, uint32_t h,
                                                             const std::vector<ActionGuideLandmark>& landmarks) {
    auto bytes = std::make_shared<std::vector<uint8_t>>(size_t(w) * h * 4, uint8_t{0});
    std::unordered_map<std::string, const ActionGuideLandmark*> byId;
    byId.reserve(landmarks.size());
    for (const auto& landmark : landmarks) byId.emplace(landmark.boneId, &landmark);
    const int radius = std::max(2, int(std::lround(std::min(w, h) * 0.02)));
    for (const auto& landmark : landmarks) {
        const auto rgb = boneHue(landmark.boneId);
        if (landmark.parentId) {
            auto parent = byId.find(*landmark.parentId);
            if (parent != byId.end())
                drawLine(*bytes, w, h, int(std::lround(parent->second->x)), int(std::lround(parent->second->y)),
                         int(std::lround(landmark.x)), int(std::lround(landmark.y)), rgb);
        }
        drawCircle(*bytes, w, h, int(std::lround(landmark.x)), int(std::lround(landmark.y)), radius, rgb);
    }
    return bytes;
}

std::shared_ptr<const std::vector<uint8_t>> depthToGrayRgba(const float* depth, uint32_t w, uint32_t h) {
    auto bytes = std::make_shared<std::vector<uint8_t>>(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t i      = size_t(y) * w + x;
            const float  z      = depth[i];
            const float  nearW  = (!std::isfinite(z) || z >= 1.f) ? 0.f : std::clamp(1.f - z, 0.f, 1.f);
            const auto   g      = static_cast<uint8_t>(std::lround(nearW * 255.f));
            (*bytes)[i * 4 + 0] = g;
            (*bytes)[i * 4 + 1] = g;
            (*bytes)[i * 4 + 2] = g;
            (*bytes)[i * 4 + 3] = 255;
        }
    }
    return bytes;
}

}  // namespace

// Owner-thread provider. Graphics owns this service and retires GPU work before destruction.
// Frozen geometry + CPU AOV bytes are job leases; never mutate the live gameplay Camera3D.
struct Graphics::GuideCaptureService final : IActionGuideCapture {
    Graphics&                         graphics;
    ActionGuideCaptureSnapshot        evidence;
    std::vector<std::string>          selectedShotIds;
    std::vector<ActionGuideShot>      pendingShots;
    size_t                            shotIndex = 0;
    std::vector<Primitive3DVertex>    frozenGeometry;
    std::vector<ActionGuideBoneWorld> frozenBones;
    uint64_t                          frozenFrame = 0;
    vkb::ColorTarget                  color;
    vkb::DepthStencilImage            depth;
    vkb::HostVertexBuffer             vertices;
    vkb::GenericBuffer                colorStaging;
    vkb::GenericBuffer                depthStaging;
    vkb::BuiltRenderPass              pass;
    vk::Framebuffer                   framebuffer{};
    vk::PipelineLayout                layout{};
    vk::Pipeline                      pipeline{};
    vk::CommandPool                   pool{};
    vk::Fence                         fence{};
    bool                              submitted      = false;
    bool                              needSilhouette = false;
    bool                              needDepth      = false;
    bool                              needBone       = false;

    explicit GuideCaptureService(Graphics& owner) : graphics(owner) {}
    ~GuideCaptureService() override { clearGpu(); }

    void clearGpu() {
        auto& device = graphics.device;
        if (pool) device->destroyCommandPool(pool);
        if (fence) device->destroyFence(fence);
        if (pipeline) device->destroyPipeline(pipeline);
        if (layout) device->destroyPipelineLayout(layout);
        if (framebuffer) device->destroyFramebuffer(framebuffer);
        if (pass) device->destroyRenderPass(pass.handle);
        pool        = nullptr;
        fence       = nullptr;
        pipeline    = nullptr;
        layout      = nullptr;
        framebuffer = nullptr;
        pass        = {};
        vertices.release();
        colorStaging.release();
        depthStaging.release();
        color     = {};
        depth     = {};
        submitted = false;
    }

    bool busy() const {
        if (evidence.token.empty()) return false;
        return evidence.phase == ActionGuideCapturePhase::Queued ||
               evidence.phase == ActionGuideCapturePhase::Rendering;
    }

    Result<std::string> requestCapture(std::string_view packJson, std::span<const std::string> shotIds) override {
        if (busy())
            return guideFail<std::string>(DiagnosticCode::InvalidArgument,
                                          "Guide capture busy; release the previous token");
        auto parsed = Value::fromJson(std::string(packJson));
        if (!parsed) return Result<std::string>::failure(parsed.status());
        auto pack = decodeActionGuidePack(parsed.value());
        if (!pack) return Result<std::string>::failure(pack.status());
        if (pack.value().canvas.width < 8 || pack.value().canvas.height < 8 || pack.value().canvas.width > 2048 ||
            pack.value().canvas.height > 2048)
            return guideFail<std::string>(DiagnosticCode::InvalidArgument, "canvas must be 8..2048 on each side");

        selectedShotIds.assign(shotIds.begin(), shotIds.end());
        pendingShots.clear();
        for (const auto& shot : pack.value().shots)
            if (shotSelected(selectedShotIds, shot.shotId)) pendingShots.push_back(shot);
        if (pendingShots.empty())
            return guideFail<std::string>(DiagnosticCode::InvalidArgument, "no shots selected for capture");

        evidence = {};
        evidence.token =
            "guide:" + std::to_string(graphics.captureFrameId_) + ":" + std::to_string(pack.value().revision);
        evidence.phase    = ActionGuideCapturePhase::Queued;
        evidence.revision = pack.value().revision;
        evidence.backend  = "vulkan-action-guide";
        evidence.pack     = std::move(pack).takeValue();
        evidence.shots.clear();
        shotIndex = 0;
        frozenBones.clear();

        auto frozen = freeze();
        if (!frozen) {
            evidence.phase  = ActionGuideCapturePhase::Failed;
            evidence.reason = frozen.status().describe();
            return Result<std::string>::failure(frozen.status());
        }
        frozenGeometry = std::move(frozen.value());
        frozenFrame    = graphics.captureFrameId_;
        return Result<std::string>::success(evidence.token);
    }

    Result<ActionGuideCaptureSnapshot> captureSnapshot(std::string_view token) override {
        if (token != evidence.token)
            return guideFail<ActionGuideCaptureSnapshot>(DiagnosticCode::NotFound, "Unknown guide-capture token");
        return Result<ActionGuideCaptureSnapshot>::success(evidence);
    }

    Result<void> releaseCapture(std::string_view token) override {
        if (token != evidence.token) return guideFail<void>(DiagnosticCode::NotFound, "Unknown guide-capture token");
        if (submitted) {
            evidence.phase = ActionGuideCapturePhase::Cancelled;
            // GPU resources survive until fence completes in poll().
            return Result<void>::success();
        }
        clearGpu();
        frozenGeometry = {};
        frozenBones    = {};
        pendingShots.clear();
        evidence.phase = ActionGuideCapturePhase::Released;
        evidence.shots.clear();
        return Result<void>::success();
    }

    Result<std::vector<Primitive3DVertex>> freeze() {
        using Output = std::vector<Primitive3DVertex>;
        if (evidence.pack.subject.id > std::numeric_limits<uint32_t>::max() ||
            evidence.pack.subject.generation > std::numeric_limits<uint32_t>::max())
            return guideFail<Output>(DiagnosticCode::InvalidArgument, "subject id/generation out of range");
        ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(Renderable3D)),
                                 static_cast<uint32_t>(evidence.pack.subject.id),
                                 static_cast<uint32_t>(evidence.pack.subject.generation)};
        auto*             entity = dynamic_cast<Renderable3D*>(ecs::try_get(handle));
        if (!entity) return guideFail<Output>(DiagnosticCode::StaleHandle, "Renderable3D generation is stale");
        const auto renderer = entity->meshRenderer();
        if (renderer->instances || renderer->lodCount || renderer->shader || renderer->isHair ||
            renderer->heightTexture)
            return guideFail<Output>(
                DiagnosticCode::Unsupported,
                "Guide capture does not support instances, LOD, custom shaders, hair or displacement");

        const auto& camera = evidence.pack.camera;
        const auto& canvas = evidence.pack.canvas;
        const float aspect = float(canvas.width) / float(canvas.height);
        const auto  view =
            glm::lookAtRH(glm::vec3(float(camera.eye[0]), float(camera.eye[1]), float(camera.eye[2])),
                          glm::vec3(float(camera.target[0]), float(camera.target[1]), float(camera.target[2])),
                          glm::vec3(float(camera.up[0]), float(camera.up[1]), float(camera.up[2])));
        // Pack orthoSize is half-height (landmark contract); Vulkan helper halves orthoHeight again.
        const float orthoHeight =
            camera.projection == ActionGuideProjection::Orthographic ? float(camera.orthoSize) * 2.f : 1.f;
        const auto projection = cameraProjectionVulkanRH_ZO(camera.projection == ActionGuideProjection::Orthographic,
                                                            glm::radians(float(camera.fovYDegrees)), orthoHeight,
                                                            aspect, float(camera.nearPlane), float(camera.farPlane));
        const auto viewProj   = projection * view;

        const auto model = eve::graphics::detail::modelFromTransform(*entity->transform());
        if (!std::isfinite(glm::determinant(model)) || std::abs(glm::determinant(model)) < 1e-12f)
            return guideFail<Output>(DiagnosticCode::InvalidArgument, "Entity transform is singular or non-finite");
        const auto normalModel = glm::transpose(glm::inverse(glm::mat3(model)));

        std::vector<glm::vec3> worldPositions;
        auto                   append = [&](Mesh* mesh, Material* material, int /*part*/) -> Result<void> {
            if (!mesh || !graphics.describeMesh(mesh))
                return guideFail<void>(DiagnosticCode::StaleHandle, "Mesh is unavailable in this backend");
            if ((material && (material->effectiveShader() || material->getShadingModel() == "hair")) ||
                mesh->isMorphDirty())
                return guideFail<void>(DiagnosticCode::Unsupported,
                                                         "Custom material or unapplied morph cannot be frozen faithfully");
            auto* gpu = static_cast<GpuMesh*>(mesh->gpuHandle);
            if (!gpu || gpu->residentVertexOverride || !gpu->vertexCount || !gpu->indexCount || gpu->indexCount % 3 ||
                gpu->vertexCount > 250000 || gpu->indexCount > 450000 ||
                worldPositions.size() + gpu->indexCount > 450000)
                return guideFail<void>(DiagnosticCode::InvalidArgument,
                                                         "Unsupported geometry or guide geometry budget exceeded");
            std::vector<MeshVertex> source(gpu->vertexCount);
            auto&                   vb = meshDrawVertices(*gpu);
            std::memcpy(source.data(), vb.map(), source.size() * sizeof(MeshVertex));
            vb.unmap();
            std::vector<uint32_t> indices(gpu->indexCount);
            auto&                 ib    = meshDrawIndices(*gpu);
            const auto*           bytes = static_cast<const uint8_t*>(ib.map());
            for (size_t i = 0; i < indices.size(); ++i) {
                if (gpu->indexType == vk::IndexType::eUint16)
                    indices[i] = reinterpret_cast<const uint16_t*>(bytes)[i];
                else
                    indices[i] = reinterpret_cast<const uint32_t*>(bytes)[i];
            }
            ib.unmap();
            if (mesh->hasGpuSkinning()) {
                const auto& palette = mesh->skinPalette();
                if (palette.empty() || palette.size() % 16)
                    return guideFail<void>(DiagnosticCode::InvalidArgument, "Skin palette is empty or malformed");
                if (palette.size() > 65536u * 16u)
                    return guideFail<void>(DiagnosticCode::InvalidArgument, "Skin palette exceeds 65536 matrices");
                for (size_t bone = 0; bone < palette.size(); bone += 16) {
                    for (size_t element = 0; element < 16; ++element)
                        if (!std::isfinite(palette[bone + element]))
                            return guideFail<void>(DiagnosticCode::InvalidArgument, "Skin palette is non-finite");
                }
                for (size_t bone = 0; bone < palette.size() / 16; ++bone) {
                    const glm::mat4 boneMat = glm::make_mat4(palette.data() + bone * 16);
                    const glm::vec3 joint = glm::vec3(model * boneMat * glm::vec4(0.f, 0.f, 0.f, 1.f));
                    if (!finitePosition(joint)) continue;
                    ActionGuideBoneWorld sample;
                    sample.boneId   = "joint." + std::to_string(bone);
                    sample.name     = sample.boneId;
                    sample.position = {joint.x, joint.y, joint.z};
                    if (bone > 0) sample.parentId = "joint." + std::to_string(bone - 1);
                    frozenBones.push_back(std::move(sample));
                }
                for (auto& vertex : source) {
                    auto weights = vertex.weights;
                    for (int influence = 0; influence < 4; ++influence) {
                        if (size_t(vertex.joints[influence]) >= palette.size() / 16 ||
                            !std::isfinite(weights[influence]) || weights[influence] < 0.f)
                            return guideFail<void>(DiagnosticCode::InvalidArgument, "Invalid skin joint or weight");
                    }
                    const float sum = glm::dot(weights, glm::vec4(1.f));
                    if (!std::isfinite(sum) || sum <= 1e-8f)
                        return guideFail<void>(DiagnosticCode::InvalidArgument,
                                                                 "Skin weights have no finite positive sum");
                    weights /= sum;
                    glm::mat4 skin(0.f);
                    for (int influence = 0; influence < 4; ++influence)
                        skin +=
                            weights[influence] * glm::make_mat4(palette.data() + size_t(vertex.joints[influence]) * 16);
                    vertex.pos    = glm::vec3(skin * glm::vec4(vertex.pos, 1.f));
                    vertex.normal = glm::mat3(skin) * vertex.normal;
                }
            }
            for (auto index : indices) {
                if (index >= source.size())
                    return guideFail<void>(DiagnosticCode::InvalidArgument, "Mesh index outside vertex stream");
                const auto& vertex   = source[index];
                const auto  position = glm::vec3(model * glm::vec4(vertex.pos, 1.f));
                const auto  normal   = normalModel * vertex.normal;
                if (!finitePosition(position) || !finitePosition(normal) || glm::length(normal) < 1e-8f)
                    return guideFail<void>(DiagnosticCode::InvalidArgument,
                                                             "Mesh has non-finite positions or invalid normals");
                worldPositions.push_back(position);
            }
            return Result<void>::success();
        };

        if (renderer->partCount < 0 || renderer->partCount > Renderable3D::MeshRenderer::kMaxParts)
            return guideFail<Output>(DiagnosticCode::InvalidArgument, "Invalid model part count");
        const int count = std::max(1, renderer->partCount);
        for (int part = 0; part < count; ++part) {
            auto copied = renderer->partCount ? append(renderer->parts[part].mesh, renderer->parts[part].material, part)
                                              : append(renderer->mesh, renderer->material, part);
            if (!copied.ok()) return Result<Output>::failure(copied.status());
        }
        if (worldPositions.empty())
            return guideFail<Output>(DiagnosticCode::InvalidArgument, "Guide subject has no triangles");

        Output output;
        output.reserve(worldPositions.size());
        for (const auto& position : worldPositions)
            output.push_back({viewProj * glm::vec4(position, 1.f), glm::vec4(1.f, 1.f, 1.f, 1.f)});
        return Result<Output>::success(std::move(output));
    }

    void recordCurrentShot() {
        auto&          device = graphics.device;
        const uint32_t width  = static_cast<uint32_t>(evidence.pack.canvas.width);
        const uint32_t height = static_cast<uint32_t>(evidence.pack.canvas.height);
        const auto&    shot   = pendingShots[shotIndex];
        needSilhouette        = wantsAov(shot, "silhouette");
        needDepth             = wantsAov(shot, "depth");
        needBone              = wantsAov(shot, "bone");
        if (!needSilhouette && !needDepth && !needBone) throw Exception("Guide shot requests no supported AOVs");

        color = device.createColorTarget(width, height, vk::Format::eR8G8B8A8Unorm);
        depth = vkb::DepthStencilImage{device, width, height, vk::Format::eD32Sfloat};
        pass =
            device.createRenderPass()
                .addSampledColorAttachment(vk::Format::eR8G8B8A8Unorm)
                .addDepthAttachment(vk::Format::eD32Sfloat, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addExternalShaderReadDependencies()
                .build();
        framebuffer = pass.createFramebuffer(device, width, height, {color.asAttachment(), depth.asAttachment()});
        layout      = device.createPipelineLayout().build();
        pipeline    = device.createPipeline()
                       .useClassicPipeline(embeddedSpirv(primitive3d_vert_spv), embeddedSpirv(primitive3d_frag_spv))
                       .setPipelineLayout(layout)
                       .setVertexInputState(vkb::VertexInputStateBuilder()
                                                .addInputBinding<Primitive3DVertex>()
                                                .addAttributeDescription<Primitive3DVertex>())
                       .setDynamicStatesViewportScissor()
                       .setRasterizer(vk::PolygonMode::eFill, false, false, 1.f, vk::CullModeFlagBits::eNone,
                                      vk::FrontFace::eCounterClockwise)
                       .setDepthStencil(true, true, vk::CompareOp::eLessOrEqual)
                       .build(pass);
        vertices.allocate(vkb::FrameSlot::gpuIdle(), device, frozenGeometry);
        colorStaging = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eTransferDst,
                                          vk::DeviceSize(width) * height * 4, kHostVisibleCoherent);
        depthStaging = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eTransferDst,
                                          vk::DeviceSize(width) * height * 4, kHostVisibleCoherent);
        pool         = device.createCommandPool();
        vk::CommandBufferAllocateInfo allocate(pool, vk::CommandBufferLevel::ePrimary, 1);
        auto                          cb = device->allocateCommandBuffers(allocate).front();
        fence                            = device->createFence({});
        cb.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        std::array<vk::ClearValue, 2> clears;
        const auto&                   bg = evidence.pack.canvas.backgroundRgba;
        clears[0].color =
            vk::ClearColorValue(std::array<float, 4>{float(bg[0]), float(bg[1]), float(bg[2]), float(bg[3])});
        clears[1].depthStencil = vk::ClearDepthStencilValue(1.f, 0);
        vk::RenderPassBeginInfo begin(pass.handle, framebuffer, vk::Rect2D({0, 0}, {width, height}), 2, clears.data());
        color.beginColorAttachment();
        depth.beginDepthAttachment();
        cb.beginRenderPass(begin, vk::SubpassContents::eInline);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        const vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, vertices, &offset);
        const vk::Viewport viewport(0.f, 0.f, float(width), float(height), 0.f, 1.f);
        const vk::Rect2D   scissor({0, 0}, {width, height});
        cb.setViewport(0, 1, &viewport);
        cb.setScissor(0, 1, &scissor);
        cb.draw(uint32_t(frozenGeometry.size()), 1, 0, 0);
        cb.endRenderPass();
        color.endSampledLayout();
        detail::recordFrameReadback(cb, color.image(), colorStaging.buffer, {width, height},
                                    vk::ImageAspectFlagBits::eColor, 0, vk::ImageLayout::eShaderReadOnlyOptimal);
        detail::recordFrameReadback(cb, depth.image(), depthStaging.buffer, {width, height},
                                    vk::ImageAspectFlagBits::eDepth, 0,
                                    vk::ImageLayout::eDepthStencilAttachmentOptimal);
        cb.end();
        vk::SubmitInfo submit;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cb;
        const auto result         = device.getQueue(vkb::QueueType::graphics).submit(1, &submit, fence);
        if (result != vk::Result::eSuccess) throw Exception("Guide capture queue submission failed");
        submitted = true;
    }

    void publishCurrentShot() {
        const uint32_t width  = static_cast<uint32_t>(evidence.pack.canvas.width);
        const uint32_t height = static_cast<uint32_t>(evidence.pack.canvas.height);
        const auto&    shot   = pendingShots[shotIndex];

        ActionGuideShotArtifact artifact;
        artifact.shotId      = shot.shotId;
        artifact.timeSeconds = shot.timeSeconds;
        artifact.sampleIndex = shot.sampleIndex;
        artifact.view        = shot.view;
        artifact.tags        = shot.tags;

        auto colorBytes = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4);
        std::memcpy(colorBytes->data(), colorStaging.map(), colorBytes->size());
        colorStaging.unmap();
        auto depthFloat = std::make_shared<std::vector<float>>(size_t(width) * height);
        std::memcpy(depthFloat->data(), depthStaging.map(), depthFloat->size() * sizeof(float));
        depthStaging.unmap();

        if (needSilhouette) {
            ActionGuideAovBuffer buffer;
            buffer.aov    = "silhouette";
            buffer.width  = width;
            buffer.height = height;
            buffer.rgba8  = std::move(colorBytes);
            artifact.aovs.push_back(std::move(buffer));
        }
        if (needDepth) {
            ActionGuideAovBuffer buffer;
            buffer.aov    = "depth";
            buffer.width  = width;
            buffer.height = height;
            buffer.rgba8  = depthToGrayRgba(depthFloat->data(), width, height);
            artifact.aovs.push_back(std::move(buffer));
        }
        if (needBone) {
            ActionGuideAovBuffer buffer;
            buffer.aov    = "bone";
            buffer.width  = width;
            buffer.height = height;
            if (frozenBones.empty()) {
                buffer.rgba8 = std::make_shared<const std::vector<uint8_t>>(size_t(width) * height * 4, uint8_t{0});
            } else {
                auto landmarks = projectActionGuideLandmarks(evidence.pack, frozenBones);
                if (!landmarks)
                    throw Exception(
                        "%s", (std::string("Guide bone projection failed: ") + landmarks.status().describe()).c_str());
                buffer.rgba8 = rasterizeBoneAov(width, height, landmarks.value());
            }
            artifact.aovs.push_back(std::move(buffer));
        }
        evidence.shots.push_back(std::move(artifact));
    }

    void fail(std::string reason) {
        evidence.phase  = ActionGuideCapturePhase::Failed;
        evidence.reason = std::move(reason);
        clearGpu();
        frozenGeometry = {};
    }

    void poll() {
        if (evidence.token.empty()) return;
        if (evidence.phase == ActionGuideCapturePhase::Released || evidence.phase == ActionGuideCapturePhase::Failed ||
            evidence.phase == ActionGuideCapturePhase::Ready)
            return;

        if (evidence.phase == ActionGuideCapturePhase::Cancelled) {
            if (submitted) {
                const auto status = graphics.device->getFenceStatus(fence);
                if (status == vk::Result::eNotReady) return;
                clearGpu();
            }
            frozenGeometry = {};
            return;
        }

        if (submitted) {
            const auto status = graphics.device->getFenceStatus(fence);
            if (status == vk::Result::eNotReady) return;
            if (status != vk::Result::eSuccess) {
                fail("Guide capture fence failed");
                return;
            }
            publishCurrentShot();
            clearGpu();
            ++shotIndex;
            if (shotIndex >= pendingShots.size()) {
                evidence.phase = ActionGuideCapturePhase::Ready;
                frozenGeometry = {};
                return;
            }
        }

        if (!submitted && (evidence.phase == ActionGuideCapturePhase::Queued ||
                           evidence.phase == ActionGuideCapturePhase::Rendering)) {
            evidence.phase = ActionGuideCapturePhase::Rendering;
            recordCurrentShot();
        }
    }
};

void Graphics::pollGuideCapture() {
    if (!initialized || !device.instance) return;
    if (!guideCaptureService_) {
        guideCaptureService_ = std::make_shared<GuideCaptureService>(*this);
        cap::provide<IActionGuideCapture>(guideCaptureService_.get());
    }
    try {
        guideCaptureService_->poll();
    } catch (const std::exception& exception) {
        guideCaptureService_->fail(exception.what());
    }
}

void Graphics::destroyGuideCapture() {
    if (!guideCaptureService_) return;
    cap::revoke<IActionGuideCapture>(guideCaptureService_.get());
    guideCaptureService_.reset();
}

}  // namespace eve::graphics::vulkan
