#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/VulkanUtil.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::gpgpu {
namespace {

ComputeShader *buildComputeShader(const std::vector<uint32_t> &spv) {
    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();

    auto *shader = new ComputeShader();
    shader->device_ = &device;
    shader->module_ = vkb::PipelineBuilder::createShaderModule(device.instance, spv);

    // Fixed layout: set 0, bindings 0..N-1 = storage buffers (compute stage).
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(ComputeShader::kMaxBindings);
    for (int i = 0; i < ComputeShader::kMaxBindings; ++i) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = uint32_t(i);
        b.descriptorType = vk::DescriptorType::eStorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(b);
    }
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = uint32_t(bindings.size());
    layoutInfo.pBindings = bindings.data();
    shader->setLayout_ = device->createDescriptorSetLayout(layoutInfo, device.allocation_callbacks);

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcr.offset = 0;
    pcr.size = ComputeShader::kPushConstantBytes;

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &shader->setLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcr;
    shader->pipelineLayout_ = device->createPipelineLayout(plInfo, device.allocation_callbacks);

    vk::PipelineShaderStageCreateInfo stage{};
    stage.stage = vk::ShaderStageFlagBits::eCompute;
    stage.module = shader->module_;
    stage.pName = "main";

    vk::ComputePipelineCreateInfo cpInfo{};
    cpInfo.stage = stage;
    cpInfo.layout = shader->pipelineLayout_;
    auto result = device->createComputePipeline(vk::PipelineCache{}, cpInfo, device.allocation_callbacks);
    if (result.result != vk::Result::eSuccess) {
        delete shader;
        throw Exception("Gpgpu.newShader: createComputePipeline failed");
    }
    shader->pipeline_ = result.value;
    return shader;
}

GpuBuffer *buildBuffer(int byteSize, const std::string &usage) {
    if (byteSize <= 0) throw Exception("Gpgpu.newBuffer: byteSize must be > 0");
    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();

    const bool staging = (usage == "staging");
    const bool storage = (usage == "storage" || usage.empty());
    if (!staging && !storage)
        throw Exception("Gpgpu.newBuffer: usage must be \"storage\" or \"staging\"");

    using buf = vk::BufferUsageFlagBits;
    using pfb = vk::MemoryPropertyFlagBits;

    vk::BufferUsageFlags flags = buf::eTransferSrc | buf::eTransferDst;
    if (storage || staging) flags |= buf::eStorageBuffer;

    vk::MemoryPropertyFlags mem =
        staging ? (pfb::eHostVisible | pfb::eHostCoherent) : pfb::eDeviceLocal;

    auto *b = new GpuBuffer();
    b->device_ = &device;
    b->size_ = vk::DeviceSize(byteSize);
    b->usage_ = staging ? "staging" : "storage";
    b->hostVisible_ = staging;

    vkb::GenericBuffer tmp(device, flags, b->size_, mem);
    b->buffer_ = tmp.buffer;
    b->memory_ = tmp.memory;
    // tmp destructor would double-free if GenericBuffer had one — it doesn't; we own handles.
    return b;
}

}  // namespace

Module_IMPL(Gpgpu, new Gpgpu());

bool Gpgpu::isAvailable() const { return vulkanGraphicsReady(); }

ComputeShader *Gpgpu::newShader(const std::string &glsl) {
    return buildComputeShader(compileComputeGlsl(glsl));
}

ComputeShader *Gpgpu::newShaderFromSpvFile(const std::string &path) {
    return buildComputeShader(loadSpirvFile(path));
}

GpuBuffer *Gpgpu::newBuffer(int byteSize, const std::string &usage) {
    return buildBuffer(byteSize, usage);
}

void Gpgpu::dispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    if (!shader || !shader->pipeline_) return;
    if (groupsX <= 0) groupsX = 1;
    if (groupsY <= 0) groupsY = 1;
    if (groupsZ <= 0) groupsZ = 1;

    auto *vkg = requireVulkanGraphics();
    auto &device = vkg->getDevice();
    auto queue = computeQueue(vkg);
    auto pool = computeCommandPool(vkg);
    if (!queue) throw Exception("Gpgpu.dispatch: no compute/graphics queue");

    shader->flushDescriptors(device);

    vkb::executeImmediately(device.instance, pool, queue, [&](vk::CommandBuffer cb) {
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, shader->pipeline_);
        if (shader->descriptorSet_) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, shader->pipelineLayout_, 0,
                                  shader->descriptorSet_, nullptr);
        }
        cb.pushConstants(shader->pipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0,
                         ComputeShader::kPushConstantBytes, shader->push_.data());
        cb.dispatch(uint32_t(groupsX), uint32_t(groupsY), uint32_t(groupsZ));
    });
}

void Gpgpu::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Gpgpu::create, false);
    expose(cls);

    auto shader = table.addClass<ComputeShader>(
        "ComputeShader",
        std::function<ComputeShader *()>([]() -> ComputeShader * { return nullptr; }), true);
    shader.addFunc("bindBuffer", &ComputeShader::bindBuffer);
    shader.addFunc("getBoundBuffer", &ComputeShader::getBoundBuffer);
    shader.addFunc("setFloat", &ComputeShader::setFloat);
    shader.addFunc("getFloat", &ComputeShader::getFloat);
    shader.addFunc("clearBindings", &ComputeShader::clearBindings);

    auto buf = table.addClass<GpuBuffer>(
        "GpuBuffer", std::function<GpuBuffer *()>([]() -> GpuBuffer * { return nullptr; }), true);
    buf.addFunc("getSize", &GpuBuffer::getSize);
    buf.addFunc("getUsage", &GpuBuffer::getUsage);
    buf.addFunc("writeData", &GpuBuffer::writeData);
    buf.addFunc("readData", &GpuBuffer::readData);
    buf.addFunc("writeFloat32", &GpuBuffer::writeFloat32);
    buf.addFunc("readFloat32", &GpuBuffer::readFloat32);
    buf.addFunc("fillFloat32", &GpuBuffer::fillFloat32);
}

void Gpgpu::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Gpgpu::getName);
    cls.addFunc("isAvailable", &Gpgpu::isAvailable);
    cls.addFunc("newShader", &Gpgpu::newShader);
    cls.addFunc("newShaderFromSpvFile", &Gpgpu::newShaderFromSpvFile);
    cls.addFunc("newBuffer", &Gpgpu::newBuffer);
    cls.addFunc("dispatch", &Gpgpu::dispatch);
}

}  // namespace eve::gpgpu
