#pragma once

#include "common/Result.h"
#include "physics/PhysicsLink.h"
#include "pixelworld/PixelWorld.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::physics {
class World;
}

namespace eve::pixelworld_physics {

/** @brief One deterministic axis-aligned fixture produced from a fragment bitmap. */
struct FragmentCollisionRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/** @brief Policy for dynamic fragment fixture creation and terrain settlement. */
struct FragmentBodyConfig {
    float density = 1.0f;
    float friction = 0.4f;
    float restitution = 0.05f;
    std::uint32_t maximumFixtures = 256;
};

/** @brief Observable outcome of polling a fragment body for settlement. */
enum class FragmentSettleDisposition : std::uint8_t { StillAwake, Rasterized };

/** @brief Receipt for one checked fragment-body settlement poll. */
struct FragmentSettleReceipt {
    FragmentSettleDisposition disposition = FragmentSettleDisposition::StillAwake;
    std::uint64_t fragmentId = 0;
    std::uint32_t cellsRasterized = 0;
    int quarterTurns = 0;
};

/** @brief Counters for one transactional dirty-Chunk terrain collision sync. */
struct TerrainCollisionSyncReceipt {
    std::uint64_t sourceRevision = 0;
    std::uint32_t chunksRebuilt = 0;
    std::uint32_t fixturesCreated = 0;
    std::uint32_t bodiesRemoved = 0;
};

/** @brief One simplified local-space boundary contour generated for a terrain Chunk. */
struct TerrainCollisionContour {
    /** @brief Packed local pixel-space vertices x0,y0,x1,y1,... without a repeated loop endpoint. */
    std::vector<float> vertices;
    /** @brief Whether the last vertex connects back to the first. */
    bool loop = false;
};

/** @brief Deterministic contact returned by an authoritative terrain probe or sweep. */
struct PixelTerrainContact {
    bool hit = false;
    eve::pixelworld::MaterialId material = eve::pixelworld::MaterialId::Air;
    int cellX = 0;
    int cellY = 0;
    float pointX = 0.f;
    float pointY = 0.f;
    float normalX = 0.f;
    float normalY = 0.f;
    /** @brief Probe penetration depth, or zero for a non-overlapping sweep hit. */
    float depth = 0.f;
    /** @brief Sweep time in [0,1], or zero for a probe. */
    float fraction = 0.f;
};

/**
 * @brief Extract simplified binary Marching-Squares-equivalent boundary chains for one Chunk.
 * @remarks Solid occupancy is sampled through PixelWorld, including the one-cell neighbor halo,
 * so shared Chunk borders do not emit internal collision edges. Output is deterministic and owning.
 */
[[nodiscard]] eve::Result<std::vector<TerrainCollisionContour>> extractTerrainContours(
    const eve::pixelworld::PixelWorld& pixelWorld, int chunkX, int chunkY,
    std::uint32_t maximumVertices = 4096);

/**
 * @brief Probe a circular character contact directly against authoritative solid material cells.
 * @param maximumCells Positive upper bound on candidate cells inspected.
 */
[[nodiscard]] eve::Result<PixelTerrainContact> probeTerrainCircle(
    const eve::pixelworld::PixelWorld& pixelWorld, float centerX, float centerY, float radius,
    std::uint32_t maximumCells = 4096);

/**
 * @brief Sweep a circular character shape continuously through authoritative solid material cells.
 * @remarks Uses an exact segment-vs-expanded-cell slab test and deterministic fraction/cell tie-breaks.
 */
[[nodiscard]] eve::Result<PixelTerrainContact> sweepTerrainCircle(
    const eve::pixelworld::PixelWorld& pixelWorld, float startX, float startY, float endX,
    float endY, float radius, std::uint32_t maximumCells = 16384);

/**
 * @brief Owning detached bitmap plus a non-owning, stale-safe dynamic body link.
 *
 * PixelWorld remains the only owner of live terrain. This adapter owns the detached
 * bitmap while Physics World owns the body and fixtures. It stores no World/Body raw
 * pointer across calls; every operation resolves `PhysicsLink` against a caller-borrowed
 * world on the owning simulation thread. No method invokes scripts or unknown callbacks.
 */
class PixelFragmentBody {
public:
    ~PixelFragmentBody() = default;
    PixelFragmentBody(PixelFragmentBody&&) noexcept = default;
    PixelFragmentBody& operator=(PixelFragmentBody&&) noexcept = default;
    PixelFragmentBody(const PixelFragmentBody&) = delete;
    PixelFragmentBody& operator=(const PixelFragmentBody&) = delete;

    /**
     * @brief Create one dynamic Box2D body using deterministic greedy rectangle decomposition.
     * @param world Borrowed Physics world; it owns the created body and fixtures.
     * @param fragment Owning detached bitmap transferred into the adapter on success.
     * @param config Finite fixture properties and a positive fixture budget.
     * @return Owning adapter; failure destroys any partially created body.
     */
    [[nodiscard]] static eve::Result<std::unique_ptr<PixelFragmentBody>> create(
        eve::physics::World& world, eve::pixelworld::PixelFragment fragment,
        FragmentBodyConfig config = {});

    /** @brief Borrow deterministic collision rectangles for diagnostics until adapter mutation/destruction. */
    [[nodiscard]] const std::vector<FragmentCollisionRect>& collisionRects() const noexcept;
    /** @brief Detached bitmap id retained by this adapter. */
    [[nodiscard]] std::uint64_t fragmentId() const noexcept;
    /** @brief Process-local physics relationship; resolve it before every use. */
    [[nodiscard]] eve::physics::PhysicsLink physicsLink() const noexcept;
    /** @brief Whether the bitmap has already returned to authoritative terrain. */
    [[nodiscard]] bool isRasterized() const noexcept;

    /**
     * @brief If the body sleeps, snap its bitmap to the nearest quarter turn and rasterize it.
     * @remarks Physics body destruction occurs only after PixelWorld accepts the entire bitmap.
     * A stale physics/world link or terrain conflict returns failure without changing ownership.
     */
    [[nodiscard]] eve::Result<FragmentSettleReceipt> settleIfSleeping(
        eve::physics::World& physicsWorld, eve::pixelworld::PixelWorld& pixelWorld);

    /**
     * @brief Explicitly destroy a still-live physics body without rasterizing its bitmap.
     * @return Success if already released; stale/foreign world links are reported.
     */
    [[nodiscard]] eve::Result<void> releasePhysics(eve::physics::World& physicsWorld);

private:
    PixelFragmentBody(eve::pixelworld::PixelFragment fragment, eve::physics::PhysicsLink link,
                      std::vector<FragmentCollisionRect> rectangles);

    eve::pixelworld::PixelFragment fragment_;
    eve::physics::PhysicsLink link_;
    std::vector<FragmentCollisionRect> rectangles_;
    bool rasterized_ = false;
    bool physicsReleased_ = false;
};

/**
 * @brief Incremental static collision projection of authoritative PixelWorld chunks.
 *
 * The cache owns only process-local PhysicsLinks and revision metadata. Physics World
 * owns all bodies/fixtures and PixelWorld owns all material cells. Sync is owner-thread
 * affine, invokes no callbacks, and stages every replacement body before committing.
 */
class PixelTerrainCollisionCache {
public:
    PixelTerrainCollisionCache();
    ~PixelTerrainCollisionCache();
    PixelTerrainCollisionCache(PixelTerrainCollisionCache&&) noexcept;
    PixelTerrainCollisionCache& operator=(PixelTerrainCollisionCache&&) noexcept;
    PixelTerrainCollisionCache(const PixelTerrainCollisionCache&) = delete;
    PixelTerrainCollisionCache& operator=(const PixelTerrainCollisionCache&) = delete;

    /**
     * @brief Rebuild only chunks newer than the cached source revision.
     * @param physicsWorld Borrowed owner of static bodies.
     * @param pixelWorld Borrowed authoritative material world.
     * @param maximumFixturesPerChunk Positive deterministic complexity budget.
     * @return Applied counters; failure destroys staged candidates and preserves prior cache entries.
     */
    [[nodiscard]] eve::Result<TerrainCollisionSyncReceipt> sync(
        eve::physics::World& physicsWorld, const eve::pixelworld::PixelWorld& pixelWorld,
        std::uint32_t maximumFixturesPerChunk = 256);

    /** @brief Explicitly destroy every resolvable projected body and reset revision tracking. */
    [[nodiscard]] eve::Result<void> clearPhysics(eve::physics::World& physicsWorld);
    /** @brief Last authoritative revision fully represented by this cache. */
    [[nodiscard]] std::uint64_t sourceRevision() const noexcept;
    /** @brief Number of chunks currently owning a non-empty static collision body. */
    [[nodiscard]] std::size_t bodyCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::pixelworld_physics
