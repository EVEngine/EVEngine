#include "graphics/ShaderResources.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace eve::graphics {
Result<void> Graphics::replaceMeshShaderResources(Shader&, const std::vector<uint32_t>&, const std::vector<uint32_t>&,
                                                  const ShaderResourceInputs&) {
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "Resource mesh programs are unavailable on this backend", "backend"));
}

Result<std::vector<ShaderImageRegion>> shaderImageRegions(const ShaderImageInput& image) {
    const auto invalid = [](const char* message) {
        return Result<std::vector<ShaderImageRegion>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "image"));
    };
    if (!image.width || !image.height || image.width > 32768 || image.height > 32768 || !image.layers ||
        image.layers > 2048 || !image.mipLevels ||
        image.mipLevels > std::bit_width(std::max(image.width, image.height)) || image.binding > 31)
        return invalid("Invalid shader image dimensions, mip count or binding");
    switch (image.dimension) {
        case ShaderImageDimension::Image2D:
            if (image.layers != 1) return invalid("A 2D image must have one layer");
            break;
        case ShaderImageDimension::Array2D: break;
        case ShaderImageDimension::Cube:
            if (image.layers != 6 || image.width != image.height) return invalid("A cube image needs six square faces");
            break;
        default: return invalid("Unknown image dimension");
    }
    std::size_t block = 1, stride = 0;
    switch (image.format) {
        case ShaderImageFormat::R8: stride = 1; break;
        case ShaderImageFormat::RG8:
        case ShaderImageFormat::R16: stride = 2; break;
        case ShaderImageFormat::RGBA8:
        case ShaderImageFormat::BGRA8:
        case ShaderImageFormat::RGBA8Srgb:
        case ShaderImageFormat::BGRA8Srgb: stride = 4; break;
        case ShaderImageFormat::BC1:
        case ShaderImageFormat::BC1Srgb:
            block  = 4;
            stride = 8;
            break;
        case ShaderImageFormat::BC3:
        case ShaderImageFormat::BC7:
        case ShaderImageFormat::BC7Srgb:
        case ShaderImageFormat::BC3Srgb:
            block  = 4;
            stride = 16;
            break;
        default: return invalid("Unknown image format");
    }
    if (!std::isfinite(image.sampler.maxAnisotropy) || image.sampler.maxAnisotropy < 1 ||
        !std::isfinite(image.sampler.lodBias) || !std::isfinite(image.sampler.minLod) ||
        !std::isfinite(image.sampler.maxLod) || image.sampler.minLod < 0 || image.sampler.maxLod < image.sampler.minLod)
        return invalid("Invalid shader image sampler");
    std::vector<ShaderImageRegion> result;
    std::size_t                    offset = 0;
    for (std::uint32_t layer = 0; layer < image.layers; ++layer) {
        for (std::uint32_t mip = 0; mip < image.mipLevels; ++mip) {
            const auto width  = std::max(1u, image.width >> mip);
            const auto height = std::max(1u, image.height >> mip);
            const auto size   = ((width + block - 1) / block) * ((height + block - 1) / block) * stride;
            if (size > 1024ull * 1024 * 1024 || offset > 1024ull * 1024 * 1024 - size)
                return invalid("Shader image exceeds 1 GiB upload limit");
            result.push_back({layer, mip, width, height, offset, size});
            offset += size;
        }
    }
    if (image.bytes.size() != offset) return invalid("Shader image payload size does not match its layout");
    return Result<std::vector<ShaderImageRegion>>::success(std::move(result));
}
Result<std::uint32_t> shaderInstanceMatrixCount(std::span<const std::byte> bytes) {
    auto bad = [](const char* message) {
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "shader.instances"));
    };
    if (bytes.size() % 64 || bytes.size() > 64ull * 1024 * 1024)
        return bad("Instance matrices require complete 64-byte records, at most 64 MiB");
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        float m[16];
        std::memcpy(m, bytes.data() + offset, sizeof(m));
        for (float value : m)
            if (!std::isfinite(value)) return bad("Nonfinite instance transform");
        if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) return bad("Instance transform must be affine");
        const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6]) -
                                   double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2]) +
                                   double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
        if (!std::isfinite(determinant) || determinant == 0) return bad("Instance transform must be invertible");
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(bytes.size() / 64));
}
}  // namespace eve::graphics
