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
#include <memory>
#include <string>
#include <vector>

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Volumetric.h"
#include "image/Image.h"
#include "image/ImageData.h"
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
    if (occupancy < 0.18f) return;
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
}

void auditGpuFrame(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg,
                   bool flicker = false) {
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
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    if (flicker) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
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

Texture *makeChecker(Graphics *gfx, int w, int h) {
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
    return gfx->newTexture(w, h, px.data());
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
        fillRect(img.get(), 24, 24, 16, 16, 0, 0, 0);
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
        fillRect(img.get(), 0, 30, 64, 2, 20, 23, 28);
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
