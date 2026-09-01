#include "hd2d/Hd2d.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "map/TileLayer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace eve::hd2d {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/**
 * Atlas UV rectangle (0..1) for a global tile id, honoring tileset layout
 * (columns / margin / spacing / tile size) against the atlas texture size.
 * Falls back to a full-tile UV when the texture size is unknown.
 */
void atlasUvForGid(const map::TileLayer::Tileset &set, uint32_t gid, float &u0, float &v0,
                   float &u1, float &v1) {
    const int cols = std::max(1, set.columns);
    const int tileW = std::max(1, set.tileW);
    const int tileH = std::max(1, set.tileH);
    const int margin = std::max(0, set.margin);
    const int spacing = std::max(0, set.spacing);
    const int cell = int(gid) - set.firstGid;
    if (cell < 0) {
        u0 = 0.f;
        v0 = 0.f;
        u1 = 1.f;
        v1 = 1.f;
        return;
    }
    const int cx = cell % cols;
    const int cy = cell / cols;
    const float texW = set.texture ? float(std::max(1, set.texture->getWidth())) : float(tileW);
    const float texH = set.texture ? float(std::max(1, set.texture->getHeight())) : float(tileH);
    const float px0 = float(margin + cx * (tileW + spacing));
    const float py0 = float(margin + cy * (tileH + spacing));
    u0 = px0 / texW;
    v0 = py0 / texH;
    u1 = (px0 + float(tileW)) / texW;
    v1 = (py0 + float(tileH)) / texH;
}

}  // namespace

// ---------------------------------------------------------------------------
// TileMap3D
// ---------------------------------------------------------------------------

void TileMap3D::setSideDepth(float depth) {
    if (depth < 0.f) throw eve::Exception("TileMap3D.setSideDepth: depth must be >= 0");
    sideDepth_ = depth;
}
float TileMap3D::getSideDepth() const { return sideDepth_; }
void TileMap3D::setHeightScale(float scale) {
    if (scale < 0.f) throw eve::Exception("TileMap3D.setHeightScale: scale must be >= 0");
    heightScale_ = scale;
}
float TileMap3D::getHeightScale() const { return heightScale_; }
void TileMap3D::setWallUV(float u0, float v0, float u1, float v1) {
    wallU0_ = u0;
    wallV0_ = v0;
    wallU1_ = u1;
    wallV1_ = v1;
}
void TileMap3D::setTint(float r, float g, float b, float a) {
    tintR_ = std::max(0.f, std::min(1.f, r));
    tintG_ = std::max(0.f, std::min(1.f, g));
    tintB_ = std::max(0.f, std::min(1.f, b));
    tintA_ = std::max(0.f, std::min(1.f, a));
}

graphics::Mesh *TileMap3D::buildMesh(graphics::Graphics *gfx, map::TileLayer *layer) {
    if (!gfx) throw eve::Exception("TileMap3D.buildMesh: null graphics");
    if (!layer) throw eve::Exception("TileMap3D.buildMesh: null tile layer");

    const int mapW = layer->getMapWidth();
    const int mapH = layer->getMapHeight();
    if (mapW <= 0 || mapH <= 0) throw eve::Exception("TileMap3D.buildMesh: empty tile layer");

    const float tileW = std::max(1.f, layer->getTileWidth());
    const float tileH = std::max(1.f, layer->getTileHeight());
    const float hx = tileW * 0.5f;
    const float hz = tileH * 0.5f;

    auto set = layer->tileset();  // ecs::ComponentRef<Tileset>; always valid
    const bool haveSet = set->texture != nullptr && set->columns > 0;

    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> uv;
    std::vector<uint32_t> idx;

    // Unit-cube face topology (RH Y-up, outward CCW): faces = -Z,+Z,-X,+X,-Y,+Y.
    // Face 5 (+Y) is the textured top; faces 0..4 are extruded side walls.
    static const int kQuads[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7},
    };
    static const glm::vec3 kFaces[6] = {
        {0.f, 0.f, -1.f}, {0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},  {0.f, -1.f, 0.f}, {0.f, 1.f, 0.f},
    };

    const auto pushBoxTile = [&](float cx, float cz, float topY, float botY, float tu0, float tv0,
                                 float tu1, float tv1) {
        // Corners (index matches kQuads[kFace]).
        const glm::vec3 corners[8] = {
            {cx - hx, botY, cz - hz}, {cx + hx, botY, cz - hz}, {cx + hx, topY, cz - hz},
            {cx - hx, topY, cz - hz}, {cx - hx, botY, cz + hz}, {cx + hx, botY, cz + hz},
            {cx + hx, topY, cz + hz}, {cx - hx, topY, cz + hz},
        };
        const float sideU0 = wallU0_, sideV0 = wallV0_, sideU1 = wallU1_, sideV1 = wallV1_;
        for (int f = 0; f < 6; ++f) {
            const uint32_t base = uint32_t(pos.size() / 3);
            const bool top = (f == 5);
            const float su0 = top ? tu0 : sideU0;
            const float sv0 = top ? tv0 : sideV0;
            const float su1 = top ? tu1 : sideU1;
            const float sv1 = top ? tv1 : sideV1;
            const float us[4] = {su0, su1, su1, su0};
            const float vs[4] = {sv0, sv0, sv1, sv1};
            const glm::vec3 &n = kFaces[f];
            for (int k = 0; k < 4; ++k) {
                const glm::vec3 &p = corners[kQuads[f][k]];
                pos.insert(pos.end(), {p.x, p.y, p.z});
                nrm.insert(nrm.end(), {n.x, n.y, n.z});
                uv.insert(uv.end(), {us[k], vs[k]});
            }
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    };

    tileCount_ = 0;

    for (int ty = 0; ty < mapH; ++ty) {
        for (int tx = 0; tx < mapW; ++tx) {
            const uint32_t gid = eve::map::tileGid(uint32_t(layer->getTile(tx, ty)));
            if (gid == 0) continue;

            // World X/Z from the layer's 2D projection; elevation from metadata.
            const float wx = layer->tileToWorldX(tx, ty);
            const float wz = layer->tileToWorldY(tx, ty);
            const float elev = layer->getTileDataNumber(int(gid), "height") * heightScale_;
            const float topY = elev;
            const float botY = elev - sideDepth_;

            float tu0 = 0.f, tv0 = 0.f, tu1 = 1.f, tv1 = 1.f;
            if (haveSet) atlasUvForGid(*set, gid, tu0, tv0, tu1, tv1);

            pushBoxTile(wx, wz, topY, botY, tu0, tv0, tu1, tv1);
            ++tileCount_;
        }
    }

    if (pos.empty()) return nullptr;
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                  idx.data(), int(idx.size()));
}

graphics::Renderable3D *TileMap3D::buildRenderable(graphics::Graphics *gfx,
                                                   map::TileLayer *layer) {
    if (!gfx) throw eve::Exception("TileMap3D.buildRenderable: null graphics");
    if (!layer) throw eve::Exception("TileMap3D.buildRenderable: null tile layer");

    graphics::Mesh *mesh = buildMesh(gfx, layer);
    if (!mesh) throw eve::Exception("TileMap3D.buildRenderable: no non-empty tiles");

    auto *renderable = graphics::Renderable3D::create();
    if (!renderable) throw eve::Exception("TileMap3D.buildRenderable: Renderable3D create failed");
    renderable->setMesh(mesh);
    if (layer->tileset()->texture) renderable->setTexture(layer->tileset()->texture);
    renderable->setTint(tintR_, tintG_, tintB_, tintA_);
    return renderable;
}

}  // namespace eve::hd2d