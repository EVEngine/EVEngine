#pragma once

// Bridge C++ ECS.hpp Views ↔ gpgpu::ShaderSystem storage buffers.
// Component types must be trivially copyable and a multiple of sizeof(float)
// (e.g. struct Position { float x, y; }).

#include "gpgpu/ShaderSystem.h"

#include "ECS.hpp"

#include <cstring>
#include <type_traits>
#include <vector>

namespace eve::gpgpu {

template <typename Comp>
constexpr int componentFloatCount() {
    static_assert(std::is_trivially_copyable_v<Comp>,
                  "EcsGpu component must be trivially copyable");
    static_assert(sizeof(Comp) % sizeof(float) == 0,
                  "EcsGpu component size must be a multiple of float");
    return int(sizeof(Comp) / sizeof(float));
}

/** @brief Count live entities matching View<Base, Comp>. */
template <typename Base, typename Comp>
int countViewEntities() {
    int n = 0;
    auto view = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end(); ++it)
        ++n;
    return n;
}

/**
 * @brief Pack View<Base, Comp> into ShaderSystem binding as AoS floats.
 * Returns entity count (0 if empty).
 */
template <typename Base, typename Comp>
int packViewComponent(ShaderSystem &sys, int binding) {
    const int floatsPer = componentFloatCount<Comp>();
    int n = 0;
    {
        auto view = ecs::View<Base, Comp>();
        for (auto it = view.begin(); it != view.end(); ++it)
            ++n;
    }
    if (n <= 0) return 0;

    std::vector<float> tmp(size_t(n) * size_t(floatsPer));
    int i = 0;
    auto view = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [comp] = *it;
        std::memcpy(tmp.data() + size_t(i) * size_t(floatsPer), static_cast<const void *>(comp),
                    sizeof(Comp));
        ++i;
    }
    sys.upload(binding, tmp.data(), n * floatsPer);
    return n;
}

/**
 * @brief Write ShaderSystem binding floats back into View<Base, Comp> (same order as pack).
 * `count` should be the value returned by packViewComponent.
 */
template <typename Base, typename Comp>
void unpackViewComponent(ShaderSystem &sys, int binding, int count) {
    if (count <= 0) return;
    const int floatsPer = componentFloatCount<Comp>();
    std::vector<float> tmp = sys.download(binding, count * floatsPer);
    int i = 0;
    auto view = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end() && i < count; ++it) {
        auto [comp] = *it;
        std::memcpy(static_cast<void *>(comp), tmp.data() + size_t(i) * size_t(floatsPer),
                    sizeof(Comp));
        ++i;
    }
}

/**
 * @brief Pack + dispatch + unpack a single-component GPU system.
 * Additional bindings must be uploaded by the caller before this helper, or use
 * ShaderSystem manually for multi-buffer systems.
 */
template <typename Base, typename Comp>
int runViewComponentSystem(ShaderSystem &sys, int binding, float dt) {
    const int n = packViewComponent<Base, Comp>(sys, binding);
    if (n <= 0) return 0;
    sys.dispatch(n, dt);
    unpackViewComponent<Base, Comp>(sys, binding, n);
    return n;
}

}  // namespace eve::gpgpu
