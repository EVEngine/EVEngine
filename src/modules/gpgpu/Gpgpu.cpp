#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/ShaderSystem.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"

#ifdef EVENGINE_WEBGPU
#include "gpgpu/webgpu/WebGpuGpgpu.h"
#else
#include "gpgpu/vulkan/VulkanGpgpu.h"
#include "gpgpu/vulkan/VulkanUtil.h"
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::gpgpu {
namespace {

std::string currentGraphicsBackend() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    if (!gfx) return {};
    return gfx->getBackendName();
}

}  // namespace

Module_IMPL(Gpgpu, new Gpgpu());

bool Gpgpu::isAvailable() const {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu") return false;
    return webgpuGpgpuReady();
#else
    if (currentGraphicsBackend() != "vulkan") return false;
    return vulkanGpgpuReady();
#endif
}

ComputeShader *Gpgpu::newShader(const std::string &source) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShader: requires webgpu Graphics backend");
    // Source is WGSL on the WebGPU backend (browsers cannot compile GLSL).
    return webgpuNewShaderFromWgsl(source);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShader: requires vulkan Graphics backend");
    return vulkanNewShaderFromSpirv(compileComputeGlsl(source));
#endif
}

ComputeShader *Gpgpu::newShaderFromBytecode(const std::string &path) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShaderFromBytecode: requires webgpu Graphics backend");
    // Load WGSL text from the given path.
    auto *fs = eve::filesystem::Filesystem::create();
    if (!fs) throw Exception("Gpgpu.newShaderFromBytecode: no filesystem");
    std::unique_ptr<eve::filesystem::FileData> file(fs->read(path));
    if (!file) throw Exception("Gpgpu.newShaderFromBytecode: cannot open '%s'", path.c_str());
    std::string src(reinterpret_cast<const char *>(file->getData()), file->getSize());
    return webgpuNewShaderFromWgsl(src);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromBytecode: requires vulkan Graphics backend");
    return vulkanNewShaderFromSpirv(loadSpirvFile(path));
#endif
}

ComputeShader *Gpgpu::newShaderFromSpvFile(const std::string &path) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShaderFromSpvFile: requires webgpu Graphics backend");
    throw Exception("Gpgpu.newShaderFromSpvFile: SPIR-V is only supported on vulkan; "
                    "use newShaderFromBytecode with a .wgsl file on the WebGPU backend");
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromSpvFile: SPIR-V is only supported on vulkan");
    return newShaderFromBytecode(path);
#endif
}

GpuBuffer *Gpgpu::newBuffer(int byteSize, const std::string &usage) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newBuffer: requires webgpu Graphics backend");
    return webgpuNewBuffer(byteSize, usage);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newBuffer: requires vulkan Graphics backend");
    return vulkanNewBuffer(byteSize, usage);
#endif
}

void Gpgpu::dispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    if (!shader) return;
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.dispatch: requires webgpu Graphics backend");
    webgpuDispatch(shader, groupsX, groupsY, groupsZ);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.dispatch: requires vulkan Graphics backend");
    vulkanDispatch(shader, groupsX, groupsY, groupsZ);
#endif
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

    // Native ECS↔GPU helper (used by eve.ShaderSystem script class).
    auto ecsSys = table.addClass<ShaderSystem>(
        "EcsShaderSystem",
        std::function<ShaderSystem *()>([]() -> ShaderSystem * { return new ShaderSystem(); }),
        true);
    ecsSys.addFunc("setGpgpu", &ShaderSystem::setGpgpu);
    ecsSys.addFunc("getGpgpu", &ShaderSystem::getGpgpu);
    ecsSys.addFunc("setShaderSource", &ShaderSystem::setShaderSource);
    ecsSys.addFunc("setShader", std::function<void(ShaderSystem *, ComputeShader *)>(
                                    [](ShaderSystem *self, ComputeShader *s) {
                                        if (self) self->setShader(s, false);
                                    }));
    ecsSys.addFunc("getShader", &ShaderSystem::getShader);
    ecsSys.addFunc("setLocalSize", &ShaderSystem::setLocalSize);
    ecsSys.addFunc("getLocalSize", &ShaderSystem::getLocalSize);
    ecsSys.addFunc("ensureBuffer", &ShaderSystem::ensureBuffer);
    ecsSys.addFunc("getBuffer", &ShaderSystem::getBuffer);
    ecsSys.addFunc("setFloat", &ShaderSystem::setFloat);
    ecsSys.addFunc("getFloat", &ShaderSystem::getFloat);
    ecsSys.addFunc("dispatch", std::function<void(ShaderSystem *, int, float)>(
                                   [](ShaderSystem *self, int n, float dt) {
                                       if (self) self->dispatch(n, dt);
                                   }));
    ecsSys.addFunc("clearBuffers", &ShaderSystem::clearBuffers);

    table.addFunc("packEcsFloats", packScriptEntityFloats);
    table.addFunc("unpackEcsFloats", unpackScriptEntityFloats);
}

void Gpgpu::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Gpgpu::getName);
    cls.addFunc("isAvailable", &Gpgpu::isAvailable);
    cls.addFunc("newShader", &Gpgpu::newShader);
    cls.addFunc("newShaderFromBytecode", &Gpgpu::newShaderFromBytecode);
    cls.addFunc("newShaderFromSpvFile", &Gpgpu::newShaderFromSpvFile);
    cls.addFunc("newBuffer", &Gpgpu::newBuffer);
    cls.addFunc("dispatch", &Gpgpu::dispatch);
}

}  // namespace eve::gpgpu
