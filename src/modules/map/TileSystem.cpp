#include "map/TileSystem.h"
#include "map/TileLayer.h"
#include "map/TileConfig.h"
#include "map/TileProjection.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Canvas.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <vector>

namespace eve::map {
namespace {

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

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

}  // namespace

void TileRenderSystem::collect(std::vector<graphics::DrawItem2D> &out) {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;

    auto view = ecs::View<TileLayer, TileLayer::Config, TileLayer::Tiles, TileLayer::Tileset,
                          TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, tiles, ts, draw] = *it;
        if (!draw->visible || cfg->mapW <= 0 || cfg->mapH <= 0) continue;
        if (tiles->gids.size() < size_t(cfg->mapW * cfg->mapH)) continue;

        const Color &tint = draw->tint;
        const ViewCam cam = fromEntity(draw->camera);

        for (int ty = 0; ty < cfg->mapH; ++ty) {
            for (int tx = 0; tx < cfg->mapW; ++tx) {
                const uint32_t raw = tiles->gids[size_t(ty * cfg->mapW + tx)];
                const uint32_t gid = tileGid(raw);
                if (gid == 0) continue;

                graphics::DrawItem2D item;
                tileToWorld(*cfg, tx, ty, item.x, item.y);
                item.w = cfg->tileW;
                item.h = cfg->tileH;
                item.depthY = tileToDepthY(*cfg, tx, ty);
                item.layer = draw->layer;
                item.canvas = draw->canvas;
                item.camera = draw->camera;
                item.camValid = cam.valid;
                item.camX = cam.x;
                item.camY = cam.y;
                item.camZoom = cam.zoom;
                item.receiveLight = false;
                item.litPath = false;

                float u0, v0, u1, v1;
                if (atlasUV(*ts, gid, u0, v0, u1, v1)) {
                    item.texture = ts->texture;
                    item.hasUV = true;
                    item.u0 = u0;
                    item.v0 = v0;
                    item.u1 = u1;
                    item.v1 = v1;
                    item.color = tint;
                } else {
                    item.texture = nullptr;
                    item.color = solidForGid(gid, tint);
                }
                out.push_back(item);
            }
        }
    }
}

void TileRenderSystem::render(graphics::Graphics *gfx) {
    if (!gfx) return;
    std::vector<graphics::DrawItem2D> items;
    collect(items);
    graphics::RenderSystem::drawItems(*gfx, items, false);
}

int TileConfigSystem::poll() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return 0;

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
