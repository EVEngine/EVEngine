#include "graphics/ShaderResourceValidation.h"
#include <algorithm>
#include <exception>
#include <string>
#if defined(EVENGINE_SPIRV_ASSET_VALIDATION)
#include <spirv-tools/libspirv.hpp>
#include <spirv_cross/spirv_cross.hpp>
#endif

namespace eve::graphics::detail {
namespace {
#if defined(EVENGINE_SPIRV_ASSET_VALIDATION)
bool floats(const spirv_cross::SPIRType& t, uint32_t size) {
    return t.basetype == spirv_cross::SPIRType::Float && t.width == 32 && t.vecsize == size && t.columns == 1 &&
           t.array.empty();
}
Result<void> stage(std::span<const uint32_t> words, bool vertex, const ShaderResourceInputs& inputs) {
    spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
    if (!validator.Validate(words.data(), words.size()))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move("Invalid Vulkan 1.2 SPIR-V"), "shader.resources"));
    spirv_cross::Compiler compiler(words.data(), words.size());
    const auto            entries = compiler.get_entry_points_and_stages();
    if (entries.size() != 1 || entries[0].name != "main" ||
        entries[0].execution_model != (vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Expected one main entry point with the requested stage"), "shader.resources"));
    for (auto cap : compiler.get_declared_capabilities())
        if (cap != spv::CapabilityShader && cap != spv::CapabilityMatrix)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Resource programs require only Shader/Matrix capabilities"), "shader.resources"));
    if (!compiler.get_declared_extensions().empty() || !compiler.get_specialization_constants().empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Extensions and specialization constants are unsupported"), "shader.resources"));
    const auto r = compiler.get_shader_resources();
    if (!r.storage_images.empty() || !r.subpass_inputs.empty() || !r.separate_images.empty() ||
        !r.separate_samplers.empty() || !r.atomic_counters.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Resource programs require combined sampled images and uniform buffers"), "shader.resources"));
    if (r.storage_buffers.size() > 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Only the readonly instance matrix buffer is supported"), "shader.resources"));
    for (const auto& buffer : r.storage_buffers) {
        const auto& t = compiler.get_type(buffer.base_type_id);
        if (!vertex || inputs.instanceMatrices.empty() ||
            compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet) != 1 ||
            compiler.get_decoration(buffer.id, spv::DecorationBinding) != 33 ||
            !compiler.get_type(buffer.type_id).array.empty() || t.member_types.size() != 1 ||
            compiler.type_struct_member_offset(t, 0) != 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Instance storage must be vertex-only set 1 binding 33"), "shader.resources"));
        const auto& m = compiler.get_type(t.member_types[0]);
        if ((!compiler.has_decoration(buffer.id, spv::DecorationNonWritable) &&
             !compiler.has_member_decoration(t.self, 0, spv::DecorationNonWritable)) ||
            m.basetype != spirv_cross::SPIRType::Float || m.width != 32 || m.vecsize != 4 || m.columns != 4 ||
            m.array.size() != 1 || m.array[0] != 0 || compiler.type_struct_member_array_stride(t, 0) != 64 ||
            compiler.type_struct_member_matrix_stride(t, 0) != 16 ||
            compiler.has_member_decoration(t.self, 0, spv::DecorationRowMajor))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Instance storage must contain readonly column-major mat4 records"), "shader.resources"));
    }
    for (const auto& image : r.sampled_images) {
        const auto& t       = compiler.get_type(image.type_id);
        const auto  set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
        const auto  binding = compiler.get_decoration(image.id, spv::DecorationBinding);
        if (!t.array.empty() || t.image.ms || t.image.depth ||
            compiler.get_type(t.image.type).basetype != spirv_cross::SPIRType::Float)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Expected a non-shadow, non-MS float combined image"), "shader.resources"));
        ShaderImageDimension expected = ShaderImageDimension::Image2D;
        if (set == 0 && binding == 1 && !vertex) {
            // The existing per-material albedo binding.
        } else if (set == 1) {
            auto found = std::find_if(inputs.images.begin(), inputs.images.end(),
                                      [&](const auto& item) { return item.binding == binding; });
            if (found == inputs.images.end()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Shader image binding has no supplied resource"), "shader.resources"));
            expected = found->dimension;
        } else
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Image binding is outside the resource program ABI"), "shader.resources"));
        if (t.image.dim != (expected == ShaderImageDimension::Cube ? spv::DimCube : spv::Dim2D) ||
            bool(t.image.arrayed) != (expected == ShaderImageDimension::Array2D))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Shader image view dimension does not match the supplied resource"), "shader.resources"));
    }
    for (const auto& buffer : r.uniform_buffers) {
        const auto  set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
        const auto  binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
        const auto& t       = compiler.get_type(buffer.base_type_id);
        if (!compiler.get_type(buffer.type_id).array.empty()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Buffer descriptor arrays unsupported"), "shader.resources"));
        if (set == 1 && binding == 32) {
            if (inputs.constants.empty() || compiler.get_declared_struct_size(t) > inputs.constants.size())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Uniform buffer is larger than its supplied bytes"), "shader.resources"));
        } else if (set == 0 && binding == 0) {
            if (t.member_types.empty() || t.member_types.size() > 6) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Expected the supported Frame prefix"), "shader.resources"));
            for (unsigned i = 0; i < t.member_types.size(); ++i) {
                const auto& m = compiler.get_type(t.member_types[i]);
                if (compiler.type_struct_member_offset(t, i) != (i < 2 ? i * 64 : 128 + (i - 2) * 16))
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Frame member offset mismatch"), "shader.resources"));
                if (i < 2) {
                    if (m.basetype != spirv_cross::SPIRType::Float || m.width != 32 || m.columns != 4 ||
                        m.vecsize != 4 || !m.array.empty() || compiler.type_struct_member_matrix_stride(t, i) != 16 ||
                        compiler.has_member_decoration(t.self, i, spv::DecorationRowMajor))
                        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Frame matrices must be column-major mat4"), "shader.resources"));
                } else if (!floats(m, 4))
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Frame trailing members must be vec4"), "shader.resources"));
            }
        } else
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Uniform binding is outside the resource program ABI"), "shader.resources"));
    }
    if (r.push_constant_buffers.size() > 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Multiple push constant blocks unsupported"), "shader.resources"));
    for (const auto& buffer : r.push_constant_buffers) {
        const auto& t = compiler.get_type(buffer.base_type_id);
        if (t.member_types.size() != 1 || compiler.type_struct_member_offset(t, 0) != 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Push constants must contain float data[32]"), "shader.resources"));
        const auto& m = compiler.get_type(t.member_types[0]);
        if (m.basetype != spirv_cross::SPIRType::Float || m.width != 32 || m.vecsize != 1 || m.columns != 1 ||
            m.array.size() != 1 || !m.array_size_literal[0] || m.array[0] != 32 ||
            compiler.type_struct_member_array_stride(t, 0) != 4)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Push constants must be 128 tightly packed float bytes"), "shader.resources"));
    }
    if (vertex) {
        for (const auto& input : r.stage_inputs) {
            auto           location = compiler.get_decoration(input.id, spv::DecorationLocation);
            const uint32_t sizes[]  = {3, 3, 2, 0, 0, 4};
            if (location > 5 || sizes[location] == 0 || !floats(compiler.get_type(input.type_id), sizes[location]))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Resource vertex inputs require position/normal/UV or optional vec4 tangent at location 5"), "shader.resources"));
        }
    } else if (r.stage_outputs.size() != 1 ||
               compiler.get_decoration(r.stage_outputs[0].id, spv::DecorationLocation) != 0 ||
               !floats(compiler.get_type(r.stage_outputs[0].type_id), 4))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("Fragment output must be one vec4 at location 0"), "shader.resources"));
    return Result<void>::success();
}
#endif
}  // namespace

Result<void> validateResourceShaderStages(std::span<const uint32_t> vertex, std::span<const uint32_t> fragment,
                                          const ShaderResourceInputs& inputs) {
#if defined(EVENGINE_SPIRV_ASSET_VALIDATION)
    try {
        if (!vertex.empty()) {
            auto result = stage(vertex, true, inputs);
            if (!result) return result;
        }
        return stage(fragment, false, inputs);
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move(error.what()), "shader.resources"));
    }
#else
    (void)vertex;
    (void)fragment;
    (void)inputs;
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("SPIR-V validation provider is unavailable"), "shader.resources"));
#endif
}
}  // namespace eve::graphics::detail
