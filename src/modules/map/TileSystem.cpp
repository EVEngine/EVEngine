#include "map/TileSystem.h"
#include "map/TileLayer.h"
#include "map/TileConfig.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Canvas.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <algorithm>
#include <vector>

namespace eve::map {
namespace {

float clampZoom(float z) { return z <= 0.f ? 1e-4f : z; }

struct ViewCam {
    float x = 0.f;
    float y = 0.f;
    float zoom = 1.f;
    bool valid = false;
};

ViewCam fromEntity(graphics::Camera2D *ent) {
    ViewCam v;
    if (!ent) return v;
    auto d = ent->data();
    v.valid = true;
    v.x = d->x;
    v.y = d->y;
    v.zoom = d->zoom;
    return v;
}

void applyCamera(float wx, float wy, float ww, float wh, const ViewCam &cam, int viewW, int viewH,
                 float &sx, float &sy, float &sw, float &sh) {
    if (!cam.valid) {
        sx = wx;
        sy = wy;
        sw = ww;
        sh = wh;
        return;
    }
    const float z = clampZoom(cam.zoom);
    sx = (wx - cam.x) * z + float(viewW) * 0.5f;
    sy = (wy - cam.y) * z + float(viewH) * 0.5f;
    sw = ww * z;
    sh = wh * z;
}

/** Deterministic debug color when no tileset texture is bound. */
Color solidForGid(uint32_t gid, const Color &tint) {
    const uint32_t h = gid * 2654435761u;
    const float r = ((h >> 0) & 255u) / 255.f;
    const float g = ((h >> 8) & 255u) / 255.f;
    const float b = ((h >> 16) & 255u) / 255.f;
    return Color{r * tint.r, g * tint.g, b * tint.b, tint.a};
}

bool atlasUV(const TileLayer::Tileset &ts, uint32_t gid, float &u0, float &v0, float &u1,
             float &v1) {
    if (!ts.texture || ts.columns <= 0 || ts.tileW <= 0 || ts.tileH <= 0) return false;
    if (gid < uint32_t(ts.firstGid)) return false;
    const int local = int(gid) - ts.firstGid;
    const int col = local % ts.columns;
    const int row = local / ts.columns;
    const float iw = float(ts.texture->getWidth());
    const float ih = float(ts.texture->getHeight());
    if (iw <= 0.f || ih <= 0.f) return false;

    const float px = float(ts.margin + col * (ts.tileW + ts.spacing));
    const float py = float(ts.margin + row * (ts.tileH + ts.spacing));
    u0 = px / iw;
    v0 = py / ih;
    u1 = (px + float(ts.tileW)) / iw;
    v1 = (py + float(ts.tileH)) / ih;
    return true;
}

void drawLayer(const TileLayer::Config &cfg, const TileLayer::Tiles &tiles,
               const TileLayer::Tileset &ts, const TileLayer::Draw &draw, graphics::Graphics *gfx,
               const ViewCam &cam) {
    if (!gfx || !draw.visible || cfg.mapW <= 0 || cfg.mapH <= 0) return;
    if (tiles.gids.size() < size_t(cfg.mapW * cfg.mapH)) return;

    const int viewW = draw.canvas ? draw.canvas->getWidth() : gfx->getWidth();
    const int viewH = draw.canvas ? draw.canvas->getHeight() : gfx->getHeight();
    const Color &tint = draw.tint;

    for (int ty = 0; ty < cfg.mapH; ++ty) {
        for (int tx = 0; tx < cfg.mapW; ++tx) {
            const uint32_t raw = tiles.gids[size_t(ty * cfg.mapW + tx)];
            const uint32_t gid = tileGid(raw);
            if (gid == 0) continue;

            const float wx = cfg.originX + float(tx) * cfg.tileW;
            const float wy = cfg.originY + float(ty) * cfg.tileH;
            float sx, sy, sw, sh;
            applyCamera(wx, wy, cfg.tileW, cfg.tileH, cam, viewW, viewH, sx, sy, sw, sh);

            float u0, v0, u1, v1;
            if (atlasUV(ts, gid, u0, v0, u1, v1)) {
                gfx->drawTexturedRectUV(ts.texture, sx, sy, sw, sh, u0, v0, u1, v1, tint);
            } else {
                gfx->drawSolidRect(sx, sy, sw, sh, solidForGid(gid, tint));
            }
        }
    }
}

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

}  // namespace

void TileRenderSystem::render(graphics::Graphics *gfx) {
    if (!gfx) return;
    if (ecs::ComponentManager<TileLayer>::inst().registy == nullptr) return;

    struct Item {
        TileLayer::Config *cfg;
        TileLayer::Tiles *tiles;
        TileLayer::Tileset *ts;
        TileLayer::Draw *draw;
        ViewCam cam;
    };
    std::vector<Item> items;

    auto view = ecs::View<TileLayer, TileLayer::Config, TileLayer::Tiles, TileLayer::Tileset,
                          TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, tiles, ts, draw] = *it;
        if (!draw->visible) continue;
        items.push_back(Item{cfg, tiles, ts, draw, fromEntity(draw->camera)});
    }

    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const bool aOff = a.draw->canvas != nullptr;
        const bool bOff = b.draw->canvas != nullptr;
        if (aOff != bOff) return aOff && !bOff;
        if (a.draw->canvas != b.draw->canvas) return a.draw->canvas < b.draw->canvas;
        return a.draw->layer < b.draw->layer;
    });

    graphics::Canvas *current = reinterpret_cast<graphics::Canvas *>(static_cast<uintptr_t>(1));
    for (const Item &it : items) {
        graphics::Canvas *next = it.draw->canvas;
        if (next != current) {
            gfx->setCanvas(next);
            current = next;
        }
        drawLayer(*it.cfg, *it.tiles, *it.ts, *it.draw, gfx, it.cam);
    }
    if (current != nullptr) gfx->setCanvas();
}

int TileConfigSystem::poll() {
    if (ecs::ComponentManager<TileLayer>::inst().registy == nullptr) return 0;

    int reloaded = 0;
    auto view = ecs::View<TileLayer, TileLayer::Config, TileLayer::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!res->autoReload || res->path.empty() || !cfg->entity) continue;

        const int64_t mt = fileModtime(res->path);
        if (mt < 0 || mt == res->modtime) continue;
        if (reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded;
}

}  // namespace eve::map
