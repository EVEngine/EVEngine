#include "asset/graphics/ShaderAssetValidation.h"
#include "asset/ShaderAsset.h"

#ifdef EVENGINE_SPIRV_ASSET_VALIDATION
#include <exception>
#include <map>
#include <spirv-tools/libspirv.hpp>
#include <spirv_cross/spirv_cross.hpp>
#endif

namespace eve::asset_graphics {
namespace {
Result<void> failure(DiagnosticCode code, std::string message) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.shader.gpu-layout"));
}
#ifdef EVENGINE_SPIRV_ASSET_VALIDATION
bool floatType(const spirv_cross::SPIRType& type, unsigned components) {
    return type.basetype == spirv_cross::SPIRType::Float && type.width == 32 && type.vecsize == components &&
           type.columns == 1 && type.array.empty();
}
Result<void> stageLayout(spirv_cross::Compiler& compiler, bool vertex, bool mesh) {
    for (const auto capability : compiler.get_declared_capabilities())
        if (capability != spv::CapabilityShader && capability != spv::CapabilityMatrix)
            return failure(DiagnosticCode::Unsupported,
                           "Shader requires device features outside the baseline contract");
    if (!compiler.get_declared_extensions().empty())
        return failure(DiagnosticCode::Unsupported, "Shader extensions are outside the baseline contract");
    const auto resources = compiler.get_shader_resources();
    if (!resources.storage_buffers.empty() || !resources.storage_images.empty() || !resources.subpass_inputs.empty() ||
        !resources.separate_images.empty() || !resources.separate_samplers.empty() ||
        !resources.atomic_counters.empty() || !resources.acceleration_structures.empty() ||
        !compiler.get_specialization_constants().empty())
        return failure(DiagnosticCode::Unsupported, "Shader uses unsupported resources or specialization constants");
    if (resources.sampled_images.size() > 1 || (vertex && !resources.sampled_images.empty()))
        return failure(DiagnosticCode::Unsupported, "Only one fragment albedo sampler is supported");
    for (const auto& sampler : resources.sampled_images) {
        const auto& type = compiler.get_type(sampler.type_id);
        if (compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet) != 0 ||
            compiler.get_decoration(sampler.id, spv::DecorationBinding) != (mesh ? 1u : 0u) || !type.array.empty() ||
            type.image.dim != spv::Dim2D || type.image.arrayed || type.image.ms || type.image.depth ||
            compiler.get_type(type.image.type).basetype != spirv_cross::SPIRType::Float)
            return failure(DiagnosticCode::Unsupported, "Albedo must use the engine's single float sampler2D slot");
    }
    if (resources.uniform_buffers.size() > 1 || (!mesh && !resources.uniform_buffers.empty()))
        return failure(DiagnosticCode::Unsupported, "Shader uses unsupported uniform buffers");
    for (const auto& buffer : resources.uniform_buffers) {
        const auto& type = compiler.get_type(buffer.base_type_id);
        if (compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet) != 0 ||
            compiler.get_decoration(buffer.id, spv::DecorationBinding) != 0 ||
            !compiler.get_type(buffer.type_id).array.empty() || type.member_types.empty() ||
            type.member_types.size() > 6)
            return failure(DiagnosticCode::Unsupported, "Mesh Frame buffer does not match the supported prefix");
        for (unsigned i = 0; i < type.member_types.size(); ++i) {
            const auto& member = compiler.get_type(type.member_types[i]);
            const auto  offset = i < 2 ? i * 64 : 128 + (i - 2) * 16;
            if (compiler.type_struct_member_offset(type, i) != offset)
                return failure(DiagnosticCode::Unsupported, "Mesh Frame member offset mismatch");
            if (i < 2) {
                if (member.basetype != spirv_cross::SPIRType::Float || member.width != 32 || member.columns != 4 ||
                    member.vecsize != 4 || !member.array.empty() ||
                    compiler.type_struct_member_matrix_stride(type, i) != 16 ||
                    compiler.has_member_decoration(type.self, i, spv::DecorationRowMajor))
                    return failure(DiagnosticCode::Unsupported, "Mesh Frame matrices require column-major mat4");
            } else if (!floatType(member, 4))
                return failure(DiagnosticCode::Unsupported, "Mesh Frame requires vec4 fields");
        }
    }
    if (resources.push_constant_buffers.size() > 1)
        return failure(DiagnosticCode::Unsupported, "Multiple push-constant blocks are unsupported");
    for (const auto& buffer : resources.push_constant_buffers) {
        const auto& type = compiler.get_type(buffer.base_type_id);
        if (type.member_types.size() != 1 || compiler.type_struct_member_offset(type, 0) != 0)
            return failure(DiagnosticCode::Unsupported, "Push constants require one float data[32] member");
        const auto& member = compiler.get_type(type.member_types[0]);
        if (member.basetype != spirv_cross::SPIRType::Float || member.width != 32 || member.vecsize != 1 ||
            member.columns != 1 || member.array.size() != 1 || !member.array_size_literal[0] || member.array[0] != 32 ||
            compiler.type_struct_member_array_stride(type, 0) != 4)
            return failure(DiagnosticCode::Unsupported, "Push constants require tightly packed float data[32]");
    }
    if (vertex) {
        for (const auto& input : resources.stage_inputs) {
            const auto     location = compiler.get_decoration(input.id, spv::DecorationLocation);
            const unsigned sizes[]  = {mesh ? 3u : 2u, mesh ? 3u : 4u, 2u};
            if (location > 2 || compiler.get_decoration(input.id, spv::DecorationComponent) != 0 ||
                !floatType(compiler.get_type(input.type_id), sizes[location]))
                return failure(DiagnosticCode::Unsupported, "Vertex attributes do not match the engine layout");
        }
    } else {
        if (resources.stage_outputs.size() != 1 ||
            compiler.get_decoration(resources.stage_outputs[0].id, spv::DecorationLocation) != 0 ||
            compiler.get_decoration(resources.stage_outputs[0].id, spv::DecorationIndex) != 0 ||
            compiler.get_decoration(resources.stage_outputs[0].id, spv::DecorationComponent) != 0 ||
            !floatType(compiler.get_type(resources.stage_outputs[0].type_id), 4))
            return failure(DiagnosticCode::Unsupported, "Fragment output must be location zero vec4");
    }
    return Result<void>::success();
}
#endif
}  // namespace
Result<void> validateShaderAssetGpu(const asset::ShaderAsset& shader) {
#ifndef EVENGINE_SPIRV_ASSET_VALIDATION
    (void)shader;
    return failure(DiagnosticCode::Unsupported, "Shader asset upload requires SPIRV-Tools and SPIRV-Cross");
#else
    try {
        spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
        std::string          diagnostics;
        validator.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&, const char* text) {
            if (diagnostics.size() < 4096) diagnostics += text;
        });
        if (!validator.Validate(shader.vertex) || !validator.Validate(shader.fragment))
            return failure(DiagnosticCode::ParseError, "SPIR-V validation failed: " + diagnostics);
        spirv_cross::Compiler vertex(shader.vertex), fragment(shader.fragment);
        vertex.set_entry_point("main", spv::ExecutionModelVertex);
        fragment.set_entry_point("main", spv::ExecutionModelFragment);
        const bool mesh = shader.interface == asset::ShaderAssetInterface::Mesh3D;
        auto       v    = stageLayout(vertex, true, mesh);
        if (!v) return v;
        auto f = stageLayout(fragment, false, mesh);
        if (!f) return f;
        const auto outputs = vertex.get_shader_resources().stage_outputs;
        for (const auto& input : fragment.get_shader_resources().stage_inputs) {
            const auto  location = fragment.get_decoration(input.id, spv::DecorationLocation);
            bool        matched  = false;
            const auto& type     = fragment.get_type(input.type_id);
            if (!floatType(type, type.vecsize) || fragment.get_decoration(input.id, spv::DecorationComponent) != 0)
                return failure(DiagnosticCode::Unsupported, "Fragment varyings must be float vectors");
            for (const auto& output : outputs)
                if (vertex.get_decoration(output.id, spv::DecorationLocation) == location &&
                    vertex.get_decoration(output.id, spv::DecorationComponent) == 0 &&
                    floatType(vertex.get_type(output.type_id), type.vecsize))
                    matched = true;
            if (!matched) return failure(DiagnosticCode::Unsupported, "Vertex/fragment varying mismatch");
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        return failure(DiagnosticCode::ParseError, error.what());
    }
#endif
}
}  // namespace eve::asset_graphics
