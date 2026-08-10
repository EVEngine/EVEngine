#pragma once

#include "map/TileLayer.h"

#include <memory>
#include <string>

namespace eve::map {

/**
 * Dynamic field-of-view / fog-of-war facade (Phase A: 2D grid).
 * Multi-revealer recursive shadowcasting + explored memory.
 * Opaque semantics are separate from Pathfinder walkability.
 */
class Fov {
public:
    Fov();
    explicit Fov(TileLayer *layer);
    explicit Fov(int width, int height);
    ~Fov();

    Fov(Fov &&) noexcept;
    Fov &operator=(Fov &&) noexcept;
    Fov(const Fov &) = delete;
    Fov &operator=(const Fov &) = delete;

    void bindLayer(TileLayer *layer);
    void setSize(int width, int height);
    int getWidth() const;
    int getHeight() const;

    /** "shadowcast" (default). Unknown names fall back to shadowcast. */
    void setAlgorithm(const std::string &name);
    std::string getAlgorithm() const;

    /** "euclidean" (default) | "chebyshev" | "manhattan" */
    void setRadiusMetric(const std::string &name);
    std::string getRadiusMetric() const;

    /** Reserved for Phase B peek tweaks; stored for script parity. */
    void setCornerPeek(bool enable);
    bool getCornerPeek() const;

    void blockOpaqueGid(int gid);
    void unblockOpaqueGid(int gid);
    void clearOpaqueGids();
    void setBlockEmpty(bool enable);
    bool getBlockEmpty() const;
    void setOpaque(int x, int y, bool opaque);
    bool isOpaque(int x, int y) const;
    void syncFromLayer();

    /** Returns revealer id (>= 1). radius < 0 treated as 0. */
    int addRevealer(int x, int y, int radius);
    void removeRevealer(int id);
    void clearRevealers();
    void setRevealerPosition(int id, int x, int y);
    void setRevealerRadius(int id, int radius);
    /** Cone: facingDeg 0 = +X, degrees CCW toward +Y; halfAngleDeg is half-width. */
    void setRevealerFacing(int id, float facingDeg, float halfAngleDeg);
    void clearRevealerFacing(int id);
    void setRevealerEnabled(int id, bool enabled);
    int getRevealerCount() const;

    void compute();
    bool isVisible(int x, int y) const;
    bool isExplored(int x, int y) const;
    /** "unknown" | "explored" | "visible" */
    std::string getState(int x, int y) const;
    void clearMemory();
    void resetVisibleOnly();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::map
