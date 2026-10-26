#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <glm/mat4x4.hpp>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <vector>
#include "common/BorrowedRef.h"
#include "common/SquirrelBinding.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/PreparedFile.h"
#include "graphics/Bloom.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/ShaderResources.h"
#include "graphics/ShaderScriptBindings.h"
#include "graphics/shaders/stylized_grass_spv.inc"

// ShaderResourceScriptBindings.cpp
namespace eve::graphics::detail {
namespace {
const Value& field(const Value& object, const std::string& name, Value::Type type) {
    auto* found = object.find(name);
    if (!found || found->type() != type) throw std::runtime_error("Missing or invalid image field: " + name);
    return *found;
}
uint32_t number(const Value& object, const std::string& name) {
    auto value = field(object, name, Value::Type::Int64).asInt();
    if (value < 0 || uint64_t(value) > UINT32_MAX) throw std::runtime_error("Image integer out of range: " + name);
    return uint32_t(value);
}
std::shared_ptr<const filesystem::FileData> readBytes(const std::string& path, size_t limit) {
    auto* filesystem = filesystem::Filesystem::create();
    if (!filesystem) throw std::runtime_error("Filesystem module initialization failed");
    auto result = filesystem::readPreparedFile(path, limit);
    if (!result) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
Result<void> replace(Graphics& graphics, Shader& shader, const std::string& vertex, const std::string& fragment,
                     const Value& descriptions, const std::string& constants, const std::string& instances = {}) {
    try {
        if (!descriptions.isArray() || descriptions.arraySize() > 16)
            throw std::runtime_error("Expected at most 16 image resource descriptions");
        std::vector<uint32_t> vert;
        if (!vertex.empty()) {
            auto stage = readShaderStageFile(vertex);
            if (!stage) return Result<void>::failure(stage.status());
            vert = std::move(stage).takeValue();
        }
        auto frag = readShaderStageFile(fragment);
        if (!frag) return Result<void>::failure(frag.status());
        std::vector<ShaderImageInput>                            images;
        std::vector<std::shared_ptr<const filesystem::FileData>> files;
        constexpr std::string_view keys[] = {"binding", "format",     "dimension", "width",  "height",
                                             "layers",  "mips",       "path",      "filter", "wrapU",
                                             "wrapV",   "anisotropy", "depth",     "wrapW"};
        constexpr std::pair<std::string_view, ShaderImageFormat> formats[] = {
            {"r8-unorm", ShaderImageFormat::R8},
            {"rg8-unorm", ShaderImageFormat::RG8},
            {"r16-unorm", ShaderImageFormat::R16},
            {"rgba8-unorm", ShaderImageFormat::RGBA8},
            {"bgra8-unorm", ShaderImageFormat::BGRA8},
            {"rgba8-srgb", ShaderImageFormat::RGBA8Srgb},
            {"bgra8-srgb", ShaderImageFormat::BGRA8Srgb},
            {"bc3-unorm", ShaderImageFormat::BC3},
            {"bc1-unorm", ShaderImageFormat::BC1},
            {"bc1-srgb", ShaderImageFormat::BC1Srgb},
            {"bc3-srgb", ShaderImageFormat::BC3Srgb},
            {"bc7-unorm", ShaderImageFormat::BC7},
            {"bc7-srgb", ShaderImageFormat::BC7Srgb},
            {"rgba16-unorm", ShaderImageFormat::RGBA16Unorm},
            {"rgba16-float", ShaderImageFormat::RGBA16Float},
            {"rgba32-float", ShaderImageFormat::RGBA32Float}};
        size_t totalBytes = 0;
        for (size_t i = 0; i < descriptions.arraySize(); ++i) {
            const auto& row = descriptions.at(i);
            if (!row.isObject()) throw std::runtime_error("Image description must be an object");
            for (const auto& key : row.keys())
                if (std::find(std::begin(keys), std::end(keys), key) == std::end(keys))
                    throw std::runtime_error("Unknown image resource field: " + key);
            ShaderImageInput image;
            image.binding     = number(row, "binding");
            image.width       = number(row, "width");
            image.height      = number(row, "height");
            image.layers      = number(row, "layers");
            image.mipLevels   = number(row, "mips");
            const auto format = field(row, "format", Value::Type::String).asString();
            auto       found  = std::find_if(std::begin(formats), std::end(formats),
                                             [&](const auto& item) { return item.first == format; });
            if (found == std::end(formats)) throw std::runtime_error("Unknown image format: " + format);
            image.format         = found->second;
            const auto dimension = field(row, "dimension", Value::Type::String).asString();
            if (dimension == "2d")
                image.dimension = ShaderImageDimension::Image2D;
            else if (dimension == "array2d")
                image.dimension = ShaderImageDimension::Array2D;
            else if (dimension == "cube")
                image.dimension = ShaderImageDimension::Cube;
            else if (dimension == "3d")
                image.dimension = ShaderImageDimension::Image3D;
            else
                throw std::runtime_error("Unknown image view dimension: " + dimension);
            if (row.find("depth") || image.dimension == ShaderImageDimension::Image3D)
                image.depth = number(row, "depth");
            auto filter = number(row, "filter");
            auto wrapU = number(row, "wrapU"), wrapV = number(row, "wrapV");
            if (filter > 2 || wrapU > 1 || wrapV > 1) throw std::runtime_error("Unknown resource sampler mode");
            image.sampler.min = image.sampler.mag = filter == 0 ? FilterMode::Nearest : FilterMode::Linear;
            image.sampler.mipmap                  = image.mipLevels <= 1 ? MipmapMode::Disabled
                                                    : filter == 2        ? MipmapMode::Linear
                                                                         : MipmapMode::Nearest;
            image.sampler.repeatU                 = wrapU == 0;
            image.sampler.repeatV                 = wrapV == 0;
            if (row.find("wrapW") || image.dimension == ShaderImageDimension::Image3D) {
                const auto wrapW = number(row, "wrapW");
                if (wrapW > 1) throw std::runtime_error("Unknown resource W sampler mode");
                image.sampler.repeatW = wrapW == 0;
            }
            image.sampler.maxAnisotropy = float(number(row, "anisotropy"));
            const auto path             = field(row, "path", Value::Type::String).asString();
            files.push_back(readBytes(path, 1024ull * 1024 * 1024));
            const auto& bytes = *files.back();
            totalBytes += bytes.getSize();
            if (totalBytes > 2ull * 1024 * 1024 * 1024) throw std::runtime_error("Resource set exceeds 2 GiB");
            image.bytes        = {static_cast<const std::byte*>(bytes.getData()), size_t(bytes.getSize())};
            image.contentOwner = files.back();
            images.push_back(image);
        }
        std::shared_ptr<const filesystem::FileData> uniform;
        std::shared_ptr<const filesystem::FileData> matrices;
        ShaderResourceInputs                        inputs{images, {}};
        if (!constants.empty()) {
            uniform          = readBytes(constants, 65536);
            inputs.constants = {static_cast<const std::byte*>(uniform->getData()), size_t(uniform->getSize())};
        }
        if (!instances.empty()) {
            matrices                = readBytes(instances, 64ull * 1024 * 1024);
            inputs.instanceMatrices = {static_cast<const std::byte*>(matrices->getData()), size_t(matrices->getSize())};
        }
        return graphics.replaceMeshShaderResources(shader, vert, frag.value(), inputs);
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "resources"));
    }
}
}  // namespace
void exposeShaderResourceBindings(ssq::Table& table, ssq::Class& cls) {
    cls.addFunc("captureMesh3DSceneColor", [](Graphics* graphics) {
        switch (graphics->captureMesh3DSceneColor()) {
            case Graphics::Mesh3DSceneColorCaptureStatus::ExplicitOverride: return std::string("explicitOverride");
            case Graphics::Mesh3DSceneColorCaptureStatus::Scheduled: return std::string("scheduled");
            case Graphics::Mesh3DSceneColorCaptureStatus::Captured: return std::string("captured");
            case Graphics::Mesh3DSceneColorCaptureStatus::HistoryReuse: return std::string("historyReuse");
            case Graphics::Mesh3DSceneColorCaptureStatus::Unavailable: return std::string("unavailable");
        }
        return std::string("unavailable");
    });
    const auto vm = table.getHandle();
    cls.addFunc("setBloomFilter", [vm](Graphics* graphics, const std::string& mode, float scatter, int iterations,
                                       float clamp) {
        if (!graphics || (mode != "karisTent" && mode != "gaussianScatter" && mode != "gaussianPyramid"))
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument,
                        "Expected bloom filter karisTent, gaussianScatter or gaussianPyramid", "graphics.bloom")));
        BloomFilterSettings settings{mode == "gaussianScatter"   ? BloomFilter::GaussianScatter
                                     : mode == "gaussianPyramid" ? BloomFilter::GaussianPyramid
                                                                 : BloomFilter::KarisTent,
                                     scatter, iterations, clamp};
        auto valid = validateBloomFilterSettings(settings);
        if (!valid) return script::projectResult(vm, Result<void>::failure(valid.status()));
        return script::projectResult(vm, graphics->pipelineBloom()->configureFilter(settings));
    });
    cls.addFunc("getBloomFilter", [](Graphics* graphics) {
        return std::string(
            graphics->pipelineBloom()->filterSettings().filter == BloomFilter::GaussianScatter   ? "gaussianScatter"
            : graphics->pipelineBloom()->filterSettings().filter == BloomFilter::GaussianPyramid ? "gaussianPyramid"
                                                                                                 : "karisTent");
    });
    cls.addFunc("setSceneToneMapping", [vm](Graphics* graphics, const std::string& mode) {
        if (!graphics || (mode != "none" && mode != "aces" && mode != "filmic"))
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Expected scene tone mapping none, aces or filmic",
                                                            "graphics.presentation")));
        return script::projectResult(
            vm, graphics->setSceneToneMapping(mode == "none"     ? Graphics::SceneToneMapping::None
                                              : mode == "filmic" ? Graphics::SceneToneMapping::Filmic
                                                                 : Graphics::SceneToneMapping::Aces));
    });
    cls.addFunc("setScenePhotographicVignette", [vm](Graphics* graphics, float intensity) {
        return script::projectResult(vm, graphics->setScenePhotographicVignette(intensity));
    });
    cls.addFunc("getScenePhotographicVignette",
                [](Graphics* graphics) { return graphics->getScenePhotographicVignette(); });
    cls.addFunc("getSceneToneMapping", [](Graphics* graphics) {
        return std::string(graphics->getSceneToneMapping() == Graphics::SceneToneMapping::None     ? "none"
                           : graphics->getSceneToneMapping() == Graphics::SceneToneMapping::Filmic ? "filmic"
                                                                                                   : "aces");
    });
    cls.addFunc("setDisplayOutputMode", [vm](Graphics* graphics, const std::string& mode) {
        if (!graphics)
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics must be present",
                                                            "graphics.presentation")));
        Graphics::DisplayOutputMode value = Graphics::DisplayOutputMode::Sdr;
        if (mode == "sdr")
            value = Graphics::DisplayOutputMode::Sdr;
        else if (mode == "auto")
            value = Graphics::DisplayOutputMode::Auto;
        else if (mode == "hdr10")
            value = Graphics::DisplayOutputMode::Hdr10;
        else if (mode == "scrgb")
            value = Graphics::DisplayOutputMode::ScRgb;
        else
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Expected display output mode sdr|auto|hdr10|scrgb",
                                                            "graphics.presentation")));
        return script::projectResult(vm, graphics->setDisplayOutputMode(value));
    });
    cls.addFunc("getDisplayOutputMode", [](Graphics* graphics) {
        switch (graphics->getDisplayOutputMode()) {
            case Graphics::DisplayOutputMode::Auto: return std::string("auto");
            case Graphics::DisplayOutputMode::Hdr10: return std::string("hdr10");
            case Graphics::DisplayOutputMode::ScRgb: return std::string("scrgb");
            case Graphics::DisplayOutputMode::Sdr:
            default: return std::string("sdr");
        }
    });
    cls.addFunc("setDisplayHdrCalibration", [vm](Graphics* graphics, float paperWhiteNits, float peakNits) {
        if (!graphics)
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics must be present",
                                                            "graphics.presentation")));
        return script::projectResult(vm, graphics->setDisplayHdrCalibration(paperWhiteNits, peakNits));
    });
    cls.addFunc("getDisplayPaperWhiteNits", &Graphics::getDisplayPaperWhiteNits);
    cls.addFunc("getDisplayPeakNits", &Graphics::getDisplayPeakNits);
    cls.addFunc("getActiveDisplayColorSpace", [](Graphics* graphics) {
        switch (graphics->getActiveDisplayColorSpace()) {
            case Graphics::DisplayColorSpace::Hdr10: return std::string("hdr10");
            case Graphics::DisplayColorSpace::ScRgb: return std::string("scrgb");
            case Graphics::DisplayColorSpace::Sdr:
            default: return std::string("sdr");
        }
    });
    cls.addFunc("isDisplayHdrActive", &Graphics::isDisplayHdrActive);
    cls.addFunc("queryDisplayOutputSupport", [vm](Graphics* graphics) {
        if (!graphics)
            return script::projectResult(
                vm,
                Result<Graphics::DisplayOutputSupport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Graphics must be present", "graphics.presentation")),
                [](Graphics::DisplayOutputSupport) { return Value::Object{}; });
        return script::projectResult(
            vm, graphics->queryDisplayOutputSupport(), [](const Graphics::DisplayOutputSupport& support) {
                return Value::Object{{"sdr", support.sdr}, {"hdr10", support.hdr10}, {"scRgb", support.scRgb}};
            });
    });
    cls.addFunc("drawMeshShaderInstances", [vm](Graphics* graphics, Mesh* mesh, Shader* shader, ssq::Array model,
                                                int64_t first, int64_t count) {
        try {
            if (!graphics || !mesh || !shader || model.size() != 16 || first < 0 || count < 0 ||
                uint64_t(first) > UINT32_MAX || uint64_t(count) > UINT32_MAX)
                throw std::runtime_error("Expected mesh, shader, 16 matrix floats and nonnegative uint32 range");
            glm::mat4 transform;
            for (int i = 0; i < 16; ++i) transform[i / 4][i % 4] = model.get<float>(i);
            return script::projectResult(vm, graphics->drawMeshShaderInstances(*mesh, *shader, transform, Color(1.f),
                                                                               uint32_t(first), uint32_t(count)));
        } catch (const std::exception& error) {
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, error.what(), "graphics.instances")));
        }
    });
    cls.addFunc("replaceMeshShaderResourcesFromFiles", [vm](Graphics* graphics, Shader* shader,
                                                            const std::string& vertex, const std::string& fragment,
                                                            ssq::Object images, const std::string& constants) {
        if (!graphics || !shader)
            return script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics and Shader must be present")));
        auto value = script::valueFromSquirrel(images);
        if (!value) return script::projectResult(vm, Result<void>::failure(value.status()));
        return script::projectResult(vm, replace(*graphics, *shader, vertex, fragment, value.value(), constants));
    });
    cls.addFunc("replaceInstancedMeshShaderResourcesFromFiles",
                [vm](Graphics* graphics, Shader* shader, const std::string& vertex, const std::string& fragment,
                     ssq::Object images, const std::string& constants, const std::string& instances) {
                    if (!graphics || !shader || instances.empty())
                        return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                             DiagnosticCode::InvalidArgument,
                                                             "Graphics, Shader and instance file must be present")));
                    auto value = script::valueFromSquirrel(images);
                    if (!value) return script::projectResult(vm, Result<void>::failure(value.status()));
                    return script::projectResult(
                        vm, replace(*graphics, *shader, vertex, fragment, value.value(), constants, instances));
                });
}
}  // namespace eve::graphics::detail

// ShaderResources.cpp
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
        image.layers > 2048 || !image.depth || image.depth > 2048 || !image.mipLevels ||
        image.mipLevels > static_cast<uint32_t>(std::bit_width(std::max({image.width, image.height, image.depth}))) ||
        image.binding > 31)
        return invalid("Invalid shader image dimensions, mip count or binding");
    if (image.dimension != ShaderImageDimension::Image3D && image.depth != 1)
        return invalid("Non-volume images require depth one");
    switch (image.dimension) {
        case ShaderImageDimension::Image2D:
            if (image.layers != 1) return invalid("A 2D image must have one layer");
            break;
        case ShaderImageDimension::Array2D: break;
        case ShaderImageDimension::Image3D:
            if (image.layers != 1 || image.width > 2048 || image.height > 2048)
                return invalid("A volume requires one layer and at most 2048 texels per dimension");
            break;
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
        case ShaderImageFormat::RGBA16Unorm:
        case ShaderImageFormat::RGBA16Float: stride = 8; break;
        case ShaderImageFormat::RGBA32Float: stride = 16; break;
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
    if (image.dimension == ShaderImageDimension::Image3D && block != 1)
        return invalid("Block-compressed volume images are unsupported");
    if (!std::isfinite(image.sampler.maxAnisotropy) || image.sampler.maxAnisotropy < 1 ||
        !std::isfinite(image.sampler.lodBias) || !std::isfinite(image.sampler.minLod) ||
        !std::isfinite(image.sampler.maxLod) || image.sampler.minLod < 0 || image.sampler.maxLod < image.sampler.minLod)
        return invalid("Invalid shader image sampler");
    std::vector<ShaderImageRegion> result;
    std::size_t                    offset = 0;
    for (std::uint32_t layer = 0; layer < image.layers; ++layer) {
        for (std::uint32_t mip = 0; mip < image.mipLevels; ++mip) {
            const auto     width  = std::max(1u, image.width >> mip);
            const auto     height = std::max(1u, image.height >> mip);
            const auto     depth  = std::max(1u, image.depth >> mip);
            const uint64_t size =
                uint64_t((width + block - 1) / block) * ((height + block - 1) / block) * stride * depth;
            if (size > 1024ull * 1024 * 1024 || offset > 1024ull * 1024 * 1024 - size)
                return invalid("Shader image exceeds 1 GiB upload limit");
            result.push_back({layer, mip, width, height, offset, static_cast<size_t>(size), depth});
            offset += static_cast<size_t>(size);
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

// ShaderScriptBindings.cpp
namespace eve::graphics {
namespace {
Result<Value> buildGrassPoints(double radius, double extent, int seed) {
    if (!std::isfinite(radius) || !std::isfinite(extent) || radius <= 0 || extent <= 0 || radius < extent / 1000 ||
        seed < 0)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "Invalid bounded grass scatter dimensions or seed", "scatter"));
    const double     cell = radius / std::sqrt(2.0);
    const int        side = int(std::ceil(2 * extent / cell)) + 1;
    std::vector<int> grid(size_t(side) * size_t(side), -1), active;
    struct Point {
        double x, z;
    };
    std::vector<Point> points;
    int64_t            state = int64_t(seed) % 2147483646 + 1;
    auto               next  = [&]() {
        state = state * 48271 % 2147483647;
        return double(state) / 2147483647.0;
    };
    auto insert = [&](double x, double z) {
        const auto id                                                                            = int(points.size());
        grid[size_t(int((z + extent) / cell)) * size_t(side) + size_t(int((x + extent) / cell))] = id;
        points.push_back({x, z});
        active.push_back(id);
    };
    const double firstX = (next() * 2 - 1) * extent, firstZ = (next() * 2 - 1) * extent;
    insert(firstX, firstZ);
    while (!active.empty()) {
        const auto index = size_t(next() * double(active.size()));
        const auto p     = points[size_t(active[index])];
        bool       found = false;
        for (int trial = 0; trial < 30; ++trial) {
            const double angle = next() * 6.283185307179586, distance = radius * std::sqrt(1 + 3 * next());
            const double x = p.x + std::cos(angle) * distance, z = p.z + std::sin(angle) * distance;
            if (std::abs(x) > extent || std::abs(z) > extent) continue;
            const int gx = int((x + extent) / cell), gz = int((z + extent) / cell);
            bool      valid = true;
            for (int dz = -2; dz <= 2 && valid; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int cx = gx + dx, cz = gz + dz;
                    if (cx < 0 || cz < 0 || cx >= side || cz >= side) continue;
                    const auto other = grid[size_t(cz) * size_t(side) + size_t(cx)];
                    if (other < 0) continue;
                    const auto   q  = points[size_t(other)];
                    const double ox = x - q.x, oz = z - q.z;
                    if (ox * ox + oz * oz < radius * radius) {
                        valid = false;
                        break;
                    }
                }
            if (valid) {
                insert(x, z);
                found = true;
                break;
            }
        }
        // Two coordinates per point plus the array container stay inside the
        // common Squirrel Value projection's 100,000-element limit.
        if (points.size() > 49999)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Grass scatter exceeds 49,999 points", "scatter"));
        if (!found) {
            active[index] = active.back();
            active.pop_back();
        }
    }
    Value::Array output;
    output.reserve(points.size() * 2);
    for (const auto& p : points) {
        output.emplace_back(p.x);
        output.emplace_back(p.z);
    }
    return Result<Value>::success(Value(std::move(output)));
}

// CPU-authored vector blades rasterized once per field. No texture file or
// shader-time shape generation is involved; RGB remains untinted white.
std::vector<std::byte> grassMaskPixels() {
    constexpr int size = 96, width = size * 4, height = size * 2, samples = 3;
    struct Blade {
        float root, height, bend, width;
    };
    constexpr Blade blades[2][4] = {
        {{0.18f, 0.65f, -0.17f, 0.075f}, {0.48f, 0.96f, 0.08f, 0.10f}, {0.79f, 0.76f, 0.15f, 0.085f}, {}},
        {{0.12f, 0.50f, -0.15f, 0.07f},
         {0.32f, 0.84f, -0.10f, 0.065f},
         {0.57f, 0.97f, 0.17f, 0.085f},
         {0.83f, 0.61f, 0.16f, 0.075f}}};
    std::vector<std::byte> pixels(width * height * 4, std::byte{255});
    for (int variant = 0; variant < 2; ++variant)
        for (int frame = 0; frame < 4; ++frame) {
            constexpr float pi      = 3.14159265358979323846f;
            const float     sway    = std::sin(float(frame) * pi * 0.5f) * 0.075f;
            const float     flutter = std::cos(float(frame) * pi * 0.5f) * 0.12f;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    int covered = 0;
                    for (int sy = 0; sy < samples; ++sy)
                        for (int sx = 0; sx < samples; ++sx) {
                            const float u = (float(x) + (float(sx) + 0.5f) / samples) / size;
                            const float v = 1.f - (float(y) + (float(sy) + 0.5f) / samples) / size;
                            for (int leaf = 0; leaf < (variant == 0 ? 3 : 4); ++leaf) {
                                const auto& b = blades[variant][leaf];
                                const float t = v / b.height;
                                if (t < 0.f || t > 1.f) continue;
                                const float center = b.root + (b.bend + sway) * t * t;
                                const float half   = b.width * std::pow(1.f - t, 0.8f) * (1.f + flutter * t);
                                if (std::abs(u - center) <= half) {
                                    ++covered;
                                    break;
                                }
                            }
                        }
                    const auto offset = size_t(((variant * size + y) * width + frame * size + x) * 4 + 3);
                    pixels[offset]    = std::byte((covered * 255 + samples * samples / 2) / (samples * samples));
                }
        }
    return pixels;
}

ResultRef<Shader> buildGrassShader(Graphics& graphics, ssq::Array records, const std::string& pass) {
    using namespace detail::grass_spv;
    try {
        if (graphics.getBackendName() != "vulkan")
            return ResultRef<Shader>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Stylized grass requires Vulkan", "backend"));
        if (pass != "grass" && pass != "ground")
            return ResultRef<Shader>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected grass or ground pass", "pass"));
        if (records.size() == 0 || records.size() % 16 != 0 || records.size() > 64u * 1024u * 1024u / sizeof(float))
            return ResultRef<Shader>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Expected nonempty affine matrix records within 64 MiB", "instances"));
        std::vector<float> matrices;
        matrices.reserve(records.size());
        for (size_t i = 0; i < records.size(); ++i) {
            const auto type = records.get<ssq::Object>(i).getType();
            if (type != ssq::Type::FLOAT && type != ssq::Type::INTEGER)
                return ResultRef<Shader>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Matrix elements must be numbers", "instances"));
            matrices.push_back(type == ssq::Type::INTEGER ? float(records.get<SQInteger>(i)) : records.get<float>(i));
        }
        ShaderResourceInputs inputs{};
        inputs.instanceMatrices = std::as_bytes(std::span(matrices));
        auto count              = shaderInstanceMatrixCount(inputs.instanceMatrices);
        if (!count) return ResultRef<Shader>::failure(count.status());
        std::vector<std::byte> pixels;
        ShaderImageInput       image;
        if (pass == "grass") {
            pixels            = grassMaskPixels();
            image.binding     = 0;
            image.width       = 384;
            image.height      = 192;
            image.sampler.min = image.sampler.mag = FilterMode::Linear;
            image.sampler.repeatU = image.sampler.repeatV = false;
            image.bytes                                   = pixels;
            inputs.images                                 = std::span(&image, 1);
        }
        const std::vector<uint32_t> vertex =
            pass == "grass" ? std::vector<uint32_t>(std::begin(meadow_vert_spv), std::end(meadow_vert_spv))
                            : std::vector<uint32_t>(std::begin(ground_vert_spv), std::end(ground_vert_spv));
        const std::vector<uint32_t> fragment =
            pass == "grass" ? std::vector<uint32_t>(std::begin(meadow_frag_spv), std::end(meadow_frag_spv))
                            : std::vector<uint32_t>(std::begin(ground_frag_spv), std::end(ground_frag_spv));
        auto* shader = graphics.newMeshShaderFromSpv({std::begin(bootstrap_vert_spv), std::end(bootstrap_vert_spv)},
                                                     {std::begin(bootstrap_frag_spv), std::end(bootstrap_frag_spv)});
        if (!shader)
            return ResultRef<Shader>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "Shader allocation failed", "shader"));
        auto uploaded = graphics.replaceMeshShaderResources(*shader, vertex, fragment, inputs);
        if (!uploaded) return ResultRef<Shader>::failure(uploaded.status());
        return ResultRef<Shader>::success(std::ref(*shader));
    } catch (const std::exception& error) {
        return ResultRef<Shader>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "stylized-grass"));
    }
}

Result<std::vector<uint32_t>> shaderWords(ssq::Array array) {
    std::vector<uint32_t> words;
    words.reserve(array.size());
    for (size_t i = 0; i < array.size(); ++i) {
        if (array.get<ssq::Object>(i).getType() != ssq::Type::INTEGER)
            return Result<std::vector<uint32_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "SPIR-V words must be integers"));
        const auto word = array.get<SQInteger>(i);
        if (word < 0 || static_cast<uint64_t>(word) > UINT32_MAX)
            return Result<std::vector<uint32_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "SPIR-V words must be unsigned 32-bit integers"));
        words.push_back(static_cast<uint32_t>(word));
    }
    return Result<std::vector<uint32_t>>::success(std::move(words));
}

}  // namespace
namespace detail {
Result<std::vector<std::uint32_t>> readShaderStageFile(const std::string& path) {
    try {
        auto*                                 fs = filesystem::Filesystem::create();
        std::unique_ptr<filesystem::FileData> bytes(fs->read(path));
        if (!bytes || bytes->getSize() < 20 || bytes->getSize() % 4 != 0)
            return Result<std::vector<std::uint32_t>>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "SPIR-V must contain a complete word-aligned header", path));
        std::vector<std::uint32_t> words(bytes->getSize() / 4);
        std::memcpy(words.data(), bytes->getData(), bytes->getSize());
        if (words[0] != 0x07230203)
            return Result<std::vector<std::uint32_t>>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "SPIR-V magic mismatch", path));
        return Result<std::vector<std::uint32_t>>::success(std::move(words));
    } catch (const std::exception& error) {
        return Result<std::vector<std::uint32_t>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), path));
    }
}

}  // namespace detail
namespace {
// The existing Graphics factory owns the shader; this adapter only borrows it.
ResultRef<Shader> loadMeshShader(Graphics& graphics, const std::string& vertex, const std::string& fragment) {
    if (graphics.getBackendName() != "vulkan")
        return ResultRef<Shader>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "SPIR-V mesh shaders require the Vulkan backend", "backend"));
    if (fragment.empty())
        return ResultRef<Shader>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "fragment path must not be empty", "fragment"));
    std::vector<std::uint32_t> vert;
    if (!vertex.empty()) {
        auto decoded = detail::readShaderStageFile(vertex);
        if (!decoded) return ResultRef<Shader>::failure(decoded.status());
        vert = std::move(decoded).takeValue();
    }
    auto frag = detail::readShaderStageFile(fragment);
    if (!frag) return ResultRef<Shader>::failure(frag.status());
    try {
        auto* shader = graphics.newMeshShaderFromSpv(vert, frag.value());
        if (!shader)
            return ResultRef<Shader>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "Mesh shader allocation failed", "shader"));
        return ResultRef<Shader>::success(std::ref(*shader));
    } catch (const std::exception& error) {
        return ResultRef<Shader>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "shader"));
    }
}
}  // namespace

void exposeShaderScriptBindings(ssq::Table& table, ssq::Class& cls) {
    detail::exposeShaderResourceBindings(table, cls);
    const auto vm = table.getHandle();
    cls.addFunc("buildStylizedGrassPoints", [vm](Graphics*, float radius, float extent, int seed) {
        return eve::script::projectResult(vm, buildGrassPoints(radius, extent, seed),
                                          [](const Value& value) { return value; });
    });
    cls.addFunc("buildStylizedGrassShader", [vm](Graphics* self, ssq::Array instances, const std::string& pass) {
        if (!self)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "Graphics must be present")));
        auto result = buildGrassShader(*self, instances, pass);
        if (!result) return eve::script::projectStatusResult(vm, result.status());
        auto projected = eve::script::projectStatusResult(vm, result.status());
        projected.set("value", &result.value().get());
        eve::script::markResultHasValue(projected);
        projected.set("ownership", std::string("borrowed-from-graphics"));
        return projected;
    });
    cls.addFunc(
        "replaceShaderFromSpv", [vm](Graphics* graphics, Shader* shader, ssq::Array vertex, ssq::Array fragment) {
            if (!graphics || !shader)
                return eve::script::projectResult(
                    vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "graphics and shader must not be null")));
            auto vert = shaderWords(vertex);
            if (!vert.ok()) return eve::script::projectResult(vm, Result<void>::failure(vert.status()));
            auto frag = shaderWords(fragment);
            if (!frag.ok()) return eve::script::projectResult(vm, Result<void>::failure(frag.status()));
            return eve::script::projectResult(vm, graphics->replaceShaderFromSpv(*shader, vert.value(), frag.value()));
        });
    cls.addFunc("replaceShaderFromGlsl",
                [vm](Graphics* self, Shader* shader, const std::string& vertex, const std::string& fragment) {
                    if (!self || !shader)
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(
                                    DiagnosticCode::InvalidArgument, "graphics and shader must not be null", "shader",
                                    {}, "graphics.shader_reload.binding")));
                    return eve::script::projectResult(vm, self->replaceShaderFromGlsl(*shader, vertex, fragment));
                });
    cls.addFunc("replaceShaderFromWgsl",
                [vm](Graphics* self, Shader* shader, const std::string& vertex, const std::string& fragment) {
                    if (!self || !shader)
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(
                                    DiagnosticCode::InvalidArgument, "graphics and shader must not be null", "shader",
                                    {}, "graphics.shader_reload.binding")));
                    return eve::script::projectResult(vm, self->replaceShaderFromWgsl(*shader, vertex, fragment));
                });


    cls.addFunc("configureMeshShaderSurface",
                [vm](Graphics* self, Shader* shader, const std::string& name, bool depthWrite, bool doubleSided) {
                    BlendMode blend = BlendMode::Opaque;
                    if (name == "alpha")
                        blend = BlendMode::Alpha;
                    else if (name == "premultiplied")
                        blend = BlendMode::Premultiplied;
                    else if (name == "additive")
                        blend = BlendMode::Additive;
                    else if (name == "multiply")
                        blend = BlendMode::Multiply;
                    else if (name != "opaque")
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "Unknown mesh blend mode", "blend")));
                    if (!self || !shader)
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "Expected graphics and shader", "shader")));
                    return eve::script::projectResult(
                        vm, self->configureMeshShaderSurface(*shader, blend, depthWrite, doubleSided));
                });
    cls.addFunc("configureMeshShaderRaster", [vm](Graphics* self, Shader* shader, const std::string& compare,
                                                  float constantBias, float slopeBias, int colorMask) {
        MeshShaderRasterState state;
        if (compare == "less")
            state.depthCompare = MeshDepthCompare::Less;
        else if (compare == "lessEqual")
            state.depthCompare = MeshDepthCompare::LessEqual;
        else if (compare == "always")
            state.depthCompare = MeshDepthCompare::Always;
        else
            colorMask = -1;
        if (!self || !shader || colorMask < 0 || colorMask > 15)
            return eve::script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Invalid shader, depth comparison or RGBA mask",
                                                            "shader.raster")));
        state.depthBiasConstant = constantBias;
        state.depthBiasSlope    = slopeBias;
        state.colorWriteMask    = static_cast<std::uint8_t>(colorMask);
        return eve::script::projectResult(vm, self->configureMeshShaderRaster(*shader, state));
    });
    cls.addFunc("loadMeshShaderSpv", [vm](Graphics* self, const std::string& vertex, const std::string& fragment) {
        auto result = loadMeshShader(*self, vertex, fragment);
        if (!result) return eve::script::projectStatusResult(vm, result.status());
        auto projected = eve::script::projectStatusResult(vm, result.status());
        projected.set("value", &result.value().get());
        eve::script::markResultHasValue(projected);
        projected.set("ownership", std::string("borrowed-from-graphics"));
        return projected;
    });
}
}  // namespace eve::graphics
