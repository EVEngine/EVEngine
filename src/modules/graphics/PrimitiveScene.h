#pragma once

#include "common/Result.h"
#include "common/RuntimeHandle.h"
#include "graphics/PrimitiveDrawList.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace eve::graphics {

struct PrimitiveSceneTag;
/** @brief Instance-qualified persistent primitive handle. */
class PrimitiveHandle {
public:
    using LocalHandle                        = eve::RuntimeHandle<PrimitiveSceneTag>;
    using index_type                         = LocalHandle::index_type;
    using generation_type                    = LocalHandle::generation_type;
    using owner_type                         = std::uint64_t;
    static constexpr index_type invalidIndex = LocalHandle::invalidIndex;

    /** @brief Constructs a PrimitiveHandle. */
    constexpr PrimitiveHandle() noexcept = default;
    /** @brief Constructs a PrimitiveHandle. */
    constexpr PrimitiveHandle(owner_type owner, index_type index, generation_type generation) noexcept
        /** @brief Owner. */
        : owner_(owner), local_(index, generation) {}
    /** @brief True when valid. */
    [[nodiscard]] constexpr bool            isValid() const noexcept { return owner_ != 0 && local_.isValid(); }
    /** @brief Index. */
    [[nodiscard]] constexpr index_type      index() const noexcept { return local_.index(); }
    /** @brief Generation. */
    [[nodiscard]] constexpr generation_type generation() const noexcept { return local_.generation(); }
    /** @brief Owner. */
    [[nodiscard]] constexpr owner_type      owner() const noexcept { return owner_; }
    /** @brief Next generation. */
    [[nodiscard]] static constexpr std::optional<generation_type> nextGeneration(generation_type current) noexcept {
        /** @brief Next generation. */
        return LocalHandle::nextGeneration(current);
    }
    /** @brief Operator ==. */
    friend constexpr bool operator==(const PrimitiveHandle&, const PrimitiveHandle&) noexcept = default;

private:
    owner_type  owner_ = 0;
    LocalHandle local_{};
};

/** @brief Owning persistent polyline descriptor. */
struct PrimitivePolyline3D {
    std::vector<glm::vec3> points;
    bool                   closed = false;
};
/** @brief PrimitiveAabb3D public API. */
struct PrimitiveAabb3D {
    glm::vec3 minimum{0.f};
    glm::vec3 maximum{0.f};
};
/** @brief PrimitiveObb3D public API. */
struct PrimitiveObb3D {
    glm::vec3                center{0.f};
    std::array<glm::vec3, 3> halfAxes{{{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}}};
};
/** @brief PrimitiveDisk3D public API. */
struct PrimitiveDisk3D {
    glm::vec3     center{0.f};
    glm::vec3     normal{0.f, 1.f, 0.f};
    float         radius   = 1.f;
    std::uint32_t segments = 32;
};
/** @brief PrimitiveArc3D public API. */
struct PrimitiveArc3D {
    glm::vec3     center{0.f};
    glm::vec3     normal{0.f, 1.f, 0.f};
    glm::vec3     zeroDirection{1.f, 0.f, 0.f};
    float         radius       = 1.f;
    float         startRadians = 0.f;
    float         sweepRadians = 1.f;
    std::uint32_t segments     = 32;
};
/** @brief PrimitiveSphere3D public API. */
struct PrimitiveSphere3D {
    glm::vec3     center{0.f};
    float         radius   = 1.f;
    std::uint32_t segments = 32;
};
/** @brief PrimitiveCapsule3D public API. */
struct PrimitiveCapsule3D {
    glm::vec3     a{0.f};
    glm::vec3     b{0.f, 1.f, 0.f};
    float         radius   = 1.f;
    std::uint32_t segments = 32;
};
/** @brief PrimitiveCylinder3D public API. */
struct PrimitiveCylinder3D {
    glm::vec3     a{0.f};
    glm::vec3     b{0.f, 1.f, 0.f};
    float         radius   = 1.f;
    std::uint32_t segments = 32;
};
/** @brief PrimitiveCone3D public API. */
struct PrimitiveCone3D {
    glm::vec3     apex{0.f};
    glm::vec3     axis{0.f, 1.f, 0.f};
    float         height   = 1.f;
    float         radius   = 1.f;
    std::uint32_t segments = 32;
};
/** @brief PrimitiveGrid3D public API. */
struct PrimitiveGrid3D {
    glm::vec3     origin{0.f};
    glm::vec3     axisU{1.f, 0.f, 0.f};
    glm::vec3     axisV{0.f, 0.f, 1.f};
    std::uint32_t cellsU = 10;
    std::uint32_t cellsV = 10;
};
/** @brief PrimitiveArrow3D public API. */
struct PrimitiveArrow3D {
    glm::vec3 from{0.f};
    glm::vec3 to{0.f, 1.f, 0.f};
    float     headLength = 0.2f;
    float     headRadius = 0.1f;
};
/** @brief PrimitiveFrustum3D public API. */
struct PrimitiveFrustum3D {
    std::array<glm::vec3, 8> corners{};
};

using PrimitiveGeometry3D = std::variant<PrimitivePolyline3D, PrimitiveAabb3D, PrimitiveObb3D, PrimitiveDisk3D,
                                         PrimitiveArc3D, PrimitiveSphere3D, PrimitiveCapsule3D, PrimitiveCylinder3D,
                                         PrimitiveCone3D, PrimitiveGrid3D, PrimitiveArrow3D, PrimitiveFrustum3D>;

/** @brief One persistent scene primitive owned by PrimitiveScene. */
struct PrimitiveDescriptor3D {
    PrimitiveGeometry3D geometry;
    ScenePrimitivePaint paint;
    glm::mat4           transform{1.f};
    bool                visible = true;
};

/** @brief PrimitiveUpdateStatus public API. */
enum class PrimitiveUpdateStatus : std::uint8_t { Updated, Unchanged };
/** @brief PrimitiveRemoveStatus public API. */
enum class PrimitiveRemoveStatus : std::uint8_t { Removed };
/** @brief PrimitiveBatchUpdate public API. */
struct PrimitiveBatchUpdate {
    PrimitiveHandle       handle;
    PrimitiveDescriptor3D descriptor;
};

/**
 * @brief Authoritative owner for primitives that persist across frames.
 *
 * All operations, including render(), are owner-thread affine and invoke no callbacks.
 * Descriptors are authoritative; cached CPU commands are disposable derived data.
 * @ownership Owns descriptors and caches; borrows the destination Canvas only during rendering.
 * @lifetime Cached data lives until successful replacement, removal, clear, or scene destruction.
 * Rendering synchronously copies commands into a caller-owned Canvas without retaining it.
 */
class EVENGINE_API_BACKENDS PrimitiveScene {
public:
    /** @brief Primitive scene. */
    PrimitiveScene();
    PrimitiveScene(const PrimitiveScene&)                                        = delete;
    PrimitiveScene& operator=(const PrimitiveScene&)                             = delete;
    PrimitiveScene(PrimitiveScene&&)                                             = delete;
    PrimitiveScene&                                  operator=(PrimitiveScene&&) = delete;
    /** @brief Adds . */
    [[nodiscard]] eve::Result<PrimitiveHandle>       add(PrimitiveDescriptor3D descriptor);
    /** @brief Updates . */
    [[nodiscard]] eve::Result<PrimitiveUpdateStatus> update(PrimitiveHandle handle, PrimitiveDescriptor3D descriptor);
    /** @brief Removes . */
    [[nodiscard]] eve::Result<PrimitiveRemoveStatus> remove(PrimitiveHandle handle);
    /** @brief Atomically replaces several live descriptors after validating every handle. */
    [[nodiscard]] eve::Result<std::size_t> updateMany(std::span<const PrimitiveBatchUpdate> updates);
    /**
     * @brief Resolves a live primitive handle.
     * @param handle Generation-qualified handle issued by this scene.
     * @return Borrowed descriptor or nullptr when the handle is invalid or stale.
     * @ownership Borrowed; PrimitiveScene retains ownership.
     * @lifetime Valid until the next non-const PrimitiveScene operation or scene destruction.
     * @thread Owner-thread affine; concurrent mutation is not supported.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] const PrimitiveDescriptor3D* tryGet(PrimitiveHandle handle) const noexcept;
    /** @brief Returns true when a handle is invalid, removed, foreign, or generation-stale. */
    [[nodiscard]] bool                         isStale(PrimitiveHandle handle) const noexcept;
    /** @brief Invalidates all live handles while retaining slot capacity. */
    void clear();
    /**
     * @brief Projects visible descriptors in stable slot order using per-slot CPU caches.
     * Successful updates invalidate the affected cache; removal/clear release it.
     * Camera changes reuse geometry, but clipping and screen-space stroke resolution
     * remain frame-local. No GPU allocation is retained by this cache.
     * A primitive exceeding the remaining command budget returns a failure without
     * publishing that primitive; earlier primitives remain recorded. droppedCommands
     * reports the rejected primitive's commands. Allocation failure throws before
     * publishing that primitive; already recorded primitives remain valid.
     * @param canvas Borrowed for this call only; receives independent owning commands.
     * @return Success, or Failed when the next primitive exceeds the remaining budget.
     * @thread Owner-thread affine, including concurrent const calls.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> tryRender(PrimitiveSceneCanvas3D& canvas) const;
    /**
     * @brief Compatibility-only wrapper over tryRender; throws on budget failure.
     * @param canvas Borrowed for this synchronous call; retains owning command copies.
     * @thread Owner-thread affine.
     * @reentrancy Does not invoke callbacks.
     */
    void                      render(PrimitiveSceneCanvas3D& canvas) const;
    /** @brief Returns the size of . */
    [[nodiscard]] std::size_t size() const noexcept { return liveCount_; }

private:
    struct Slot {
        std::optional<PrimitiveDescriptor3D> descriptor;
        PrimitiveHandle::generation_type     generation = 1;
        bool                                 retired    = false;
        mutable std::optional<PrimitiveSceneCanvas3D> cache;
    };

    [[nodiscard]] bool                       matches(PrimitiveHandle handle) const noexcept;
    std::vector<Slot>                        slots_;
    std::vector<PrimitiveHandle::index_type> freeSlots_;
    std::size_t                              liveCount_ = 0;
    PrimitiveHandle::owner_type              owner_     = 0;
};

}  // namespace eve::graphics
