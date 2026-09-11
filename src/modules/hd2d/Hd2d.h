#pragma once

// HD-2D module: renders 2D tilemaps and 2D sprite characters inside the 3D
// scene. Both techniques layer on top of the existing graphics 3D pipeline
// (GBuffer + CSM shadows):
//
//   * TileMap3D  -- extrudes a map::TileLayer into a 3D relief mesh. Every
//                   non-empty tile becomes a box whose top face shows the
//                   tile's atlas region and whose side walls extrude down to
//                   the configured depth (or to the per-tile elevation read
//                   from tile metadata). Handed to a graphics::Renderable3D so
//                   it picks up depth, shadows and GBuffer automatically.
//   * Sprite3D   -- a camera-facing billboard quad in the 3D world carrying a
//                   2D texture (a character / animation frame). It is an ECS
//                   graphics::Renderable3D oriented toward the camera each
//                   frame (screen-aligned billboard), so it composites
//                   into the forward pass and casts shadows like any mesh.
//                   Supports a sprite-sheet frame grid so existing 2D
//                   animations play in 3D.
//
// Script binding: eve.Hd2d (slot hd2d). See module_manifest.cmake.

#include "common/Module.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Camera3D;
class Graphics;
class Mesh;
class Renderable3D;
class Texture;
}  // namespace eve::graphics
namespace eve::map { class TileLayer; }

namespace eve::hd2d {

/**
 * @brief Extrudes a 2D tile layer into a 3D terrain mesh (HD-2D ground).
 *
 * Coordinates map the layer's tileToWorld() top-left origin onto world X/Z;
 * the footprint extends by tileW × tileH
 * in positive X/Z. The top face of every
 * non-empty tile is textured with the tile's atlas region from the layer's
 * tileset; the side walls sample a configurable wall region of the same atlas
 * (default: a small region at the atlas origin). Per-tile elevation is read
 * from the tile's "height" custom data (0 when absent); each tile spans from
 * `elevation` down to `elevation - sideDepth`. Zero depth emits only the top
 * face. Tiled
 * diagonal/horizontal/vertical flags transform the top UVs.
 */
class TileMap3D {
public:
    TileMap3D() = default;
    TileMap3D(const TileMap3D &) = delete;
    TileMap3D &operator=(const TileMap3D &) = delete;

    /** @brief Downward side-wall depth in world units (default 6). */
    void setSideDepth(float depth);
    float getSideDepth() const;
    /** @brief World units per unit of tile "height" metadata (default 1). */
    void setHeightScale(float scale);
    float getHeightScale() const;
    /** @brief UV sub-rectangle of the atlas used for extruded side walls. */
    void setWallUV(float u0, float v0, float u1, float v1);
    /** @brief Tint multiplier applied to the whole terrain. */
    void setTint(float r, float g, float b, float a = 1.f);

    /**
     * @brief Build and upload the terrain mesh from a tile layer.
     * @param gfx Active Graphics (owns the returned mesh).
     * @param layer Non-null TileLayer; its tileset must be bound.
     * @return Uploaded mesh (owner = gfx). Empty tiles produce a null mesh.
     */
    graphics::Mesh *buildMesh(graphics::Graphics *gfx, map::TileLayer *layer);

    /**
     * @brief Build a scene-ready Renderable3D carrying the extruded terrain.
     * The returned entity is already registered in the ECS and will draw once a
     * Camera3D is active; caller sets its position/rotation/scale.
     */
    graphics::Renderable3D *buildRenderable(graphics::Graphics *gfx, map::TileLayer *layer);

    /** @brief Number of box tiles baked by the last build. */
    int getTileCount() const { return tileCount_; }

private:
    float sideDepth_ = 6.f;
    float heightScale_ = 1.f;
    float wallU0_ = 0.f, wallV0_ = 0.f, wallU1_ = 0.05f, wallV1_ = 0.05f;
    float tintR_ = 1.f, tintG_ = 1.f, tintB_ = 1.f, tintA_ = 1.f;
    int tileCount_ = 0;
};

/**
 * @brief Camera-facing 2D sprite billboard rendered inside the 3D scene.
 *
 * A unit quad uses an ECS Renderable3D with a Graphics-owned masked material
 * in the forward, G-buffer and shadow
 * paths, so transparent pixels discard and
 * the sprite casts a silhouette shadow like other billboard/card geometry.
 * The billboard is oriented toward the attached camera each frame. Graphics owns
 * its mesh, material and texture;
 * they must outlive the sprite. Calls belong
 * to the render thread and do not invoke user callbacks.
 * 2D
 * sprite-sheet animation: setFrameGrid(cols, rows) + setFrameIndex / play advance a frame index; update(dt) steps the
 * clock. This lets existing 2D character animations run in 3D.
 */
class Sprite3D {
public:
    Sprite3D();
    ~Sprite3D();
    Sprite3D(const Sprite3D &) = delete;
    Sprite3D &operator=(const Sprite3D &) = delete;

    /** @brief Albedo texture (a sprite frame / sheet / animation image). */
    void setTexture(graphics::Texture *texture);
    graphics::Texture *getTexture() const;

    /** @brief Direct atlas sub-rect frame selection (0..1 UV). */
    void setFrame(float u0, float v0, float u1, float v1);
    void getFrame(float &u0, float &v0, float &u1, float &v1) const;
    /** @brief Flip the sampled frame horizontally / vertically. */
    void setFlipX(bool flip);
    void setFlipY(bool flip);

    /** @brief Configure a sprite-sheet grid (frame 0 = top-left); stops the previous clip. */
    void setFrameGrid(int columns, int rows);
    int getFrameGridColumns() const;
    int getFrameGridRows() const;
    /** @brief Jump to a grid frame index, clamped within the grid. */
    void setFrameIndex(int index);
    int getFrameIndex() const;
    /** @brief Total grid frames. */
    int getFrameCount() const;
    /**
     * @brief Run a frame animation.
     * @param start First grid frame.
     * @param end   Inclusive last grid frame.
     * @param fps   Frames per second (must be > 0).
     */
    void play(int start, int end, float fps);
    void stop();
    bool isPlaying() const;
    /**
     * @brief Advance the animation clock and re-orient the billboard toward the
     * attached camera. Call once per frame.
     * @param dt Seconds elapsed since the last update. Non-positive and non-finite
     * values do not advance
     * animation; camera orientation is still refreshed.
     */
    void update(float dt);

    /**
     * @brief Borrow the camera used for screen alignment, including pitch/roll.
     * @details Render-thread
     * only, no callbacks. The camera must outlive this
     * binding; detach with null before destroying it. Null or a
     * degenerate
     * camera basis preserves the last valid orientation. All sprites share the
     * camera
     * image-plane basis rather than pointing individually at its eye.
     */
    void setCamera(graphics::Camera3D *camera);

    /**
     * @brief Set the image-space pivot; (0,0) is visible top-left, (1,1) bottom-right.
     * @param x Finite
     * horizontal fraction; default 0.5.
     * @param y Finite vertical fraction; default 0.5. Use 1 for bottom
     * center.
     * @pre Both values are finite. Values outside [0,1] allow an external pivot.
     * @details
     * Render-thread only; no callbacks. Position remains fixed while the
     * geometry rotates/scales about this
     * pivot. UV flips do not move the pivot.
     */
    void setPivot(float x, float y);
    /** @brief World position of the pivot (center by default). */
    void setPosition(float x, float y, float z);
    float getPositionX() const;
    float getPositionY() const;
    float getPositionZ() const;
    /** @brief Billboard size in world units. */
    void setSize(float width, float height);
    float getWidth() const;
    float getHeight() const;
    /** @brief Color multiplier (alpha participates in cutout discards). */
    void setTint(float r, float g, float b, float a = 1.f);
    void setVisible(bool visible);
    bool getVisible() const;

    /**
     * @brief Alpha cutout threshold for the masked material (DopFix-style depth write).
     * @param cutoff Finite value clamped to [0,1]; default 0.5.
     */
    void setAlphaCutoff(float cutoff);
    float getAlphaCutoff() const;
    /**
     * @brief Whether opaque/cutout texels write depth (required for DOF focus).
     * Default true — matches HD2DURP DopFix / TransparentCutout materials.
     */
    void setDepthWrite(bool enabled);
    bool getDepthWrite() const;
    /** @brief Render both faces of the billboard quad (default true). */
    void setDoubleSided(bool enabled);
    bool getDoubleSided() const;
    /**
     * @brief Billboard orientation mode.
     * @param mode "screen" (default, full camera basis) or "yaw" (Y-up cylindrical).
     */
    void setBillboardMode(const std::string &mode);
    std::string getBillboardMode() const;

    graphics::Mesh *quadMesh() const { return quad_; }

private:
    friend class Hd2D;
    struct Anim {
        int start = 0, end = 0;
        float fps = 12.f;
        float clock = 0.f;
        bool playing = false;
    };

    void buildQuad(graphics::Graphics *gfx);
    void updateFrameUv();
    void orientToCamera();
    void syncMaterial();

    graphics::Graphics *gfx_ = nullptr;
    graphics::Texture *texture_ = nullptr;
    graphics::Mesh *quad_ = nullptr;
    graphics::Renderable3D *renderable_ = nullptr;
    graphics::Camera3D *camera_ = nullptr;

    // frame UV
    float u0_ = 0.f, v0_ = 0.f, u1_ = 1.f, v1_ = 1.f;
    bool flipX_ = false, flipY_ = false;

    // grid
    int gridCols_ = 1, gridRows_ = 1;
    int frameIndex_ = 0;
    Anim anim_;

    // placement
    float x_ = 0.f, y_ = 0.f, z_ = 0.f;
    float width_ = 1.f, height_ = 1.f;
    float pivotX_ = 0.5f, pivotY_ = 0.5f;
    float tintR_ = 1.f, tintG_ = 1.f, tintB_ = 1.f, tintA_ = 1.f;
    bool visible_ = true;
    float alphaCutoff_ = 0.5f;
    bool depthWrite_ = true;
    bool doubleSided_ = true;
    bool yawBillboard_ = false;
};

/**
 * @brief HD-2D presentation look inspired by HD2DURP (DOF + bloom + pixel sampling).
 *
 * Applies camera post settings for the miniature look and nearest-neighbor
 * filtering on sprite/terrain atlases. Depth-of-field needs Sprite3D masked
 * cutout / DopFix depth writes so the focus plane can resolve.
 */
class Hd2dLook {
public:
    Hd2dLook() = default;

    /** @brief View-space focus plane distance (default 18). */
    void setFocusDistance(float distance);
    float getFocusDistance() const { return focusDistance_; }
    /** @brief Max Gaussian blur radius in texels; 0 disables DOF (default 5). */
    void setMaxBlur(float blurPx);
    float getMaxBlur() const { return maxBlurPx_; }
    /** @brief Distance from focus where blur reaches the max (default 14). */
    void setFocusRange(float range);
    float getFocusRange() const { return focusRange_; }
    /** @brief HDR bloom intensity (default 0.35). */
    void setBloomIntensity(float intensity);
    float getBloomIntensity() const { return bloomIntensity_; }
    /** @brief HDR bloom threshold (default 1.2). */
    void setBloomThreshold(float threshold);
    float getBloomThreshold() const { return bloomThreshold_; }

    /**
     * @brief Write DOF + bloom onto a Camera3D (render-thread).
     * @param camera Non-null active scene camera.
     * @ownership `camera` is borrowed; Hd2dLook does not retain it.
     * @lifetime `camera` must remain valid only for this call.
     */
    void apply(graphics::Camera3D *camera) const;

    /**
     * @brief Point-filter a texture for crisp pixel art (nearest / no mip).
     * @param gfx Active Graphics.
     * @param texture Atlas or sprite sheet.
     * @ownership `gfx` and `texture` are borrowed; Hd2dLook does not retain them.
     * @lifetime Both arguments must remain valid only for this call.
     */
    void applyPixelSampler(graphics::Graphics *gfx, graphics::Texture *texture) const;

    /** @brief Strong miniature preset matching HD2DURP-style shallow DOF. */
    static Hd2dLook miniature();
    /** @brief Mild look with light bloom and modest DOF. */
    static Hd2dLook soft();

private:
    float focusDistance_ = 18.f;
    float maxBlurPx_ = 5.f;
    float focusRange_ = 14.f;
    float bloomIntensity_ = 0.35f;
    float bloomThreshold_ = 1.2f;
};

/**
 * @brief HD-2D module: tilemap-to-3D extrusion + 2D sprites as 3D billboards.
 */
class Hd2D : public Module {
public:
    Module_REG(Hd2D);
    Hd2D();
    ~Hd2D() override;

    /** @brief Create a tilemap-to-3D builder. */
    TileMap3D *newTileMap3D();
    /** @brief Create a 3D billboard sprite bound to the given Graphics. */
    Sprite3D *newSprite(graphics::Graphics *gfx);
    /**
     * @brief Create an HD-2D look preset with soft defaults.
     * @ownership Caller owns the returned object and must delete it.
     * @lifetime Valid until the caller deletes it; independent of Hd2D.
     */
    Hd2dLook *newLook();
    /**
     * @brief Create the miniature (strong DOF) look preset.
     * @ownership Caller owns the returned object and must delete it.
     * @lifetime Valid until the caller deletes it; independent of Hd2D.
     */
    Hd2dLook *newMiniatureLook();
};

}  // namespace eve::hd2d
