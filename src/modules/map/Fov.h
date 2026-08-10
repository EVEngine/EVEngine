#pragma once

#include "map/TileLayer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::map {

/**
 * Dynamic field-of-view / fog-of-war facade.
 * Phase A: 2D shadowcast + multi-revealer + explored memory.
 * Phase B: raycast/permissive, heightmap, volume (slice RSC + vertical),
 *          dirty/incremental visible-list reset, CPU mask export.
 */
class Fov {
public:
    Fov();
    explicit Fov(TileLayer *layer);
    explicit Fov(int width, int height);
    Fov(int width, int height, int depth);
    ~Fov();

    Fov(Fov &&) noexcept;
    Fov &operator=(Fov &&) noexcept;
    Fov(const Fov &) = delete;
    Fov &operator=(const Fov &) = delete;

    void bindLayer(TileLayer *layer);
    void setSize(int width, int height);
    void setVolumeSize(int width, int height, int depth);
    int getWidth() const;
    int getHeight() const;
    int getDepth() const;

    /** "grid2d" (default) | "heightmap" | "volume" */
    void setMode(const std::string &name);
    std::string getMode() const;

    /** "shadowcast" (default) | "raycast" | "permissive" */
    void setAlgorithm(const std::string &name);
    std::string getAlgorithm() const;

    /** "euclidean" (default) | "chebyshev" | "manhattan" */
    void setRadiusMetric(const std::string &name);
    std::string getRadiusMetric() const;

    void setCornerPeek(bool enable);
    bool getCornerPeek() const;

    void blockOpaqueGid(int gid);
    void unblockOpaqueGid(int gid);
    void clearOpaqueGids();
    void setBlockEmpty(bool enable);
    bool getBlockEmpty() const;
    void setOpaque(int x, int y, bool opaque);
    bool isOpaque(int x, int y) const;
    void setOpaque3(int x, int y, int z, bool opaque);
    bool isOpaque3(int x, int y, int z) const;
    void syncFromLayer();

    /** heightmap */
    void setElevation(int x, int y, float elev);
    float getElevation(int x, int y) const;
    void setCliffBlock(float delta);
    float getCliffBlock() const;
    void setEyeOffset(float offset);
    float getEyeOffset() const;

    /** volume vertical extend distance (voxels). Default = depth. */
    void setVerticalRange(int range);
    int getVerticalRange() const;

    /** Returns revealer id (>= 1). */
    int addRevealer(int x, int y, int radius);
    int addRevealer3(int x, int y, int z, int radius);
    void removeRevealer(int id);
    void clearRevealers();
    void setRevealerPosition(int id, int x, int y);
    void setRevealerPosition3(int id, int x, int y, int z);
    void setRevealerRadius(int id, int radius);
    void setRevealerFacing(int id, float facingDeg, float halfAngleDeg);
    void clearRevealerFacing(int id);
    void setRevealerEnabled(int id, bool enabled);
    int getRevealerCount() const;

    void markDirty();
    bool isDirty() const;
    void compute();
    bool isVisible(int x, int y) const;
    bool isExplored(int x, int y) const;
    bool isVisible3(int x, int y, int z) const;
    bool isExplored3(int x, int y, int z) const;
    /** "unknown" | "explored" | "visible" */
    std::string getState(int x, int y) const;
    std::string getState3(int x, int y, int z) const;
    void clearMemory();
    void resetVisibleOnly();

    /**
     * Mask helpers for CPU FoW overlays.
     * visible=1, explored≈0.35, unknown=0 (byte: 255 / 89 / 0).
     */
    float getMaskValue(int x, int y) const;
    int getMaskByte(int x, int y) const;
    float getMaskValue3(int x, int y, int z) const;
    int getMaskByte3(int x, int y, int z) const;
    /**
     * Fill tightly packed R8 buffer of size width*height (2D / z-slice).
     * Returns false if out size mismatch. sliceZ ignored in non-volume modes.
     */
    bool fillMaskR8(std::vector<uint8_t> &out) const;
    bool fillMaskR8Slice(std::vector<uint8_t> &out, int sliceZ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::map
