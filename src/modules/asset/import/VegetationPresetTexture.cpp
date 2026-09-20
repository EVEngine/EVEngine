#include "asset/import/VegetationPreset.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace eve::asset_import {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), {}, {}, "asset.import.vegetation-preset.texture"));
}

bool valid(const VegetationPresetImage& image, std::uint64_t maximumPixels) {
    const auto count = std::uint64_t(image.width) * image.height;
    return image.width && image.height && count <= maximumPixels && count <= SIZE_MAX / 4 &&
           image.pixels.size() == std::size_t(count) * 4;
}

float channel(const VegetationPresetImage& image, float u, float v, unsigned component) {
    const float x  = std::clamp(u * image.width - 0.5f, 0.f, float(image.width - 1));
    const float y  = std::clamp(v * image.height - 0.5f, 0.f, float(image.height - 1));
    const auto  x0 = std::uint32_t(x), y0 = std::uint32_t(y);
    const auto  x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
    const auto  at = [&](std::uint32_t px, std::uint32_t py) {
        return image.pixels[(std::size_t(py) * image.width + px) * 4 + component] / 255.f;
    };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), x - x0), std::lerp(at(x0, y1), at(x1, y1), x - x0), y - y0);
}

Result<float> sample(const VegetationPresetImage& image, const VegetationTextureChannelRecipe& recipe, float u,
                     float v) {
    float value = 0.f;
    if (recipe.selector == "GET_RED")
        value = channel(image, u, v, 0);
    else if (recipe.selector == "GET_GREEN")
        value = channel(image, u, v, 1);
    else if (recipe.selector == "GET_BLUE")
        value = channel(image, u, v, 2);
    else if (recipe.selector == "GET_ALPHA")
        value = channel(image, u, v, 3);
    else if (recipe.selector == "GET_MAX")
        value = std::max({channel(image, u, v, 0), channel(image, u, v, 1), channel(image, u, v, 2)});
    else if (recipe.selector == "GET_GRAY" || recipe.selector == "GET_GREY")
        value = (channel(image, u, v, 0) + channel(image, u, v, 1) + channel(image, u, v, 2)) / 3.f;
    else
        return failure<float>(DiagnosticCode::Unsupported, "unsupported texture channel selector: " + recipe.selector);
    if (recipe.action == "ACTION_ONE_MINUS")
        value = 1.f - value;
    else if (!recipe.action.empty())
        return failure<float>(DiagnosticCode::Unsupported, "unsupported texture channel action: " + recipe.action);
    return Result<float>::success(std::clamp(value, 0.f, 1.f));
}

float gammaToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
float linearToGamma(float value) {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.f / 2.4f) - 0.055f;
}

Result<void> transformNormalMap(VegetationPresetImage& image, const asset::CanonicalMeshData& mesh,
                                bool objectToTangent) {
    const auto count   = mesh.positions.size() / 3;
    const auto tangent = mesh.attributes.find("TANGENT");
    const auto uv      = mesh.texcoords.find(0);
    if (!count || mesh.normals.size() != count * 3 || tangent == mesh.attributes.end() ||
        tangent->second.components != 4 || tangent->second.values.size() != count * 4 || uv == mesh.texcoords.end() ||
        uv->second.size() != count * 2 || mesh.indices.size() % 3)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "normal-space transform requires normals, tangent float4, UV0 and triangles");
    auto norm = [](float& x, float& y, float& z) {
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l < 1e-8f) return false;
        x /= l;
        y /= l;
        z /= l;
        return true;
    };
    struct Frame {
        std::uint32_t a = 0, b = 0, c = 0;
        float         w0 = 0, w1 = 0, w2 = 0;
        bool          covered = false;
    };
    struct Triangle {
        std::uint32_t a = 0, b = 0, c = 0;
        float         ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0, determinant = 0;
        int           minX = 0, maxX = -1, minY = 0, maxY = -1;
    };
    constexpr std::uint32_t tileSize = 16;
    const auto              tilesX   = (image.width + tileSize - 1) / tileSize;
    const auto              tilesY   = (image.height + tileSize - 1) / tileSize;
    std::vector<Frame>      frames(std::size_t(image.width) * image.height);
    std::vector<Triangle>   triangles;
    triangles.reserve(mesh.indices.size() / 3);
    std::vector<std::vector<std::uint32_t>> bins(std::size_t(tilesX) * tilesY);
    std::uint64_t                           rasterWork = 0, binReferences = 0;
    for (std::size_t k = 0; k < mesh.indices.size(); k += 3) {
        const auto a = mesh.indices[k], b = mesh.indices[k + 1], c = mesh.indices[k + 2];
        if (a >= count || b >= count || c >= count)
            return failure<void>(DiagnosticCode::InvalidArgument, "normal-space mesh index out of range");
        const float ax = uv->second[a * 2], ay = uv->second[a * 2 + 1], bx = uv->second[b * 2],
                    by = uv->second[b * 2 + 1], cx = uv->second[c * 2], cy = uv->second[c * 2 + 1];
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by) ||
            !std::isfinite(cx) || !std::isfinite(cy))
            return failure<void>(DiagnosticCode::InvalidArgument, "normal-space mesh contains nonfinite UVs");
        const float determinant = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(determinant) < 1e-10f) continue;
        const int minX = std::max(0, int(std::ceil(std::min({ax, bx, cx}) * image.width - .5f)));
        const int maxX = std::min(int(image.width) - 1, int(std::floor(std::max({ax, bx, cx}) * image.width - .5f)));
        const int minY = std::max(0, int(std::ceil(std::min({ay, by, cy}) * image.height - .5f)));
        const int maxY = std::min(int(image.height) - 1, int(std::floor(std::max({ay, by, cy}) * image.height - .5f)));
        if (minX > maxX || minY > maxY) continue;
        rasterWork += std::uint64_t(maxX - minX + 1) * std::uint64_t(maxY - minY + 1);
        if (rasterWork > 512ull * 1024ull * 1024ull)
            return failure<void>(DiagnosticCode::InvalidArgument, "normal-space raster work budget exceeded");
        const auto triangleIndex = std::uint32_t(triangles.size());
        triangles.push_back({a, b, c, ax, ay, bx, by, cx, cy, determinant, minX, maxX, minY, maxY});
        const auto firstTileX = std::uint32_t(minX) / tileSize, lastTileX = std::uint32_t(maxX) / tileSize;
        const auto firstTileY = std::uint32_t(minY) / tileSize, lastTileY = std::uint32_t(maxY) / tileSize;
        binReferences += std::uint64_t(lastTileX - firstTileX + 1) * (lastTileY - firstTileY + 1);
        if (binReferences > 32ull * 1024ull * 1024ull)
            return failure<void>(DiagnosticCode::InvalidArgument, "normal-space triangle-bin budget exceeded");
        for (auto tileY = firstTileY; tileY <= lastTileY; ++tileY)
            for (auto tileX = firstTileX; tileX <= lastTileX; ++tileX)
                bins[std::size_t(tileY) * tilesX + tileX].push_back(triangleIndex);
    }
    for (std::uint32_t tileY = 0; tileY < tilesY; ++tileY)
        for (std::uint32_t tileX = 0; tileX < tilesX; ++tileX) {
            const int tileMinX = int(tileX * tileSize), tileMinY = int(tileY * tileSize);
            const int tileMaxX = std::min(int(image.width) - 1, tileMinX + int(tileSize) - 1);
            const int tileMaxY = std::min(int(image.height) - 1, tileMinY + int(tileSize) - 1);
            for (const auto triangleIndex : bins[std::size_t(tileY) * tilesX + tileX]) {
                const auto& triangle = triangles[triangleIndex];
                for (int py = std::max(tileMinY, triangle.minY); py <= std::min(tileMaxY, triangle.maxY); ++py)
                    for (int px = std::max(tileMinX, triangle.minX); px <= std::min(tileMaxX, triangle.maxX); ++px) {
                        auto&       frame = frames[std::size_t(py) * image.width + px];
                        const float u = (px + .5f) / image.width, v = (py + .5f) / image.height;
                        const float w0 = ((triangle.by - triangle.cy) * (u - triangle.cx) +
                                          (triangle.cx - triangle.bx) * (v - triangle.cy)) /
                                         triangle.determinant;
                        const float w1 = ((triangle.cy - triangle.ay) * (u - triangle.cx) +
                                          (triangle.ax - triangle.cx) * (v - triangle.cy)) /
                                         triangle.determinant;
                        const float w2 = 1 - w0 - w1;
                        if (w0 >= -1e-5f && w1 >= -1e-5f && w2 >= -1e-5f)
                            frame = {triangle.a, triangle.b, triangle.c, w0, w1, w2, true};
                    }
            }
        }
    for (std::uint32_t py = 0; py < image.height; ++py)
        for (std::uint32_t px = 0; px < image.width; ++px) {
            const auto& frame = frames[std::size_t(py) * image.width + px];
            if (!frame.covered) continue;
            auto interp = [&](const std::vector<float>& values, unsigned stride, unsigned component) {
                return values[frame.a * stride + component] * frame.w0 +
                       values[frame.b * stride + component] * frame.w1 +
                       values[frame.c * stride + component] * frame.w2;
            };
            float nx = interp(mesh.normals, 3, 0), ny = interp(mesh.normals, 3, 1), nz = interp(mesh.normals, 3, 2);
            float tx = interp(tangent->second.values, 4, 0), ty = interp(tangent->second.values, 4, 1),
                  tz = interp(tangent->second.values, 4, 2);
            if (!norm(nx, ny, nz) || !norm(tx, ty, tz))
                return failure<void>(DiagnosticCode::InvalidArgument, "degenerate normal-space frame");
            const float sign = interp(tangent->second.values, 4, 3) >= 0 ? 1.f : -1.f;
            float bx = (ny * tz - nz * ty) * sign, by = (nz * tx - nx * tz) * sign, bz = (nx * ty - ny * tx) * sign;
            norm(bx, by, bz);
            const auto at = (std::size_t(py) * image.width + px) * 4;
            float      x = image.pixels[at] / 127.5f - 1, y = image.pixels[at + 1] / 127.5f - 1,
                       z = image.pixels[at + 2] / 127.5f - 1;
            float      ox, oy, oz;
            if (objectToTangent) {
                ox = x * tx + y * ty + z * tz;
                oy = x * bx + y * by + z * bz;
                oz = x * nx + y * ny + z * nz;
            } else {
                ox = tx * x + bx * y + nx * z;
                oy = ty * x + by * y + ny * z;
                oz = tz * x + bz * y + nz * z;
            }
            if (!norm(ox, oy, oz)) return failure<void>(DiagnosticCode::InvalidArgument, "zero normal-map direction");
            image.pixels[at]     = std::uint8_t(std::clamp(ox * .5f + .5f, 0.f, 1.f) * 255 + .5f);
            image.pixels[at + 1] = std::uint8_t(std::clamp(oy * .5f + .5f, 0.f, 1.f) * 255 + .5f);
            image.pixels[at + 2] = std::uint8_t(std::clamp(oz * .5f + .5f, 0.f, 1.f) * 255 + .5f);
        }
    return Result<void>::success();
}
}  // namespace

Result<std::map<std::string, VegetationPresetImage>> executeVegetationTexturePacks(
    const VegetationConversionCandidate& candidate, const std::map<std::string, VegetationPresetImage>& sources,
    std::uint64_t maximumPixels) {
    if (!maximumPixels)
        return failure<std::map<std::string, VegetationPresetImage>>(DiagnosticCode::InvalidArgument,
                                                                     "texture pixel budget must be positive");
    try {
        std::map<std::string, VegetationPresetImage> outputs;
        for (const auto& recipe : candidate.texturePacks) {
            if (recipe.targetProperty.empty())
                return failure<std::map<std::string, VegetationPresetImage>>(DiagnosticCode::ParseError,
                                                                             "texture recipe has no target property");
            if (recipe.transformSpace == "OBJECT_TO_TANGENT" || recipe.transformSpace == "TANGENT_TO_OBJECT")
                return failure<std::map<std::string, VegetationPresetImage>>(
                    DiagnosticCode::Unsupported, "texture tangent-space conversion requires a mesh execution context");
            std::uint32_t width = 0, height = 0;
            for (const auto& instruction : recipe.channels) {
                if (instruction.selector.empty() || instruction.selector == "NONE") continue;
                const auto found = sources.find(instruction.sourceProperty);
                if (found == sources.end()) continue;
                if (!valid(found->second, maximumPixels))
                    return failure<std::map<std::string, VegetationPresetImage>>(DiagnosticCode::InvalidArgument,
                                                                                 "invalid source texture image");
                width  = std::max(width, found->second.width);
                height = std::max(height, found->second.height);
            }
            const auto count = std::uint64_t(width) * height;
            if (!width || !height) continue;
            if (count > maximumPixels || count > SIZE_MAX / 4)
                return failure<std::map<std::string, VegetationPresetImage>>(DiagnosticCode::InvalidArgument,
                                                                             "texture recipe exceeds pixel budget");
            VegetationPresetImage output{width, height, std::vector<std::uint8_t>(std::size_t(count) * 4, 0)};
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    for (unsigned c = 0; c < 4; ++c) {
                        const auto& instruction = recipe.channels[c];
                        if (instruction.selector.empty() || instruction.selector == "NONE") continue;
                        const auto found = sources.find(instruction.sourceProperty);
                        if (found == sources.end()) continue;
                        auto value = sample(found->second, instruction, (x + 0.5f) / width, (y + 0.5f) / height);
                        if (!value)
                            return Result<std::map<std::string, VegetationPresetImage>>::failure(value.status());
                        float component = value.value();
                        if (recipe.transformSpace == "GAMMA_TO_LINEAR")
                            component = gammaToLinear(component);
                        else if (recipe.transformSpace == "LINEAR_TO_GAMMA")
                            component = linearToGamma(component);
                        else if (!recipe.transformSpace.empty() && recipe.transformSpace != "NONE")
                            return failure<std::map<std::string, VegetationPresetImage>>(
                                DiagnosticCode::Unsupported,
                                "unsupported texture transform space: " + recipe.transformSpace);
                        output.pixels[(std::size_t(y) * width + x) * 4 + c] =
                            std::uint8_t(std::clamp(component, 0.f, 1.f) * 255.f + 0.5f);
                    }
                }
            }
            outputs[recipe.targetProperty] = std::move(output);
        }
        return Result<std::map<std::string, VegetationPresetImage>>::success(std::move(outputs));
    } catch (const std::bad_alloc&) {
        return failure<std::map<std::string, VegetationPresetImage>>(DiagnosticCode::Failed,
                                                                     "texture packing allocation failed");
    }
}

Result<std::map<std::string, VegetationPresetImage>> executeVegetationTexturePacks(
    const VegetationConversionCandidate& candidate, const std::map<std::string, VegetationPresetImage>& sources,
    const asset::CanonicalMeshData& mesh, std::uint64_t maximumPixels) {
    try {
        auto plain = candidate;
        for (auto& recipe : plain.texturePacks)
            if (recipe.transformSpace == "OBJECT_TO_TANGENT" || recipe.transformSpace == "TANGENT_TO_OBJECT")
                recipe.transformSpace = "NONE";
        auto outputs = executeVegetationTexturePacks(plain, sources, maximumPixels);
        if (!outputs) return outputs;
        for (const auto& recipe : candidate.texturePacks) {
            if (recipe.transformSpace != "OBJECT_TO_TANGENT" && recipe.transformSpace != "TANGENT_TO_OBJECT") continue;
            auto found = outputs.value().find(recipe.targetProperty);
            if (found == outputs.value().end()) continue;
            auto changed = transformNormalMap(found->second, mesh, recipe.transformSpace == "OBJECT_TO_TANGENT");
            if (!changed) return Result<std::map<std::string, VegetationPresetImage>>::failure(changed.status());
        }
        return outputs;
    } catch (const std::bad_alloc&) {
        return failure<std::map<std::string, VegetationPresetImage>>(
            DiagnosticCode::Failed, "normal-space texture conversion allocation failed");
    }
}
}  // namespace eve::asset_import
