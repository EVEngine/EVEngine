#include "graphics/ShaderScriptBindings.h"
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <vector>
#include "common/SquirrelBinding.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"

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

void exposeShaderScriptBindings(ssq::Table& table, ssq::Class& cls) {
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
}
}  // namespace eve::graphics
