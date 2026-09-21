#include "graphics/webgpu/Graphics.h"

#if !defined(__EMSCRIPTEN__)
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>

namespace eve::graphics::webgpu {
namespace {
struct UploadError {
    wgpu::PopErrorScopeStatus status = wgpu::PopErrorScopeStatus::Error;
    wgpu::ErrorType           type   = wgpu::ErrorType::NoError;
    std::array<char, 512>     message{};
};

// Scopes belong to this graphics-thread operation. Only its own callbacks are
// dispatched by WaitAny; callback state outlives a failed/timed-out observation.
class UploadScopes {
public:
    explicit UploadScopes(const wgpu::Device& device) : device_(device) {
        for (auto& result : results_) result = std::make_shared<UploadError>();
        for (auto filter :
             {wgpu::ErrorFilter::Validation, wgpu::ErrorFilter::OutOfMemory, wgpu::ErrorFilter::Internal}) {
            device_.PushErrorScope(filter);
            ++pending_;
        }
    }
    ~UploadScopes() {
        // Unwind any scopes left by a CPU allocation exception before publication.
        while (pending_) {
            WGPUPopErrorScopeCallbackInfo callback{};
            callback.mode     = WGPUCallbackMode_AllowSpontaneous;
            callback.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) {};
            (void)wgpuDevicePopErrorScope(device_.Get(), callback);
            --pending_;
        }
    }
    [[nodiscard]] Result<void> finish(const wgpu::Instance& instance) {
        std::array<wgpu::Future, 3> futures{};
        for (size_t i = 0; i < futures.size(); ++i) {
            futures[i] = device_.PopErrorScope(
                wgpu::CallbackMode::WaitAnyOnly,
                [result = results_[i]](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type,
                                       wgpu::StringView message) noexcept {
                    result->status = status;
                    result->type   = type;
                    if (message.data) {
                        const auto length = message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length;
                        std::memcpy(result->message.data(), message.data, std::min(length, result->message.size() - 1));
                    }
                });
            --pending_;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (auto future : futures) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            const auto timeout =
                std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count());
            if (instance.WaitAny(future, uint64_t(timeout)) != wgpu::WaitStatus::Success)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                               "WebGPU mip upload error scope could not be observed",
                                                               {}, {}, "graphics.texture.mips"));
        }
        for (const auto& result : results_)
            if (result->status != wgpu::PopErrorScopeStatus::Success || result->type != wgpu::ErrorType::NoError)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Failed, std::string("WebGPU mip upload rejected: ") + result->message.data(), {},
                    {}, "graphics.texture.mips"));
        return Result<void>::success();
    }

private:
    wgpu::Device                                device_;
    unsigned                                    pending_ = 0;
    std::array<std::shared_ptr<UploadError>, 3> results_;
};
}  // namespace

Result<Texture*> Graphics::newTextureMipChain(uint32_t width, uint32_t height, uint32_t levels,
                                              std::span<const uint8_t> rgba) {
    auto fail = [](DiagnosticCode code, std::string message) {
        return Result<Texture*>::failure(Diagnostic::error(code, std::move(message), {}, {}, "graphics.texture.mips"));
    };
    if (!initialized) return fail(DiagnosticCode::Failed, "graphics is not initialized");
    const auto maximum = caps.maxTextureDimension2D();
    if (!width || !height || width > maximum || height > maximum)
        return fail(DiagnosticCode::InvalidArgument, "mip dimensions exceed device limits");
    uint64_t expected = 0;
    uint32_t count = 0, w = width, h = height;
    for (;;) {
        expected += uint64_t(w) * h * 4;
        ++count;
        if (w == 1 && h == 1) break;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
    if (levels != count || expected != rgba.size() || expected > UINT32_MAX)
        return fail(DiagnosticCode::InvalidArgument, "mip count, packed bytes or staging offset budget invalid");
    try {
        ownedTextures.reserve(ownedTextures.size() + 1);
        ownedGpuTextures.reserve(ownedGpuTextures.size() + 1);
        auto tex          = std::make_unique<Texture>();
        auto gpu          = std::make_unique<GpuTexture>();
        gpu->width        = int(width);
        gpu->height       = int(height);
        gpu->mipLevels    = levels;
        gpu->samplerState = TextureSampler::linearMipmap();
        UploadScopes            scopes(device);
        wgpu::TextureDescriptor descriptor{};
        descriptor.dimension     = wgpu::TextureDimension::e2D;
        descriptor.size          = {width, height, 1};
        descriptor.format        = wgpu::TextureFormat::RGBA8Unorm;
        descriptor.mipLevelCount = levels;
        descriptor.sampleCount   = 1;
        descriptor.usage         = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        gpu->texture             = device.CreateTexture(&descriptor);
        if (!gpu->texture) {
            auto checked = scopes.finish(instance);
            if (!checked) return fail(DiagnosticCode::Failed, checked.error()->message());
            return fail(DiagnosticCode::Failed, "WebGPU mip texture allocation returned no object");
        }
        w             = width;
        h             = height;
        size_t offset = 0;
        for (uint32_t level = 0; level < levels; ++level) {
            wgpu::TexelCopyTextureInfo destination{};
            destination.texture  = gpu->texture;
            destination.mipLevel = level;
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow  = w * 4;
            layout.rowsPerImage = h;
            wgpu::Extent3D extent{w, h, 1};
            const size_t   bytes = size_t(w) * h * 4;
            queue.WriteTexture(&destination, rgba.data() + offset, bytes, &layout, &extent);
            offset += bytes;
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        gpu->view    = gpu->texture.CreateView();
        gpu->sampler = makeSampler(gpu->samplerState, levels);
        auto checked = scopes.finish(instance);
        if (!checked) return fail(DiagnosticCode::Failed, checked.error()->message());
        if (!gpu->view || !gpu->sampler)
            return fail(DiagnosticCode::Failed, "WebGPU mip view or sampler allocation returned no object");
        tex->width = tex->pixelWidth = int(width);
        tex->height = tex->pixelHeight = int(height);
        tex->mipmapCount               = int(levels);
        tex->sampler                   = gpu->samplerState;
        tex->gpuHandle                 = gpu.get();
        auto* result                   = tex.get();
        ownedTextures.push_back(std::move(tex));
        ownedGpuTextures.push_back(std::move(gpu));
        return Result<Texture*>::success(result);
    } catch (const std::exception& error) {
        return fail(DiagnosticCode::Failed, error.what());
    }
}

Result<Texture*> Graphics::newTextureArrayRgba16f(uint32_t width, uint32_t height, uint32_t layers,
                                                  std::span<const uint16_t> rgbaHalf) {
    auto fail = [](DiagnosticCode code, std::string message) {
        return Result<Texture*>::failure(
            Diagnostic::error(code, std::move(message), {}, {}, "graphics.texture.array"));
    };
    if (!initialized) return fail(DiagnosticCode::Failed, "graphics is not initialized");
    wgpu::Limits limits{};
    const uint64_t texels = uint64_t(width) * height * layers;
    if (device.GetLimits(&limits) != wgpu::Status::Success || !width || !height || !layers ||
        width > limits.maxTextureDimension2D || height > limits.maxTextureDimension2D ||
        layers > limits.maxTextureArrayLayers || texels > SIZE_MAX / 4u || rgbaHalf.size() != texels * 4u)
        return fail(DiagnosticCode::InvalidArgument, "array dimensions or packed RGBA16F size are invalid");
    try {
        ownedTextures.reserve(ownedTextures.size() + 1u);
        ownedGpuTextures.reserve(ownedGpuTextures.size() + 1u);
        auto tex = std::make_unique<Texture>();
        auto gpu = std::make_unique<GpuTexture>();
        gpu->width = int(width);
        gpu->height = int(height);
        gpu->isArray = true;
        gpu->samplerState = TextureSampler::linear();
        UploadScopes scopes(device);
        wgpu::TextureDescriptor descriptor{};
        descriptor.dimension = wgpu::TextureDimension::e2D;
        descriptor.size = {width, height, layers};
        descriptor.format = wgpu::TextureFormat::RGBA16Float;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        gpu->texture = device.CreateTexture(&descriptor);
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = gpu->texture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = width * 8u;
        layout.rowsPerImage = height;
        const wgpu::Extent3D extent{width, height, layers};
        queue.WriteTexture(&destination, rgbaHalf.data(), rgbaHalf.size_bytes(), &layout, &extent);
        wgpu::TextureViewDescriptor view{};
        view.format = wgpu::TextureFormat::RGBA16Float;
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.mipLevelCount = 1;
        view.arrayLayerCount = layers;
        gpu->view = gpu->texture.CreateView(&view);
        gpu->sampler = makeSampler(gpu->samplerState, 1);
        auto checked = scopes.finish(instance);
        if (!checked) return fail(DiagnosticCode::Failed, checked.error()->message());
        if (!gpu->texture || !gpu->view || !gpu->sampler)
            return fail(DiagnosticCode::Failed, "WebGPU array texture allocation returned no object");
        tex->width = tex->pixelWidth = int(width);
        tex->height = tex->pixelHeight = int(height);
        tex->layers = int(layers);
        tex->sampler = gpu->samplerState;
        tex->gpuHandle = gpu.get();
        auto* result = tex.get();
        ownedTextures.push_back(std::move(tex));
        ownedGpuTextures.push_back(std::move(gpu));
        return Result<Texture*>::success(result);
    } catch (const std::exception& error) {
        return fail(DiagnosticCode::Failed, error.what());
    }
}

Result<Texture*> Graphics::newTexture3DRgba8(uint32_t width, uint32_t height, uint32_t depth,
                                             std::span<const uint8_t> rgba) {
    auto fail = [](DiagnosticCode code, std::string message) {
        return Result<Texture*>::failure(
            Diagnostic::error(code, std::move(message), {}, {}, "graphics.texture.volume"));
    };
    if (!initialized) return fail(DiagnosticCode::Failed, "graphics is not initialized");
    wgpu::Limits limits{};
    const uint64_t texels = uint64_t(width) * height * depth;
    if (device.GetLimits(&limits) != wgpu::Status::Success || !width || !height || !depth ||
        width > limits.maxTextureDimension3D || height > limits.maxTextureDimension3D ||
        depth > limits.maxTextureDimension3D || texels > SIZE_MAX / 4u || rgba.size() != texels * 4u)
        return fail(DiagnosticCode::InvalidArgument, "volume dimensions or packed RGBA8 size are invalid");
    try {
        ownedTextures.reserve(ownedTextures.size() + 1u);
        ownedGpuTextures.reserve(ownedGpuTextures.size() + 1u);
        auto tex = std::make_unique<Texture>();
        auto gpu = std::make_unique<GpuTexture>();
        gpu->width = int(width);
        gpu->height = int(height);
        gpu->isVolume = true;
        gpu->samplerState = TextureSampler::linear();
        gpu->samplerState.repeatU = gpu->samplerState.repeatV = gpu->samplerState.repeatW = true;
        UploadScopes scopes(device);
        wgpu::TextureDescriptor descriptor{};
        descriptor.dimension = wgpu::TextureDimension::e3D;
        descriptor.size = {width, height, depth};
        descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        gpu->texture = device.CreateTexture(&descriptor);
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = gpu->texture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = width * 4u;
        layout.rowsPerImage = height;
        const wgpu::Extent3D extent{width, height, depth};
        queue.WriteTexture(&destination, rgba.data(), rgba.size(), &layout, &extent);
        wgpu::TextureViewDescriptor view{};
        view.format = wgpu::TextureFormat::RGBA8Unorm;
        view.dimension = wgpu::TextureViewDimension::e3D;
        view.mipLevelCount = 1;
        view.arrayLayerCount = 1;
        gpu->view = gpu->texture.CreateView(&view);
        gpu->sampler = makeSampler(gpu->samplerState, 1);
        auto checked = scopes.finish(instance);
        if (!checked) return fail(DiagnosticCode::Failed, checked.error()->message());
        if (!gpu->texture || !gpu->view || !gpu->sampler)
            return fail(DiagnosticCode::Failed, "WebGPU volume texture allocation returned no object");
        tex->width = tex->pixelWidth = int(width);
        tex->height = tex->pixelHeight = int(height);
        tex->depth = int(depth);
        tex->sampler = gpu->samplerState;
        tex->gpuHandle = gpu.get();
        auto* result = tex.get();
        ownedTextures.push_back(std::move(tex));
        ownedGpuTextures.push_back(std::move(gpu));
        return Result<Texture*>::success(result);
    } catch (const std::exception& error) {
        return fail(DiagnosticCode::Failed, error.what());
    }
}
}  // namespace eve::graphics::webgpu
#endif
