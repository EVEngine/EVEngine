#include "graphics/webgpu/PbrVariantSource.h"
#include "graphics/webgpu/Graphics.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <array>

#include "graphics/shaders/pbr_surface.frag_wgsl.inc"
#include "graphics/shaders/pbr_surface.vert_wgsl.inc"

namespace eve::graphics::webgpu {
namespace {
std::string_view sourceView(const std::uint32_t* words, std::size_t count) {
    std::string_view source(reinterpret_cast<const char*>(words), count * sizeof(std::uint32_t));
    while (!source.empty() && source.back() == '\0') source.remove_suffix(1);
    return source;
}

void eraseDeclaration(std::string& source, std::string_view variable) {
    const std::string marker = " var " + std::string(variable) + " :";
    auto              position = source.find(marker);
    if (position == std::string::npos) return;
    auto begin = source.rfind('\n', position);
    begin      = begin == std::string::npos ? 0 : begin + 1;
    auto end   = source.find('\n', position);
    end        = end == std::string::npos ? source.size() : end + 1;
    source.erase(begin, end - begin);
}

void eraseKeepalive(std::string& source, std::string_view variable) {
    const std::string marker = "_ = " + std::string(variable) + ";";
    std::size_t       position = 0;
    while ((position = source.find(marker, position)) != std::string::npos) {
        auto begin = source.rfind('\n', position);
        begin      = begin == std::string::npos ? 0 : begin + 1;
        auto end   = source.find('\n', position);
        end        = end == std::string::npos ? source.size() : end + 1;
        source.erase(begin, end - begin);
    }
}

void replaceAll(std::string& source, std::string_view from, std::string_view to) {
    std::size_t position = 0;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string samplerName(std::uint32_t resource) {
    constexpr auto canonicalCount = std::uint32_t(PbrTextureSlot::Count);
    if (resource < canonicalCount) return "map" + std::to_string(resource) + "_sampler";
    return "detailMap" + std::to_string(resource - canonicalCount) + "_sampler";
}

void coalesceSampler(std::string& source, std::string_view resource, std::uint32_t resourceIndex,
                     std::uint32_t representative) {
    if (representative == PbrVariantPlan::NoSampler || representative == resourceIndex) return;
    const std::string sampler = std::string(resource) + "_sampler";
    eraseDeclaration(source, sampler);
    eraseKeepalive(source, sampler);
    replaceAll(source, sampler, samplerName(representative));
}

void replaceResourceCalls(std::string& source, std::string_view function, std::string_view resource,
                          std::string_view replacement) {
    const std::string prefix = std::string(function) + "(" + std::string(resource);
    std::size_t       begin  = 0;
    while ((begin = source.find(prefix, begin)) != std::string::npos) {
        const auto delimiter = begin + prefix.size();
        if (delimiter >= source.size() || (source[delimiter] != ',' && source[delimiter] != ')')) {
            begin = delimiter;
            continue;
        }
        std::size_t depth = 0;
        std::size_t end   = begin;
        for (; end < source.size(); ++end) {
            if (source[end] == '(') ++depth;
            if (source[end] == ')' && --depth == 0) {
                ++end;
                break;
            }
        }
        source.replace(begin, end - begin, replacement);
        begin += replacement.size();
    }
}

void removeSampledResource(std::string& source, std::string_view base,
                           std::string_view replacement = "vec4<f32>(1.0f)") {
    const std::string image   = std::string(base) + "_image";
    const std::string sampler = std::string(base) + "_sampler";
    replaceResourceCalls(source, "textureSampleLevel", image, replacement);
    replaceResourceCalls(source, "textureSampleCompare", image, "1.0f");
    replaceResourceCalls(source, "textureSample", image, replacement);
    eraseDeclaration(source, image);
    eraseDeclaration(source, sampler);
    eraseKeepalive(source, image);
    eraseKeepalive(source, sampler);
}
}  // namespace

PbrVariantSources pbrVariantBaseSources() {
    return {sourceView(pbr_surface_vert_wgsl, pbr_surface_vert_wgsl_count),
            sourceView(pbr_surface_frag_wgsl, pbr_surface_frag_wgsl_count)};
}

PbrSpecializedSources specializePbrVariantSources(const PbrVariantPlan& plan) {
    const auto base = pbrVariantBaseSources();
    PbrSpecializedSources result{std::string(base.vertex), std::string(base.fragment)};
    static constexpr std::array<std::string_view, 11> maps{
        "map0", "map1", "map2", "map3", "map4", "map5", "map6", "map7", "map8", "map9", "map10"};
    static constexpr std::array<std::string_view, 3> detailMaps{"detailMap0", "detailMap1", "detailMap2"};
    for (std::uint32_t index = 0; index < maps.size(); ++index)
        if ((plan.canonicalTextureMask & (1u << index)) == 0u)
            removeSampledResource(result.fragment, maps[index]);
    for (std::uint32_t index = 0; index < detailMaps.size(); ++index)
        if ((plan.detailTextureMask & (1u << index)) == 0u)
            removeSampledResource(result.fragment, detailMaps[index]);
    for (std::uint32_t index = 0; index < maps.size(); ++index)
        if ((plan.canonicalTextureMask & (1u << index)) != 0u)
            coalesceSampler(result.fragment, maps[index], index,
                            plan.canonicalSamplerRepresentatives[index]);
    for (std::uint32_t index = 0; index < detailMaps.size(); ++index)
        if ((plan.detailTextureMask & (1u << index)) != 0u)
            coalesceSampler(result.fragment, detailMaps[index],
                            std::uint32_t(PbrTextureSlot::Count) + index,
                            plan.detailSamplerRepresentatives[index]);
    if ((plan.fragmentFlags & PbrExtras) == 0u) removeSampledResource(result.fragment, "extrasMap");
    if ((plan.fragmentFlags & PbrColors) == 0u) removeSampledResource(result.fragment, "colorsMap");
    if ((plan.fragmentFlags & PbrFadeNoise) == 0u)
        removeSampledResource(result.fragment, "vegetationFadeNoise",
                              "vec4<f32>(0.0f, 0.0f, 0.0f, 1.0f)");
    if ((plan.fragmentFlags & PbrEnvironment) == 0u) {
        replaceResourceCalls(result.fragment, "textureNumLevels", "environmentMap_image", "1u");
        removeSampledResource(result.fragment, "environmentMap");
    }
    if ((plan.fragmentFlags & PbrShadows) == 0u) {
        replaceResourceCalls(result.fragment, "textureDimensions", "shadowMap_image", "vec2<u32>(1u)");
        replaceResourceCalls(result.fragment, "textureNumLayers", "shadowMap_image", "1u");
        removeSampledResource(result.fragment, "shadowMap", "1.0f");
    }
    if ((plan.vertexFlags & PbrVertexField) == 0u) removeSampledResource(result.vertex, "vertexField");
    if ((plan.vertexFlags & PbrMotionField) == 0u) removeSampledResource(result.vertex, "motionField");
    if ((plan.vertexFlags & PbrMotionNoise) == 0u) removeSampledResource(result.vertex, "motionNoise");
    return result;
}

Result<void> Graphics::debugValidatePbrVariantBaseSources() {
    return debugValidatePbrVariantSources(pbrVariantBaseSources());
}

Result<void> Graphics::debugValidatePbrVariantSources(const PbrVariantSources& sources) {
    if (!device || !instance)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "WebGPU device is not initialized"));
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    auto makeModule = [&](std::string_view source) {
        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = source.data();
        wgpu::ShaderModuleDescriptor descriptor{};
        descriptor.nextInChain = &wgsl;
        return device.CreateShaderModule(&descriptor);
    };
    auto vertex   = makeModule(sources.vertex);
    auto fragment = makeModule(sources.fragment);

    bool            done = false;
    wgpu::ErrorType error = wgpu::ErrorType::Unknown;
    std::string     message;
    device.PopErrorScope(
        wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView text) {
            error = status == wgpu::PopErrorScopeStatus::Success ? type : wgpu::ErrorType::Unknown;
            if (text.data) message.assign(text.data, text.length);
            done = true;
        });
    while (!done) wgpuInstanceProcessEvents(instance.Get());
    if (!vertex || !fragment || error != wgpu::ErrorType::NoError)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed,
            message.empty() ? "WebGPU rejected generated PBR WGSL" : message));
    return Result<void>::success();
}

Result<void> Graphics::debugValidatePbrVariantPipeline(const PbrVariantSources& sources) {
    auto modules = debugValidatePbrVariantSources(sources);
    if (!modules) return modules;

    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    auto makeModule = [&](std::string_view source) {
        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = source.data();
        wgpu::ShaderModuleDescriptor moduleDescriptor{};
        moduleDescriptor.nextInChain = &wgsl;
        return device.CreateShaderModule(&moduleDescriptor);
    };
    auto vertex = makeModule(sources.vertex);
    auto fragment = makeModule(sources.fragment);

    std::array<wgpu::VertexAttribute, 16> attributes{};
    auto attribute = [&](std::size_t index, wgpu::VertexFormat format, std::uint64_t offset,
                         std::uint32_t location) {
        attributes[index].format = format;
        attributes[index].offset = offset;
        attributes[index].shaderLocation = location;
    };
    attribute(0, wgpu::VertexFormat::Float32x3, 0, 0);
    attribute(1, wgpu::VertexFormat::Float32x3, 12, 1);
    attribute(2, wgpu::VertexFormat::Float32x2, 24, 2);
    attribute(3, wgpu::VertexFormat::Uint16x4, 32, 3);
    attribute(4, wgpu::VertexFormat::Float32x4, 40, 4);
    for (std::uint32_t index = 0; index < 11; ++index)
        attribute(index + 5, wgpu::VertexFormat::Float32x2, std::uint64_t(index) * 8u, index + 5);
    std::array<wgpu::VertexBufferLayout, 2> buffers{};
    buffers[0].arrayStride = 56;
    buffers[0].attributeCount = 5;
    buffers[0].attributes = attributes.data();
    buffers[1].arrayStride = 88;
    buffers[1].attributeCount = 11;
    buffers[1].attributes = attributes.data() + 5;

    wgpu::ColorTargetState target{};
    target.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::FragmentState fragmentState{};
    fragmentState.module = fragment;
    fragmentState.entryPoint = "main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &target;
    wgpu::RenderPipelineDescriptor descriptor{};
    descriptor.vertex.module = vertex;
    descriptor.vertex.entryPoint = "main";
    descriptor.vertex.bufferCount = buffers.size();
    descriptor.vertex.buffers = buffers.data();
    descriptor.fragment = &fragmentState;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.multisample.count = 1;
    auto pipeline = device.CreateRenderPipeline(&descriptor);

    bool done = false;
    wgpu::ErrorType error = wgpu::ErrorType::Unknown;
    std::string message;
    device.PopErrorScope(
        wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView text) {
            error = status == wgpu::PopErrorScopeStatus::Success ? type : wgpu::ErrorType::Unknown;
            if (text.data) message.assign(text.data, text.length);
            done = true;
        });
    while (!done) wgpuInstanceProcessEvents(instance.Get());
    if (!pipeline || error != wgpu::ErrorType::NoError)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, message.empty() ? "WebGPU rejected generated PBR pipeline" : message));
    return Result<void>::success();
}

}  // namespace eve::graphics::webgpu
