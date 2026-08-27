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
//                   frame (cylindrical billboard, upright), so it composites
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
 * Coordinates map the layer's tileToWorld() X/Z plane onto world X/Z; the box
 * footprint per tile is tileW × tileH world units. The top face of every
 * non-empty tile is textured with the tile's atlas region from the layer's
 * tileset; the side walls sample a configurable wall region of the same atlas
 * (default: a small region at the atlas origin). Per-tile elevation is read
 * from the tile's "height" custom data (0 when absent); each tile spans from
 * `elevation` down to `elevation - sideDepth`.
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
 * A unit quad is drawn through the alpha-cutout G-buffer + shadow extra-drawer
 * path (graphics/RenderSystem3D.h), so transparent sprite pixels discard and
 * the sprite casts a silhouette shadow like other billboard/card geometry. The
 * billboard is oriented toward the active camera each frame.
 *
 * 2D sprite-sheet animation: setFrameGrid(cols, rows) + setFrameIndex / play
 * advance a frame index; update(dt) steps the clock. This lets existing 2D
 * character animations run in 3D.
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

    /** @brief Configure a sprite-sheet frame grid (cell frame 0 = top-left). */
    void setFrameGrid(int columns, int rows);
    int getFrameGridColumns() const;
    int getFrameGridRows() const;
    /** @brief Jump to a grid frame index; wraps within the grid. */
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
     * @param dt Seconds elapsed since the last update.
     */
    void update(float dt);

    /** @brief Camera the billboard faces; null disables auto-orientation. */
    void setCamera(graphics::Camera3D *camera);

    /** @brief World position (billboard center). */
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
    float tintR_ = 1.f, tintG_ = 1.f, tintB_ = 1.f, tintA_ = 1.f;
    bool visible_ = true;
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
};

}  // namespace eve::hd2d