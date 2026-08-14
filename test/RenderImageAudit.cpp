#include "RenderImageAudit.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <assimp/mesh.h>
#include <glm/gtc/matrix_transform.hpp>

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "avatar/Avatar.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "font/Font.h"
#include "font/FontData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/TextureSampler.h"
#include "graphics/Volumetric.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "physics/Body.h"
#include "physics/Cloth.h"
#include "physics/Fluid.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "stylize/Stylize.h"
#include "ui/UI.h"
#include "voxel/VoxelWorld.h"
#include "window/Window.h"

using eve::image::ImageData;
using namespace eve::graphics;

namespace {

constexpr int kTile = 16;
constexpr int kMaxDefects = 32;
constexpr int kEmptyDist2 = 14 * 14;  // ~0.055 in 8-bit
constexpr int kDeadLumaSum = 15;      // (r+2g+b) < 15 ≈ luma 0.02
constexpr float kOccTile = 0.40f;
constexpr float kEmptyTile = 0.12f;
constexpr float kFlickerTile = 0.07f;
constexpr float kFlickerSevere = 0.15f;

int lumaSumU8(const uint8_t *p) { return int(p[0]) + 2 * int(p[1]) + int(p[2]); }

float lumaU8(const uint8_t *p) {
    return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f;
}

struct BgBytes {
    int r, g, b;
};

BgBytes toBytes(const RenderAuditBg &bg) {
    return BgBytes{int(bg.r * 255.f + 0.5f), int(bg.g * 255.f + 0.5f), int(bg.b * 255.f + 0.5f)};
}

bool pixelEmpty(const uint8_t *p, const BgBytes &bg) {
    const int dr = int(p[0]) - bg.r;
    const int dg = int(p[1]) - bg.g;
    const int db = int(p[2]) - bg.b;
    return dr * dr + dg * dg + db * db < kEmptyDist2 || lumaSumU8(p) < kDeadLumaSum;
}

bool pixelMagenta(const uint8_t *p) { return p[0] > 200 && p[1] < 40 && p[2] > 200; }

bool pixelCyan(const uint8_t *p) { return p[0] < 40 && p[1] > 200 && p[2] > 200; }

const uint8_t *pxAt(const uint8_t *rgba, int w, int x, int y) {
    return rgba + (size_t(y) * size_t(w) + size_t(x)) * 4u;
}

std::string jsonEscape(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back(c);
        } else if (c == '\n') {
            o += "\\n";
        } else {
            o.push_back(c);
        }
    }
    return o;
}

void addDefect(RenderAuditResult &r, RenderDefect::Kind kind, int x, int y, int w, int h,
               float score, const std::string &detail) {
    if (int(r.defects.size()) >= kMaxDefects) return;
    x = std::max(0, x);
    y = std::max(0, y);
    RenderDefect d;
    d.kind = kind;
    d.x = x;
    d.y = y;
    d.w = std::max(1, w);
    d.h = std::max(1, h);
    d.score = score;
    d.detail = detail;
    r.defects.push_back(std::move(d));
}

struct TileStat {
    float occ = 0.f;
    float meanL = 0.f;
};

int occupiedNeighbors(const std::vector<TileStat> &tiles, int tilesX, int tilesY, int tx, int ty) {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int nx = tx + dx;
            const int ny = ty + dy;
            if (nx < 0 || ny < 0 || nx >= tilesX || ny >= tilesY) continue;
            if (tiles[size_t(ny * tilesX + nx)].occ > kOccTile) ++n;
        }
    }
    return n;
}

void detectHoles(const std::vector<TileStat> &tiles, int tilesX, int tilesY, int w, int h,
                 RenderAuditResult &r) {
    std::vector<uint8_t> hole(size_t(tilesX * tilesY), 0);
    for (int ty = 1; ty < tilesY - 1; ++ty) {
        for (int tx = 1; tx < tilesX - 1; ++tx) {
            const TileStat &t = tiles[size_t(ty * tilesX + tx)];
            if (t.occ > kEmptyTile) continue;
            if (occupiedNeighbors(tiles, tilesX, tilesY, tx, ty) >= 5) hole[size_t(ty * tilesX + tx)] = 1;
        }
    }
    std::vector<uint8_t> seen(size_t(tilesX * tilesY), 0);
    for (int ty = 1; ty < tilesY - 1; ++ty) {
        for (int tx = 1; tx < tilesX - 1; ++tx) {
            const int seed = ty * tilesX + tx;
            if (!hole[size_t(seed)] || seen[size_t(seed)]) continue;
            int minX = tx, maxX = tx, minY = ty, maxY = ty, count = 0;
            std::vector<int> stack{seed};
            seen[size_t(seed)] = 1;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                ++count;
                const int cx = i % tilesX;
                const int cy = i / tilesX;
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                const int nb[4] = {i - 1, i + 1, i - tilesX, i + tilesX};
                for (int n : nb) {
                    if (n < 0 || n >= tilesX * tilesY) continue;
                    if (!hole[size_t(n)] || seen[size_t(n)]) continue;
                    seen[size_t(n)] = 1;
                    stack.push_back(n);
                }
            }
            const int px = minX * kTile;
            const int py = minY * kTile;
            const int pw = std::min(w, (maxX + 1) * kTile) - px;
            const int ph = std::min(h, (maxY + 1) * kTile) - py;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "interior hole tiles=%d occ=%.2f", count,
                          tiles[size_t(ty * tilesX + tx)].occ);
            addDefect(r, RenderDefect::Kind::Incomplete, px, py, pw, ph, float(count), buf);
        }
    }
}

void detectCoarseIncomplete(const std::vector<TileStat> &tiles, int tilesX, int tilesY, int w, int h,
                            float occupancy, RenderAuditResult &r) {
    // Sparse frames (small object + lots of sky) have empty quadrants by design.
    // Only look for missing chunks when the frame is mostly filled.
    if (occupancy < 0.40f) return;
    const int gx = 4, gy = 4;
    for (int gyi = 0; gyi < gy; ++gyi) {
        for (int gxi = 0; gxi < gx; ++gxi) {
            const int tx0 = (tilesX * gxi) / gx;
            const int tx1 = (tilesX * (gxi + 1)) / gx;
            const int ty0 = (tilesY * gyi) / gy;
            const int ty1 = (tilesY * (gyi + 1)) / gy;
            if (tx1 <= tx0 || ty1 <= ty0) continue;
            double occSum = 0.0;
            int n = 0;
            for (int ty = ty0; ty < ty1; ++ty) {
                for (int tx = tx0; tx < tx1; ++tx) {
                    occSum += tiles[size_t(ty * tilesX + tx)].occ;
                    ++n;
                }
            }
            const float cellOcc = n ? float(occSum / n) : 0.f;
            if (cellOcc > 0.08f) continue;
            int richN = 0;
            const int nbs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (auto &d : nbs) {
                const int nx = gxi + d[0];
                const int ny = gyi + d[1];
                if (nx < 0 || ny < 0 || nx >= gx || ny >= gy) continue;
                const int ntx0 = (tilesX * nx) / gx;
                const int ntx1 = (tilesX * (nx + 1)) / gx;
                const int nty0 = (tilesY * ny) / gy;
                const int nty1 = (tilesY * (ny + 1)) / gy;
                double nOcc = 0.0;
                int nn = 0;
                for (int ty = nty0; ty < nty1; ++ty) {
                    for (int tx = ntx0; tx < ntx1; ++tx) {
                        nOcc += tiles[size_t(ty * tilesX + tx)].occ;
                        ++nn;
                    }
                }
                if (nn && float(nOcc / nn) > 0.30f) ++richN;
            }
            if (richN < 2) continue;
            const int px = (w * gxi) / gx;
            const int py = (h * gyi) / gy;
            const int pw = (w * (gxi + 1)) / gx - px;
            const int ph = (h * (gyi + 1)) / gy - py;
            char buf[80];
            std::snprintf(buf, sizeof(buf), "coarse empty cell occ=%.2f neighbors=%d", cellOcc, richN);
            addDefect(r, RenderDefect::Kind::Incomplete, px, py, pw, ph, 1.f - cellOcc, buf);
        }
    }
}

struct ScanAccum {
    std::vector<TileStat> tiles;
    int tilesX = 0;
    int tilesY = 0;
    double sumL = 0.0;
    int n = 0;
    int occ = 0;
    int interiorOcc = 0;
    int interiorN = 0;
    int magN = 0, cyanN = 0;
    int magX0 = 0, magY0 = 0, magX1 = 0, magY1 = 0;
    int cyanX0 = 0, cyanY0 = 0, cyanX1 = 0, cyanY1 = 0;
    std::vector<int> rowLit;
    std::vector<int> rowN;
    std::vector<int> colLit;
    std::vector<int> colN;
};

ScanAccum scanFrame(const uint8_t *rgba, int w, int h, const BgBytes &bg, int step) {
    ScanAccum a;
    a.tilesX = (w + kTile - 1) / kTile;
    a.tilesY = (h + kTile - 1) / kTile;
    a.tiles.assign(size_t(a.tilesX * a.tilesY), {});
    a.rowLit.assign(size_t(h), 0);
    a.rowN.assign(size_t(h), 0);
    a.colLit.assign(size_t(w), 0);
    a.colN.assign(size_t(w), 0);
    a.magX0 = w;
    a.magY0 = h;
    a.cyanX0 = w;
    a.cyanY0 = h;
    const int ix0 = w / 10, ix1 = w - w / 10;
    const int iy0 = h / 10, iy1 = h - h / 10;
    std::vector<int> tileOcc(size_t(a.tilesX * a.tilesY), 0);
    std::vector<int> tileN(size_t(a.tilesX * a.tilesY), 0);
    std::vector<double> tileL(size_t(a.tilesX * a.tilesY), 0.0);

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const uint8_t *p = pxAt(rgba, w, x, y);
            const float L = lumaU8(p);
            const bool emptyPx = pixelEmpty(p, bg);
            a.sumL += L;
            ++a.n;
            if (!emptyPx) ++a.occ;
            if (x >= ix0 && x < ix1 && y >= iy0 && y < iy1) {
                ++a.interiorN;
                if (!emptyPx) ++a.interiorOcc;
            }
            const int tx = x / kTile;
            const int ty = y / kTile;
            const int ti = ty * a.tilesX + tx;
            ++tileN[size_t(ti)];
            tileL[size_t(ti)] += L;
            if (!emptyPx) ++tileOcc[size_t(ti)];
            ++a.rowN[size_t(y)];
            ++a.colN[size_t(x)];
            if (!emptyPx) {
                ++a.rowLit[size_t(y)];
                ++a.colLit[size_t(x)];
            }
            if (pixelMagenta(p)) {
                ++a.magN;
                a.magX0 = std::min(a.magX0, x);
                a.magY0 = std::min(a.magY0, y);
                a.magX1 = std::max(a.magX1, x);
                a.magY1 = std::max(a.magY1, y);
            }
            if (pixelCyan(p)) {
                ++a.cyanN;
                a.cyanX0 = std::min(a.cyanX0, x);
                a.cyanY0 = std::min(a.cyanY0, y);
                a.cyanX1 = std::max(a.cyanX1, x);
                a.cyanY1 = std::max(a.cyanY1, y);
            }
        }
    }
    for (int i = 0; i < a.tilesX * a.tilesY; ++i) {
        const int n = tileN[size_t(i)];
        a.tiles[size_t(i)].occ = n ? float(tileOcc[size_t(i)]) / float(n) : 0.f;
        a.tiles[size_t(i)].meanL = n ? float(tileL[size_t(i)] / n) : 0.f;
    }
    return a;
}

void emitDamage(const ScanAccum &a, int w, int h, int step, RenderAuditResult &r) {
    const int magMin = std::max(2, 8 / (step * step));
    if (a.magN >= magMin) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "debug magenta samples=%d", a.magN);
        addDefect(r, RenderDefect::Kind::Damage, a.magX0, a.magY0, a.magX1 - a.magX0 + 1,
                  a.magY1 - a.magY0 + 1, float(a.magN), buf);
    }
    if (a.cyanN >= magMin) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "debug cyan samples=%d", a.cyanN);
        addDefect(r, RenderDefect::Kind::Damage, a.cyanX0, a.cyanY0, a.cyanX1 - a.cyanX0 + 1,
                  a.cyanY1 - a.cyanY0 + 1, float(a.cyanN), buf);
    }

    auto tornRun = [&](const std::vector<int> &lit, const std::vector<int> &nn, int n, bool rows) {
        auto occAt = [&](int i) -> float {
            return nn[size_t(i)] ? float(lit[size_t(i)]) / float(nn[size_t(i)]) : -1.f;
        };
        int y = step;
        while (y < n - step) {
            const float o = occAt(y);
            const float prev = occAt(y - step);
            const float next = occAt(y + step);
            if (!(o >= 0.f && o < 0.05f && prev > 0.35f && next > 0.35f)) {
                y += step;
                continue;
            }
            const int start = y;
            y += step;
            while (y < n - step) {
                const float cur = occAt(y);
                if (!(cur >= 0.f && cur < 0.05f)) break;
                y += step;
            }
            const int len = std::max(step, y - start);
            char buf[72];
            if (rows) {
                std::snprintf(buf, sizeof(buf), "torn scanlines y=%d..%d", start, start + len - 1);
                addDefect(r, RenderDefect::Kind::Damage, 0, start, w, len, float(len), buf);
            } else {
                std::snprintf(buf, sizeof(buf), "torn columns x=%d..%d", start, start + len - 1);
                addDefect(r, RenderDefect::Kind::Damage, start, 0, len, h, float(len), buf);
            }
        }
    };
    tornRun(a.rowLit, a.rowN, h, true);
    tornRun(a.colLit, a.colN, w, false);
}

}  // namespace

bool RenderAuditResult::hasSevere() const {
    if (empty) return true;
    int holeArea = 0;
    int imgArea = std::max(1, cfg.width * cfg.height);
    for (const auto &d : defects) {
        if (d.kind == RenderDefect::Kind::Damage) return true;
        if (d.kind == RenderDefect::Kind::Flicker && d.score >= kFlickerSevere) return true;
        if (d.kind == RenderDefect::Kind::Incomplete) holeArea += d.w * d.h;
    }
    if (flickerMad >= 0.12f) return true;
    return holeArea > imgArea / 16;
}

RenderAuditResult auditRgba8(const uint8_t *rgba, int w, int h, const RenderAuditConfig &cfg,
                             const RenderAuditBg &bg, int step) {
    RenderAuditResult r;
    r.cfg = cfg;
    r.cfg.width = w;
    r.cfg.height = h;
    if (!rgba || w <= 0 || h <= 0) {
        r.empty = true;
        return r;
    }
    step = std::max(1, step);
    const ScanAccum a = scanFrame(rgba, w, h, toBytes(bg), step);
    r.meanLuma = a.n ? float(a.sumL / a.n) : 0.f;
    r.occupancy = a.n ? float(a.occ) / float(a.n) : 0.f;
    r.interiorOccupancy = a.interiorN ? float(a.interiorOcc) / float(a.interiorN) : 0.f;
    r.empty = r.interiorOccupancy < 0.02f;

    detectHoles(a.tiles, a.tilesX, a.tilesY, w, h, r);
    detectCoarseIncomplete(a.tiles, a.tilesX, a.tilesY, w, h, r.occupancy, r);
    emitDamage(a, w, h, step, r);
    if (r.empty) {
        addDefect(r, RenderDefect::Kind::Incomplete, 0, 0, w, h, 1.f - r.interiorOccupancy,
                  "empty interior (occupancy < 2%)");
    }
    return r;
}

RenderAuditResult auditImage(const ImageData &img, const RenderAuditConfig &cfg,
                             const RenderAuditBg &bg, int step) {
    return auditRgba8(static_cast<const uint8_t *>(img.getData()), img.getWidth(), img.getHeight(),
                      cfg, bg, step);
}

RenderAuditResult auditFlickerRgba8(const uint8_t *a, const uint8_t *b, int w, int h,
                                    const RenderAuditConfig &cfg, const RenderAuditBg &bg, int step) {
    RenderAuditResult r;
    r.cfg = cfg;
    r.cfg.width = w;
    r.cfg.height = h;
    r.cfg.extra = r.cfg.extra.empty() ? "flicker" : r.cfg.extra + "+flicker";
    if (!a || !b || w <= 0 || h <= 0) return r;
    step = std::max(1, step);

    double sumMad = 0.0;
    int nAll = 0;
    const int tilesX = (w + kTile - 1) / kTile;
    const int tilesY = (h + kTile - 1) / kTile;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const int x0 = tx * kTile;
            const int y0 = ty * kTile;
            const int x1 = std::min(w, x0 + kTile);
            const int y1 = std::min(h, y0 + kTile);
            double tileMad = 0.0;
            int n = 0;
            for (int y = y0; y < y1; y += step) {
                for (int x = x0; x < x1; x += step) {
                    const float d = std::fabs(lumaU8(pxAt(a, w, x, y)) - lumaU8(pxAt(b, w, x, y)));
                    tileMad += d;
                    sumMad += d;
                    ++n;
                }
            }
            nAll += n;
            const float mad = n ? float(tileMad / n) : 0.f;
            if (mad < kFlickerTile) continue;
            char buf[80];
            std::snprintf(buf, sizeof(buf), "static-camera tile MAD=%.3f", mad);
            addDefect(r, RenderDefect::Kind::Flicker, x0, y0, x1 - x0, y1 - y0, mad, buf);
        }
    }
    r.flickerMad = nAll ? float(sumMad / double(nAll)) : 0.f;
    (void)bg;
    return r;
}

RenderAuditResult auditFlicker(const ImageData &a, const ImageData &b, const RenderAuditConfig &cfg,
                               const RenderAuditBg &bg, int step) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) {
        RenderAuditResult r;
        r.cfg = cfg;
        addDefect(r, RenderDefect::Kind::Flicker, 0, 0, a.getWidth(), a.getHeight(), 1.f,
                  "flicker compare size mismatch");
        return r;
    }
    return auditFlickerRgba8(static_cast<const uint8_t *>(a.getData()),
                             static_cast<const uint8_t *>(b.getData()), a.getWidth(), a.getHeight(),
                             cfg, bg, step);
}

void mergeAudit(RenderAuditResult &dst, const RenderAuditResult &src) {
    dst.flickerMad = std::max(dst.flickerMad, src.flickerMad);
    for (const auto &d : src.defects) {
        if (int(dst.defects.size()) >= kMaxDefects) break;
        dst.defects.push_back(d);
    }
}

void paintDefectOverlay(ImageData &img, const std::vector<RenderDefect> &defs) {
    const int w = img.getWidth();
    const int h = img.getHeight();
    auto *data = static_cast<uint8_t *>(img.getData());
    auto plot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t *p = data + (size_t(y) * size_t(w) + size_t(x)) * 4u;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = 255;
    };
    for (const auto &d : defs) {
        uint8_t cr = 255, cg = 40, cb = 40;
        if (d.kind == RenderDefect::Kind::Damage) {
            cr = 255;
            cg = 220;
            cb = 0;
        } else if (d.kind == RenderDefect::Kind::Flicker) {
            cr = 40;
            cg = 180;
            cb = 255;
        }
        const int x1 = std::min(w - 1, d.x + d.w - 1);
        const int y1 = std::min(h - 1, d.y + d.h - 1);
        for (int t = 0; t < 2; ++t) {
            for (int x = d.x; x <= x1; ++x) {
                plot(x, d.y + t, cr, cg, cb);
                plot(x, y1 - t, cr, cg, cb);
            }
            for (int y = d.y; y <= y1; ++y) {
                plot(d.x + t, y, cr, cg, cb);
                plot(x1 - t, y, cr, cg, cb);
            }
        }
    }
}

bool saveImagePng(const ImageData &img, const std::string &path) {
    eve::image::Image::create();
    eve::filesystem::FileData *png =
        img.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false);
    if (!png) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    const bool ok = out.good();
    if (ok) {
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
    }
    delete png;
    return ok && out.good();
}

void appendAuditReport(const std::string &outDir, const RenderAuditResult &r) {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string mdPath = outDir + "/image_audit.md";
    const std::string jsPath = outDir + "/image_audit.jsonl";

    {
        std::ofstream md(mdPath, std::ios::app);
        if (md.tellp() == 0) {
            md << "# Render image audit\n\n"
               << "Generated by unit_test. Boxes are pixel coordinates `(x,y,w,h)`.\n"
               << "Kinds: `incomplete` (holes / missing tiles), `damage` (magenta/cyan / tearing), "
                  "`flicker` (static-camera MAD).\n\n";
        }
        md << "## " << r.cfg.scene << " / " << r.cfg.phase;
        if (!r.cfg.extra.empty()) md << " / " << r.cfg.extra;
        md << "\n\n";
        md << "- size=" << r.cfg.width << "x" << r.cfg.height;
        if (r.cfg.frame >= 0) md << " frame=" << r.cfg.frame;
        md << "\n- meanLuma=" << r.meanLuma << " occupancy=" << r.occupancy
           << " interiorOcc=" << r.interiorOccupancy << " flickerMad=" << r.flickerMad << "\n";
        if (r.empty) md << "- **empty interior**\n";
        if (r.defects.empty()) {
            md << "- no defects\n\n";
        } else {
            for (const auto &d : r.defects) {
                md << "- DEFECT `" << RenderDefect::kindName(d.kind) << "` bbox=(" << d.x << ","
                   << d.y << "," << d.w << "," << d.h << ") score=" << d.score << " " << d.detail
                   << "\n";
            }
            md << "\n";
        }
    }

    {
        std::ofstream js(jsPath, std::ios::app);
        js << "{\"scene\":\"" << jsonEscape(r.cfg.scene) << "\",\"phase\":\""
           << jsonEscape(r.cfg.phase) << "\",\"extra\":\"" << jsonEscape(r.cfg.extra)
           << "\",\"width\":" << r.cfg.width << ",\"height\":" << r.cfg.height
           << ",\"frame\":" << r.cfg.frame << ",\"meanLuma\":" << r.meanLuma
           << ",\"occupancy\":" << r.occupancy << ",\"interiorOccupancy\":" << r.interiorOccupancy
           << ",\"flickerMad\":" << r.flickerMad << ",\"empty\":" << (r.empty ? "true" : "false")
           << ",\"severe\":" << (r.hasSevere() ? "true" : "false") << ",\"defects\":[";
        for (size_t i = 0; i < r.defects.size(); ++i) {
            const auto &d = r.defects[i];
            if (i) js << ",";
            js << "{\"kind\":\"" << RenderDefect::kindName(d.kind) << "\",\"x\":" << d.x
               << ",\"y\":" << d.y << ",\"w\":" << d.w << ",\"h\":" << d.h << ",\"score\":" << d.score
               << ",\"detail\":\"" << jsonEscape(d.detail) << "\"}";
        }
        js << "]}\n";
    }

    std::printf("RenderImageAudit[%s/%s] defects=%zu severe=%d meanLuma=%.4f occ=%.3f flickerMad=%.4f\n",
                r.cfg.scene.c_str(), r.cfg.phase.c_str(), r.defects.size(), int(r.hasSevere()),
                r.meanLuma, r.occupancy, r.flickerMad);
    for (const auto &d : r.defects) {
        std::printf("  %s bbox=(%d,%d,%d,%d) score=%.3f %s\n", RenderDefect::kindName(d.kind), d.x,
                    d.y, d.w, d.h, d.score, d.detail.c_str());
    }
}

std::string saveAuditOverlay(ImageData &img, const RenderAuditResult &r, const std::string &outDir) {
    if (!r.hasDefects()) return {};
    paintDefectOverlay(img, r.defects);
    std::string name = r.cfg.scene + "_" + r.cfg.phase;
    if (!r.cfg.extra.empty()) name += "_" + r.cfg.extra;
    name += "_audit.png";
    const std::string path = outDir + "/" + name;
    if (!saveImagePng(img, path)) return {};
    std::printf("RenderImageAudit overlay: %s\n", path.c_str());
    return path;
}

namespace {

ImageData *makeRgba(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    auto *img = new ImageData(w, h, "RGBA8");
    auto *p = static_cast<uint8_t *>(img->getData());
    for (int i = 0; i < w * h; ++i) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = 255;
    }
    return img;
}

void fillRect(ImageData *img, int x0, int y0, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    const int w = img->getWidth();
    const int h = img->getHeight();
    auto *p = static_cast<uint8_t *>(img->getData());
    for (int y = y0; y < y0 + rh && y < h; ++y) {
        for (int x = x0; x < x0 + rw && x < w; ++x) {
            uint8_t *q = p + (y * w + x) * 4;
            q[0] = r;
            q[1] = g;
            q[2] = b;
            q[3] = 255;
        }
    }
}

bool hasKind(const RenderAuditResult &r, RenderDefect::Kind k) {
    for (const auto &d : r.defects)
        if (d.kind == k) return true;
    return false;
}

std::string auditOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/classic_scenes";
}

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *makeStudioCubemap(Graphics *gfx, int face = 8) {
    const uint8_t faceRgb[6][3] = {
        {220, 180, 140}, {120, 150, 210}, {245, 245, 250}, {40, 40, 45}, {200, 210, 230}, {180, 160, 140},
    };
    const size_t faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
}

void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
            data->envMap = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
    if (ecs::current()->getManager<Light2D>() != nullptr) {
        auto lightView = ecs::View<Light2D, Light2D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [d] = *it;
            d->active = false;
        }
    }
    if (ecs::current()->getManager<eve::particles::ParticleEmitter>() != nullptr) {
        auto view = ecs::View<eve::particles::ParticleEmitter, eve::particles::ParticleEmitter::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d] = *it;
            d->visible = false;
        }
    }
    if (ecs::current()->getManager<eve::map::TileLayer>() != nullptr) {
        auto view = ecs::View<eve::map::TileLayer, eve::map::TileLayer::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d] = *it;
            d->visible = false;
        }
    }
}

void auditGpuFrame(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg,
                   bool flicker = false, const std::function<void()> &after3D = {}) {
    auto drawOnce = [&]() {
        RenderSystem3D::render(*gfx);
        if (after3D) after3D();
        RenderSystem::render(*gfx);
    };
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) drawOnce();
    gfx->setScreenReadbackEnabled(true);
    drawOnce();
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    if (flicker) {
        drawOnce();
        std::unique_ptr<ImageData> b(gfx->newImageData());
        REQUIRE(b.get() != nullptr);
        mergeAudit(result, auditFlicker(*a, *b, cfg, bg, /*step=*/2));
    }
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

void auditGpu2D(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg) {
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) RenderSystem::render(*gfx);
    gfx->setScreenReadbackEnabled(true);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

void blitAndPresent(Graphics *gfx, Texture *tex) {
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
}

void auditSwapchain(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg) {
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

Texture *makeChecker(Graphics *gfx, int w, int h, bool repeat = false) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = on ? 210 : 50;
            px[i + 1] = on ? 210 : 50;
            px[i + 2] = on ? 215 : 55;
            px[i + 3] = 255;
        }
    }
    // Studio / classic-style 3D audits: mipmaps + anisotropy (engine default stays off).
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU = repeat;
    info.sampler.repeatV = repeat;
    return gfx->newTexture(w, h, px.data(), info);
}

struct CloseWin {
    eve::window::Window *w = nullptr;
    ~CloseWin() {
        if (w) w->close();
    }
};

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

Mesh *makeFloor(Graphics *gfx, float half = 4.5f) {
    const float pos[] = {
        -half, 0.f, -half, -half, 0.f, half, half, 0.f, half, half, 0.f, -half,
    };
    const float nrm[] = {
        0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f,
    };
    const float uv[] = {0.f, 0.f, 0.f, 4.f, 4.f, 4.f, 4.f, 0.f};
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

Texture *makeAtlas4(Graphics *gfx, int cell = 16) {
    const int w = cell * 2;
    const int h = cell * 2;
    std::vector<uint8_t> px(size_t(w * h * 4));
    const uint8_t cols[4][3] = {{220, 70, 60}, {60, 180, 80}, {60, 90, 220}, {230, 200, 50}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int ci = (y < cell ? 0 : 2) + (x < cell ? 0 : 1);
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = cols[ci][0];
            px[i + 1] = cols[ci][1];
            px[i + 2] = cols[ci][2];
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

Texture *makeFlatNormal(Graphics *gfx) {
    const uint8_t px[4] = {128, 128, 255, 255};
    return gfx->newTexture(1, 1, px);
}

struct Studio3D {
    Camera3D *cam = nullptr;
    Renderable3D *ground = nullptr;
    Renderable3D *subject = nullptr;
    Light3D *sun = nullptr;
    Renderable2D *hud = nullptr;
    Mesh *sphere = nullptr;
    Mesh *floor = nullptr;
};

Studio3D makeStudio3D(Graphics *gfx) {
    Studio3D s;
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    s.cam = Camera3D::createCamera();
    s.cam->setEye(0.f, 1.5f, 4.0f);
    s.cam->setTarget(0.f, 0.15f, 0.f);
    s.cam->data()->nearZ = 0.05f;
    s.cam->data()->farZ = 40.f;
    s.cam->setAmbient(0.06f, 0.06f, 0.07f);
    s.cam->setEnvMap(makeStudioCubemap(gfx));
    s.cam->setEnvIntensity(0.f);
    s.floor = makeFloor(gfx, 4.5f);
    s.sphere = gfx->newMeshSphere(18, 12);
    s.ground = Renderable3D::create();
    s.ground->setMesh(s.floor);
    s.ground->setTexture(makeSolid(gfx, 70, 72, 78));
    s.ground->setPosition(0.f, -0.7f, 0.f);
    s.ground->setMetallic(0.f);
    s.ground->setRoughness(0.92f);
    s.ground->setCastShadow(false);
    s.ground->setReceiveShadow(true);
    s.subject = Renderable3D::create();
    s.subject->setMesh(s.sphere);
    s.subject->setTexture(makeSolid(gfx, 210, 210, 215));
    s.subject->setPosition(0.f, 0.15f, 0.f);
    s.subject->setScale(0.55f, 0.55f, 0.55f);
    s.subject->setMetallic(0.15f);
    s.subject->setRoughness(0.45f);
    s.subject->setCastShadow(true);
    s.subject->setReceiveShadow(true);
    s.sun = Light3D::createLight("dir");
    s.sun->setDirection(0.45f, 1.f, 0.35f);
    s.sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    s.sun->setCastShadow(true);
    s.sun->setEnabled(true);
    s.hud = Renderable2D::create();
    s.hud->transform()->x = 0;
    s.hud->transform()->y = 0;
    s.hud->sprite()->width = 2;
    s.hud->sprite()->height = 2;
    s.hud->sprite()->receiveLight = false;
    s.hud->sprite()->castOcclusion = false;
    return s;
}

Texture *capturePresented(Graphics *gfx) {
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    Texture *tex = gfx->newTexture(img.get());
    gfx->setScreenReadbackEnabled(false);
    return tex;
}

Renderable2D *makeSprite(Texture *tex, float x, float y, float w, float h, bool receiveLight = false) {
    auto *sp = Renderable2D::create();
    sp->transform()->x = x;
    sp->transform()->y = y;
    sp->sprite()->width = w;
    sp->sprite()->height = h;
    sp->sprite()->texture = tex;
    sp->sprite()->receiveLight = receiveLight;
    sp->sprite()->castOcclusion = false;
    return sp;
}

Texture *makeAtlasGrid(Graphics *gfx, int cells, int cell = 8) {
    const int w = cell * cells;
    const int h = cell * cells;
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cx = x / cell;
            const int cy = y / cell;
            const int id = cy * cells + cx;
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = uint8_t(40 + (id * 37) % 200);
            px[i + 1] = uint8_t(50 + (id * 53) % 190);
            px[i + 2] = uint8_t(70 + (id * 19) % 180);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

Mesh *makeHairCard(Graphics *gfx) {
    const float pos[] = {-0.45f, 0.f, 0.f, 0.45f, 0.f, 0.f, 0.45f, 1.3f, 0.f, -0.45f, 1.3f, 0.f};
    const float nrm[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float uv[] = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

Mesh *uploadSkinnedAiMesh(Graphics *gfx, const aiMesh *src, const std::vector<float> &posXYZ) {
    REQUIRE(src != nullptr);
    const int vc = int(src->mNumVertices);
    REQUIRE(vc > 0);
    REQUIRE(int(posXYZ.size()) == vc * 3);
    std::vector<float> nrm(size_t(vc) * 3u, 0.f);
    std::vector<float> uv(size_t(vc) * 2u, 0.f);
    for (int i = 0; i < vc; ++i) {
        if (src->mNormals) {
            nrm[size_t(i) * 3u + 0] = src->mNormals[i].x;
            nrm[size_t(i) * 3u + 1] = src->mNormals[i].y;
            nrm[size_t(i) * 3u + 2] = src->mNormals[i].z;
        } else {
            nrm[size_t(i) * 3u + 1] = 1.f;
        }
        if (src->mTextureCoords[0]) {
            uv[size_t(i) * 2u + 0] = src->mTextureCoords[0][i].x;
            uv[size_t(i) * 2u + 1] = src->mTextureCoords[0][i].y;
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(src->mNumFaces) * 3u);
    for (unsigned f = 0; f < src->mNumFaces; ++f) {
        const aiFace &face = src->mFaces[f];
        if (face.mNumIndices < 3) continue;
        for (unsigned k = 1; k + 1 < face.mNumIndices; ++k) {
            idx.push_back(face.mIndices[0]);
            idx.push_back(face.mIndices[k]);
            idx.push_back(face.mIndices[k + 1]);
        }
    }
    REQUIRE(!idx.empty());
    return gfx->newMeshFromArrays(posXYZ.data(), nrm.data(), uv.data(), vc, idx.data(),
                                  int(idx.size()));
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF");
    REQUIRE(std::filesystem::is_regular_file(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

void printOverlayThunk(void *userdata, void *) {
    auto *gfx = static_cast<Graphics *>(userdata);
    if (!gfx || !gfx->getFont()) return;
    const char *icon = "\xEF\x80\x80";
    std::string row;
    for (int i = 0; i < 8; ++i) row += icon;
    gfx->print(row, 8.f, 8.f, Color(1.f, 0.82f, 0.2f, 1.f), 2.4f);
}

}  // namespace

TEST_CASE("graphics.imageAudit.syntheticDefects") {
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    const uint8_t geoR = 180, geoG = 180, geoB = 185;

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        RenderAuditConfig cfg{"synthetic", "clean", "", 0};
        auto r = auditImage(*img, cfg, bg);
        CHECK(!r.empty);
        CHECK(r.defects.empty());
        CHECK(r.occupancy > 0.9f);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        // One full interior 16x16 tile; unaligned holes split across tiles and
        // stay above kEmptyTile so detectHoles never fires.
        fillRect(img.get(), 16, 16, 16, 16, 0, 0, 0);
        RenderAuditConfig cfg{"synthetic", "hole", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Incomplete));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Incomplete) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 16 && cx < 48 && cy >= 16 && cy < 48) locOk = true;
        }
        CHECK(locOk);
        appendAuditReport(auditOutDir(), r);
        saveAuditOverlay(*img, r, auditOutDir());
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 8, 8, 12, 12, 255, 0, 255);
        RenderAuditConfig cfg{"synthetic", "magenta", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
        REQUIRE(r.hasSevere());
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 0, 30, 64, 1, 20, 23, 28);
        RenderAuditConfig cfg{"synthetic", "tearing", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> a(makeRgba(64, 64, geoR, geoG, geoB));
        std::unique_ptr<ImageData> b(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(b.get(), 16, 16, 24, 24, 40, 40, 40);
        RenderAuditConfig cfg{"synthetic", "flicker", "", 0};
        auto r = auditFlicker(*a, *b, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Flicker));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Flicker) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 12 && cx < 44 && cy >= 12 && cy < 44) locOk = true;
        }
        CHECK(locOk);
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, 20, 23, 28));
        RenderAuditConfig cfg{"synthetic", "empty", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(r.empty);
        REQUIRE(r.hasSevere());
        appendAuditReport(auditOutDir(), r);
    }
}

TEST_CASE("graphics.imageAudit.pipelineConfigs") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 480;
    s.height = 360;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.6f, 4.2f);
    cam->setTarget(0.f, 0.2f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;
    cam->setAmbient(0.06f, 0.06f, 0.07f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    auto *ground = Renderable3D::create();
    ground->setMesh(gfx->newMeshSphere(16, 8));
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);
    box->setCastShadow(true);
    box->setReceiveShadow(true);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(false);
    sun->setEnabled(false);

    auto *point = Light3D::createLight("point");
    point->setPosition(-1.2f, 1.1f, 1.0f);
    point->setColor(1.f, 0.6f, 0.4f, 5.f);
    point->setRadius(6.f);
    point->setEnabled(false);

    auto *pointB = Light3D::createLight("point");
    pointB->setPosition(1.1f, 0.9f, -0.8f);
    pointB->setColor(0.4f, 0.55f, 1.f, 4.5f);
    pointB->setRadius(6.f);
    pointB->setEnabled(false);

    std::vector<Light3D *> clusterLights;
    clusterLights.reserve(10);
    for (int i = 0; i < 10; ++i) {
        auto *l = Light3D::createLight("point");
        const float a = float(i) / 10.f * 6.2831853f;
        l->setPosition(std::cos(a) * 1.7f, 0.55f + 0.15f * std::sin(a * 2.f), std::sin(a) * 1.7f);
        l->setColor(0.85f, 0.9f, 1.f, 2.2f);
        l->setRadius(3.2f);
        l->setEnabled(false);
        clusterLights.push_back(l);
    }

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    auto resetRc = [&]() {
        rc->enable("depthTest");
        rc->enable("shadow");
        rc->enable("forward");
        rc->enable("hair");
        rc->enable("clustered");
        rc->disable("gbuffer");
        rc->compile();
    };
    resetRc();

    struct Phase {
        const char *name;
        int light;  // 0 amb 1 dir 2 shadows 3 point 4 ibl 5 dir+point 6 shadows+ibl 7 full
        const char *enableFeat;
        const char *disableFeat;
        bool manyPoints;
        bool flicker;
    };
    const Phase phases[] = {
        {"ambient", 0, nullptr, nullptr, false, true},
        {"dir_lit", 1, nullptr, nullptr, false, true},
        {"dir_shadows", 2, nullptr, nullptr, false, true},
        {"point_lit", 3, nullptr, nullptr, false, false},
        {"ibl_reflect", 4, nullptr, nullptr, false, true},
        {"dir_point", 5, nullptr, nullptr, false, false},
        {"shadows_ibl", 6, nullptr, nullptr, false, false},
        {"full_lit", 7, nullptr, nullptr, false, true},
        {"gbuffer", 1, "gbuffer", nullptr, false, false},
        {"gbuffer_albedo", 1, "gbufferAlbedo", nullptr, false, false},
        {"no_shadow_pass", 2, nullptr, "shadow", false, false},
        {"clustered_many", 1, "clustered", nullptr, true, false},
        {"packed_only", 1, nullptr, "clustered", true, false},
    };

    const std::string outDir = auditOutDir();

    for (const auto &ph : phases) {
        sun->setEnabled(false);
        sun->setCastShadow(false);
        point->setEnabled(false);
        pointB->setEnabled(false);
        for (auto *l : clusterLights) l->setEnabled(false);
        cam->setEnvIntensity(0.f);
        cam->setAmbient(0.18f, 0.18f, 0.2f);
        resetRc();
        if (ph.enableFeat) {
            rc->enable(ph.enableFeat);
            rc->compile();
        }
        if (ph.disableFeat) {
            rc->disable(ph.disableFeat);
            rc->compile();
        }

        auto enableSun = [&](bool shadows) {
            cam->setAmbient(0.05f, 0.05f, 0.06f);
            sun->setEnabled(true);
            sun->setCastShadow(shadows);
            sun->setShadowStrength(1.f);
            sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
        };
        switch (ph.light) {
        case 1:
            enableSun(false);
            break;
        case 2:
            enableSun(true);
            break;
        case 3:
            cam->setAmbient(0.04f, 0.04f, 0.05f);
            point->setEnabled(true);
            break;
        case 4:
            cam->setAmbient(0.03f, 0.03f, 0.04f);
            sun->setEnabled(true);
            cam->setEnvIntensity(1.2f);
            break;
        case 5:
            enableSun(false);
            point->setEnabled(true);
            pointB->setEnabled(true);
            break;
        case 6:
            enableSun(true);
            cam->setEnvIntensity(1.1f);
            break;
        case 7:
            enableSun(true);
            point->setEnabled(true);
            pointB->setEnabled(true);
            cam->setEnvIntensity(1.0f);
            break;
        default:
            break;
        }
        if (ph.manyPoints) {
            for (auto *l : clusterLights) l->setEnabled(true);
        }

        gfx->setScreenReadbackEnabled(false);
        for (int i = 0; i < 2; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        gfx->setScreenReadbackEnabled(true);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        std::unique_ptr<ImageData> a(gfx->newImageData());
        REQUIRE(a.get() != nullptr);
        RenderAuditConfig cfg{"pipeline_cube", ph.name, "", 0};
        auto result = auditImage(*a, cfg, bg, /*step=*/2);

        if (ph.flicker) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            std::unique_ptr<ImageData> b(gfx->newImageData());
            REQUIRE(b.get() != nullptr);
            mergeAudit(result, auditFlicker(*a, *b, cfg, bg, /*step=*/2));
        }
        gfx->setScreenReadbackEnabled(false);

        if (result.hasDefects()) {
            appendAuditReport(outDir, result);
            saveAuditOverlay(*a, result, outDir);
        } else {
            saveImagePng(*a, outDir + "/pipeline_cube_" + std::string(ph.name) + ".png");
        }

        REQUIRE(result.interiorOccupancy > 0.04f);
        REQUIRE(!result.hasSevere());
    }

    resetRc();
    win->close();
}

TEST_CASE("graphics.imageAudit.materialsAndCamera") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.15f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;
    cam->setAmbient(0.06f, 0.06f, 0.07f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    Mesh *cyl = gfx->newMeshCylinder(16, 1, true);
    Texture *checker = makeChecker(gfx, 32, 32);
    Texture *height = makeChecker(gfx, 32, 32);

    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(sphere);
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);
    box->setCastShadow(true);
    box->setReceiveShadow(true);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    Material *unlit = gfx->newMaterial();
    unlit->setShadingModel("unlit");
    unlit->setAlbedoTexture(makeSolid(gfx, 200, 50, 50));
    unlit->setReceiveLight(false);

    auditGpuFrame(gfx, "materials", "pbr_default", bg, true);

    box->setMaterial(unlit);
    box->setReceiveLight(false);
    auditGpuFrame(gfx, "materials", "unlit", bg);
    box->setMaterial(nullptr);
    box->setReceiveLight(true);
    box->setTexture(makeSolid(gfx, 210, 210, 215));

    box->setMetallic(1.f);
    box->setRoughness(0.08f);
    cam->setEnvIntensity(1.3f);
    auditGpuFrame(gfx, "materials", "metal_ibl", bg);
    cam->setEnvIntensity(0.f);
    box->setMetallic(0.f);
    box->setRoughness(0.95f);
    auditGpuFrame(gfx, "materials", "dielectric_rough", bg);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);

    box->setTexture(checker);
    box->setTexCellBomb(4.f, 0.85f, 1.f);
    auditGpuFrame(gfx, "materials", "tex_cell_bomb", bg);
    box->setTexCellBomb(4.f, 0.f, 1.f);
    box->setHeightTexture(height);
    box->setParallax(0.06f, 8.f, 24.f);
    auditGpuFrame(gfx, "materials", "parallax", bg);
    box->setParallax(0.f);
    box->setHeightTexture(nullptr);
    box->setTexture(makeSolid(gfx, 210, 210, 215));

    box->setHair(true);
    auditGpuFrame(gfx, "materials", "hair", bg);
    box->setHair(false);

    box->setMesh(cyl);
    auditGpuFrame(gfx, "materials", "cylinder", bg);
    box->setMesh(sphere);

    ground->setReceiveShadow(false);
    auditGpuFrame(gfx, "materials", "receive_shadow_off", bg);
    ground->setReceiveShadow(true);

    sun->setShadowBias(0.02f);
    auditGpuFrame(gfx, "materials", "shadow_bias_high", bg);
    sun->setShadowBias(0.002f);

    cam->setFov(28.f);
    auditGpuFrame(gfx, "materials", "fov_narrow", bg);
    cam->setFov(75.f);
    auditGpuFrame(gfx, "materials", "fov_wide", bg);
    cam->setFov(60.f);

    cam->data()->nearZ = 0.8f;
    cam->data()->farZ = 6.f;
    auditGpuFrame(gfx, "materials", "near_far_tight", bg);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;

    delete unlit;
    win->close();
}

TEST_CASE("graphics.imageAudit.postFx") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    const int w = 400;
    const int h = 300;

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.15f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;
    cam->setAmbient(0.05f, 0.05f, 0.06f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(sphere);
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setCastShadow(true);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(true);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);

    std::unique_ptr<ImageData> sceneImg(gfx->newImageData());
    REQUIRE(sceneImg.get() != nullptr);
    Texture *sceneTex = gfx->newTexture(sceneImg.get());
    REQUIRE(sceneTex != nullptr);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *depth = gb->getDepthTexture();
    REQUIRE(depth != nullptr);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setQuality("low");
    Canvas *aaDest = gfx->newCanvas(w, h);
    REQUIRE(aaDest != nullptr);
    const char *aaModes[] = {"fxaa", "smaa", "nfaa"};
    for (const char *mode : aaModes) {
        aa->setMode(mode);
        aa->applyTo(gfx, sceneTex, aaDest);
        blitAndPresent(gfx, aaDest->getTexture());
        auditSwapchain(gfx, "postfx", (std::string("aa_") + mode).c_str(), bg);
    }

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setQuality("low");
    ao->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f,
                  40.f);
    Canvas *aoMap = gfx->newCanvas(ao->resolutionFor(w), ao->resolutionFor(h));
    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(aoMap != nullptr);
    REQUIRE(composed != nullptr);
    const char *aoModes[] = {"ssao", "hbao", "gtao"};
    for (const char *mode : aoModes) {
        ao->setMode(mode);
        ao->computeTo(gfx, depth, aoMap);
        gfx->setCanvas(composed);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        ao->applyOverlay(gfx, aoMap->getTexture());
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(gfx, "postfx", (std::string("ao_") + mode).c_str(), bg);
    }

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setQuality("low");
    vol->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f,
                   40.f);
    vol->setFogColor(0.45f, 0.5f, 0.55f);
    vol->setFogHeight(-0.7f);
    vol->setFogHeightFalloff(0.8f);
    vol->setFogStart(1.2f);
    vol->setFogEnd(9.f);
    vol->setDensity(0.35f);
    vol->setMode("fog");
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->applyFog(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "postfx", "vol_fog", bg);

    vol->setMode("raymarch");
    vol->setLightDirection(0.45f, 1.f, 0.35f);
    vol->setLightScreenUV(0.62f, 0.28f);
    vol->setIntensity(0.8f);
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->rayMarch(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "postfx", "vol_raymarch", bg);

    vol->setMode("screenspace");
    vol->applyFromSceneTo(gfx, sceneTex, composed);
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "postfx", "vol_screenspace", bg);

    rc->disable("gbuffer");
    rc->compile();
    win->close();
}

TEST_CASE("graphics.imageAudit.composite2d3d") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    studio.hud->sprite()->visible = false;

    Texture *cells[4] = {
        makeSolid(gfx, 220, 80, 70),
        makeSolid(gfx, 70, 190, 90),
        makeSolid(gfx, 70, 110, 220),
        makeSolid(gfx, 230, 200, 60),
    };
    Texture *flatN = makeFlatNormal(gfx);
    Renderable2D *packed[4];
    const float ox = 80.f, oy = 60.f, cw = 120.f, ch = 90.f;
    for (int i = 0; i < 4; ++i) {
        packed[i] = makeSprite(cells[i], ox + float(i % 2) * cw, oy + float(i / 2) * ch, cw, ch, false);
        packed[i]->sprite()->normalTexture = flatN;
    }
    auditGpuFrame(gfx, "composite", "sprites_over_3d", bg);

    auto *pt = Light2D::createLight("point");
    pt->setPosition(200.f, 150.f);
    pt->setColor(1.f, 0.95f, 0.85f, 2.2f);
    pt->setRadius(280.f);
    pt->setEnabled(true);
    for (auto *sp : packed) sp->sprite()->receiveLight = true;
    auditGpuFrame(gfx, "composite", "lit2d_normal", bg);

    pt->setEnabled(false);
    auto *dir2 = Light2D::createLight("dir");
    dir2->setDirection(0.6f, -0.4f);
    dir2->setColor(0.95f, 0.9f, 1.f, 1.6f);
    dir2->setEnabled(true);
    auditGpuFrame(gfx, "composite", "dir2d", bg);

    dir2->setEnabled(false);
    for (auto *sp : packed) sp->sprite()->visible = false;
    Texture *atlas = makeAtlas4(gfx, 16);
    auto *atlasSp = makeSprite(atlas, 80.f, 60.f, 240.f, 180.f, false);
    atlasSp->sprite()->quad = gfx->newQuad(0, 0, 16, 16);
    auditGpuFrame(gfx, "composite", "atlas_quad", bg);
}

TEST_CASE("graphics.imageAudit.stylizeAndSsaa") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    Mesh *cyl = gfx->newMeshCylinder(24, 1, true);
    studio.subject->setMesh(cyl);
    studio.subject->setTexture(makeChecker(gfx, 32, 32));
    studio.subject->setScale(0.45f, 0.7f, 0.45f);

    auto *stylize = eve::stylize::Stylize::create();
    Shader *toon = stylize->newMeshShader(gfx, "cartoon");
    REQUIRE(toon != nullptr);
    if (toon->hasUniform("bands")) toon->sendFloat("bands", 3.f);
    if (toon->hasUniform("rimPower")) toon->sendFloat("rimPower", 2.6f);
    if (toon->hasUniform("rimStrength")) toon->sendFloat("rimStrength", 0.45f);
    studio.subject->setShader(toon);
    auditGpuFrame(gfx, "stylize", "mesh_cartoon", bg);

    Shader *ink = stylize->newMeshShader(gfx, "ink");
    REQUIRE(ink != nullptr);
    studio.subject->setShader(ink);
    auditGpuFrame(gfx, "stylize", "mesh_ink", bg);
    studio.subject->setShader(nullptr);

    Texture *sceneTex = capturePresented(gfx);
    REQUIRE(sceneTex != nullptr);
    const int w = 400, h = 300;
    Canvas *dest = gfx->newCanvas(w, h);
    REQUIRE(dest != nullptr);
    const char *posts[] = {"cartoon", "watercolor", "ink", "pixel"};
    for (const char *id : posts) {
        eve::stylize::StylePass *pass = stylize->newPass(gfx, id);
        REQUIRE(pass != nullptr);
        pass->applyTo(gfx, sceneTex, dest);
        blitAndPresent(gfx, dest->getTexture());
        auditSwapchain(gfx, "stylize", (std::string("post_") + id).c_str(), bg);
        delete pass;
    }

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setQuality("low");
    aa->setMode("ssaa");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(gfx, "stylize", "aa_ssaa", bg);
}

TEST_CASE("graphics.imageAudit.multiObjectLod") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    Mesh *cyl = gfx->newMeshCylinder(12, 1, true);
    Mesh *hi = gfx->newMeshSphere(20, 14);
    Mesh *lo = gfx->newMeshSphere(8, 6);

    auto *left = Renderable3D::create();
    left->setMesh(studio.sphere);
    left->setTexture(makeSolid(gfx, 210, 90, 80));
    left->setPosition(-1.1f, 0.2f, 0.f);
    left->setScale(0.4f, 0.4f, 0.4f);
    left->setCastShadow(true);

    auto *right = Renderable3D::create();
    right->setMesh(cyl);
    right->setTexture(makeSolid(gfx, 80, 160, 210));
    right->setPosition(1.1f, 0.25f, 0.f);
    right->setScale(0.35f, 0.55f, 0.35f);
    right->setCastShadow(true);

    auditGpuFrame(gfx, "multi", "gallery", bg);

    studio.subject->setTint(1.f, 0.25f, 0.2f, 1.f);
    auditGpuFrame(gfx, "multi", "tint_red", bg);
    studio.subject->setTint(1.f, 1.f, 1.f, 0.72f);
    auditGpuFrame(gfx, "multi", "tint_alpha", bg);
    studio.subject->setTint(1.f, 1.f, 1.f, 1.f);

    left->setVisible(false);
    right->setVisible(false);
    studio.subject->setMesh(nullptr);
    auto *matA = gfx->newMaterial();
    matA->setShadingModel("pbr");
    matA->setAlbedoTexture(makeSolid(gfx, 220, 180, 70));
    auto *matB = gfx->newMaterial();
    matB->setShadingModel("pbr");
    matB->setAlbedoTexture(makeSolid(gfx, 70, 140, 210));
    studio.subject->setPart(0, "body", hi, matA);
    studio.subject->setPart(1, "cap", cyl, matB);
    studio.subject->setScale(0.5f, 0.5f, 0.5f);
    auditGpuFrame(gfx, "multi", "mesh_parts", bg);
    studio.subject->clearParts();
    studio.subject->setMesh(hi);
    left->setVisible(true);
    right->setVisible(true);

    studio.subject->setMeshLod(0, hi);
    studio.subject->setMeshLod(1, lo, 3.2f);
    studio.cam->setEye(0.f, 1.2f, 2.2f);
    auditGpuFrame(gfx, "multi", "lod_near", bg);
    studio.cam->setEye(0.f, 2.2f, 7.5f);
    auditGpuFrame(gfx, "multi", "lod_far", bg);
    studio.cam->setEye(4.2f, 1.5f, 0.1f);
    studio.cam->setTarget(0.f, 0.15f, 0.f);
    auditGpuFrame(gfx, "multi", "camera_side", bg);
    studio.cam->setEye(0.f, 1.5f, 4.0f);

    Canvas *cv = gfx->newCanvas(400, 300);
    REQUIRE(cv != nullptr);
    Texture *cellA = makeSolid(gfx, 200, 90, 70);
    Texture *cellB = makeSolid(gfx, 70, 180, 110);
    auto *c0 = makeSprite(cellA, 40.f, 30.f, 160.f, 120.f);
    auto *c1 = makeSprite(cellB, 200.f, 30.f, 160.f, 120.f);
    auto *c2 = makeSprite(cellB, 40.f, 150.f, 160.f, 120.f);
    auto *c3 = makeSprite(cellA, 200.f, 150.f, 160.f, 120.f);
    c0->sprite()->canvas = cv;
    c1->sprite()->canvas = cv;
    c2->sprite()->canvas = cv;
    c3->sprite()->canvas = cv;
    RenderSystem::render(*gfx);
    blitAndPresent(gfx, cv->getTexture());
    auditSwapchain(gfx, "multi", "canvas_2d", bg);
}

TEST_CASE("graphics.imageAudit.lightsAndXform") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);

    auto *pR = Light3D::createLight("point");
    pR->setPosition(-1.3f, 1.0f, 0.8f);
    pR->setColor(1.f, 0.25f, 0.2f, 5.f);
    pR->setRadius(5.f);
    pR->setEnabled(true);
    auto *pG = Light3D::createLight("point");
    pG->setPosition(1.2f, 0.9f, 0.6f);
    pG->setColor(0.2f, 1.f, 0.35f, 4.5f);
    pG->setRadius(5.f);
    pG->setEnabled(true);
    auto *pB = Light3D::createLight("point");
    pB->setPosition(0.1f, 1.2f, -1.0f);
    pB->setColor(0.25f, 0.4f, 1.f, 5.f);
    pB->setRadius(5.f);
    pB->setEnabled(true);
    auditGpuFrame(gfx, "lights", "dir_rgb_points", bg);

    studio.sun->setShadowStrength(0.35f);
    auditGpuFrame(gfx, "lights", "shadow_soft", bg);
    studio.sun->setShadowStrength(1.f);
    studio.subject->setCastShadow(false);
    studio.subject->setReceiveShadow(false);
    studio.ground->setReceiveShadow(false);
    auditGpuFrame(gfx, "lights", "cast_receive_off", bg);
    studio.subject->setCastShadow(true);
    studio.subject->setReceiveShadow(true);
    studio.ground->setReceiveShadow(true);

    studio.subject->setRotation(0.9f, 0.4f, 0.15f);
    auditGpuFrame(gfx, "lights", "yaw_pitch_roll", bg);
    studio.subject->setRotation(0.f, 0.f, 0.f);

    studio.cam->setUp(0.25f, 0.97f, 0.f);
    auditGpuFrame(gfx, "lights", "camera_roll", bg);
    studio.cam->setUp(0.f, 1.f, 0.f);

    pR->setEnabled(false);
    pG->setEnabled(false);
    pB->setEnabled(false);
    studio.sun->setEnabled(false);
    studio.subject->setMetallic(1.f);
    studio.subject->setRoughness(0.12f);
    studio.cam->setEnvIntensity(1.8f);
    auditGpuFrame(gfx, "lights", "ibl_metal", bg);
}

TEST_CASE("graphics.imageAudit.samplerMorph") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    Texture *checker = makeChecker(gfx, 32, 32);
    studio.subject->setTexture(checker);

    gfx->setTextureSamplerParams(checker, "nearest", "none", 1.f, 0.f);
    auditGpuFrame(gfx, "sampler", "nearest", bg);
    gfx->setTextureSamplerParams(checker, "linear", "none", 1.f, 0.f);
    auditGpuFrame(gfx, "sampler", "linear", bg);

    TextureCreateInfo mip = TextureCreateInfo::withMipmaps(true, 8.f);
    std::vector<uint8_t> mipPx(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            const size_t i = size_t((y * 32 + x) * 4);
            mipPx[i + 0] = on ? 210 : 50;
            mipPx[i + 1] = on ? 210 : 50;
            mipPx[i + 2] = on ? 215 : 55;
            mipPx[i + 3] = 255;
        }
    }
    Texture *mipTex = gfx->newTexture(32, 32, mipPx.data(), mip);
    studio.subject->setTexture(mipTex);
    gfx->setTextureSamplerParams(mipTex, "linear", "linear", 8.f, 2.5f);
    auditGpuFrame(gfx, "sampler", "mip_lod_bias", bg);

    Texture *rep = makeChecker(gfx, 16, 16, true);
    studio.ground->setTexture(rep);
    auditGpuFrame(gfx, "sampler", "repeat_uv", bg);

    const float hs = 0.45f;
    const float pos[] = {
        -hs, -hs, -hs, hs, -hs, -hs, hs, hs, -hs, -hs, hs, -hs,
        -hs, -hs,  hs, hs, -hs,  hs, hs, hs,  hs, -hs, hs,  hs,
    };
    float nrm[24];
    float uv[16];
    for (int i = 0; i < 8; ++i) {
        float x = pos[i * 3 + 0], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) len = 1.f;
        nrm[i * 3 + 0] = x / len;
        nrm[i * 3 + 1] = y / len;
        nrm[i * 3 + 2] = z / len;
        uv[i * 2 + 0] = (i & 1) ? 1.f : 0.f;
        uv[i * 2 + 1] = (i & 2) ? 1.f : 0.f;
    }
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                            3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    Mesh *morphMesh = gfx->newMeshFromArrays(pos, nrm, uv, 8, idx, 36);
    morphMesh->initMorphBase(8, pos, nrm, uv);
    float delta[24];
    for (int i = 0; i < 8; ++i) {
        delta[i * 3 + 0] = nrm[i * 3 + 0] * 0.28f;
        delta[i * 3 + 1] = nrm[i * 3 + 1] * 0.28f;
        delta[i * 3 + 2] = nrm[i * 3 + 2] * 0.28f;
    }
    REQUIRE(morphMesh->addMorphTarget("inflate", delta));
    studio.subject->setMesh(morphMesh);
    studio.subject->setTexture(makeSolid(gfx, 200, 160, 90));
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(gfx, "sampler", "morph_base", bg);
    morphMesh->setMorphWeight("inflate", 1.f);
    gfx->bakeMeshMorph(morphMesh);
}

TEST_CASE("graphics.imageAudit.gbufferViews") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *albedo = gb->getAlbedoTexture();
    Texture *normal = gb->getNormalTexture();
    Texture *depth = gb->getDepthTexture();
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(depth != nullptr);

    blitAndPresent(gfx, albedo);
    auditSwapchain(gfx, "gbuffer", "albedo", bg);
    blitAndPresent(gfx, normal);
    auditSwapchain(gfx, "gbuffer", "normal", bg);

    rc->disable("gbuffer");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.occlusionScatter") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    const int w = 400, h = 300;

    auto *plate = makeSprite(makeSolid(gfx, 190, 175, 150), 0.f, 0.f, float(w), float(h), false);
    plate->setCastOcclusion(false);
    auto *occA = makeSprite(makeSolid(gfx, 40, 40, 48), 70.f, 80.f, 90.f, 110.f, false);
    occA->setCastOcclusion(true);
    auto *occB = makeSprite(makeSolid(gfx, 48, 40, 40), 230.f, 90.f, 80.f, 100.f, false);
    occB->setCastOcclusion(true);

    gfx->setScreenReadbackEnabled(true);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> sceneImg(gfx->newImageData());
    REQUIRE(sceneImg.get() != nullptr);
    Texture *sceneTex = gfx->newTexture(sceneImg.get());
    gfx->setScreenReadbackEnabled(false);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setQuality("low");
    vol->setMode("screenspace");
    vol->setLightScreenPos(320.f, 40.f, float(w), float(h));
    vol->setIntensity(0.9f);

    Canvas *occMap = gfx->newCanvas(w, h);
    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(occMap != nullptr);
    REQUIRE(composed != nullptr);

    gfx->setCanvas(occMap);
    vol->beginOcclusionMap(gfx, 320.f, 40.f, 22.f);
    vol->drawOccluders2D(gfx);
    gfx->setCanvas();
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->scatter(gfx, occMap->getTexture());
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "scatter", "scatter_on", bg);

    occA->setCastOcclusion(false);
    occB->setCastOcclusion(false);
    gfx->setCanvas(occMap);
    vol->beginOcclusionMap(gfx, 320.f, 40.f, 22.f);
    vol->drawOccluders2D(gfx);
    gfx->setCanvas();
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->scatter(gfx, occMap->getTexture());
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "scatter", "scatter_off", bg);
}

TEST_CASE("graphics.imageAudit.textureSources") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);

    std::unique_ptr<ImageData> dst(makeRgba(32, 32, 50, 90, 160));
    std::unique_ptr<ImageData> src(makeRgba(16, 16, 220, 70, 40));
    dst->paste(src.get(), 8, 8, 0, 0, 16, 16);
    Texture *pasted = gfx->newTexture(dst.get());
    REQUIRE(pasted != nullptr);
    studio.subject->setTexture(pasted);
    auditGpuFrame(gfx, "texsrc", "imagedata_paste", bg);

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texfile", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string texDir = pathBesideThisSource("Textures");
    fs->allowMountingForPath(texDir);
    REQUIRE(fs->mount(texDir, "", false));
    std::unique_ptr<eve::filesystem::FileData> diffFile(fs->read("Rock1_Diffuse.png"));
    std::unique_ptr<eve::filesystem::FileData> nrmFile(fs->read("Rock_Normal.png"));
    REQUIRE(diffFile.get() != nullptr);
    REQUIRE(nrmFile.get() != nullptr);
    auto *imgMod = eve::image::Image::create();
    std::unique_ptr<ImageData> diffImg(imgMod->newImageData(diffFile.get()));
    std::unique_ptr<ImageData> nrmImg(imgMod->newImageData(nrmFile.get()));
    REQUIRE(diffImg.get() != nullptr);
    REQUIRE(nrmImg.get() != nullptr);
    Texture *diff = gfx->newTexture(diffImg.get(), TextureCreateInfo::withMipmaps(true));
    Texture *nrm = gfx->newTexture(nrmImg.get(), TextureCreateInfo::withMipmaps(true));
    REQUIRE(diff != nullptr);
    REQUIRE(nrm != nullptr);
    REQUIRE(diff->getMipmapCount() > 1);
    REQUIRE(diff->getSampler().maxAnisotropy > 1.f);
    studio.subject->setTexture(diff);
    studio.subject->setNormalTexture(nrm);
    auditGpuFrame(gfx, "texsrc", "file_rock", bg);
}

TEST_CASE("graphics.imageAudit.renderControlToggles") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);

    rc->disable("depthTest");
    rc->compile();
    auditGpuFrame(gfx, "rctrl", "depth_test_off", bg);
    rc->enable("depthTest");

    rc->disable("hair");
    rc->compile();
    auditGpuFrame(gfx, "rctrl", "hair_off", bg);
    rc->enable("hair");

    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 210, 160, 70));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.35f);
    card->setCastShadow(false);
    studio.subject->setVisible(false);
    rc->disable("forward");
    rc->compile();
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    auditGpuFrame(gfx, "rctrl", "forward_off_hair", bg);
    rc->enable("forward");
    rc->compile();
    studio.subject->setVisible(true);
    card->setVisible(false);

    TextureCreateInfo mip = TextureCreateInfo::withMipmaps(true, 16.f);
    std::vector<uint8_t> px(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            const size_t i = size_t((y * 32 + x) * 4);
            px[i + 0] = on ? 210 : 50;
            px[i + 1] = on ? 180 : 70;
            px[i + 2] = on ? 90 : 160;
            px[i + 3] = 255;
        }
    }
    Texture *aniso = gfx->newTexture(32, 32, px.data(), mip);
    gfx->setTextureSamplerParams(aniso, "linear", "linear", 16.f, 0.f);
    studio.ground->setTexture(aniso);
    studio.subject->setTexture(aniso);
    auditGpuFrame(gfx, "rctrl", "aniso_mip", bg);
}

TEST_CASE("graphics.imageAudit.sprite2dCamera") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    Texture *cells[4] = {
        makeSolid(gfx, 220, 80, 70),
        makeSolid(gfx, 70, 190, 90),
        makeSolid(gfx, 70, 110, 220),
        makeSolid(gfx, 230, 200, 60),
    };
    Renderable2D *packed[4];
    for (int i = 0; i < 4; ++i) {
        packed[i] = makeSprite(cells[i], float((i % 2) * 200), float((i / 2) * 150), 200.f, 150.f);
    }
    auditGpu2D(gfx, "sprite2d", "identity", bg);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.3f);
    const float visW = 400.f / 1.3f;
    const float visH = 300.f / 1.3f;
    const float x0 = 200.f - visW * 0.5f;
    const float y0 = 150.f - visH * 0.5f;
    const float cw = visW * 0.5f;
    const float ch = visH * 0.5f;
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x = x0 + float(i % 2) * cw;
        packed[i]->transform()->y = y0 + float(i / 2) * ch;
        packed[i]->sprite()->width = cw;
        packed[i]->sprite()->height = ch;
        packed[i]->transform()->sx = 1.f;
        packed[i]->transform()->sy = 1.f;
    }
    auditGpu2D(gfx, "sprite2d", "zoom_1_3", bg);

    cam->setZoom(1.f);
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x = float((i % 2) * 200);
        packed[i]->transform()->y = float((i / 2) * 150);
        packed[i]->sprite()->width = 100.f;
        packed[i]->sprite()->height = 75.f;
        packed[i]->transform()->sx = 2.f;
        packed[i]->transform()->sy = 2.f;
    }
    auditGpu2D(gfx, "sprite2d", "scale_sx_sy", bg);

    cam->data()->active = false;
    std::vector<uint32_t> frag(custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    REQUIRE(sh != nullptr);
    sh->declareFloat("factor");
    sh->sendFloat("factor", 0.55f);
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x = float((i % 2) * 200);
        packed[i]->transform()->y = float((i / 2) * 150);
        packed[i]->sprite()->width = 200.f;
        packed[i]->sprite()->height = 150.f;
        packed[i]->transform()->sx = 1.f;
        packed[i]->transform()->sy = 1.f;
        packed[i]->sprite()->shader = sh;
    }
    auditGpu2D(gfx, "sprite2d", "custom_spv", bg);
}

TEST_CASE("graphics.imageAudit.overlayFx") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    Texture *sceneTex = capturePresented(gfx);
    REQUIRE(sceneTex != nullptr);

    auto *stylize = eve::stylize::Stylize::create();
    eve::stylize::StyleChain *chain = stylize->newChain();
    eve::stylize::StylePass *cartoon = stylize->newPass(gfx, "cartoon");
    eve::stylize::StylePass *pixel = stylize->newPass(gfx, "pixel");
    REQUIRE(chain != nullptr);
    REQUIRE(cartoon != nullptr);
    REQUIRE(pixel != nullptr);
    chain->add(cartoon);
    chain->add(pixel);
    Canvas *dest = gfx->newCanvas(400, 300);
    Canvas *temp = gfx->newCanvas(400, 300);
    chain->apply(gfx, sceneTex, dest, temp);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(gfx, "overlay", "stylechain_cartoon_pixel", bg);
    delete pixel;
    delete cartoon;
    delete chain;

    auto raw = [&]() {
        std::ifstream in(pathBesideThisSource("fonts/FontAwesome.ttf"), std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }();
    REQUIRE(!raw.empty());
    eve::data::ByteData bytes(raw.data(), raw.size());
    std::unique_ptr<eve::font::FontData> fd(eve::font::Font::create()->newFontData(&bytes, 32));
    REQUIRE(fd.get() != nullptr);
    Font *gpuFont = gfx->newFont(fd.get(), std::string("\xEF\x80\x80"));
    REQUIRE(gpuFont != nullptr);
    gfx->setFont(gpuFont);
    gfx->setPresentOverlay(&printOverlayThunk, gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    auditGpuFrame(gfx, "overlay", "font_print", bg);
    gfx->setPresentOverlay(nullptr, nullptr);

    auto *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em = parts->newEmitter(64);
    em->setPosition(200.f, 140.f);
    em->setEmissionArea("rect", 120.f, 70.f);
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setParticleSize(36.f, 36.f);
    em->setColorStart(1.f, 0.55f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.55f, 0.15f, 1.f);
    em->setTexture(makeSolid(gfx, 230, 140, 50));
    em->emit(48);
    auditGpuFrame(gfx, "overlay", "particles", bg, false, [&]() {
        eve::particles::ParticleRenderSystem::render(gfx);
    });
}

TEST_CASE("graphics.imageAudit.voxelAndHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    studio.subject->setVisible(false);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    eve::voxel::VoxelWorld world;
    for (int z = 0; z < 14; ++z) {
        for (int x = 0; x < 14; ++x) world.setVoxel(x, 0, z, uint8_t(1 + (x + z) % 4));
    }
    for (int y = 1; y < 8; ++y) {
        for (int z = 5; z < 8; ++z)
            for (int x = 5; x < 8; ++x) world.setVoxel(x, y, z, 3);
    }
    REQUIRE(world.remeshDirty() > 0);
    Texture *atlas = makeAtlasGrid(gfx, 2, 16);
    studio.cam->setEye(7.f, 9.f, 18.f);
    studio.cam->setTarget(7.f, 2.f, 7.f);
    auto drawVoxels = [&]() {
        auto cd = studio.cam->data();
        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float aspect = float(gfx->getWidth()) / float(std::max(gfx->getHeight(), 1));
        const glm::mat4 projM =
            perspectiveVulkanRH_ZO(cd->fovYDeg * 0.017453292519943295f, aspect, cd->nearZ, cd->farZ);
        const glm::mat4 vp = projM * viewM;
        gfx->setMesh3DViewProj(vp);
        world.selectVisible(&vp[0][0], cd->eyeX, cd->eyeY, cd->eyeZ, 40.f, true);
        world.drawVisible(gfx, atlas, 2);
    };
    auditGpuFrame(gfx, "voxel_hair", "voxels", bg, false, drawVoxels);

    studio.cam->setEye(0.f, 1.5f, 4.0f);
    studio.cam->setTarget(0.f, 0.15f, 0.f);
    studio.ground->setVisible(true);
    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 200, 150, 80));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.2f);
    card->setCastShadow(false);
    auditGpuFrame(gfx, "voxel_hair", "hair_cards", bg);
}

TEST_CASE("graphics.imageAudit.uiAndMap") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *map = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 1 + (x + y) % 15);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };
    presentMap();
    auditSwapchain(gfx, "uimap", "ortho_fill", bg);

    cam->setZoom(1.25f);
    presentMap();
    auditSwapchain(gfx, "uimap", "ortho_zoom", bg);
    cam->setZoom(1.f);

    auto *ui = eve::ui::UI::create();
    REQUIRE(ui->initBackend());
    ui->beginFrameAndRender();
    presentMap();
    auditSwapchain(gfx, "uimap", "imgui_hook", bg);
    ui->dispatchEvents();

    layer->setVisible(false);
    eve::map::TileLayer *logic = map->newLayer(16, 12, 16.f, 16.f);
    eve::map::TileLayer *display = map->newLayer(1, 1, 16.f, 16.f);
    logic->setTileset(tiles, 1, 4);
    display->setTileset(tiles, 1, 4);
    display->setTilesetTileSize(8, 8);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x) logic->setTile(x, y, ((x + y) % 3 == 0) ? 1 : 0);
    eve::map::DualGridOptions opts;
    opts.filledGid = 1;
    opts.hideLogic = true;
    REQUIRE(eve::map::resolveDualGrid(logic, display, opts, nullptr));
    display->setCamera(cam);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(gfx, "uimap", "dual_grid", bg);

    display->setVisible(false);
    eve::map::TileLayer *iso = map->newLayer(18, 18, 32.f, 16.f);
    iso->config()->orientation = eve::map::MapOrientation::Isometric;
    iso->config()->originX = 200.f;
    iso->config()->originY = 16.f;
    iso->setTileset(tiles, 1, 4);
    iso->setTilesetTileSize(8, 8);
    iso->setCamera(cam);
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 18; ++x) iso->setTile(x, y, 1 + (x * 3 + y) % 15);
    presentMap();
    auditSwapchain(gfx, "uimap", "isometric", bg);
}

TEST_CASE("graphics.imageAudit.skinnedStill") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    std::unique_ptr<eve::model3d::ModelData> model(loadCesiumMan("ev_ut_image_audit_skin_still"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);
    Mesh *gpu = gfx->newMeshFromAssimp(*src);
    REQUIRE(gpu != nullptr);
    studio.subject->setMesh(gpu);
    studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
    studio.subject->setScale(1.15f, 1.15f, 1.15f);
    studio.subject->setPosition(0.f, 0.f, 0.f);
    studio.subject->setYaw(1.2f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);
    auditGpuFrame(gfx, "skinned", "bind_pose", bg);
}

TEST_CASE("graphics.imageAudit.skinnedPose") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    std::unique_ptr<eve::model3d::ModelData> model(loadCesiumMan("ev_ut_image_audit_skin_pose"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);

    using eve::animation::AnimClip;
    using eve::animation::AnimImporter;
    using eve::animation::AnimPose;
    using eve::animation::AnimSkeleton;
    using eve::animation::AnimSkin;
    std::unique_ptr<AnimSkeleton> sk(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(sk.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), sk.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), mi, sk.get()));
    REQUIRE(skin.get() != nullptr);

    auto uploadAt = [&](float t) {
        AnimPose pose;
        clip->sample(t, &pose, sk.get());
        pose.computeWorld(sk.get());
        std::vector<float> pos;
        REQUIRE(skin->skinPositionsTo(&pose, pos));
        return uploadSkinnedAiMesh(gfx, src, pos);
    };

    studio.subject->setMesh(uploadAt(0.f));
    studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
    studio.subject->setScale(1.15f, 1.15f, 1.15f);
    studio.subject->setPosition(0.f, 0.f, 0.f);
    studio.subject->setYaw(1.2f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);
    auditGpuFrame(gfx, "skinned", "pose_t0", bg);

    studio.subject->setMesh(uploadAt(clip->getDuration() * 0.5f));
    auditGpuFrame(gfx, "skinned", "pose_tmid", bg);
}

TEST_CASE("graphics.imageAudit.reloadTex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texreload", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string texDir = pathBesideThisSource("Textures");
    fs->allowMountingForPath(texDir);
    REQUIRE(fs->mount(texDir, "", false));
    Texture *diff = gfx->newTextureFromFile("Rock1_Diffuse.png");
    REQUIRE(diff != nullptr);
    studio.subject->setTexture(diff);
    auditGpuFrame(gfx, "reload", "before", bg);
    REQUIRE(gfx->reloadTextureFromFile("Rock1_Diffuse.png"));
    auditGpuFrame(gfx, "reload", "after", bg);
}

TEST_CASE("graphics.imageAudit.morphInflate") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    const float hs = 0.45f;
    const float pos[] = {
        -hs, -hs, -hs, hs, -hs, -hs, hs, hs, -hs, -hs, hs, -hs,
        -hs, -hs,  hs, hs, -hs,  hs, hs, hs,  hs, -hs, hs,  hs,
    };
    float nrm[24];
    float uv[16];
    for (int i = 0; i < 8; ++i) {
        float x = pos[i * 3 + 0], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) len = 1.f;
        nrm[i * 3 + 0] = x / len;
        nrm[i * 3 + 1] = y / len;
        nrm[i * 3 + 2] = z / len;
        uv[i * 2 + 0] = (i & 1) ? 1.f : 0.f;
        uv[i * 2 + 1] = (i & 2) ? 1.f : 0.f;
    }
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                            3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    Mesh *morphMesh = gfx->newMeshFromArrays(pos, nrm, uv, 8, idx, 36);
    morphMesh->initMorphBase(8, pos, nrm, uv);
    float delta[24];
    for (int i = 0; i < 8; ++i) {
        delta[i * 3 + 0] = nrm[i * 3 + 0] * 0.28f;
        delta[i * 3 + 1] = nrm[i * 3 + 1] * 0.28f;
        delta[i * 3 + 2] = nrm[i * 3 + 2] * 0.28f;
    }
    REQUIRE(morphMesh->addMorphTarget("inflate", delta));
    studio.subject->setMesh(morphMesh);
    studio.subject->setTexture(makeSolid(gfx, 200, 160, 90));
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(gfx, "morph", "base", bg);
    morphMesh->setMorphWeight("inflate", 1.f);
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(gfx, "morph", "inflate", bg);
    morphMesh->setMorphWeight("inflate", 0.45f);
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(gfx, "morph", "inflate_mid", bg);
}

TEST_CASE("graphics.imageAudit.procgenAssets") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto *pg = eve::procgen::Procgen::create();
    REQUIRE(pg != nullptr);
    eve::procgen::Params texP;
    texP.setSeed(11);
    texP.setSize(64, 64);
    texP.setInt("colors", 6);
    texP.setInt("pixelSize", 2);
    texP.setInt("seamless", 1);
    const char *recipes[] = {"tex.soil", "tex.stone", "tex.marble"};
    for (const char *id : recipes) {
        Texture *tex = pg->generateTexture(id, &texP, gfx);
        REQUIRE(tex != nullptr);
        studio.subject->setTexture(tex);
        auditGpuFrame(gfx, "procgen", id, bg);
    }

    Texture *nrm = nullptr;
    {
        eve::image::ImageData *nimg = pg->generateNormalImage("tex.stone", &texP);
        REQUIRE(nimg != nullptr);
        nrm = gfx->newTexture(nimg);
        delete nimg;
    }
    REQUIRE(nrm != nullptr);
    studio.subject->setNormalTexture(nrm);
    studio.subject->setTexture(pg->generateTexture("tex.stone", &texP, gfx));
    auditGpuFrame(gfx, "procgen", "stone_normal", bg);
    studio.subject->setNormalTexture(nullptr);

    eve::procgen::Params meshP;
    meshP.setSeed(1);
    meshP.setInt("resolution", 16);
    meshP.setString("field", "sphere");
    meshP.setFloat("radius", 0.7f);
    meshP.setFloat("isolevel", 0.f);
    Mesh *mc = pg->generateMesh("mesh.marchingcubes", &meshP, gfx);
    REQUIRE(mc != nullptr);
    studio.subject->setMesh(mc);
    studio.subject->setTexture(pg->generateTexture("tex.marble", &texP, gfx));
    studio.subject->setScale(0.7f, 0.7f, 0.7f);
    auditGpuFrame(gfx, "procgen", "marching_cubes", bg);

    Texture *sceneTex = capturePresented(gfx);
    REQUIRE(sceneTex != nullptr);
    Canvas *dest = gfx->newCanvas(400, 300);
    REQUIRE(dest != nullptr);
    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setMode("ssaa");
    aa->setQuality("medium");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(gfx, "procgen", "ssaa_medium", bg);
    aa->setQuality("high");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(gfx, "procgen", "ssaa_high", bg);
}

TEST_CASE("graphics.imageAudit.mapFovHex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *map = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 1 + (x + y) % 15);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    std::unique_ptr<eve::map::Fov> fov(map->newFov(layer));
    REQUIRE(fov.get() != nullptr);
    fov->setAlgorithm("shadowcast");
    fov->setRadiusMetric("chebyshev");
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        fov->setOpaque(8, y, true);
        layer->setTile(8, y, 2);
    }
    fov->addRevealer(4, 9, 10);
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            layer->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(gfx, "mapfov", "shadowcast", bg);

    fov->setAlgorithm("raycast");
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            layer->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(gfx, "mapfov", "raycast", bg);

    std::vector<uint8_t> r8;
    REQUIRE(fov->fillMaskR8(r8));
    const int mw = fov->getWidth();
    const int mh = fov->getHeight();
    REQUIRE(mw == 25);
    REQUIRE(mh == 19);
    std::vector<uint8_t> rgba(size_t(mw * mh * 4));
    for (int i = 0; i < mw * mh; ++i) {
        const uint8_t v = r8[size_t(i)];
        const size_t o = size_t(i) * 4u;
        rgba[o + 0] = uint8_t(50 + int(v) * 180 / 255);
        rgba[o + 1] = uint8_t(90 + int(v) * 110 / 255);
        rgba[o + 2] = uint8_t(200 - int(v) * 70 / 255);
        rgba[o + 3] = 255;
    }
    Texture *maskVis = gfx->newTexture(mw, mh, rgba.data());
    REQUIRE(maskVis != nullptr);
    REQUIRE(fov->buildMaskTexture(gfx) != nullptr);
    layer->setVisible(false);
    auto *maskSp = makeSprite(maskVis, 0.f, 0.f, 400.f, 300.f, false);
    presentMap();
    auditSwapchain(gfx, "mapfov", "mask_vis", bg);
    maskSp->sprite()->visible = false;

    auto *pg = eve::procgen::Procgen::create();
    eve::procgen::Params dungeonP;
    dungeonP.setSeed(1);
    dungeonP.setSize(25, 19);
    eve::procgen::Grid2D *grid = pg->generate("dungeon.bsp", &dungeonP);
    REQUIRE(grid != nullptr);
    pg->setPaletteGid("audit", "wall", 2);
    pg->setPaletteGid("audit", "floor", 5);
    pg->setPaletteGid("audit", "corridor", 6);
    layer->setVisible(true);
    REQUIRE(pg->applyToLayer(grid, "audit", layer));
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x)
            if (layer->getTile(x, y) == 0) layer->setTile(x, y, 5);
    delete grid;
    presentMap();
    auditSwapchain(gfx, "mapfov", "dungeon_bsp", bg);
    layer->setVisible(false);

    eve::map::TileLayer *hex = map->newLayer(18, 16, 32.f, 28.f);
    hex->config()->orientation = eve::map::MapOrientation::Hexagonal;
    hex->config()->hexSideLength = 16.f;
    hex->config()->originX = 16.f;
    hex->config()->originY = 8.f;
    hex->setTileset(tiles, 1, 4);
    hex->setTilesetTileSize(8, 8);
    hex->setCamera(cam);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 18; ++x) hex->setTile(x, y, 1 + (x * 3 + y) % 15);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(gfx, "mapfov", "hexagonal", bg);
    hex->setVisible(false);

    eve::map::TileLayer *stag = map->newLayer(18, 20, 32.f, 16.f);
    stag->config()->orientation = eve::map::MapOrientation::Staggered;
    stag->config()->staggerAxis = eve::map::StaggerAxis::Y;
    stag->config()->staggerIndex = eve::map::StaggerIndex::Odd;
    stag->config()->originX = 0.f;
    stag->config()->originY = 0.f;
    stag->setTileset(tiles, 1, 4);
    stag->setTilesetTileSize(8, 8);
    stag->setCamera(cam);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 18; ++x) stag->setTile(x, y, 1 + (x + y * 2) % 15);
    presentMap();
    auditSwapchain(gfx, "mapfov", "staggered", bg);
}

TEST_CASE("graphics.imageAudit.particleSkin") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    std::unique_ptr<eve::model3d::ModelData> model(loadCesiumMan("ev_ut_image_audit_skin_part"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);
    Mesh *gpu = gfx->newMeshFromAssimp(*src);
    REQUIRE(gpu != nullptr);
    studio.subject->setMesh(gpu);
    studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
    studio.subject->setScale(1.15f, 1.15f, 1.15f);
    studio.subject->setPosition(0.f, 0.f, 0.f);
    studio.subject->setYaw(1.2f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);

    using eve::animation::AnimClip;
    using eve::animation::AnimImporter;
    using eve::animation::AnimPose;
    using eve::animation::AnimSkeleton;
    using eve::animation::AnimSkin;
    std::unique_ptr<AnimSkeleton> sk(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(sk.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), sk.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), mi, sk.get()));
    REQUIRE(skin.get() != nullptr);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(0.f, pose.get(), sk.get());
    pose->computeWorld(sk.get());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));

    auto *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em = parts->newEmitter(128);
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setParticleSize(28.f, 28.f);
    em->setColorStart(1.f, 0.45f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.45f, 0.15f, 1.f);
    em->setTexture(makeSolid(gfx, 230, 120, 40));
    em->setEmissionArea("none", 0.f, 0.f);
    em->setSkinScale(90.f);
    em->setSkinPlane("xy");
    em->setSkinSource(skin.get(), pose.get());
    REQUIRE(em->hasSkinSource());
    em->emitFromSkin(64);
    REQUIRE(em->getCount() >= 16);
    auditGpuFrame(gfx, "partskin", "emit_from_skin", bg, false, [&]() {
        eve::particles::ParticleRenderSystem::render(gfx);
    });

    em->clearSkinSource();
    std::unique_ptr<AnimSkeleton> attachSk(new AnimSkeleton());
    const int root = attachSk->addBone("root", -1);
    std::unique_ptr<AnimPose> bonePose(new AnimPose());
    attachSk->applyBindPose(bonePose.get());
    bonePose->setLocalPosition(root, 200.f, 160.f, 0.f);
    bonePose->computeWorld(attachSk.get());
    em->setParticleSize(40.f, 40.f);
    em->attachToBone(bonePose.get(), root);
    em->syncAttach();
    em->emit(24);
    auditGpuFrame(gfx, "partskin", "attach_bone", bg, false, [&]() {
        eve::particles::ParticleRenderSystem::render(gfx);
    });
}

TEST_CASE("graphics.imageAudit.materialHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    studio.subject->setReceiveLight(false);
    studio.subject->setTexture(makeSolid(gfx, 220, 90, 70));
    auditGpuFrame(gfx, "mathair", "receive_off", bg);
    studio.subject->setReceiveLight(true);

    Material *hairMat = gfx->newMaterial();
    hairMat->setShadingModel("hair");
    hairMat->setAlbedoTexture(makeSolid(gfx, 210, 160, 70));
    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    hairMat->setShader(hairSh);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setMaterial(hairMat);
    card->setPosition(0.f, 0.05f, 0.35f);
    card->setCastShadow(false);
    auditGpuFrame(gfx, "mathair", "hair_material", bg);

    studio.subject->setVisible(false);
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    auditGpuFrame(gfx, "mathair", "hair_only_packed", bg);
}

TEST_CASE("graphics.imageAudit.clusteredHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    studio.sun->setEnabled(false);
    studio.cam->setAmbient(0.08f, 0.08f, 0.09f);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("clustered");
    rc->enable("hair");
    rc->enable("forward");
    rc->compile();

    const float cols[12][3] = {
        {1.f, 0.25f, 0.2f}, {0.2f, 1.f, 0.3f}, {0.25f, 0.4f, 1.f}, {1.f, 0.9f, 0.2f},
        {1.f, 0.4f, 0.9f},  {0.2f, 0.95f, 0.9f}, {1.f, 0.55f, 0.15f}, {0.6f, 0.3f, 1.f},
        {0.4f, 1.f, 0.55f}, {1.f, 0.2f, 0.45f}, {0.55f, 0.75f, 1.f}, {0.95f, 0.95f, 0.7f},
    };
    for (int i = 0; i < 12; ++i) {
        auto *l = Light3D::createLight("point");
        const float a = float(i) / 12.f * 6.2831853f;
        l->setPosition(std::cos(a) * 1.6f, 0.5f + 0.2f * std::sin(a * 2.f), std::sin(a) * 1.6f);
        l->setColor(cols[i][0], cols[i][1], cols[i][2], 3.2f);
        l->setRadius(3.5f);
        l->setEnabled(true);
    }
    auditGpuFrame(gfx, "cluster", "twelve_points", bg);

    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 210, 160, 70));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.4f);
    card->setCastShadow(false);
    auditGpuFrame(gfx, "cluster", "hair_plus_clustered", bg);

    rc->disable("clustered");
    rc->compile();
    auditGpuFrame(gfx, "cluster", "packed_fallback", bg);
    rc->enable("clustered");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.clothFluid") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);

    auto presentExtra = [&](const std::function<void()> &extra) {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        std::vector<DrawItem2D> items;
        RenderSystem::collectSprites(items);
        RenderSystem::drawItems(*gfx, items, false);
        extra();
        gfx->present();
    };

    eve::physics::Cloth cloth(20, 16, 20.f, 8.f, 8.f);
    cloth.pinTopRow();
    cloth.setGravity(0.f, 0.f);
    cloth.setColor(0.95f, 0.35f, 0.2f, 1.f);
    cloth.update(0.016f);
    presentExtra([&]() { cloth.draw(gfx); });
    auditSwapchain(gfx, "phys2d", "cloth", bg);

    cloth.setGravity(0.f, 400.f);
    cloth.applyForce(80.f, 0.f);
    for (int i = 0; i < 8; ++i) cloth.update(0.016f);
    presentExtra([&]() { cloth.draw(gfx); });
    auditSwapchain(gfx, "phys2d", "cloth_wind", bg);

    eve::physics::Fluid fluid(512);
    fluid.setGravity(0.f, 0.f);
    fluid.setParticleSize(18.f);
    fluid.setColor(0.25f, 0.6f, 0.95f, 1.f);
    fluid.setBounds(0.f, 0.f, 400.f, 300.f);
    for (int y = 30; y < 280; y += 16)
        for (int x = 30; x < 380; x += 16) fluid.emit(float(x), float(y), 1);
    REQUIRE(fluid.getParticleCount() > 100);
    presentExtra([&]() { fluid.draw(gfx); });
    auditSwapchain(gfx, "phys2d", "fluid", bg);
}

TEST_CASE("graphics.imageAudit.particlePresets") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em = parts->newEmitter(128);
    em->setTexture(makeSolid(gfx, 230, 160, 50));
    em->setPosition(200.f, 160.f);

    auto burst = [&](const char *preset, const char *phase, float r, float g, float b) {
        em->reset();
        em->applyPreset(preset);
        em->setParticleLifetime(8.f, 8.f);
        em->setSpeed(0.f, 0.f);
        em->setParticleSize(32.f, 32.f);
        em->setColorStart(r, g, b, 1.f);
        em->setColorEnd(r, g, b, 1.f);
        em->setEmissionArea("rect", 140.f, 80.f);
        em->emit(48);
        REQUIRE(em->getCount() >= 16);
        auditGpuFrame(gfx, "presets", phase, bg, false, [&]() {
            eve::particles::ParticleRenderSystem::render(gfx);
        });
    };
    burst("spark", "spark", 1.f, 0.85f, 0.25f);
    burst("smoke", "smoke", 0.75f, 0.78f, 0.82f);
    burst("fire", "fire", 1.f, 0.4f, 0.12f);

    em->reset();
    REQUIRE(em->applyConfig("{\"preset\":\"fire\",\"x\":200,\"y\":160,\"particleSize\":[36,36]}"));
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setColorStart(1.f, 0.45f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.45f, 0.15f, 1.f);
    em->setEmissionArea("rect", 140.f, 80.f);
    em->emit(40);
    auditGpuFrame(gfx, "presets", "json_fire", bg, false, [&]() {
        eve::particles::ParticleRenderSystem::render(gfx);
    });
}

TEST_CASE("graphics.imageAudit.mapPath") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *map = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 5);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    std::unique_ptr<eve::map::Pathfinder> pf(map->newPathfinder(layer));
    REQUIRE(pf.get() != nullptr);
    pf->setDiagonal(true);
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        pf->setBlocked(12, y, true);
        layer->setTile(12, y, 2);
    }
    std::unique_ptr<eve::map::Path> path(pf->findPath(1, 9, 23, 9));
    REQUIRE(path.get() != nullptr);
    REQUIRE(path->getLength() > 4);
    for (int i = 0; i < path->getLength(); ++i)
        layer->setTile(path->getX(i), path->getY(i), 4);
    layer->setTile(1, 9, 8);
    layer->setTile(23, 9, 12);
    presentMap();
    auditSwapchain(gfx, "mappath", "astar", bg);

    std::unique_ptr<eve::map::FlowField> field(pf->buildFlowField(23, 9));
    REQUIRE(field.get() != nullptr);
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            if (layer->getTile(x, y) == 2) continue;
            layer->setTile(x, y, field->isReachable(x, y) ? (6 + (x + y) % 3) : 10);
        }
    }
    presentMap();
    auditSwapchain(gfx, "mappath", "flow_field", bg);

    std::unique_ptr<eve::map::Path> group(pf->findGroupPath(2, 2, 23, 9));
    REQUIRE(group.get() != nullptr);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x)
            if (layer->getTile(x, y) != 2) layer->setTile(x, y, 5);
    for (int i = 0; i < group->getLength(); ++i)
        layer->setTile(group->getX(i), group->getY(i), 14);
    presentMap();
    auditSwapchain(gfx, "mappath", "group_flow", bg);
}

TEST_CASE("graphics.imageAudit.reloadTexBytes") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texbytes", true));
    REQUIRE(fs->setupWriteDirectory());
    eve::image::Image::create();

    auto writeSolidPng = [&](uint8_t r, uint8_t g, uint8_t b) {
        eve::image::ImageData img(32, 32, "RGBA8");
        auto *px = static_cast<uint8_t *>(img.getData());
        for (int i = 0; i < 32 * 32; ++i) {
            px[i * 4 + 0] = r;
            px[i * 4 + 1] = g;
            px[i * 4 + 2] = b;
            px[i * 4 + 3] = 255;
        }
        std::unique_ptr<eve::filesystem::FileData> encoded(
            img.encode(medialoader::FormatHandler::ENCODED_PNG, "audit_reload_swap.png", true));
        REQUIRE(encoded.get() != nullptr);
    };

    writeSolidPng(220, 60, 50);
    Texture *tex = gfx->newTextureFromFile("audit_reload_swap.png");
    REQUIRE(tex != nullptr);
    studio.subject->setTexture(tex);
    auditGpuFrame(gfx, "texbytes", "red", bg);

    writeSolidPng(50, 200, 80);
    REQUIRE(gfx->reloadTextureFromFile("audit_reload_swap.png"));
    auditGpuFrame(gfx, "texbytes", "green", bg);

    writeSolidPng(60, 90, 220);
    REQUIRE(gfx->reloadTextureFromFile("audit_reload_swap.png"));
    auditGpuFrame(gfx, "texbytes", "blue", bg);
}

TEST_CASE("graphics.imageAudit.postQuality") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    const int w = 400, h = 300;

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    Texture *sceneTex = capturePresented(gfx);
    REQUIRE(sceneTex != nullptr);
    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *depth = gb->getDepthTexture();
    REQUIRE(depth != nullptr);

    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(composed != nullptr);
    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f,
                  40.f);
    ao->setMode("ssao");
    const char *qualities[] = {"low", "medium", "high"};
    for (const char *q : qualities) {
        ao->setQuality(q);
        Canvas *aoMap = gfx->newCanvas(ao->resolutionFor(w), ao->resolutionFor(h));
        REQUIRE(aoMap != nullptr);
        ao->computeTo(gfx, depth, aoMap);
        gfx->setCanvas(composed);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        ao->applyOverlay(gfx, aoMap->getTexture());
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(gfx, "postq", (std::string("ssao_") + q).c_str(), bg);
    }

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f,
                   40.f);
    vol->setFogColor(0.45f, 0.5f, 0.55f);
    vol->setFogHeight(-0.7f);
    vol->setFogHeightFalloff(0.8f);
    vol->setFogStart(1.2f);
    vol->setFogEnd(9.f);
    vol->setDensity(0.35f);
    vol->setMode("fog");
    for (const char *q : qualities) {
        vol->setQuality(q);
        gfx->setCanvas(composed);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        vol->applyFog(gfx, depth);
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(gfx, "postq", (std::string("fog_") + q).c_str(), bg);
    }

    vol->setMode("raymarch");
    vol->setLightDirection(0.45f, 1.f, 0.35f);
    vol->setLightScreenUV(0.62f, 0.28f);
    vol->setIntensity(0.8f);
    vol->setQuality("high");
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->rayMarch(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(gfx, "postq", "raymarch_high", bg);

    rc->disable("gbuffer");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.dualIsoFov") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *map = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    eve::map::TileLayer *ortho = map->newLayer(25, 19, 16.f, 16.f);
    ortho->setTileset(tiles, 1, 4);
    ortho->setTilesetTileSize(8, 8);
    ortho->setCamera(cam);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) ortho->setTile(x, y, 1 + (x + y) % 15);

    std::unique_ptr<eve::map::Fov> fov(map->newFov(ortho));
    REQUIRE(fov.get() != nullptr);
    fov->setRadiusMetric("chebyshev");
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        fov->setOpaque(8, y, true);
        ortho->setTile(8, y, 2);
    }
    fov->addRevealer(4, 9, 10);

    auto paintFov = [&]() {
        for (int y = 0; y < 19; ++y) {
            for (int x = 0; x < 25; ++x) {
                const bool vis = fov->isVisible(x, y);
                ortho->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
            }
        }
    };
    fov->setAlgorithm("permissive");
    fov->compute();
    paintFov();
    presentMap();
    auditSwapchain(gfx, "dualiso", "fov_permissive", bg);

    fov->setAlgorithm("rectangle");
    fov->compute();
    paintFov();
    presentMap();
    auditSwapchain(gfx, "dualiso", "fov_rectangle", bg);
    ortho->setVisible(false);

    eve::map::TileLayer *logic = map->newLayer(16, 16, 32.f, 16.f);
    eve::map::TileLayer *display = map->newLayer(1, 1, 32.f, 16.f);
    logic->config()->orientation = eve::map::MapOrientation::Isometric;
    logic->config()->originX = 200.f;
    logic->config()->originY = 16.f;
    logic->setTileset(tiles, 1, 4);
    display->setTileset(tiles, 1, 4);
    display->setTilesetTileSize(8, 8);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) logic->setTile(x, y, ((x + y) % 3 == 0) ? 1 : 2);
    eve::map::DualGridOptions opts;
    opts.filledGid = 1;
    opts.hideLogic = true;
    REQUIRE(eve::map::resolveDualGrid(logic, display, opts, nullptr));
    display->setCamera(cam);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(gfx, "dualiso", "dual_isometric", bg);
}

TEST_CASE("graphics.imageAudit.avatarImage") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *mod = eve::avatar::Avatar::create();
    REQUIRE(mod != nullptr);
    eve::avatar::AvatarInstance *av = mod->newImageAvatar();
    REQUIRE(av != nullptr);
    av->setPosition(0.f, 0.f);
    REQUIRE(av->addLayer("tl", makeSolid(gfx, 90, 140, 200), 0));
    REQUIRE(av->addLayer("tr", makeSolid(gfx, 200, 90, 70), 1));
    REQUIRE(av->addLayer("bl", makeSolid(gfx, 70, 180, 110), 2));
    REQUIRE(av->addLayer("br", makeSolid(gfx, 220, 200, 60), 3));
    av->setLayerSize("tl", 200.f, 150.f);
    av->setLayerSize("tr", 200.f, 150.f);
    av->setLayerSize("bl", 200.f, 150.f);
    av->setLayerSize("br", 200.f, 150.f);
    av->setLayerOffset("tr", 200.f, 0.f);
    av->setLayerOffset("bl", 0.f, 150.f);
    av->setLayerOffset("br", 200.f, 150.f);
    av->sync();
    auditGpu2D(gfx, "avatar", "layers_packed", bg);

    av->setLayerColor("tl", 0.95f, 0.35f, 0.25f, 1.f);
    av->setLayerColor("tr", 0.25f, 0.55f, 0.95f, 1.f);
    av->sync();
    auditGpu2D(gfx, "avatar", "recolor", bg);

    REQUIRE(av->defineExpression("flash", "tl=1;tr=1;bl=1;br=1"));
    REQUIRE(av->applyExpression("flash"));
    av->setLayerColor("bl", 0.95f, 0.85f, 0.2f, 1.f);
    av->setLayerColor("br", 0.7f, 0.3f, 0.9f, 1.f);
    av->sync();
    auditGpu2D(gfx, "avatar", "expression", bg);
}

TEST_CASE("graphics.imageAudit.avatarVroid") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    studio.subject->setVisible(false);

    std::unique_ptr<eve::model3d::ModelData> model(loadCesiumMan("ev_ut_image_audit_vroid"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);
    Mesh *gpu = gfx->newMeshFromAssimp(*src);
    REQUIRE(gpu != nullptr);

    auto *mod = eve::avatar::Avatar::create();
    eve::avatar::AvatarInstance *av = mod->newVroidAvatar();
    REQUIRE(av != nullptr);
    REQUIRE(av->bindVroidModelData(model.get()));
    av->setMesh(gpu);
    av->setTexture(makeSolid(gfx, 200, 170, 120));
    av->setPosition3D(0.f, 0.f, 0.f);
    av->setRotation3D(1.2f, 0.f, 0.f);
    av->setScale3D(1.15f, 1.15f, 1.15f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);
    av->sync();
    auditGpuFrame(gfx, "vroid", "bind_pose", bg);
    av->setRotation3D(0.4f, 0.15f, 0.f);
    av->sync();
    auditGpuFrame(gfx, "vroid", "yaw_pitch", bg);
}

TEST_CASE("graphics.imageAudit.box2dDebug") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);

    auto *phys = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World> world(phys->newWorld(0.f, 0.f, true));
    REQUIRE(world.get() != nullptr);
    eve::physics::Body *ground = world->newBody("static", 200.f, 260.f);
    ground->newRectangleFixture(360.f, 40.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *wallL = world->newBody("static", 30.f, 150.f);
    wallL->newRectangleFixture(40.f, 220.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *wallR = world->newBody("static", 370.f, 150.f);
    wallR->newRectangleFixture(40.f, 220.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *box = world->newBody("dynamic", 200.f, 80.f);
    box->newRectangleFixture(70.f, 70.f, 1.f, 0.3f, 0.1f);
    eve::physics::Body *ball = world->newBody("dynamic", 120.f, 60.f);
    ball->newCircleFixture(28.f, 1.f, 0.2f, 0.4f);
    world->setGravity(0.f, 600.f);
    for (int i = 0; i < 20; ++i) world->update(1.f / 60.f);

    auto presentDebug = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        std::vector<DrawItem2D> items;
        RenderSystem::collectSprites(items);
        RenderSystem::drawItems(*gfx, items, false);
        world->drawDebug(gfx);
        gfx->present();
    };
    presentDebug();
    auditSwapchain(gfx, "box2d", "debug_draw", bg);
}

TEST_CASE("graphics.imageAudit.imageRotate") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    eve::image::Image::create();
    ImageData src(64, 64, "RGBA8");
    auto *px = static_cast<uint8_t *>(src.getData());
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool on = ((x / 8) + (y / 8)) & 1;
            const size_t i = size_t((y * 64 + x) * 4);
            px[i + 0] = on ? 220 : 50;
            px[i + 1] = on ? 90 : 180;
            px[i + 2] = on ? 70 : 210;
            px[i + 3] = 255;
        }
    }

    std::unique_ptr<ImageData> rot90(src.rotate(1.5707963f, "nearest", true));
    REQUIRE(rot90.get() != nullptr);
    Texture *t90 = gfx->newTexture(rot90.get());
    auto *full = makeSprite(t90, 0.f, 0.f, 400.f, 300.f, false);
    auditGpu2D(gfx, "imrotate", "nearest_90", bg);

    std::unique_ptr<ImageData> rotSprite(src.rotate(1.5707963f, "rotsprite", true));
    REQUIRE(rotSprite.get() != nullptr);
    full->sprite()->texture = gfx->newTexture(rotSprite.get());
    auditGpu2D(gfx, "imrotate", "rotsprite_90", bg);

    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    full->sprite()->visible = false;
    std::unique_ptr<ImageData> rot45(src.rotate(0.78539816f, "linear", true));
    REQUIRE(rot45.get() != nullptr);
    makeSprite(gfx->newTexture(rot45.get()), 80.f, 50.f, 240.f, 200.f, false);
    auditGpu2D(gfx, "imrotate", "linear_45", bg);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    Texture *bigTex = gfx->newTexture(&src);
    auto *big = makeSprite(bigTex, 0.f, 0.f, 800.f, 600.f, false);
    big->sprite()->camera = cam;
    big->sprite()->layer = 50;
    auditGpu2D(gfx, "imrotate", "cam_pan_tl", bg);
    cam->setPosition(400.f, 250.f);
    auditGpu2D(gfx, "imrotate", "cam_pan_mid", bg);
}

TEST_CASE("graphics.imageAudit.sceneGraph") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    eve::scene::SceneHost *host = eve::scene::SceneHost::createHost("audit3d");
    REQUIRE(host != nullptr);
    host->setTree(eve::scene::node(
        "root", {eve::scene::node("subject").withPosition(0.f, 0.15f, 0.f).withScale(0.55f)}));
    REQUIRE(host->linkRenderable3D("subject", studio.subject));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(gfx, "scene", "linked_rest", bg);

    host->setTreeReconcile(eve::scene::node(
        "root", {eve::scene::node("subject")
                     .withPosition(0.7f, 0.2f, 0.f)
                     .withRotation(0.8f, 0.15f, 0.f)
                     .withScale(0.6f)}));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(gfx, "scene", "linked_moved", bg);

    auto *left = Renderable3D::create();
    left->setMesh(studio.sphere);
    left->setTexture(makeSolid(gfx, 210, 90, 80));
    left->setCastShadow(true);
    host->setTree(eve::scene::node("root",
                                   {eve::scene::node("subject").withPosition(0.5f, 0.15f, 0.f).withScale(0.45f),
                                    eve::scene::node("left").withPosition(-1.0f, 0.2f, 0.f).withScale(0.4f)}));
    REQUIRE(host->linkRenderable3D("subject", studio.subject));
    REQUIRE(host->linkRenderable3D("left", left));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(gfx, "scene", "two_nodes", bg);
}

TEST_CASE("graphics.imageAudit.fovHeightHex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 400;
    s.height = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *map = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    eve::map::TileLayer *ortho = map->newLayer(25, 19, 16.f, 16.f);
    ortho->setTileset(tiles, 1, 4);
    ortho->setTilesetTileSize(8, 8);
    ortho->setCamera(cam);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) ortho->setTile(x, y, 1 + (x + y) % 15);

    std::unique_ptr<eve::map::Fov> fov(map->newFov(ortho));
    REQUIRE(fov.get() != nullptr);
    fov->setMode("heightmap");
    fov->setRadiusMetric("chebyshev");
    fov->setCliffBlock(1.f);
    fov->setEyeOffset(0.f);
    for (int y = 0; y < 19; ++y) fov->setElevation(10, y, 2.f);
    fov->addRevealer(3, 9, 12);
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            ortho->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(gfx, "fovmore", "heightmap", bg);
    ortho->setVisible(false);

    eve::map::TileLayer *hex = map->newLayer(18, 16, 32.f, 28.f);
    hex->config()->orientation = eve::map::MapOrientation::Hexagonal;
    hex->config()->hexSideLength = 16.f;
    hex->config()->originX = 16.f;
    hex->config()->originY = 8.f;
    hex->setTileset(tiles, 1, 4);
    hex->setTilesetTileSize(8, 8);
    hex->setCamera(cam);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 18; ++x) hex->setTile(x, y, 1 + (x * 3 + y) % 15);
    fov->bindLayer(hex);
    fov->setMode("grid2d");
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    fov->clearRevealers();
    fov->addRevealer(4, 6, 8);
    fov->compute();
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 18; ++x) {
            const bool vis = fov->isVisible(x, y);
            hex->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(gfx, "fovmore", "hex_topology", bg);
}


