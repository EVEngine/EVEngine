#pragma once

#include "common/Resource.h"

#include <utility>

namespace eve::test {

/**
 * @brief Test-side keep-alive for one cached resource, typed for its concrete class.
 *
 * A test that loads a cached resource and keeps using it after `unload()` needs the
 * same lifetime extension production code spells as `ResourceManager::pin()`; this
 * holder carries that pin and still behaves like the pointer the test wants.
 *
 * @tparam T Concrete `Resource` subclass the pinned payload actually is.
 * @ownership The cache keeps owning the payload; this holder only postpones its
 *            destruction until the holder is dropped.
 * @lifetime The holder must not outlive the resource cache (the process-wide
 *           manager outlives every test case).
 */
template <class T>
class PinnedResource {
public:
    /** @brief Constructs an unbound holder. */
    PinnedResource() = default;

    /** @brief Adopts @p pin, which must name a `T`. */
    explicit PinnedResource(ResourcePin pin) : pin_(std::move(pin)), value_(static_cast<T *>(pin_.get())) {}

    /** @brief Borrows the pinned resource, or null when unbound. */
    [[nodiscard]] T *get() const noexcept { return value_; }
    /** @brief Pointer-like access to the pinned resource. */
    T *operator->() const noexcept { return value_; }
    /** @brief Lets the holder stand in for the raw pointer a test would otherwise hold. */
    operator T *() const noexcept { return value_; }

private:
    ResourcePin pin_;
    T          *value_ = nullptr;
};

/**
 * @brief Pins @p resource for the lifetime of the returned holder.
 * @return An unbound holder when @p resource is null or no longer cached.
 */
template <class T>
[[nodiscard]] PinnedResource<T> pinResource(T *resource) {
    if (resource == nullptr) return PinnedResource<T>();
    auto pinned = ResourceManager::getInstance().pin(*resource);
    if (!pinned.ok()) return PinnedResource<T>();
    return PinnedResource<T>(std::move(pinned).takeValue());
}

}  // namespace eve::test
