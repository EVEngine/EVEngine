#include "virtualgeometry/VirtualGeometry.h"
#include "virtualgeometry/VirtualGeometryRenderer.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "graphics/Graphics.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <string>

namespace eve::virtualgeometry {
namespace {

std::string currentGraphicsBackend() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    if (!gfx) return {};
    return gfx->getBackendName();
}

}  // namespace

Module_IMPL(VirtualGeometry, new VirtualGeometry());

bool VirtualGeometry::isAvailable() const {
    const std::string backend = currentGraphicsBackend();
    return backend == "vulkan" || backend == "webgpu";
}

VirtualGeometryRenderer *VirtualGeometry::newRenderer() {
    const std::string backend = currentGraphicsBackend();
    if (backend != "vulkan" && backend != "webgpu")
        throw Exception("VirtualGeometry.newRenderer: requires vulkan or webgpu Graphics backend");
    return new VirtualGeometryRenderer();
}

void VirtualGeometry::expose(ssq::Table &table) {
    auto cls = table.addClass(name, VirtualGeometry::create, false);
    expose(cls);

    auto renderer = table.addClass<VirtualGeometryRenderer>(
        "VirtualGeometryRenderer",
        std::function<VirtualGeometryRenderer *()>([]() -> VirtualGeometryRenderer * {
            return nullptr;
        }),
        true);
    renderer.addFunc("buildIcosphere", &VirtualGeometryRenderer::buildIcosphere);
    renderer.addFunc("setViewport", &VirtualGeometryRenderer::setViewport);
    renderer.addFunc("setCameraSimple", &VirtualGeometryRenderer::setCameraSimple);
    renderer.addFunc("setModelYaw", &VirtualGeometryRenderer::setModelYaw);
    renderer.addFunc("update", &VirtualGeometryRenderer::update);
    renderer.addFunc("resolve", &VirtualGeometryRenderer::resolveByteData);
    renderer.addFunc("getViewWidth", &VirtualGeometryRenderer::getViewWidth);
    renderer.addFunc("getViewHeight", &VirtualGeometryRenderer::getViewHeight);
    renderer.addFunc("isReady", &VirtualGeometryRenderer::isReady);
    renderer.addFunc("getClusterCount", &VirtualGeometryRenderer::getClusterCount);
    renderer.addFunc("getVisibleCount", &VirtualGeometryRenderer::getVisibleCount);
    renderer.addFunc("getTotalTriangleCount", &VirtualGeometryRenderer::getTotalTriangleCount);
    renderer.addFunc("getLodLevel", &VirtualGeometryRenderer::getLodLevel);
    renderer.addFunc("getMaxLodLevel", &VirtualGeometryRenderer::getMaxLodLevel);
}

void VirtualGeometry::expose(ssq::Class &cls) {
    cls.addFunc("getName", &VirtualGeometry::getName);
    cls.addFunc("isAvailable", &VirtualGeometry::isAvailable);
    cls.addFunc("newRenderer", &VirtualGeometry::newRenderer);
}

}  // namespace eve::virtualgeometry
