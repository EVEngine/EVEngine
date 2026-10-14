// Shared test helper translation unit.
//
// `saveImagePng` is declared in RenderImageAudit.h but consumed by more than one
// domain: the graphics image-audit tests (RenderImageAudit.cpp) and the voxel
// render tests (VoxelRenderFixtures.h). A `unit_test_<domain>` executable is its
// own link unit, so the definition has to be compiled into every domain that
// needs it -- an engine export macro cannot help, the symbol is test-side.
//
// This file therefore holds the definition without any TEST_CASE, and
// scripts/test_domains.py lists it under SHARED_SOURCES so the domain targets
// compile it; RenderImageAudit.cpp keeps the graphics-only test cases.

#include "RenderImageAudit.h"

#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using eve::image::ImageData;

bool saveImagePng(const ImageData &img, const std::string &path) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData   *png         = img.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false);
    if (!png) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    const bool    ok = out.good();
    if (ok) {
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
    }
    delete png;
    return ok && out.good();
}

// --- CPU-side framebuffer audit implementation (moved from RenderImageAudit.cpp; shared by every consumer domain) ---
namespace {

constexpr int   kTile          = 16;
constexpr int   kMaxDefects    = 32;
constexpr int   kEmptyDist2    = 14 * 14;  // ~0.055 in 8-bit
constexpr int   kDeadLumaSum   = 15;       // (r+2g+b) < 15 ≈ luma 0.02
constexpr float kOccTile       = 0.40f;
constexpr float kEmptyTile     = 0.12f;
constexpr float kFlickerTile   = 0.07f;
constexpr float kFlickerSevere = 0.15f;

int lumaSumU8(const uint8_t *p) { return int(p[0]) + 2 * int(p[1]) + int(p[2]); }

float lumaU8(const uint8_t *p) { return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f; }

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

void addDefect(RenderAuditResult &r, RenderDefect::Kind kind, int x, int y, int w, int h, float score,
               const std::string &detail) {
    if (int(r.defects.size()) >= kMaxDefects) return;
    x = std::max(0, x);
    y = std::max(0, y);
    RenderDefect d;
    d.kind   = kind;
    d.x      = x;
    d.y      = y;
    d.w      = std::max(1, w);
    d.h      = std::max(1, h);
    d.score  = score;
    d.detail = detail;
    r.defects.push_back(std::move(d));
}

struct TileStat {
    float occ   = 0.f;
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

void detectHoles(const std::vector<TileStat> &tiles, int tilesX, int tilesY, int w, int h, RenderAuditResult &r) {
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
            int              minX = tx, maxX = tx, minY = ty, maxY = ty, count = 0;
            std::vector<int> stack{seed};
            seen[size_t(seed)] = 1;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                ++count;
                const int cx    = i % tilesX;
                const int cy    = i / tilesX;
                minX            = std::min(minX, cx);
                maxX            = std::max(maxX, cx);
                minY            = std::min(minY, cy);
                maxY            = std::max(maxY, cy);
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
            char      buf[96];
            std::snprintf(buf, sizeof(buf), "interior hole tiles=%d occ=%.2f", count,
                          tiles[size_t(ty * tilesX + tx)].occ);
            addDefect(r, RenderDefect::Kind::Incomplete, px, py, pw, ph, float(count), buf);
        }
    }
}

void detectCoarseIncomplete(const std::vector<TileStat> &tiles, int tilesX, int tilesY, int w, int h, float occupancy,
                            RenderAuditResult &r) {
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
            int    n      = 0;
            for (int ty = ty0; ty < ty1; ++ty) {
                for (int tx = tx0; tx < tx1; ++tx) {
                    occSum += tiles[size_t(ty * tilesX + tx)].occ;
                    ++n;
                }
            }
            const float cellOcc = n ? float(occSum / n) : 0.f;
            if (cellOcc > 0.08f) continue;
            int       richN     = 0;
            const int nbs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (auto &d : nbs) {
                const int nx = gxi + d[0];
                const int ny = gyi + d[1];
                if (nx < 0 || ny < 0 || nx >= gx || ny >= gy) continue;
                const int ntx0 = (tilesX * nx) / gx;
                const int ntx1 = (tilesX * (nx + 1)) / gx;
                const int nty0 = (tilesY * ny) / gy;
                const int nty1 = (tilesY * (ny + 1)) / gy;
                double    nOcc = 0.0;
                int       nn   = 0;
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
            char      buf[80];
            std::snprintf(buf, sizeof(buf), "coarse empty cell occ=%.2f neighbors=%d", cellOcc, richN);
            addDefect(r, RenderDefect::Kind::Incomplete, px, py, pw, ph, 1.f - cellOcc, buf);
        }
    }
}

struct ScanAccum {
    std::vector<TileStat> tiles;
    int                   tilesX      = 0;
    int                   tilesY      = 0;
    double                sumL        = 0.0;
    int                   n           = 0;
    int                   occ         = 0;
    int                   interiorOcc = 0;
    int                   interiorN   = 0;
    int                   magN = 0, cyanN = 0;
    int                   magX0 = 0, magY0 = 0, magX1 = 0, magY1 = 0;
    int                   cyanX0 = 0, cyanY0 = 0, cyanX1 = 0, cyanY1 = 0;
    std::vector<int>      rowLit;
    std::vector<int>      rowN;
    std::vector<int>      colLit;
    std::vector<int>      colN;
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
    a.magX0                 = w;
    a.magY0                 = h;
    a.cyanX0                = w;
    a.cyanY0                = h;
    const int           ix0 = w / 10, ix1 = w - w / 10;
    const int           iy0 = h / 10, iy1 = h - h / 10;
    std::vector<int>    tileOcc(size_t(a.tilesX * a.tilesY), 0);
    std::vector<int>    tileN(size_t(a.tilesX * a.tilesY), 0);
    std::vector<double> tileL(size_t(a.tilesX * a.tilesY), 0.0);

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const uint8_t *p       = pxAt(rgba, w, x, y);
            const float    L       = lumaU8(p);
            const bool     emptyPx = pixelEmpty(p, bg);
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
        const int n              = tileN[size_t(i)];
        a.tiles[size_t(i)].occ   = n ? float(tileOcc[size_t(i)]) / float(n) : 0.f;
        a.tiles[size_t(i)].meanL = n ? float(tileL[size_t(i)] / n) : 0.f;
    }
    return a;
}

void emitDamage(const ScanAccum &a, int w, int h, int step, RenderAuditResult &r) {
    const int magMin = std::max(2, 8 / (step * step));
    if (a.magN >= magMin) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "debug magenta samples=%d", a.magN);
        addDefect(r, RenderDefect::Kind::Damage, a.magX0, a.magY0, a.magX1 - a.magX0 + 1, a.magY1 - a.magY0 + 1,
                  float(a.magN), buf);
    }
    if (a.cyanN >= magMin) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "debug cyan samples=%d", a.cyanN);
        addDefect(r, RenderDefect::Kind::Damage, a.cyanX0, a.cyanY0, a.cyanX1 - a.cyanX0 + 1, a.cyanY1 - a.cyanY0 + 1,
                  float(a.cyanN), buf);
    }

    auto tornRun = [&](const std::vector<int> &lit, const std::vector<int> &nn, int n, bool rows) {
        auto occAt = [&](int i) -> float {
            return nn[size_t(i)] ? float(lit[size_t(i)]) / float(nn[size_t(i)]) : -1.f;
        };
        int y = step;
        while (y < n - step) {
            const float o    = occAt(y);
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
            char      buf[72];
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
    int imgArea  = std::max(1, cfg.width * cfg.height);
    for (const auto &d : defects) {
        if (d.kind == RenderDefect::Kind::Damage) return true;
        if (d.kind == RenderDefect::Kind::Flicker && d.score >= kFlickerSevere) return true;
        if (d.kind == RenderDefect::Kind::Incomplete) holeArea += d.w * d.h;
    }
    if (flickerMad >= 0.12f) return true;
    return holeArea > imgArea / 16;
}

RenderAuditResult auditRgba8(const uint8_t *rgba, int w, int h, const RenderAuditConfig &cfg, const RenderAuditBg &bg,
                             int step) {
    RenderAuditResult r;
    r.cfg        = cfg;
    r.cfg.width  = w;
    r.cfg.height = h;
    if (!rgba || w <= 0 || h <= 0) {
        r.empty = true;
        return r;
    }
    step                = std::max(1, step);
    const ScanAccum a   = scanFrame(rgba, w, h, toBytes(bg), step);
    r.meanLuma          = a.n ? float(a.sumL / a.n) : 0.f;
    r.occupancy         = a.n ? float(a.occ) / float(a.n) : 0.f;
    r.interiorOccupancy = a.interiorN ? float(a.interiorOcc) / float(a.interiorN) : 0.f;
    r.empty             = r.interiorOccupancy < 0.02f;

    detectHoles(a.tiles, a.tilesX, a.tilesY, w, h, r);
    detectCoarseIncomplete(a.tiles, a.tilesX, a.tilesY, w, h, r.occupancy, r);
    emitDamage(a, w, h, step, r);
    if (r.empty) {
        addDefect(r, RenderDefect::Kind::Incomplete, 0, 0, w, h, 1.f - r.interiorOccupancy,
                  "empty interior (occupancy < 2%)");
    }
    return r;
}

RenderAuditResult auditImage(const ImageData &img, const RenderAuditConfig &cfg, const RenderAuditBg &bg, int step) {
    return auditRgba8(static_cast<const uint8_t *>(img.getData()), img.getWidth(), img.getHeight(), cfg, bg, step);
}

RenderAuditResult auditFlickerRgba8(const uint8_t *a, const uint8_t *b, int w, int h, const RenderAuditConfig &cfg,
                                    const RenderAuditBg &bg, int step) {
    RenderAuditResult r;
    r.cfg        = cfg;
    r.cfg.width  = w;
    r.cfg.height = h;
    r.cfg.extra  = r.cfg.extra.empty() ? "flicker" : r.cfg.extra + "+flicker";
    if (!a || !b || w <= 0 || h <= 0) return r;
    step = std::max(1, step);

    double    sumMad = 0.0;
    int       nAll   = 0;
    const int tilesX = (w + kTile - 1) / kTile;
    const int tilesY = (h + kTile - 1) / kTile;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const int x0      = tx * kTile;
            const int y0      = ty * kTile;
            const int x1      = std::min(w, x0 + kTile);
            const int y1      = std::min(h, y0 + kTile);
            double    tileMad = 0.0;
            int       n       = 0;
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
    return auditFlickerRgba8(static_cast<const uint8_t *>(a.getData()), static_cast<const uint8_t *>(b.getData()),
                             a.getWidth(), a.getHeight(), cfg, bg, step);
}

void mergeAudit(RenderAuditResult &dst, const RenderAuditResult &src) {
    dst.flickerMad = std::max(dst.flickerMad, src.flickerMad);
    for (const auto &d : src.defects) {
        if (int(dst.defects.size()) >= kMaxDefects) break;
        dst.defects.push_back(d);
    }
}

void paintDefectOverlay(ImageData &img, const std::vector<RenderDefect> &defs) {
    const int w    = img.getWidth();
    const int h    = img.getHeight();
    auto     *data = static_cast<uint8_t *>(img.getData());
    auto      plot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t *p = data + (size_t(y) * size_t(w) + size_t(x)) * 4u;
        p[0]       = r;
        p[1]       = g;
        p[2]       = b;
        p[3]       = 255;
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

// saveImagePng is defined in RenderImageAuditIo.cpp: the voxel render tests use
// it too, and a test helper cannot cross a domain link unit (see
// SHARED_SOURCES in scripts/test_domains.py).

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
        md << "\n- meanLuma=" << r.meanLuma << " occupancy=" << r.occupancy << " interiorOcc=" << r.interiorOccupancy
           << " flickerMad=" << r.flickerMad << "\n";
        if (r.empty) md << "- **empty interior**\n";
        if (r.defects.empty()) {
            md << "- no defects\n\n";
        } else {
            for (const auto &d : r.defects) {
                md << "- DEFECT `" << RenderDefect::kindName(d.kind) << "` bbox=(" << d.x << "," << d.y << "," << d.w
                   << "," << d.h << ") score=" << d.score << " " << d.detail << "\n";
            }
            md << "\n";
        }
    }

    {
        std::ofstream js(jsPath, std::ios::app);
        js << "{\"scene\":\"" << jsonEscape(r.cfg.scene) << "\",\"phase\":\"" << jsonEscape(r.cfg.phase)
           << "\",\"extra\":\"" << jsonEscape(r.cfg.extra) << "\",\"width\":" << r.cfg.width
           << ",\"height\":" << r.cfg.height << ",\"frame\":" << r.cfg.frame << ",\"meanLuma\":" << r.meanLuma
           << ",\"occupancy\":" << r.occupancy << ",\"interiorOccupancy\":" << r.interiorOccupancy
           << ",\"flickerMad\":" << r.flickerMad << ",\"empty\":" << (r.empty ? "true" : "false")
           << ",\"severe\":" << (r.hasSevere() ? "true" : "false") << ",\"defects\":[";
        for (size_t i = 0; i < r.defects.size(); ++i) {
            const auto &d = r.defects[i];
            if (i) js << ",";
            js << "{\"kind\":\"" << RenderDefect::kindName(d.kind) << "\",\"x\":" << d.x << ",\"y\":" << d.y
               << ",\"w\":" << d.w << ",\"h\":" << d.h << ",\"score\":" << d.score << ",\"detail\":\""
               << jsonEscape(d.detail) << "\"}";
        }
        js << "]}\n";
    }

    std::printf("RenderImageAudit[%s/%s] defects=%zu severe=%d meanLuma=%.4f occ=%.3f flickerMad=%.4f\n",
                r.cfg.scene.c_str(), r.cfg.phase.c_str(), r.defects.size(), int(r.hasSevere()), r.meanLuma, r.occupancy,
                r.flickerMad);
    for (const auto &d : r.defects) {
        std::printf("  %s bbox=(%d,%d,%d,%d) score=%.3f %s\n", RenderDefect::kindName(d.kind), d.x, d.y, d.w, d.h,
                    d.score, d.detail.c_str());
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
