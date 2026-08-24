#pragma once

#include "common/ECS.h"
#include "common/RenderTypes.h"
#include "map/TileOrientation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Camera2D;
class Canvas;
class Texture;
}  // namespace eve::graphics

namespace eve::map {

using eve::Color;

/**
 * @brief ECS tile layer entity. Script mutates tile GIDs / tileset / draw; TileRenderSystem
 * batch-draws atlas quads (or solid debug colors when no texture).
 *
 * GID 0 = empty. Tiled flip flags (high bits) are masked on read/draw.
 */
class TileLayer : public ecs::Entity {
public:
    ENTITY(TileLayer, ecs::Entity)

    void release() override {}

    struct Config {
        int mapW = 0;
        int mapH = 0;
        float tileW = 32.f;
        float tileH = 32.f;
        float cellGapX = 0.f;
        float cellGapY = 0.f;
        float originX = 0.f;
        float originY = 0.f;
        MapOrientation orientation = MapOrientation::Orthogonal;
        StaggerAxis staggerAxis = StaggerAxis::Y;
        StaggerIndex staggerIndex = StaggerIndex::Odd;
        float hexSideLength = 0.f;
        TileLayer *entity = nullptr;
    };

    struct Tiles {
        std::vector<uint32_t> gids;
    };

    struct Tileset {
        /** @brief Per-GID visual metadata emitted by a project-defined asset pipeline. */
        struct Visual {
            int gid = 0;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            float pivotX = 0.f;
            float pivotY = 0.f;
            float sortBias = 0.f;
            int footprintW = 1;
            int footprintH = 1;
            bool walkable = true;
            float cost = 1.f;
        };

        graphics::Texture *texture = nullptr;
        int firstGid = 1;
        int columns = 1;
        int tileW = 32;
        int tileH = 32;
        int margin = 0;
        int spacing = 0;
        std::vector<Visual> visuals;
    };

    struct Draw {
        graphics::Canvas *canvas = nullptr;
        graphics::Camera2D *camera = nullptr;
        int layer = 0;
        bool visible = true;
        Color tint{1.f, 1.f, 1.f, 1.f};
    };

    struct Resource {
        std::string path;
        std::string texturePath;
        int64_t modtime = -1;
        bool autoReload = true;
    };

    COMPONENT(Config, config)
    COMPONENT(Tiles, tiles)
    COMPONENT(Tileset, tileset)
    COMPONENT(Draw, draw)
    COMPONENT(Resource, resource)

    static TileLayer *createLayer(int mapW, int mapH, float tileW = 32.f, float tileH = 32.f);

    /** @brief World origin of the layer (pixels). */
    void setOrigin(float x, float y);
    float getX();
    float getY();

    /** @brief Map dimensions in tiles. */
    int getMapWidth();
    int getMapHeight();
    /** @brief Tile size in pixels. */
    float getTileWidth();
    float getTileHeight();
    void setTileSize(float tileW, float tileH);
    /** @brief Sets extra world-space distance between projected cells. */
    void setCellGap(float gapX, float gapY);
    /** @brief Sets projected cell pitch as a multiple of the logical tile size. */
    void setRenderSpacing(float spacingX, float spacingY);
    /** @brief Horizontal and vertical world-space gaps between projected cells. */
    float getCellGapX();
    float getCellGapY();
    /** @brief Horizontal and vertical projected pitch multipliers. */
    float getRenderSpacingX();
    float getRenderSpacingY();

    /** @brief Resizes the tile grid (existing GIDs preserved where possible). */
    void resize(int mapW, int mapH);

    /** @brief Tile GID access; 0 = empty. */
    void setTile(int tx, int ty, int gid);
    int getTile(int tx, int ty);
    /** @brief Fills the whole layer with one GID. */
    void fill(int gid);
    /** @brief Clears the layer to empty. */
    void clear();

    /** @brief Binds the atlas texture and tile table. */
    void setTileset(graphics::Texture *texture, int firstGid, int columns, int margin = 0,
                    int spacing = 0);
    /** @brief Tile size in pixels of the bound tileset. */
    void setTilesetTileSize(int tileW, int tileH);
    /**
     * @brief Defines or replaces one irregular atlas region.
     * @param gid Global tile id.
     * @param x Region left in atlas pixels.
     * @param y Region top in atlas pixels.
     * @param width Region width in atlas pixels.
     * @param height Region height in atlas pixels.
     * @param pivotX Region-space point aligned with the projected tile origin.
     * @param pivotY Region-space point aligned with the projected tile origin.
     * @param sortBias Extra painter-order offset in pixels.
     */
    void setTileVisual(int gid, int x, int y, int width, int height, float pivotX,
                       float pivotY, float sortBias = 0.f);
    /** @brief Removes all irregular per-GID visual metadata. */
    void clearTileVisuals();
    /** @brief Number of irregular visual records. */
    int getTileVisualCount();
    /** @brief Configures gameplay metadata kept beside a visual record. */
    void setTileMetadata(int gid, int footprintW, int footprintH, bool walkable,
                         float cost = 1.f);
    /** @brief Loads an extensible TileSet manifest without changing map cell data. */
    bool loadTilesetManifest(const std::string &path);
    /** @brief Bound tileset accessors. */
    graphics::Texture *getTilesetTexture();
    int getTilesetFirstGid();
    int getTilesetColumns();

    /** @brief Draw target canvas / camera. */
    void setCanvas(graphics::Canvas *canvas);
    void setCamera(graphics::Camera2D *camera);

    /** @brief Draw order layer / visibility / tint. */
    void setLayer(int layer);
    int getLayer();

    void setVisible(bool visible);
    bool isVisible();

    void setTint(float r, float g, float b, float a = 1.f);

    /** @brief Applies a map JSON config (Config + Tiles + Tileset + Draw). */
    bool applyConfig(const std::string &json);
    /** @brief Loads a config file and binds it for hot reload. */
    bool loadConfig(const std::string &path);
    /** @brief Re-reads the bound config file (hot reload). */
    bool reloadConfig();
    /** @brief Enables/disables automatic config hot reload. */
    void setAutoReload(bool enable);
    bool getAutoReload();
    /** @brief Path of the bound config file. */
    std::string getConfigPath();

    /** @brief Tile-index ↔ world-pixel conversions (per layer orientation). */
    void tileToWorld(int tx, int ty, float &wx, float &wy);
    /** @brief Painter's-algorithm depth for 2.5D draw ordering. */
    float depthY(int tx, int ty);
    void worldToTile(float wx, float wy, int &tx, int &ty);

    /** @brief Scalar conversion helpers. */
    float tileToWorldX(int tx, int ty);
    float tileToWorldY(int tx, int ty);
    float depthYAt(int tx, int ty);
    int worldToTileX(float wx, float wy);
    int worldToTileY(float wx, float wy);
};

/** @brief Strip Tiled flip / rotate flags; keep low 28 bits. */
inline uint32_t tileGid(uint32_t raw) { return raw & 0x0FFFFFFFu; }

}  // namespace eve::map
