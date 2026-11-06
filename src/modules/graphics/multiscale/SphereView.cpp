// eve-hot-path
#include "graphics/multiscale/SphereView.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <future>
#include <limits>
#include <new>
#include <thread>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/ShaderImageUpdate.h"
#include "graphics/ShaderResources.h"
#include "graphics/multiscale/AngularProjectionInternal.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/shaders/sphere_batch_frag_spv.inc"
#include "graphics/multiscale/shaders/sphere_batch_vert_spv.inc"
#include "graphics/multiscale/shaders/sphere_frag_spv.inc"
#include "graphics/multiscale/shaders/sphere_vert_spv.inc"
#include "spatial/multiscale/CellPosition.h"

namespace eve::graphics::multiscale {
namespace {
static_assert(sizeof(std::array<float, 16>) == 16 * sizeof(float));
Diagnostic failure(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "graphics.multiscale.spheres");
}
// Bound the represented GPU sphere, with a float-arithmetic guard. This does not
// establish a certified total shader error bound; near-plane ambiguity keeps the full viewport.
void packRectangle(std::array<float, 16>& packet, const AngularBounds& original) {
    const double          x = packet[0], y = packet[1], z = packet[2];
    const double          guard  = 128 * std::numeric_limits<float>::epsilon() * (std::hypot(x, y, z) + packet[3]);
    const double          radius = packet[3] + guard;
    std::array<double, 2> low{-1, -1}, high{1, 1};
    if (z > radius && original.visibility == AngularVisibility::Projected) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            const double                center  = axis == 0 ? x : -y;
            const double                tangent = packet[4 + axis];
            const std::array<double, 4> corners{
                (center - radius) / (z - radius) / tangent, (center - radius) / (z + radius) / tangent,
                (center + radius) / (z - radius) / tangent, (center + radius) / (z + radius) / tangent};
            low[axis]  = std::min(*std::min_element(corners.begin(), corners.end()),
                                 axis == 0 ? original.minimum[0] : -original.maximum[1]);
            high[axis] = std::max(*std::max_element(corners.begin(), corners.end()),
                                  axis == 0 ? original.maximum[0] : -original.minimum[1]);
        }
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
        packet[12 + axis] =
            std::max(-1.f, std::nextafter(static_cast<float>(low[axis]), -std::numeric_limits<float>::infinity()));
        packet[14 + axis] =
            std::min(1.f, std::nextafter(static_cast<float>(high[axis]), std::numeric_limits<float>::infinity()));
    }
}
}  // namespace
struct SphereFrame::Impl {
    std::vector<std::array<float, 16>> packets;
};
SphereFrame::SphereFrame(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SphereFrame::~SphereFrame()                                        = default;
SphereFrame::SphereFrame(SphereFrame&&) noexcept                   = default;
SphereFrame&        SphereFrame::operator=(SphereFrame&&) noexcept = default;
Result<SphereFrame> SphereFrame::build(std::span<const spatial::CellPosition> centers,
                                       std::span<const SphereAppearance>      appearances,
                                       const spatial::CellPosition& camera, const AngularView& view,
                                       std::size_t maxSpheres) {
    if (centers.size() != appearances.size() || maxSpheres == 0 || centers.size() > maxSpheres)
        return Result<SphereFrame>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid sphere count/budget"));
    auto preparedView = detail::prepareAngularView(view);
    if (!preparedView.ok()) return Result<SphereFrame>::failure(preparedView.status());
    auto cameraCheck = detail::projectPreparedSphere(camera, camera, 1, 0, preparedView.value());
    if (!cameraCheck.ok()) return Result<SphereFrame>::failure(cameraCheck.status());
    const double horizontal = view.tanHalfVerticalFov * view.aspect;
    if (!std::isfinite(horizontal) || horizontal < 1e-6 || horizontal > 1e6 || view.tanHalfVerticalFov < 1e-6 ||
        view.tanHalfVerticalFov > 1e6)
        return Result<SphereFrame>::failure(
            failure(DiagnosticCode::Unsupported, "Reference view FOV exceeds float range"));
    try {
        auto state = std::make_unique<Impl>();
        state->packets.resize(centers.size());
        std::vector<std::uint8_t> visible(centers.size());
        const auto hardware = std::max(1u, std::thread::hardware_concurrency());
        const auto workers  = centers.size() < 16384 ? std::size_t{1}
                                                      : std::min<std::size_t>({16, hardware, centers.size() / 16384});
        std::vector<Status> errors(workers);
        const auto          prepared = std::move(preparedView).takeValue();
        const auto          origin   = camera;
        auto pack = [&](std::size_t worker) {
            const auto begin = centers.size() * worker / workers;
            const auto end   = centers.size() * (worker + 1) / workers;
            try {
                for (std::size_t i = begin; i < end; ++i) {
                    const auto& appearance = appearances[i];
                    for (float color : appearance.color)
                        if (!std::isfinite(color) || color < 0 || color > 1) {
                            errors[worker] = Status::failure(
                                failure(DiagnosticCode::InvalidArgument, "Invalid opaque linear color"));
                            return;
                        }
                    auto projected = detail::projectPreparedSphere(centers[i], origin, appearance.radiusMantissa,
                                                                   appearance.radiusExponent, prepared);
                    if (!projected.ok()) {
                        errors[worker] = projected.status();
                        return;
                    }
                    if (projected.value().bounds.visibility == AngularVisibility::Outside) continue;
                    auto         center    = projected.value().center;
                    const double radius    = projected.value().radius;
                    double       magnitude = radius;
                    for (double value : center) magnitude = std::max(magnitude, std::abs(value));
                    if (!std::isfinite(magnitude) || radius <= 0) {
                        errors[worker] = Status::failure(
                            failure(DiagnosticCode::Unsupported, "Reference sphere normalization range exceeded"));
                        return;
                    }
                    int exponent = 0;
                    std::frexp(magnitude, &exponent);
                    for (double& value : center) value = std::ldexp(value, -exponent);
                    const double r                = std::ldexp(radius, -exponent);
                    const double distance         = std::hypot(center[0], center[1], center[2]);
                    const int    physicalExponent = view.cellExponent + exponent;
                    if (r < 1e-5 * distance || std::abs(distance - r) < 1e-5 * std::max(distance, r) ||
                        physicalExponent < -2000 || physicalExponent > 2000) {
                        errors[worker] = Status::failure(failure(
                            DiagnosticCode::Unsupported,
                            "Reference sphere precision/depth range exceeded; no subpixel or surface fallback"));
                        return;
                    }
                    auto& packet = state->packets[i];
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        packet[axis]     = static_cast<float>(center[axis]);
                        packet[8 + axis] = appearance.color[axis];
                    }
                    packet[3] = static_cast<float>(r);
                    packet[4] = static_cast<float>(horizontal);
                    packet[5] = static_cast<float>(view.tanHalfVerticalFov);
                    packet[6] = static_cast<float>(physicalExponent);
                    packRectangle(packet, projected.value().bounds);
                    visible[i] = 1;
                }
            } catch (const std::bad_alloc&) {
                errors[worker] = Status::failure(failure(DiagnosticCode::Failed, "Sphere frame allocation failed"));
            }
        };
        std::vector<std::future<void>> tasks;
        tasks.reserve(workers - 1);
        for (std::size_t worker = 1; worker < workers; ++worker)
            tasks.push_back(std::async(std::launch::async, pack, worker));
        pack(0);
        for (auto& task : tasks) task.get();
        for (const auto& error : errors)
            if (error.isFailure()) return Result<SphereFrame>::failure(error);
        std::size_t packed = 0;
        for (std::size_t i = 0; i < visible.size(); ++i)
            if (visible[i]) {
                if (packed != i) state->packets[packed] = state->packets[i];
                ++packed;
            }
        state->packets.resize(packed);
        return Result<SphereFrame>::success(SphereFrame(std::move(state)));
    } catch (const std::bad_alloc&) {
        return Result<SphereFrame>::failure(failure(DiagnosticCode::Failed, "Sphere frame allocation failed"));
    } catch (const std::exception&) {
        return Result<SphereFrame>::failure(failure(DiagnosticCode::Failed, "Sphere frame worker failed"));
    }
}
struct SphereView::Impl {
    // Provider-owned pointers are guarded by its weak lifetime, on the exclusive graphics thread.
    Graphics*                 graphics = nullptr;
    std::weak_ptr<const void> lifetime;
    Mesh*                     mesh   = nullptr;
    Shader*                   shader = nullptr;
    ~Impl() {
        if (!graphics || lifetime.expired()) return;
        if (mesh && graphics->releaseMesh(mesh)) delete mesh;
        if (shader && graphics->releaseShader(shader)) delete shader;
    }
};
SphereView::SphereView() : impl_(std::make_unique<Impl>()) {}
SphereView::~SphereView() = default;
Result<std::unique_ptr<SphereView>> SphereView::build(Graphics& graphics) {
    if (graphics.resourceLifetime().expired())
        return Result<std::unique_ptr<SphereView>>::failure(
            failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (graphics.getBackendName() != "vulkan")
        return Result<std::unique_ptr<SphereView>>::failure(
            failure(DiagnosticCode::Unsupported, "Sphere reference view currently requires Vulkan"));
    try {
        std::unique_ptr<SphereView> view(new SphereView);
        auto&                       state = *view->impl_;
        state.graphics                    = &graphics;
        state.lifetime                    = graphics.resourceLifetime();
        state.shader                      = graphics.newMeshShaderFromSpv(
            std::vector<std::uint32_t>(sphere_vert_spv, sphere_vert_spv + sphere_vert_spv_count),
            std::vector<std::uint32_t>(sphere_frag_spv, sphere_frag_spv + sphere_frag_spv_count));
        if (!state.shader)
            return Result<std::unique_ptr<SphereView>>::failure(
                failure(DiagnosticCode::Unsupported, "Sphere shader unavailable"));
        auto surface = graphics.configureMeshShaderSurface(*state.shader, BlendMode::Opaque, true, true);
        if (!surface.ok()) return Result<std::unique_ptr<SphereView>>::failure(surface.status());
        auto raster = graphics.configureMeshShaderRaster(*state.shader, {});
        if (!raster.ok()) return Result<std::unique_ptr<SphereView>>::failure(raster.status());
        const float         positions[]{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
        const float         normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
        const float         uv[]{0, 0, 1, 0, 1, 1, 0, 1};
        const std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
        state.mesh = graphics.newMeshFromArrays(positions, normals, uv, 4, indices, 6);
        if (!state.mesh)
            return Result<std::unique_ptr<SphereView>>::failure(
                failure(DiagnosticCode::Failed, "Sphere quad upload failed"));
        return Result<std::unique_ptr<SphereView>>::success(std::move(view));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<SphereView>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.spheres"));
    }
}
Result<void> SphereView::render(const SphereFrame& frame) {
    auto drawn = draw(frame);
    if (!drawn.ok()) return drawn;
    try {
        impl_->graphics->present();
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.spheres"));
    }
}
Result<void> SphereView::draw(const SphereFrame& frame) {
    if (impl_->lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (!frame.impl_) return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Moved sphere frame"));
    if (impl_->graphics->isHeadless())
        return Result<void>::failure(
            failure(DiagnosticCode::Unsupported, "Sphere draw requires a presentation surface"));
    try {
        impl_->graphics->begin3DFrame();
        impl_->graphics->setMesh3DViewProj(glm::mat4(1.f));
        impl_->graphics->setMesh3DView(glm::mat4(1.f));
        for (const auto& packet : frame.impl_->packets) {
            std::array<float, 32> constants{};
            std::copy(packet.begin(), packet.end(), constants.begin());
            impl_->shader->setPushConstantBlock(constants);
            impl_->graphics->drawMeshShader(impl_->mesh, glm::mat4(1.f), nullptr, Color(1.f), impl_->shader);
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.spheres"));
    }
}
struct SphereBatch::Impl {
    std::unique_ptr<SphereView> resources;
    std::uint32_t               count    = 0;
    std::uint32_t               capacity = 0, width = 0, height = 0;
};
SphereBatch::SphereBatch() : impl_(std::make_unique<Impl>()) {}
SphereBatch::~SphereBatch() = default;
Result<std::unique_ptr<SphereBatch>> SphereBatch::prepare(Graphics& graphics, const SphereFrame& frame) {
    if (!frame.impl_)
        return Result<std::unique_ptr<SphereBatch>>::failure(
            failure(DiagnosticCode::InvalidArgument, "Moved sphere frame"));
    if (frame.impl_->packets.size() > 1000000)
        return Result<std::unique_ptr<SphereBatch>>::failure(
            failure(DiagnosticCode::Unsupported, "Sphere batch capacity exceeded"));
    try {
        auto resources = SphereView::build(graphics);
        if (!resources.ok()) return Result<std::unique_ptr<SphereBatch>>::failure(resources.status());
        std::unique_ptr<SphereBatch> batch(new SphereBatch);
        batch->impl_->resources = std::move(resources.value());
        batch->impl_->count     = static_cast<std::uint32_t>(frame.impl_->packets.size());
        // Keep a valid single dummy record for an empty snapshot, so capability absence
        // and resource preparation have identical observable semantics for all counts.
        const auto storageCount = std::max(1u, batch->impl_->count);
        auto width              = std::min(2048u, storageCount * 4u);
        if (width == 2048u) {
            // Prefer an exact image shape so the frame's contiguous records can be
            // consumed without an additional 64-byte-per-sphere packing buffer.
            for (auto candidate = width; candidate >= 1024u; --candidate) {
                if ((storageCount * 4u) % candidate == 0) {
                    width = candidate;
                    break;
                }
            }
        }
        const auto height       = (storageCount * 4u + width - 1u) / width;
        batch->impl_->capacity  = storageCount;
        batch->impl_->width     = width;
        batch->impl_->height    = height;
        std::vector<float> records;
        if (frame.impl_->packets.size() != storageCount ||
            static_cast<std::size_t>(width) * height != storageCount * 4u) {
            records.resize(static_cast<std::size_t>(width) * height * 4);
            for (std::size_t i = 0; i < frame.impl_->packets.size(); ++i)
                std::copy_n(frame.impl_->packets[i].begin(), 16, records.begin() + i * 16);
        }
        std::vector<glm::mat4> transforms(storageCount, glm::mat4(1.f));
        ShaderImageInput       image;
        image.format  = ShaderImageFormat::RGBA32Float;
        image.width   = width;
        image.height  = height;
        image.sampler = TextureSampler::nearest();
        image.bytes   = records.empty() ? std::as_bytes(std::span(frame.impl_->packets))
                                       : std::as_bytes(std::span(records));
        const std::array<std::uint32_t, 4> dimensions{width, height, storageCount, 0};
        ShaderResourceInputs               inputs;
        inputs.images           = std::span(&image, 1);
        inputs.constants        = std::as_bytes(std::span(dimensions));
        inputs.instanceMatrices = std::as_bytes(std::span(transforms));
        const std::vector<std::uint32_t> vertex(sphere_batch_vert_spv,
                                                sphere_batch_vert_spv + sphere_batch_vert_spv_count);
        const std::vector<std::uint32_t> fragment(sphere_batch_frag_spv,
                                                  sphere_batch_frag_spv + sphere_batch_frag_spv_count);
        auto                             prepared =
            graphics.replaceMeshShaderResources(*batch->impl_->resources->impl_->shader, vertex, fragment, inputs);
        if (!prepared.ok()) return Result<std::unique_ptr<SphereBatch>>::failure(prepared.status());
        return Result<std::unique_ptr<SphereBatch>>::success(std::move(batch));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<SphereBatch>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.spheres"));
    }
}
Result<void> SphereBatch::update(const SphereFrame& frame) {
    auto& state = *impl_->resources->impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (!frame.impl_ || frame.impl_->packets.size() > impl_->capacity)
        return Result<void>::failure(
            failure(DiagnosticCode::InvalidArgument, "Moved frame or batch capacity exceeded"));
    try {
        std::vector<float> records;
        if (static_cast<std::size_t>(impl_->width) * impl_->height != frame.impl_->packets.size() * 4u) {
            records.resize(static_cast<std::size_t>(impl_->width) * impl_->height * 4);
            for (std::size_t i = 0; i < frame.impl_->packets.size(); ++i)
                std::copy_n(frame.impl_->packets[i].begin(), 16, records.begin() + i * 16);
        }
        ShaderImageInput image;
        image.format  = ShaderImageFormat::RGBA32Float;
        image.width   = impl_->width;
        image.height  = impl_->height;
        image.sampler = TextureSampler::nearest();
        image.bytes   = records.empty() ? std::as_bytes(std::span(frame.impl_->packets))
                                       : std::as_bytes(std::span(records));
        auto update   = ShaderImageUpdate::prepare(image);
        if (!update.ok()) return Result<void>::failure(update.status());
        auto applied = update.value().apply(*state.graphics, *state.shader);
        if (!applied.ok()) return applied;
        impl_->count = static_cast<std::uint32_t>(frame.impl_->packets.size());
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(failure(DiagnosticCode::Failed, error.what()));
    }
}
Result<void> SphereBatch::updateResidentPackets(const GpuResidentBufferView& packets, std::uint32_t count) {
    auto& state = *impl_->resources->impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (count > impl_->capacity)
        return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Resident sphere count exceeds capacity"));
    auto prepared = ResidentShaderImageUpdate::prepare(0, impl_->width, impl_->height);
    if (!prepared.ok()) return Result<void>::failure(prepared.status());
    auto applied = prepared.value().apply(*state.graphics, *state.shader, packets);
    if (!applied.ok()) return applied;
    impl_->count = count;
    return Result<void>::success();
}
Result<void> SphereBatch::draw() {
    auto& state = *impl_->resources->impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (state.graphics->isHeadless())
        return Result<void>::failure(
            failure(DiagnosticCode::Unsupported, "Sphere draw requires a presentation surface"));
    try {
        state.graphics->begin3DFrame();
        state.graphics->setMesh3DViewProj(glm::mat4(1.f));
        state.graphics->setMesh3DView(glm::mat4(1.f));
        if (impl_->count == 0) return Result<void>::success();
        return state.graphics->drawMeshShaderInstances(*state.mesh, *state.shader, glm::mat4(1.f), Color(1.f), 0,
                                                       impl_->count);
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.spheres"));
    }
}
}  // namespace eve::graphics::multiscale
