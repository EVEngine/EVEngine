#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include "common/EntityInspection.h"
#include "graphics/ClipSpace.h"
#include "graphics/InspectionPlan.h"
#include "graphics/RenderTransformProjection.h"
#include "graphics/shaders/primitive3d_frag_spv.inc"
#include "graphics/shaders/primitive3d_vert_spv.inc"
#include "graphics/vulkan/FrameReadback.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
namespace {
template <class T>
Result<T> inspectionFailure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message)));
}
bool finitePosition(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

// Owner-thread provider. Graphics owns this service and retires its queue before destruction.
// No entity/resource pointers survive freeze(); submitted work references only private buffers.
struct Graphics::InspectionService final : IEntityInspection {
    Graphics&                      graphics;
    FrameCaptureStore              store;
    EntityViewSnapshot             evidence;
    std::string                    token;
    std::vector<Primitive3DVertex> frozenGeometry;
    uint64_t                       frozenFrame = 0;
    vkb::ColorTarget               color;
    vkb::DepthTarget               depth;
    vkb::HostVertexBuffer          vertices;
    vkb::GenericBuffer             staging;
    vkb::BuiltRenderPass           pass;
    vk::Framebuffer                framebuffer{};
    vk::PipelineLayout             layout{};
    vk::Pipeline                   pipeline{};
    vk::CommandPool                pool{};
    vk::Fence                      fence{};
    bool                           submitted = false;

    explicit InspectionService(Graphics& owner) : graphics(owner) {
        FrameCaptureCapabilities caps;
        caps.backend     = "vulkan-geometry-inspection";
        caps.attachments = {FrameAttachment::Final};
        store.configure(caps);
    }
    ~InspectionService() override { clearGpu(); }

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
        staging.release();
        color     = {};
        depth     = {};
        submitted = false;
    }
    Result<std::string> request(EntityViewRequest request) override {
        if (request.cellSize < 64 || request.cellSize > 1024)
            return inspectionFailure<std::string>(DiagnosticCode::InvalidArgument, "cellSize must be 64..1024");
        if (!token.empty()) {
            auto       old   = store.snapshot(token);
            const bool found = old.ok();
            if (submitted ||
                (found && old.value().phase != FrameCapturePhase::Released &&
                 old.value().phase != FrameCapturePhase::Expired && old.value().phase != FrameCapturePhase::Failed &&
                 old.value().phase != FrameCapturePhase::Cancelled))
                return inspectionFailure<std::string>(DiagnosticCode::InvalidArgument,
                                                      "Inspection busy; release the previous token");
        }
        evidence         = {};
        evidence.request = std::move(request);
        auto frozen      = freeze();
        evidence.request.skinPalettes.clear();
        if (!frozen.ok()) return Result<std::string>::failure(frozen.status());
        FrameCaptureRequest capture;
        capture.attachments = {FrameAttachment::Final};
        auto queued         = store.request(std::move(capture));
        if (!queued.ok()) return Result<std::string>::failure(queued.status());
        token          = queued.value();
        frozenGeometry = std::move(frozen.value());
        frozenFrame    = graphics.captureFrameId_;
        return Result<std::string>::success(token);
    }
    Result<EntityViewSnapshot> snapshot(std::string_view requested) override {
        if (requested != token)
            return inspectionFailure<EntityViewSnapshot>(DiagnosticCode::NotFound, "Unknown inspection token");
        auto state = store.snapshot(token);
        if (!state.ok()) return Result<EntityViewSnapshot>::failure(state.status());
        auto result    = evidence;
        result.capture = std::move(state.value());
        return Result<EntityViewSnapshot>::success(std::move(result));
    }
    Result<FrameCapturePhase> release(std::string_view requested) override {
        auto result = store.release(requested);
        if (!result.ok()) return result;
        if (requested == token) frozenGeometry = {};
        return result;
    }

    Result<std::vector<Primitive3DVertex>> freeze() {
        using Output = std::vector<Primitive3DVertex>;
        ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(Renderable3D)), evidence.request.entityId,
                                 evidence.request.generation};
        auto*             entity = dynamic_cast<Renderable3D*>(ecs::try_get(handle));
        if (!entity) return inspectionFailure<Output>(DiagnosticCode::StaleHandle, "Renderable3D generation is stale");
        const auto renderer = entity->meshRenderer();
        if (renderer->instances || renderer->lodCount || renderer->shader || renderer->isHair ||
            renderer->heightTexture)
            return inspectionFailure<Output>(
                DiagnosticCode::Unsupported,
                "Geometry inspection does not support instances, LOD, custom shaders, hair or displacement");
        Camera3D* camera = nullptr;
        if (ecs::current()->getManager<Camera3D>()) {
            auto view = ecs::View<Camera3D, Camera3D::Data>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [data] = *it;
                if (data->active && data->entity) {
                    camera = data->entity;
                    break;
                }
            }
        }
        if (!camera)
            return inspectionFailure<Output>(DiagnosticCode::NotFound,
                                             "No active Camera3D; inspection does not create a camera");
        if (renderer->camera && renderer->camera != camera)
            return inspectionFailure<Output>(DiagnosticCode::Unsupported,
                                             "Entity uses a camera override; active-camera inspection is ambiguous");
        const auto       cameraData = camera->data();
        InspectionCamera gameCamera;
        gameCamera.width  = uint32_t(graphics.getPixelWidth());
        gameCamera.height = uint32_t(graphics.getPixelHeight());
        if (!gameCamera.width || !gameCamera.height)
            return inspectionFailure<Output>(DiagnosticCode::InvalidArgument, "Game viewport is empty");
        gameCamera.view       = glm::lookAtRH(glm::vec3(cameraData->eyeX, cameraData->eyeY, cameraData->eyeZ),
                                              glm::vec3(cameraData->targetX, cameraData->targetY, cameraData->targetZ),
                                              glm::vec3(cameraData->upX, cameraData->upY, cameraData->upZ));
        gameCamera.projection = cameraProjectionVulkanRH_ZO(
            cameraData->orthographic, glm::radians(cameraData->fovYDeg), cameraData->orthoHeight,
            float(gameCamera.width) / float(gameCamera.height), cameraData->nearZ, cameraData->farZ);
        const auto model = eve::graphics::detail::modelFromTransform(*entity->transform());
        if (!std::isfinite(glm::determinant(model)) || std::abs(glm::determinant(model)) < 1e-12f)
            return inspectionFailure<Output>(DiagnosticCode::InvalidArgument,
                                             "Entity transform is singular or non-finite");
        const auto normalModel = glm::transpose(glm::inverse(glm::mat3(model)));
        struct WorldVertex {
            glm::vec3 position, normal;
        };
        std::vector<WorldVertex> world;
        InspectionBounds         bounds{glm::vec3(std::numeric_limits<float>::max()),
                                glm::vec3(-std::numeric_limits<float>::max())};
        auto                     append = [&](Mesh* mesh, Material* material, int part) -> Result<void> {
            if (!mesh || !graphics.describeMesh(mesh))
                return inspectionFailure<void>(DiagnosticCode::StaleHandle, "Mesh is unavailable in this backend");
            if ((material && (material->effectiveShader() || material->getShadingModel() == "hair")) ||
                mesh->isMorphDirty())
                return inspectionFailure<void>(DiagnosticCode::Unsupported,
                                                                   "Custom material or unapplied morph cannot be frozen faithfully");
            auto* gpu = static_cast<GpuMesh*>(mesh->gpuHandle);
            if (!gpu || gpu->residentVertexOverride || !gpu->vertexCount || !gpu->indexCount || gpu->indexCount % 3 ||
                gpu->vertexCount > 250000 || gpu->indexCount > 450000 || world.size() + gpu->indexCount > 450000)
                return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                   "Unsupported geometry or inspection geometry budget exceeded");
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
            if (!evidence.request.skinPalettes.empty() && !mesh->hasGpuSkinning())
                return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                   "Pose override requires a skinned mesh");
            if (mesh->hasGpuSkinning()) {
                // Non-LOD Renderable3D draws use four influences, independent of the
                // backend's mutable last-draw influence limit. Freeze on the owner thread.
                const auto& selectedPalette = evidence.request.skinPalettes.empty()
                                                                      ? mesh->skinPalette()
                                                                      : evidence.request.skinPalettes[size_t(part)];
                if (selectedPalette.size() > 65536u * 16u)
                    return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                       "Inspection skin palette exceeds 65536 matrices");
                const auto palette = selectedPalette;
                if (palette.empty() || palette.size() % 16)
                    return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                       "Skin palette is empty or malformed");
                for (size_t bone = 0; bone < palette.size(); bone += 16) {
                    for (size_t element = 0; element < 16; ++element)
                        if (!std::isfinite(palette[bone + element]))
                            return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                               "Skin palette is non-finite");
                    if (palette[bone + 3] != 0.f || palette[bone + 7] != 0.f || palette[bone + 11] != 0.f ||
                        palette[bone + 15] != 1.f)
                        return inspectionFailure<void>(DiagnosticCode::Unsupported,
                                                                           "Skin palette must contain affine matrices");
                }
                for (auto& vertex : source) {
                    auto weights = vertex.weights;
                    for (int influence = 0; influence < 4; ++influence) {
                        // Shaders access all four indices, including zero-weight lanes.
                        if (size_t(vertex.joints[influence]) >= palette.size() / 16 ||
                            !std::isfinite(weights[influence]) || weights[influence] < 0.f)
                            return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                               "Invalid skin joint or weight");
                    }
                    const float sum = glm::dot(weights, glm::vec4(1.f));
                    if (!std::isfinite(sum) || sum <= 1e-8f)
                        return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
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
                    return inspectionFailure<void>(DiagnosticCode::InvalidArgument, "Mesh index outside vertex stream");
                const auto& vertex   = source[index];
                const auto  position = glm::vec3(model * glm::vec4(vertex.pos, 1.f));
                const auto  normal   = normalModel * vertex.normal;
                if (!finitePosition(position) || !finitePosition(normal) || glm::length(normal) < 1e-8f)
                    return inspectionFailure<void>(DiagnosticCode::InvalidArgument,
                                                                       "Mesh has non-finite positions or invalid normals");
                bounds.minimum = glm::min(bounds.minimum, position);
                bounds.maximum = glm::max(bounds.maximum, position);
                world.push_back({position, glm::normalize(normal)});
            }
            return Result<void>::success();
        };
        if (renderer->partCount < 0 || renderer->partCount > Renderable3D::MeshRenderer::kMaxParts)
            return inspectionFailure<Output>(DiagnosticCode::InvalidArgument, "Invalid model part count");
        const int count = std::max(1, renderer->partCount);
        if (!evidence.request.skinPalettes.empty() && evidence.request.skinPalettes.size() != size_t(count))
            return inspectionFailure<Output>(DiagnosticCode::InvalidArgument,
                                             "Pose override must cover every mesh part");
        for (int part = 0; part < count; ++part) {
            auto copied = renderer->partCount ? append(renderer->parts[part].mesh, renderer->parts[part].material, part)
                                              : append(renderer->mesh, renderer->material, part);
            if (!copied.ok()) return Result<Output>::failure(copied.status());
        }
        const auto size    = evidence.request.cellSize;
        auto       planned = planInspectionViews(bounds, gameCamera, size, size, 1.1f);
        if (!planned.ok()) return Result<Output>::failure(planned.status());
        evidence.worldUnitsPerPixel = planned.value().worldUnitsPerPixel;
        for (int axis = 0; axis < 3; ++axis) {
            evidence.bounds[axis]     = bounds.minimum[axis];
            evidence.bounds[axis + 3] = bounds.maximum[axis];
        }
        Output output;
        output.reserve(world.size() * 4);
        for (size_t i = 0; i < 4; ++i) {
            const auto& cam      = planned.value().cameras[i];
            auto&       metadata = evidence.cameras[i];
            std::copy_n(glm::value_ptr(cam.view), 16, metadata.view.begin());
            std::copy_n(glm::value_ptr(cam.projection), 16, metadata.projection.begin());
            metadata.width  = size;
            metadata.height = size;
            if (i == 3) {
                if (cam.width > cam.height)
                    metadata.height = std::max(1u, uint32_t(uint64_t(size) * cam.height / cam.width));
                else
                    metadata.width = std::max(1u, uint32_t(uint64_t(size) * cam.width / cam.height));
            }
            metadata.x = uint32_t(i % 2) * size + (size - metadata.width) / 2;
            metadata.y = uint32_t(i / 2) * size + (size - metadata.height) / 2;
            for (const auto& vertex : world)
                output.push_back({cam.projection * cam.view * glm::vec4(vertex.position, 1.f),
                                  glm::vec4(vertex.normal * .5f + .5f, 1.f)});
        }
        return Result<Output>::success(std::move(output));
    }

    void record(const std::vector<Primitive3DVertex>& geometry) {
        auto&          device = graphics.device;
        const uint32_t size   = evidence.request.cellSize * 2;
        color                 = device.createColorTarget(size, size, vk::Format::eR8G8B8A8Unorm);
        depth                 = device.createDepthTarget(size, size, vk::Format::eD32Sfloat);
        pass                  = device.createRenderPass()
                   .addSampledColorAttachment(vk::Format::eR8G8B8A8Unorm)
                   .addDepthAttachment(vk::Format::eD32Sfloat, vk::AttachmentLoadOp::eClear,
                                       vk::AttachmentStoreOp::eDontCare)
                   .addSubpass(vkb::SubpassBuilder()
                                   .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                   .setDepthStencilAttachment(1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                   .addExternalShaderReadDependencies()
                   .build();
        framebuffer = pass.createFramebuffer(device, size, size, {color.asAttachment(), depth.asAttachment()});
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
        vertices.allocate(vkb::FrameSlot::gpuIdle(), device, geometry);
        staging = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eTransferDst, vk::DeviceSize(size) * size * 4,
                                     kHostVisibleCoherent);
        pool    = device.createCommandPool();
        vk::CommandBufferAllocateInfo allocate(pool, vk::CommandBufferLevel::ePrimary, 1);
        auto                          cb = device->allocateCommandBuffers(allocate).front();
        fence                            = device->createFence({});
        cb.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        std::array<vk::ClearValue, 2> clears;
        clears[0].color        = vk::ClearColorValue(std::array<float, 4>{.035f, .035f, .035f, 1.f});
        clears[1].depthStencil = vk::ClearDepthStencilValue(1.f, 0);
        vk::RenderPassBeginInfo begin(pass.handle, framebuffer, vk::Rect2D({0, 0}, {size, size}), 2, clears.data());
        color.beginColorAttachment();
        depth.beginDepthAttachment();
        cb.beginRenderPass(begin, vk::SubpassContents::eInline);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        const vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, vertices, &offset);
        const uint32_t count = uint32_t(geometry.size() / 4);
        for (uint32_t i = 0; i < 4; ++i) {
            const auto&        camera = evidence.cameras[i];
            const vk::Viewport viewport(float(camera.x), float(camera.y), float(camera.width), float(camera.height),
                                        0.f, 1.f);
            const vk::Rect2D   scissor({int32_t(camera.x), int32_t(camera.y)}, {camera.width, camera.height});
            cb.setViewport(0, 1, &viewport);
            cb.setScissor(0, 1, &scissor);
            cb.draw(count, 1, i * count, 0);
        }
        cb.endRenderPass();
        color.endSampledLayout();
        detail::recordFrameReadback(cb, color.image(), staging.buffer, {size, size}, vk::ImageAspectFlagBits::eColor, 0,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
        cb.end();
        vk::SubmitInfo submit;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cb;
        const auto result         = device.getQueue(vkb::QueueType::graphics).submit(1, &submit, fence);
        if (result != vk::Result::eSuccess) throw Exception("Inspection queue submission failed");
        submitted = true;
    }

    void poll() {
        store.collect();
        if (token.empty()) return;
        auto state = store.snapshot(token);
        if (!state.ok()) return;
        if (submitted) {
            const auto status = graphics.device->getFenceStatus(fence);
            if (status == vk::Result::eNotReady) return;
            if (status != vk::Result::eSuccess) throw Exception("Inspection fence failed");
            if (state.value().phase == FrameCapturePhase::ReadbackPending) {
                const auto size  = evidence.request.cellSize * 2;
                auto       bytes = std::make_shared<std::vector<uint8_t>>(size_t(size) * size * 4);
                std::memcpy(bytes->data(), staging.map(), bytes->size());
                staging.unmap();
                FrameCaptureBuffer buffer;
                buffer.width = buffer.height = size;
                buffer.producerFrameId       = state.value().frameId;
                buffer.encoding              = "isolated-geometry-world-normal-rgb";
                buffer.colorSpace            = "linear-unorm";
                buffer.bytes                 = std::move(bytes);
                FrameCaptureSnapshot result;
                result.buffers.push_back(std::move(buffer));
                auto complete = store.complete(token, std::move(result));
                if (!complete.ok()) throw Exception("Inspection publication failed");
            }
            clearGpu();
            store.retire(token);
        } else if (state.value().phase == FrameCapturePhase::Queued) {
            const size_t side  = evidence.request.cellSize * 2;
            auto         begin = store.begin(token, frozenFrame, side * side * 4);
            if (!begin.ok()) throw Exception("Inspection readback budget exceeded");
            record(frozenGeometry);
            frozenGeometry = {};
            auto pending   = store.submitted(token);
            if (!pending.ok()) throw Exception("Inspection submission state failed");
        } else
            frozenGeometry = {};
    }
};

void Graphics::pollEntityInspection() {
    if (!initialized || !device.instance) return;
    if (!inspectionService_) {
        inspectionService_ = std::make_shared<InspectionService>(*this);
        cap::provide<IEntityInspection>(inspectionService_.get());
    }
    try {
        inspectionService_->poll();
    } catch (const std::exception& exception) {
        auto failed = inspectionService_->store.fail(inspectionService_->token, exception.what());
        if (!failed.ok()) return;
        if (!inspectionService_->submitted) {
            inspectionService_->clearGpu();
            inspectionService_->store.retire(inspectionService_->token);
        }
    }
}
void Graphics::destroyEntityInspection() {
    if (!inspectionService_) return;
    cap::revoke<IEntityInspection>(inspectionService_.get());
    inspectionService_.reset();
}
}  // namespace eve::graphics::vulkan
