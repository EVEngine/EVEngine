#pragma once

#include "map/TileLayer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::map {

/**
 * @brief Dynamic field-of-view / fog-of-war facade.
 * Phase A: 2D shadowcast + multi-revealer + explored memory.
 * Phase B: raycast/permissive, heightmap, volume, dirty-skip, CPU masks.
 * Phase C: hex topology FOV, rectangle-based FOV, perception/stealth
 *          detection helpers, GPU mask Texture upload.
 */
class Fov {
public:
    /** @brief Owning visibility-memory snapshot for the current grid/volume dimensions. */
    struct Snapshot {
        int width = 0;
        int height = 0;
        int depth = 1;
        std::vector<uint8_t> states;
    };
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

    /** @brief "grid2d" (default) | "heightmap" | "volume" */
    void setMode(const std::string &name);
    std::string getMode() const;

    /** @brief "shadowcast" | "raycast" | "permissive" | "rectangle" */
    void setAlgorithm(const std::string &name);
    std::string getAlgorithm() const;

    /** @brief "euclidean" | "chebyshev" | "manhattan" — ignored when topology is hex (cube distance). */
    void setRadiusMetric(const std::string &name);
    std::string getRadiusMetric() const;

    /** @brief "ortho" (default) | "hex" | "auto" (hex if bound layer is hex/staggered). */
    void setTopology(const std::string &name);
    std::string getTopology() const;

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

    void setElevation(int x, int y, float elev);
    float getElevation(int x, int y) const;
    void setCliffBlock(float delta);
    float getCliffBlock() const;
    void setEyeOffset(float offset);
    float getEyeOffset() const;

    void setVerticalRange(int range);
    int getVerticalRange() const;

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

    /**
     * @brief Soft RPG hooks (no hard dependency on rpg module).
     * effectiveRadius = radius + floor(perception * perceptionRadiusScale)
     * canDetect: cell visible AND perception + detectionMargin >= targetStealth
     */
    void setRevealerPerception(int id, float perception);
    float getRevealerPerception(int id) const;
    void setPerceptionRadiusScale(float scale);
    float getPerceptionRadiusScale() const;
    void setDetectionMargin(float margin);
    float getDetectionMargin() const;
    int getEffectiveRadius(int id) const;
    bool canDetect(int revealerId, int x, int y, float targetStealth) const;
    bool canDetect3(int revealerId, int x, int y, int z, float targetStealth) const;

    void markDirty();
    bool isDirty() const;
    void compute();
    bool isVisible(int x, int y) const;
    bool isExplored(int x, int y) const;
    bool isVisible3(int x, int y, int z) const;
    bool isExplored3(int x, int y, int z) const;
    std::string getState(int x, int y) const;
    std::string getState3(int x, int y, int z) const;
    void clearMemory();
    void resetVisibleOnly();
    /** @brief Capture exact unknown/explored/visible cell states without revealers. */
    [[nodiscard]] Snapshot snapshot() const;
    /** @brief Restore exact cell states when dimensions and payload size match. */
    [[nodiscard]] bool restore(const Snapshot &snapshot);

    float getMaskValue(int x, int y) const;
    int getMaskByte(int x, int y) const;
    float getMaskValue3(int x, int y, int z) const;
    int getMaskByte3(int x, int y, int z) const;
    bool fillMaskR8(std::vector<uint8_t> &out) const;
    bool fillMaskR8Slice(std::vector<uint8_t> &out, int sliceZ) const;

    /**
     * @brief Upload current 2D / slice mask as RGBA8 Texture (R=G=B=A=mask byte).
     * Caller owns the returned Texture*. Null gfx / empty grid → nullptr.
     */
    graphics::Texture *buildMaskTexture(graphics::Graphics *gfx) const;
    graphics::Texture *buildMaskTextureSlice(graphics::Graphics *gfx, int sliceZ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::map
