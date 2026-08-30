#pragma once

// Bridge C++ ECS.hpp Views ↔ gpgpu::ShaderSystem storage buffers.
// Component types must be trivially copyable and a multiple of sizeof(float)
// (e.g. struct Position { float x, y; }).

#include "gpgpu/ShaderSystem.h"

#include "ECS.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

namespace eve::gpgpu {

/**
 * @brief Reusable typed CPU staging for a GPU ECS component stream.
 * Ownership stays with the caller; one workspace must not be used concurrently.
 */
template <typename Comp>
class EcsGpuWorkspace {
public:
    static_assert(std::is_trivially_copyable_v<Comp>, "EcsGpu component must be trivially copyable");
    static_assert(sizeof(Comp) >= sizeof(float) && sizeof(Comp) % sizeof(float) == 0,
                  "EcsGpu component size must be a non-zero multiple of float");

    /** @brief Reserve staging storage for at least entityCount components. */
    void reserveEntities(int entityCount) {
        if (entityCount > 0) floats_.reserve(size_t(entityCount) * size_t(sizeof(Comp) / sizeof(float)));
    }

    /** @brief Current allocated staging capacity expressed as entity records. */
    size_t entityCapacity() const { return floats_.capacity() / size_t(sizeof(Comp) / sizeof(float)); }

private:
    template <typename Base, typename C>
    friend int packViewComponent(ShaderSystem &, int, EcsGpuWorkspace<C> &);
    template <typename Base, typename C>
    friend int packViewComponentRange(ShaderSystem &, int, int, int, EcsGpuWorkspace<C> &);
    template <typename Base, typename C>
    friend void unpackViewComponent(ShaderSystem &, int, int, EcsGpuWorkspace<C> &);
    template <typename Base, typename C>
    friend int         unpackViewComponentRange(ShaderSystem &, int, int, int, EcsGpuWorkspace<C> &);
    std::vector<float> floats_;
};

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
int packViewComponent(ShaderSystem &sys, int binding, EcsGpuWorkspace<Comp> &workspace) {
    const int floatsPer = componentFloatCount<Comp>();
    int n = 0;
    {
        auto view = ecs::View<Base, Comp>();
        for (auto it = view.begin(); it != view.end(); ++it)
            ++n;
    }
    if (n <= 0) return 0;

    workspace.floats_.resize(size_t(n) * size_t(floatsPer));
    int i = 0;
    auto view = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [comp] = *it;
        std::memcpy(workspace.floats_.data() + size_t(i) * size_t(floatsPer), static_cast<const void *>(comp),
                    sizeof(Comp));
        ++i;
    }
    sys.upload(binding, workspace.floats_.data(), n * floatsPer);
    return n;
}

/** @brief Compatibility overload that uses a temporary staging workspace. */
template <typename Base, typename Comp>
int packViewComponent(ShaderSystem &sys, int binding) {
    EcsGpuWorkspace<Comp> workspace;
    return packViewComponent<Base, Comp>(sys, binding, workspace);
}

/**
 * @brief Write ShaderSystem binding floats back into View<Base, Comp> (same order as pack).
 * `count` should be the value returned by packViewComponent.
 */
template <typename Base, typename Comp>
void unpackViewComponent(ShaderSystem &sys, int binding, int count, EcsGpuWorkspace<Comp> &workspace) {
    if (count <= 0) return;
    const int floatsPer = componentFloatCount<Comp>();
    workspace.floats_.resize(size_t(count) * size_t(floatsPer));
    sys.download(binding, workspace.floats_.data(), count * floatsPer);
    int i = 0;
    auto view = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end() && i < count; ++it) {
        auto [comp] = *it;
        std::memcpy(static_cast<void *>(comp), workspace.floats_.data() + size_t(i) * size_t(floatsPer), sizeof(Comp));
        ++i;
    }
}

/** @brief Compatibility overload that uses a temporary staging workspace. */
template <typename Base, typename Comp>
void unpackViewComponent(ShaderSystem &sys, int binding, int count) {
    EcsGpuWorkspace<Comp> workspace;
    unpackViewComponent<Base, Comp>(sys, binding, count, workspace);
}

/**
 * @brief Pack a stable contiguous View range into an existing resident buffer.
 * The full buffer must have been allocated by a prior full pack/ensureBuffer.
 */
template <typename Base, typename Comp>
int packViewComponentRange(ShaderSystem &sys, int binding, int firstEntity, int entityCount,
                           EcsGpuWorkspace<Comp> &workspace) {
    if (firstEntity < 0 || entityCount <= 0) return 0;
    const int floatsPer = componentFloatCount<Comp>();
    workspace.floats_.resize(size_t(entityCount) * size_t(floatsPer));
    int  viewIndex = 0;
    int  packed    = 0;
    auto view      = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end() && packed < entityCount; ++it, ++viewIndex) {
        if (viewIndex < firstEntity) continue;
        auto [comp] = *it;
        std::memcpy(workspace.floats_.data() + size_t(packed) * size_t(floatsPer), static_cast<const void *>(comp),
                    sizeof(Comp));
        ++packed;
    }
    if (packed > 0) sys.uploadRange(binding, workspace.floats_.data(), packed * floatsPer, firstEntity * floatsPer);
    return packed;
}

/** @brief Unpack a stable contiguous resident-buffer range into a matching View range. */
template <typename Base, typename Comp>
int unpackViewComponentRange(ShaderSystem &sys, int binding, int firstEntity, int entityCount,
                             EcsGpuWorkspace<Comp> &workspace) {
    if (firstEntity < 0 || entityCount <= 0) return 0;
    const int available = std::max(0, countViewEntities<Base, Comp>() - firstEntity);
    const int count     = std::min(entityCount, available);
    if (count <= 0) return 0;
    const int floatsPer = componentFloatCount<Comp>();
    workspace.floats_.resize(size_t(count) * size_t(floatsPer));
    sys.downloadRange(binding, workspace.floats_.data(), count * floatsPer, firstEntity * floatsPer);
    int  viewIndex = 0;
    int  unpacked  = 0;
    auto view      = ecs::View<Base, Comp>();
    for (auto it = view.begin(); it != view.end() && unpacked < count; ++it, ++viewIndex) {
        if (viewIndex < firstEntity) continue;
        auto [comp] = *it;
        std::memcpy(static_cast<void *>(comp), workspace.floats_.data() + size_t(unpacked) * size_t(floatsPer),
                    sizeof(Comp));
        ++unpacked;
    }
    return unpacked;
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
