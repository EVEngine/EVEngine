#include "graphics/raytracing/RayTracingModule.h"

#include "common/Capability.h"
#include "graphics/IRayTracing.h"
#include "graphics/raytracing/vulkan/VulkanRayTracing.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::graphics::raytracing {

Module_IMPL(RayTracing, new RayTracing());

bool RayTracing::isAvailable() const {
    if (auto* rt = eve::cap::query<IRayTracing>()) return rt->isAvailable();
    return false;
}

RayTracingCaps RayTracing::caps() const {
    if (auto* rt = eve::cap::query<IRayTracing>()) return rt->caps();
    return {};
}

IRayTracing* RayTracing::backend() const { return eve::cap::query<IRayTracing>(); }

void RayTracing::expose(ssq::Class& cls) {
    cls.addFunc("isAvailable", &RayTracing::isAvailable);
    cls.addFunc("capsAvailable", [](RayTracing* self) -> bool { return self && self->caps().rayTracingAvailable(); });
}

void RayTracing::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RayTracing::create, false);
    expose(cls);
}

}  // namespace eve::graphics::raytracing
