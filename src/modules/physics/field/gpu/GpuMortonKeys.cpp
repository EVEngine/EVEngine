// eve-hot-path
#include "physics/field/gpu/GpuMortonKeys.h"

#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "physics/field/gpu/morton_keys_spv.inc"
#include "physics/field/gpu/radix_chunks_spv.inc"
#include "physics/field/gpu/radix_groups_spv.inc"
#include "physics/field/gpu/radix_local_spv.inc"
#include "physics/field/gpu/radix_scatter_spv.inc"

namespace eve::physics::field::gpu {
namespace {
static_assert(sizeof(GpuMortonKey) == 8);

template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.field.gpu.morton"));
}
std::unique_ptr<gpgpu::ComputeShader> makeShader(const std::uint32_t* begin, std::size_t count) {
    const std::vector<std::uint32_t> words(begin, begin + count);
    auto shader = gpgpu::createComputeShader(words);
    if (!shader.ok()) throw std::runtime_error(shader.status().describe());
    return std::move(shader).takeValue();
}
}  // namespace

struct GpuMortonKeys::Impl {
    gpgpu::Gpgpu*                                    compute = nullptr;
    std::array<std::unique_ptr<gpgpu::GpuBuffer>, 3> keys;
    std::unique_ptr<gpgpu::GpuBuffer>                admission, staging;
    std::unique_ptr<gpgpu::GpuBuffer>                localZero, groupZero, groupBefore, chunkZero, chunkBefore,
        totalZeros;
    std::unique_ptr<gpgpu::ComputeShader> shader, radixLocal, radixGroups, radixChunks, radixScatter;
    std::unique_ptr<gpgpu::Sequence>                 sequence;
    std::weak_ptr<const void>                        graphicsLifetime;
    std::size_t                                      count     = 0;
    std::size_t                                      groupCount = 0, chunkCount = 0;
    unsigned                                         phase     = 0;
    bool                                             published = false;
    bool                                             sorted = false;
};

GpuMortonKeys::GpuMortonKeys(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuMortonKeys::~GpuMortonKeys() = default;

Result<std::unique_ptr<GpuMortonKeys>> GpuMortonKeys::prepare(gpgpu::Gpgpu& compute, graphics::Graphics& graphics,
                                                              std::size_t sourceCount, GpuMortonBounds bounds) {
    if (graphics.getBackendName() != "vulkan" || graphics.resourceLifetime().expired() || !compute.isAvailable())
        return fail<std::unique_ptr<GpuMortonKeys>>(DiagnosticCode::Unsupported, "Vulkan GPGPU provider unavailable");
    if (sourceCount == 0 || sourceCount > 1000000)
        return fail<std::unique_ptr<GpuMortonKeys>>(DiagnosticCode::InvalidArgument, "Invalid source count");
    std::array<float, 3> extent{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        extent[axis] = bounds.upper[axis] - bounds.lower[axis];
        if (!std::isfinite(bounds.lower[axis]) || !std::isfinite(bounds.upper[axis]) || !std::isfinite(extent[axis]) ||
            extent[axis] <= 0)
            return fail<std::unique_ptr<GpuMortonKeys>>(DiagnosticCode::InvalidArgument, "Invalid spatial key domain");
    }
    try {
        auto impl              = std::make_unique<Impl>();
        impl->compute          = &compute;
        impl->count            = sourceCount;
        impl->groupCount       = (sourceCount + 255) / 256;
        impl->chunkCount       = (impl->groupCount + 255) / 256;
        impl->graphicsLifetime = graphics.resourceLifetime();
        for (auto& buffer : impl->keys)
            buffer.reset(compute.newBuffer(static_cast<int>(sourceCount * sizeof(GpuMortonKey))));
        impl->admission.reset(compute.newBuffer(sizeof(std::uint32_t)));
        impl->staging.reset(compute.newBuffer(sizeof(std::uint32_t), "staging"));
        impl->localZero.reset(compute.newBuffer(static_cast<int>(sourceCount * sizeof(std::uint32_t))));
        impl->groupZero.reset(compute.newBuffer(static_cast<int>(impl->groupCount * sizeof(std::uint32_t))));
        impl->groupBefore.reset(compute.newBuffer(static_cast<int>(impl->groupCount * sizeof(std::uint32_t))));
        impl->chunkZero.reset(compute.newBuffer(static_cast<int>(impl->chunkCount * sizeof(std::uint32_t))));
        impl->chunkBefore.reset(compute.newBuffer(static_cast<int>(impl->chunkCount * sizeof(std::uint32_t))));
        impl->totalZeros.reset(compute.newBuffer(sizeof(std::uint32_t)));
        if (!impl->keys[0] || !impl->keys[1] || !impl->keys[2] || !impl->admission || !impl->staging ||
            !impl->localZero || !impl->groupZero || !impl->groupBefore || !impl->chunkZero ||
            !impl->chunkBefore || !impl->totalZeros)
            throw std::runtime_error("GPU spatial key buffer unavailable");
        impl->shader = makeShader(gpu_field_morton_keys_spv, gpu_field_morton_keys_spv_count);
        impl->radixLocal = makeShader(gpu_field_radix_local_spv, gpu_field_radix_local_spv_count);
        impl->radixGroups = makeShader(gpu_field_radix_groups_spv, gpu_field_radix_groups_spv_count);
        impl->radixChunks = makeShader(gpu_field_radix_chunks_spv, gpu_field_radix_chunks_spv_count);
        impl->radixScatter = makeShader(gpu_field_radix_scatter_spv, gpu_field_radix_scatter_spv_count);
        impl->shader->setFloat(0, static_cast<float>(sourceCount));
        for (unsigned axis = 0; axis < 3; ++axis) {
            impl->shader->setFloat(1 + axis, bounds.lower[axis]);
            impl->shader->setFloat(4 + axis, bounds.upper[axis]);
            impl->shader->setFloat(7 + axis, extent[axis]);
        }
        impl->shader->bindBuffer(2, impl->admission.get());
        for (auto* stage : {impl->radixLocal.get(), impl->radixGroups.get(), impl->radixChunks.get(),
                            impl->radixScatter.get()}) {
            stage->setFloat(0, static_cast<float>(sourceCount));
            stage->setFloat(1, static_cast<float>(impl->groupCount));
            stage->setFloat(2, static_cast<float>(impl->chunkCount));
        }
        impl->radixLocal->bindBuffer(1, impl->localZero.get());
        impl->radixLocal->bindBuffer(2, impl->groupZero.get());
        impl->radixGroups->bindBuffer(0, impl->groupZero.get());
        impl->radixGroups->bindBuffer(1, impl->groupBefore.get());
        impl->radixGroups->bindBuffer(2, impl->chunkZero.get());
        impl->radixChunks->bindBuffer(0, impl->chunkZero.get());
        impl->radixChunks->bindBuffer(1, impl->chunkBefore.get());
        impl->radixChunks->bindBuffer(2, impl->totalZeros.get());
        impl->radixScatter->bindBuffer(1, impl->localZero.get());
        impl->radixScatter->bindBuffer(2, impl->groupBefore.get());
        impl->radixScatter->bindBuffer(3, impl->chunkBefore.get());
        impl->radixScatter->bindBuffer(4, impl->totalZeros.get());
        impl->sequence.reset(compute.newSequence());
        if (!impl->sequence || !impl->sequence->isAvailable())
            return fail<std::unique_ptr<GpuMortonKeys>>(DiagnosticCode::Unsupported,
                                                        "GPU spatial key sequence unavailable");
        return Result<std::unique_ptr<GpuMortonKeys>>::success(
            std::unique_ptr<GpuMortonKeys>(new GpuMortonKeys(std::move(impl))));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<GpuMortonKeys>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu.morton"));
    }
}

Result<void> GpuMortonKeys::update(gpgpu::GpuBuffer& sourceRows) {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<void>(DiagnosticCode::StaleHandle, "GPU spatial key provider retired");
    if (!impl_->sequence || !impl_->sequence->isAvailable())
        return fail<void>(DiagnosticCode::Unsupported, "GPU spatial key sequence unavailable");
    const auto sourceView = sourceRows.residentView();
    if (sourceView.backend != GpuResidentBackend::Vulkan || sourceView.sizeBytes < impl_->count * 4 * sizeof(float))
        return fail<void>(DiagnosticCode::InvalidArgument, "Source buffer is not a resident vec4 array");
    try {
        auto&          state     = *impl_;
        const unsigned candidate = (state.phase + 1) % 3;
        state.shader->bindBuffer(0, &sourceRows);
        state.shader->bindBuffer(1, state.keys[candidate].get());
        const std::uint32_t clear = 0;
        state.sequence->begin();
        state.sequence->recordUpload(state.admission.get(), &clear, sizeof(clear));
        state.sequence->recordDispatch(state.shader.get(), static_cast<int>((state.count + 255) / 256));
        state.sequence->recordDownload(state.admission.get(), state.staging.get(), sizeof(clear));
        state.sequence->submit();
        std::uint32_t rejected = 0;
        state.staging->downloadBytes(&rejected, sizeof(rejected));
        if (rejected != 0) {
            state.shader->bindBuffer(0, nullptr);
            return fail<void>(DiagnosticCode::Unsupported, "Source position is outside spatial key domain");
        }
        state.phase     = candidate;
        state.published = true;
        state.sorted    = false;
        state.shader->bindBuffer(0, nullptr);
        return Result<void>::success();
    } catch (const std::exception& error) {
        impl_->shader->bindBuffer(0, nullptr);
        try {
            impl_->sequence.reset();
            impl_->sequence.reset(impl_->compute->newSequence());
        } catch (...) {
            impl_->sequence.reset();
        }
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu.morton"));
    }
}

Result<void> GpuMortonKeys::sort() {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<void>(DiagnosticCode::StaleHandle, "GPU spatial key provider retired");
    auto& state = *impl_;
    if (!state.published)
        return fail<void>(DiagnosticCode::InvalidArgument, "Spatial keys have not been published");
    if (state.sorted) return Result<void>::success();
    try {
        std::unique_ptr<gpgpu::Sequence> sequence(state.compute->newSequence());
        if (!sequence || !sequence->isAvailable())
            return fail<void>(DiagnosticCode::Unsupported, "GPU spatial sort sequence unavailable");
        const unsigned original = state.phase;
        const unsigned scratchA = (original + 1) % 3;
        const unsigned scratchB = (original + 2) % 3;
        unsigned input = original;
        sequence->begin();
        for (unsigned bit = 0; bit < 30; ++bit) {
            const unsigned output = (bit & 1u) == 0 ? scratchA : scratchB;
            state.radixLocal->setFloat(3, static_cast<float>(bit));
            state.radixScatter->setFloat(3, static_cast<float>(bit));
            state.radixLocal->bindBuffer(0, state.keys[input].get());
            sequence->recordDispatch(state.radixLocal.get(), static_cast<int>(state.groupCount));
            sequence->recordDispatch(state.radixGroups.get(), static_cast<int>(state.chunkCount));
            sequence->recordDispatch(state.radixChunks.get(), 1);
            state.radixScatter->bindBuffer(0, state.keys[input].get());
            state.radixScatter->bindBuffer(5, state.keys[output].get());
            sequence->recordDispatch(state.radixScatter.get(), static_cast<int>(state.groupCount));
            input = output;
        }
        sequence->submit();
        state.phase  = input;
        state.sorted = true;
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu.morton"));
    }
}

Result<GpuResidentBufferView> GpuMortonKeys::residentView() const {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<GpuResidentBufferView>(DiagnosticCode::StaleHandle, "GPU spatial key provider retired");
    if (!impl_->published)
        return fail<GpuResidentBufferView>(DiagnosticCode::InvalidArgument, "Spatial keys have not been published");
    return Result<GpuResidentBufferView>::success(impl_->keys[impl_->phase]->residentView());
}

Result<std::vector<GpuMortonKey>> GpuMortonKeys::snapshot() const {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<std::vector<GpuMortonKey>>(DiagnosticCode::StaleHandle, "GPU spatial key provider retired");
    if (!impl_->published)
        return fail<std::vector<GpuMortonKey>>(DiagnosticCode::InvalidArgument, "Spatial keys have not been published");
    try {
        std::vector<GpuMortonKey> keys(impl_->count);
        impl_->keys[impl_->phase]->downloadBytes(keys.data(), keys.size() * sizeof(GpuMortonKey));
        return Result<std::vector<GpuMortonKey>>::success(std::move(keys));
    } catch (const std::exception& error) {
        return Result<std::vector<GpuMortonKey>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu.morton"));
    }
}

}  // namespace eve::physics::field::gpu
