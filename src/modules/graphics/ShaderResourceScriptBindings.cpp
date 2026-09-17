#include <algorithm>
#include <memory>
#include <stdexcept>
#include "common/SquirrelBinding.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/PreparedFile.h"
#include "graphics/Bloom.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/ShaderResources.h"
#include "graphics/ShaderScriptBindings.h"

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
        if (!descriptions.isArray() || !descriptions.arraySize() || descriptions.arraySize() > 16)
            throw std::runtime_error("Expected 1-16 image resource descriptions");
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
        constexpr std::string_view keys[] = {"binding", "format", "dimension", "width", "height", "layers",
                                             "mips",    "path",   "filter",    "wrapU", "wrapV",  "anisotropy"};
        constexpr std::pair<std::string_view, ShaderImageFormat> formats[] = {
            {"r8-unorm", ShaderImageFormat::R8},          {"rg8-unorm", ShaderImageFormat::RG8},
            {"r16-unorm", ShaderImageFormat::R16},        {"rgba8-unorm", ShaderImageFormat::RGBA8},
            {"bgra8-unorm", ShaderImageFormat::BGRA8},    {"rgba8-srgb", ShaderImageFormat::RGBA8Srgb},
            {"bgra8-srgb", ShaderImageFormat::BGRA8Srgb}, {"bc3-unorm", ShaderImageFormat::BC3},
            {"bc1-unorm", ShaderImageFormat::BC1},        {"bc1-srgb", ShaderImageFormat::BC1Srgb},
            {"bc3-srgb", ShaderImageFormat::BC3Srgb},     {"bc7-unorm", ShaderImageFormat::BC7},
            {"bc7-srgb", ShaderImageFormat::BC7Srgb}};
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
            else
                throw std::runtime_error("Unknown image view dimension: " + dimension);
            auto filter = number(row, "filter");
            auto wrapU = number(row, "wrapU"), wrapV = number(row, "wrapV");
            if (filter > 2 || wrapU > 1 || wrapV > 1) throw std::runtime_error("Unknown resource sampler mode");
            image.sampler.min = image.sampler.mag = filter == 0 ? FilterMode::Nearest : FilterMode::Linear;
            image.sampler.mipmap                  = image.mipLevels <= 1 ? MipmapMode::Disabled
                                                    : filter == 2        ? MipmapMode::Linear
                                                                         : MipmapMode::Nearest;
            image.sampler.repeatU                 = wrapU == 0;
            image.sampler.repeatV                 = wrapV == 0;
            image.sampler.maxAnisotropy           = float(number(row, "anisotropy"));
            const auto path                       = field(row, "path", Value::Type::String).asString();
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
    const auto vm = table.getHandle();
    cls.addFunc("setBloomFilter", [vm](Graphics* graphics, const std::string& mode, float scatter, int iterations,
                                       float clamp) {
        if (!graphics || (mode != "karisTent" && mode != "gaussianScatter"))
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Expected bloom filter karisTent or gaussianScatter",
                                                            "graphics.bloom")));
        BloomFilterSettings settings{mode == "gaussianScatter" ? BloomFilter::GaussianScatter : BloomFilter::KarisTent,
                                     scatter, iterations, clamp};
        auto valid = validateBloomFilterSettings(settings);
        if (!valid) return script::projectResult(vm, Result<void>::failure(valid.status()));
        return script::projectResult(vm, graphics->pipelineBloom()->configureFilter(settings));
    });
    cls.addFunc("getBloomFilter", [](Graphics* graphics) {
        return std::string(graphics->pipelineBloom()->filterSettings().filter == BloomFilter::GaussianScatter
                               ? "gaussianScatter"
                               : "karisTent");
    });
    cls.addFunc("setSceneToneMapping", [vm](Graphics* graphics, const std::string& mode) {
        if (!graphics || (mode != "none" && mode != "aces"))
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument,
                                                 "Expected scene tone mapping none or aces", "graphics.presentation")));
        return script::projectResult(vm,
                                     graphics->setSceneToneMapping(mode == "none" ? Graphics::SceneToneMapping::None
                                                                                  : Graphics::SceneToneMapping::Aces));
    });
    cls.addFunc("getSceneToneMapping", [](Graphics* graphics) {
        return std::string(graphics->getSceneToneMapping() == Graphics::SceneToneMapping::None ? "none" : "aces");
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
