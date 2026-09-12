#include "graphics/ShaderScriptBindings.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "common/SquirrelBinding.h"
#include "common/BorrowedRef.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

namespace eve::graphics {
namespace {
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
        auto* fs = filesystem::Filesystem::create();
        std::unique_ptr<filesystem::FileData> bytes(fs->read(path));
        if (!bytes || bytes->getSize() < 20 || bytes->getSize() % 4 != 0)
            return Result<std::vector<std::uint32_t>>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "SPIR-V must contain a complete word-aligned header", path));
        std::vector<std::uint32_t> words(bytes->getSize() / 4);
        std::memcpy(words.data(), bytes->getData(), bytes->getSize());
        if (words[0] != 0x07230203)
            return Result<std::vector<std::uint32_t>>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "SPIR-V magic mismatch", path));
        return Result<std::vector<std::uint32_t>>::success(std::move(words));
    } catch (const std::exception& error) {
        return Result<std::vector<std::uint32_t>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), path));
    }
}

}  // namespace detail
namespace {
// The existing Graphics factory owns the shader; this adapter only borrows it.
ResultRef<Shader> loadMeshShader(Graphics& graphics, const std::string& vertex,
                                  const std::string& fragment) {
    if (graphics.getBackendName() != "vulkan")
        return ResultRef<Shader>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "SPIR-V mesh shaders require the Vulkan backend", "backend"));
    if (fragment.empty())
        return ResultRef<Shader>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fragment path must not be empty", "fragment"));
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
            return ResultRef<Shader>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Mesh shader allocation failed", "shader"));
        return ResultRef<Shader>::success(std::ref(*shader));
    } catch (const std::exception& error) {
        return ResultRef<Shader>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "shader"));
    }
}
}  // namespace

void exposeShaderScriptBindings(ssq::Table& table, ssq::Class& cls) {
    detail::exposeShaderResourceBindings(table, cls);
    const auto vm = table.getHandle();
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
                [vm](Graphics* self, Shader* shader, const std::string& vertex,
                     const std::string& fragment) {
                    if (!self || !shader)
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(
                                    DiagnosticCode::InvalidArgument,
                                    "graphics and shader must not be null", "shader", {},
                                    "graphics.shader_reload.binding")));
                    return eve::script::projectResult(
                        vm, self->replaceShaderFromGlsl(*shader, vertex, fragment));
                });
    cls.addFunc("replaceShaderFromWgsl",
                [vm](Graphics* self, Shader* shader, const std::string& vertex,
                     const std::string& fragment) {
                    if (!self || !shader)
                        return eve::script::projectResult(
                            vm, Result<void>::failure(Diagnostic::error(
                                    DiagnosticCode::InvalidArgument,
                                    "graphics and shader must not be null", "shader", {},
                                    "graphics.shader_reload.binding")));
                    return eve::script::projectResult(
                        vm, self->replaceShaderFromWgsl(*shader, vertex, fragment));
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
    cls.addFunc("loadMeshShaderSpv", [vm](Graphics* self, const std::string& vertex,
                                         const std::string& fragment) {
        auto result = loadMeshShader(*self, vertex, fragment);
        if (!result) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, result.status(), true, true);
        projected.set("value", &result.value().get());
        projected.set("ownership", std::string("borrowed-from-graphics"));
        return projected;
    });
}
} // namespace eve::graphics
