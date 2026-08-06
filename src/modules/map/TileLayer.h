#pragma once

#include "common/ECS.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Camera2D;
class Canvas;
}  // namespace eve::graphics

namespace eve::map {

/**
 * ECS tile layer entity. Script mutates tile GIDs / tileset / draw; TileRenderSystem
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
        float originX = 0.f;
        float originY = 0.f;
        TileLayer *entity = nullptr;
    };

    struct Tiles {
        std::vector<uint32_t> gids;
    };

    struct Tileset {
        graphics::Texture *texture = nullptr;
        int firstGid = 1;
        int columns = 1;
        int tileW = 32;
        int tileH = 32;
        int margin = 0;
        int spacing = 0;
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

    void setOrigin(float x, float y);
    float getX();
    float getY();

    int getMapWidth();
    int getMapHeight();
    float getTileWidth();
    float getTileHeight();
    void setTileSize(float tileW, float tileH);

    void resize(int mapW, int mapH);

    void setTile(int tx, int ty, int gid);
    int getTile(int tx, int ty);
    void fill(int gid);
    void clear();

    void setTileset(graphics::Texture *texture, int firstGid, int columns, int margin = 0,
                    int spacing = 0);
    void setTilesetTileSize(int tileW, int tileH);
    graphics::Texture *getTilesetTexture();
    int getTilesetFirstGid();
    int getTilesetColumns();

    void setCanvas(graphics::Canvas *canvas);
    void setCamera(graphics::Camera2D *camera);

    void setLayer(int layer);
    int getLayer();

    void setVisible(bool visible);
    bool isVisible();

    void setTint(float r, float g, float b, float a = 1.f);

    bool applyConfig(const std::string &json);
    bool loadConfig(const std::string &path);
    bool reloadConfig();
    void setAutoReload(bool enable);
    bool getAutoReload();
    std::string getConfigPath();
};

/** Strip Tiled flip / rotate flags; keep low 28 bits. */
inline uint32_t tileGid(uint32_t raw) { return raw & 0x0FFFFFFFu; }

}  // namespace eve::map
