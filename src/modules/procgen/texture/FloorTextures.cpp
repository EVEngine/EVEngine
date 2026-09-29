#include "procgen/texture/FloorTextures.h"

#include "image/ImageData.h"
#include "procgen/ParamSchema.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace eve::procgen {
namespace {

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

struct Rgb {
    float r = 0.f, g = 0.f, b = 0.f;
};

Rgb mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb toneToRgb(std::string_view tone) {
    if (tone == "walnut") return {0.28f, 0.16f, 0.09f};
    if (tone == "pine") return {0.72f, 0.58f, 0.34f};
    if (tone == "cherry") return {0.52f, 0.24f, 0.16f};
    if (tone == "ebony") return {0.14f, 0.10f, 0.08f};
    if (tone == "ash") return {0.62f, 0.56f, 0.46f};
    return {0.55f, 0.38f, 0.20f};  // oak
}

Rgb paletteColorA(std::string_view palette) {
    if (palette == "terracotta") return {0.72f, 0.38f, 0.22f};
    if (palette == "slate") return {0.38f, 0.42f, 0.46f};
    if (palette == "porcelain") return {0.86f, 0.90f, 0.94f};
    if (palette == "marble") return {0.90f, 0.88f, 0.84f};
    if (palette == "black") return {0.12f, 0.12f, 0.14f};
    if (palette == "mosaic") return {0.78f, 0.62f, 0.42f};
    if (palette == "subway") return {0.92f, 0.93f, 0.94f};
    return {0.82f, 0.78f, 0.72f};  // ceramic
}

Rgb paletteColorB(std::string_view palette) {
    if (palette == "terracotta") return {0.58f, 0.28f, 0.16f};
    if (palette == "slate") return {0.26f, 0.30f, 0.34f};
    if (palette == "porcelain") return {0.55f, 0.68f, 0.82f};
    if (palette == "marble") return {0.72f, 0.70f, 0.66f};
    if (palette == "black") return {0.72f, 0.72f, 0.74f};
    if (palette == "mosaic") return {0.42f, 0.52f, 0.48f};
    if (palette == "subway") return {0.78f, 0.82f, 0.86f};
    return {0.68f, 0.64f, 0.58f};
}

Rgb groutRgb(std::string_view palette) {
    if (palette == "terracotta") return {0.42f, 0.32f, 0.24f};
    if (palette == "slate") return {0.18f, 0.20f, 0.22f};
    if (palette == "porcelain") return {0.72f, 0.74f, 0.76f};
    if (palette == "black") return {0.35f, 0.35f, 0.36f};
    return {0.55f, 0.52f, 0.48f};
}

void writePixel(image::ImageData& img, int x, int y, Rgb c) {
    img.setPixel(x, y, image::ImageData::Colorf{c.r, c.g, c.b, 1.f});
}

struct WoodOpts {
    std::string layout = "planks";
    std::string tone   = "oak";
    int         rows   = 6;
    int         cols   = 4;
    float       gap    = 0.04f;
    float       grain  = 8.f;
    float       warp   = 1.4f;
    float       wear   = 0.25f;
    float       stain  = 0.15f;
    float       bevel  = 0.08f;
    int         colors = 8;
    int         pixelSize = 1;
};

WoodOpts woodFromParams(const Params& params) {
    WoodOpts o;
    o.layout    = params.getString("layout", "planks");
    o.tone      = params.getString("tone", "oak");
    o.rows      = std::clamp(params.getInt("rows", 6), 1, 64);
    o.cols      = std::clamp(params.getInt("cols", 4), 1, 64);
    o.gap       = std::clamp(params.getFloat("gap", 0.04f), 0.f, 0.35f);
    o.grain     = std::clamp(params.getFloat("grain", 8.f), 0.5f, 64.f);
    o.warp      = std::clamp(params.getFloat("warp", 1.4f), 0.f, 8.f);
    o.wear      = std::clamp(params.getFloat("wear", 0.25f), 0.f, 1.f);
    o.stain     = std::clamp(params.getFloat("stain", 0.15f), 0.f, 1.f);
    o.bevel     = std::clamp(params.getFloat("bevel", 0.08f), 0.f, 0.4f);
    o.colors    = std::max(2, params.getInt("colors", 8));
    o.pixelSize = std::max(1, params.getInt("pixelSize", 1));
    return o;
}

struct TileOpts {
    std::string pattern = "square";
    std::string palette = "ceramic";
    int         tilesX  = 4;
    int         tilesY  = 4;
    float       grout   = 0.06f;
    float       bevel   = 0.10f;
    float       glaze   = 0.35f;
    float       wear    = 0.20f;
    float       speckles = 0.15f;
    float       motif   = 0.55f;
    int         colors  = 8;
    int         pixelSize = 1;
};

TileOpts tileFromParams(const Params& params) {
    TileOpts o;
    o.pattern   = params.getString("pattern", "square");
    o.palette   = params.getString("palette", "ceramic");
    o.tilesX    = std::clamp(params.getInt("tilesX", 4), 1, 64);
    o.tilesY    = std::clamp(params.getInt("tilesY", 4), 1, 64);
    o.grout     = std::clamp(params.getFloat("grout", 0.06f), 0.f, 0.4f);
    o.bevel     = std::clamp(params.getFloat("bevel", 0.10f), 0.f, 0.45f);
    o.glaze     = std::clamp(params.getFloat("glaze", 0.35f), 0.f, 1.f);
    o.wear      = std::clamp(params.getFloat("wear", 0.20f), 0.f, 1.f);
    o.speckles  = std::clamp(params.getFloat("speckles", 0.15f), 0.f, 1.f);
    o.motif     = std::clamp(params.getFloat("motif", 0.55f), 0.f, 1.f);
    o.colors    = std::max(2, params.getInt("colors", 8));
    o.pixelSize = std::max(1, params.getInt("pixelSize", 1));
    return o;
}

float woodGrain(float u, float v, const NoiseField& n, float grain, float warpAmp) {
    const float w = n.warp(u * 0.35f, v * 0.35f, warpAmp, 3);
    float       g = 0.5f + 0.5f * std::sin((u * grain + w * grain * 0.85f) * 6.2831853f);
    g             = std::pow(std::clamp(g, 0.f, 1.f), 2.2f);
    const float fleck = n.fbm(u * 2.4f + 1.7f, v * 0.55f, 3);
    return g * 0.72f + fleck * 0.28f;
}

struct PlankCell {
    int   idX = 0;
    int   idY = 0;
    float localU = 0.f;  // 0..1 inside plank body (ignoring gap)
    float localV = 0.f;
    float edge   = 1.f;  // 1 = center, 0 = in gap/edge
    bool  inGap  = false;
};

PlankCell samplePlanks(float u, float v, int rows, int cols, float gap, float stagger) {
    PlankCell cell;
    const float rowF = v * float(rows);
    const int   row  = int(std::floor(rowF));
    const float off  = (row & 1) ? stagger : 0.f;
    const float colF = u * float(cols) + off;
    const int   col  = int(std::floor(colF));
    const float fu   = colF - float(col);
    const float fv   = rowF - float(row);
    cell.idX         = col;
    cell.idY         = row;
    cell.localU      = fu;
    cell.localV      = fv;
    const float edgeU = std::min(fu, 1.f - fu);
    const float edgeV = std::min(fv, 1.f - fv);
    const float edge  = std::min(edgeU, edgeV);
    cell.inGap        = edge < gap;
    cell.edge         = cell.inGap ? 0.f : smoothstep(gap, gap + 0.08f, edge);
    return cell;
}

PlankCell sampleHerringbone(float u, float v, int rows, int cols, float gap) {
    // Map UV into diamond-rotated plank cells.
    const float sx = u * float(cols);
    const float sy = v * float(rows);
    const float rx = sx + sy;
    const float ry = sx - sy;
    const int   ix = int(std::floor(rx));
    const int   iy = int(std::floor(ry));
    const float fu = rx - float(ix);
    const float fv = ry - float(iy);
    PlankCell cell;
    cell.idX    = ix;
    cell.idY    = iy;
    cell.localU = fu;
    cell.localV = fv;
    const float edge = std::min(std::min(fu, 1.f - fu), std::min(fv, 1.f - fv));
    cell.inGap       = edge < gap;
    cell.edge        = cell.inGap ? 0.f : smoothstep(gap, gap + 0.08f, edge);
    return cell;
}

PlankCell sampleParquet(float u, float v, int rows, int cols, float gap) {
    // Basket / parquet blocks: 2x2 rotated plank groups.
    const float bx = u * float(std::max(1, cols / 2));
    const float by = v * float(std::max(1, rows / 2));
    const int   bxI = int(std::floor(bx));
    const int   byI = int(std::floor(by));
    const float fx  = bx - float(bxI);
    const float fy  = by - float(byI);
    const bool  rot = ((bxI + byI) & 1) != 0;
    float       pu  = rot ? fy : fx;
    float       pv  = rot ? fx : fy;
    const int   strips = 4;
    pu *= float(strips);
    const int   strip = int(std::floor(pu));
    const float su    = pu - float(strip);
    PlankCell cell;
    cell.idX    = bxI * 16 + strip;
    cell.idY    = byI;
    cell.localU = su;
    cell.localV = pv;
    const float edgeU = std::min(su, 1.f - su);
    const float edgeV = std::min(pv, 1.f - pv);
    const float edge  = std::min(edgeU, edgeV);
    // Soft block boundary.
    const float blockEdge = std::min(std::min(fx, 1.f - fx), std::min(fy, 1.f - fy));
    cell.inGap            = edge < gap || blockEdge < gap * 0.85f;
    cell.edge             = cell.inGap ? 0.f : smoothstep(gap, gap + 0.08f, std::min(edge, blockEdge));
    return cell;
}

Rgb shadeWood(const WoodOpts& opts, const PlankCell& cell, float grain, const NoiseField& n, float u, float v) {
    Rgb base = toneToRgb(opts.tone);
    // Per-plank tone variation.
    const float plankVar =
        n.hash01(cell.idX * 17 + 3, cell.idY * 29 + 7) * 0.22f - 0.11f;
    base.r = std::clamp(base.r + plankVar, 0.f, 1.f);
    base.g = std::clamp(base.g + plankVar * 0.85f, 0.f, 1.f);
    base.b = std::clamp(base.b + plankVar * 0.55f, 0.f, 1.f);

    Rgb dark = {base.r * 0.45f, base.g * 0.42f, base.b * 0.38f};
    Rgb lit  = {std::min(1.f, base.r * 1.18f), std::min(1.f, base.g * 1.12f), std::min(1.f, base.b * 1.05f)};
    Rgb col  = mix(dark, lit, grain);

    // Bevel / gap: darker recessed seams.
    const float bevelAmt = cell.inGap ? 0.f : smoothstep(0.f, opts.bevel + 1e-4f, cell.edge);
    col                  = mix(Rgb{0.08f, 0.05f, 0.03f}, col, cell.inGap ? 0.18f : (0.55f + 0.45f * bevelAmt));

    // Wear near edges + scratches.
    const float edgeWear = (1.f - cell.edge) * opts.wear;
    const float scratch =
        smoothstep(0.72f, 0.92f, n.valueNoise(u * 18.f + float(cell.idX), v * 3.2f + float(cell.idY)));
    col = mix(col, lit, edgeWear * 0.35f + scratch * opts.wear * 0.25f);

    // Stain darkens overall.
    col.r *= 1.f - opts.stain * 0.55f;
    col.g *= 1.f - opts.stain * 0.58f;
    col.b *= 1.f - opts.stain * 0.50f;
    return col;
}

std::unique_ptr<image::ImageData> makeWoodImage(const Params& params, std::string& error) {
    const int w = std::clamp(params.getWidth() > 0 ? params.getWidth() : 256, 8, 4096);
    const int h = std::clamp(params.getHeight() > 0 ? params.getHeight() : 256, 8, 4096);
    if (w > 4096 || h > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    const WoodOpts opts = woodFromParams(params);
    NoiseField     n;
    n.seed = params.getSeed();
    const bool seamless = params.getInt("seamless", 1) != 0;
    if (seamless) {
        n.periodX = std::max(1, opts.cols);
        n.periodY = std::max(1, opts.rows);
    }

    auto img = std::make_unique<image::ImageData>(w, h, "RGBA8");
    const int px = opts.pixelSize;
    for (int y = 0; y < h; ++y) {
        const int by = (y / px) * px;
        for (int x = 0; x < w; ++x) {
            const int   bx = (x / px) * px;
            const float u  = (float(bx) + 0.5f) / float(w);
            const float v  = (float(by) + 0.5f) / float(h);

            PlankCell cell;
            float     grainU = u;
            float     grainV = v;
            if (opts.layout == "herringbone" || opts.layout == "chevron") {
                cell = sampleHerringbone(u, v, opts.rows, opts.cols, opts.gap);
                // Align grain with diagonal plank direction.
                grainU = (u + v) * 0.5f;
                grainV = (u - v) * 0.5f + 0.5f;
            } else if (opts.layout == "parquet" || opts.layout == "basket") {
                cell   = sampleParquet(u, v, opts.rows, opts.cols, opts.gap);
                grainU = cell.localU;
                grainV = cell.localV;
            } else {
                // planks (default) and staggered.
                const float stagger = (opts.layout == "staggered") ? 0.5f : 0.f;
                cell                = samplePlanks(u, v, opts.rows, opts.cols, opts.gap, stagger);
                grainU              = cell.localU + float(cell.idX);
                grainV              = cell.localV;
            }

            const float grain =
                woodGrain(grainU * opts.grain * 0.35f, grainV * opts.grain * 0.12f, n, opts.grain * 0.55f, opts.warp);
            Rgb col = shadeWood(opts, cell, grain, n, u, v);

            // Band quantization for a slightly stylized look when colors is low.
            if (opts.colors < 64) {
                const float bands = float(opts.colors);
                col.r             = std::floor(col.r * bands) / bands;
                col.g             = std::floor(col.g * bands) / bands;
                col.b             = std::floor(col.b * bands) / bands;
            }
            writePixel(*img, x, y, col);
        }
    }
    return img;
}

struct TileCell {
    int   idX = 0;
    int   idY = 0;
    float localU = 0.f;
    float localV = 0.f;
    float edge   = 1.f;
    bool  inGrout = false;
    int   colorAlt = 0;  // 0/1 for checker-like patterns
};

TileCell sampleSquare(float u, float v, int tilesX, int tilesY, float grout) {
    TileCell c;
    const float fx = u * float(tilesX);
    const float fy = v * float(tilesY);
    c.idX          = int(std::floor(fx));
    c.idY          = int(std::floor(fy));
    c.localU       = fx - float(c.idX);
    c.localV       = fy - float(c.idY);
    const float e  = std::min(std::min(c.localU, 1.f - c.localU), std::min(c.localV, 1.f - c.localV));
    c.inGrout      = e < grout;
    c.edge         = c.inGrout ? 0.f : smoothstep(grout, grout + 0.08f, e);
    c.colorAlt     = (c.idX + c.idY) & 1;
    return c;
}

TileCell sampleDiamond(float u, float v, int tilesX, int tilesY, float grout) {
    const float sx = (u - 0.5f) * float(tilesX);
    const float sy = (v - 0.5f) * float(tilesY);
    const float rx = sx + sy;
    const float ry = sx - sy;
    TileCell    c;
    c.idX      = int(std::floor(rx));
    c.idY      = int(std::floor(ry));
    c.localU   = rx - float(c.idX);
    c.localV   = ry - float(c.idY);
    const float e = std::min(std::min(c.localU, 1.f - c.localU), std::min(c.localV, 1.f - c.localV));
    c.inGrout  = e < grout;
    c.edge     = c.inGrout ? 0.f : smoothstep(grout, grout + 0.08f, e);
    c.colorAlt = (c.idX + c.idY) & 1;
    return c;
}

TileCell sampleHex(float u, float v, int tilesX, int tilesY, float grout) {
    // Axial-ish hex lattice in UV.
    const float size = 1.f / float(std::max(1, tilesX));
    const float q    = (2.f / 3.f * u) / size;
    const float r    = (-1.f / 3.f * u + std::sqrt(3.f) / 3.f * v) / size;
    // Cube round.
    float x = q, z = r, y = -x - z;
    const float rx = std::round(x), ry = std::round(y), rz = std::round(z);
    const float xDiff = std::fabs(rx - x), yDiff = std::fabs(ry - y), zDiff = std::fabs(rz - z);
    float       qx = rx, qz = rz;
    if (xDiff > yDiff && xDiff > zDiff)
        qx = -ry - rz;
    else if (yDiff > zDiff)
        qz = -rx - ry;
    TileCell c;
    c.idX    = int(qx);
    c.idY    = int(qz);
    // Approximate edge distance via fractional axial coords.
    const float fx = q - qx;
    const float fz = r - qz;
    const float fy = (-q - r) - (-qx - qz);
    const float e  = 1.f - std::max(std::fabs(fx), std::max(std::fabs(fy), std::fabs(fz)));
    c.localU       = fx + 0.5f;
    c.localV       = fz + 0.5f;
    c.inGrout      = e < grout * 1.6f;
    c.edge         = c.inGrout ? 0.f : smoothstep(grout * 1.6f, grout * 1.6f + 0.08f, e);
    c.colorAlt     = (c.idX + c.idY * 3) & 1;
    (void)tilesY;
    return c;
}

TileCell sampleSubway(float u, float v, int tilesX, int tilesY, float grout) {
    // Wide bricks, half-offset rows.
    TileCell c;
    const float fy = v * float(tilesY);
    c.idY          = int(std::floor(fy));
    const float off = (c.idY & 1) ? 0.5f : 0.f;
    const float fx  = u * float(tilesX) + off;
    c.idX           = int(std::floor(fx));
    c.localU        = fx - float(c.idX);
    c.localV        = fy - float(c.idY);
    const float e   = std::min(std::min(c.localU, 1.f - c.localU), std::min(c.localV, 1.f - c.localV));
    c.inGrout       = e < grout;
    c.edge          = c.inGrout ? 0.f : smoothstep(grout, grout + 0.08f, e);
    c.colorAlt      = c.idX & 1;
    return c;
}

TileCell sampleMosaic(float u, float v, int tilesX, int tilesY, float grout, const NoiseField& n) {
    // Voronoi-like irregular mosaic chips.
    const float sx = u * float(tilesX);
    const float sy = v * float(tilesY);
    const int   cx0 = int(std::floor(sx));
    const int   cy0 = int(std::floor(sy));
    float       best = 1e9f;
    int         bx = cx0, by = cy0;
    float       second = 1e9f;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int   cx = cx0 + ox;
            const int   cy = cy0 + oy;
            const float px = float(cx) + n.hash01(cx, cy);
            const float py = float(cy) + n.hash01(cx * 7 + 3, cy * 13 + 5);
            const float dx = sx - px;
            const float dy = sy - py;
            const float d  = dx * dx + dy * dy;
            if (d < best) {
                second = best;
                best   = d;
                bx     = cx;
                by     = cy;
            } else if (d < second) {
                second = d;
            }
        }
    }
    TileCell c;
    c.idX      = bx;
    c.idY      = by;
    c.localU   = sx - float(bx);
    c.localV   = sy - float(by);
    const float d1 = std::sqrt(best);
    const float d2 = std::sqrt(second);
    const float e  = d2 - d1;
    c.inGrout      = e < grout * 0.55f;
    c.edge         = c.inGrout ? 0.f : smoothstep(grout * 0.55f, grout * 0.55f + 0.08f, e);
    c.colorAlt     = int(n.hash01(bx, by) * 3.f);
    return c;
}

TileCell sampleBasketTile(float u, float v, int tilesX, int tilesY, float grout) {
    // Basketweave: alternating 2x1 / 1x2 rectangles.
    const float bx = u * float(std::max(1, tilesX / 2));
    const float by = v * float(std::max(1, tilesY / 2));
    const int   ix = int(std::floor(bx));
    const int   iy = int(std::floor(by));
    const float fx = bx - float(ix);
    const float fy = by - float(iy);
    const bool  horizontal = ((ix + iy) & 1) == 0;
    TileCell    c;
    if (horizontal) {
        const float strip = fy * 2.f;
        c.idY             = iy * 2 + int(std::floor(strip));
        c.idX             = ix;
        c.localU          = fx;
        c.localV          = strip - std::floor(strip);
    } else {
        const float strip = fx * 2.f;
        c.idX             = ix * 2 + int(std::floor(strip));
        c.idY             = iy;
        c.localU          = strip - std::floor(strip);
        c.localV          = fy;
    }
    const float e = std::min(std::min(c.localU, 1.f - c.localU), std::min(c.localV, 1.f - c.localV));
    const float blockEdge =
        std::min(std::min(fx, 1.f - fx), std::min(fy, 1.f - fy));
    c.inGrout  = e < grout || blockEdge < grout * 0.9f;
    c.edge     = c.inGrout ? 0.f : smoothstep(grout, grout + 0.08f, std::min(e, blockEdge));
    c.colorAlt = (c.idX + c.idY) & 1;
    return c;
}

Rgb shadeTile(const TileOpts& opts, const TileCell& cell, const NoiseField& n, float u, float v) {
    Rgb a = paletteColorA(opts.palette);
    Rgb b = paletteColorB(opts.palette);
    Rgb body;
    if (opts.pattern == "checker") {
        body = (cell.colorAlt == 0) ? a : b;
    } else if (opts.pattern == "mosaic") {
        const float t = n.hash01(cell.idX * 3 + 1, cell.idY * 5 + 2);
        body          = mix(a, b, t);
    } else {
        const float tileVar = n.hash01(cell.idX + 11, cell.idY + 19) * 0.12f - 0.06f;
        body                = a;
        body.r              = std::clamp(body.r + tileVar, 0.f, 1.f);
        body.g              = std::clamp(body.g + tileVar, 0.f, 1.f);
        body.b              = std::clamp(body.b + tileVar * 0.8f, 0.f, 1.f);
        // Optional secondary tint for patterned ceramics.
        if (opts.motif > 0.01f && opts.pattern != "square" && opts.pattern != "subway") {
            body = mix(body, b, opts.motif * 0.35f * float(cell.colorAlt));
        }
    }

    // Decorative motif: concentric / diamond inset on ceramic faces.
    if (!cell.inGrout && opts.motif > 0.05f &&
        (opts.pattern == "square" || opts.pattern == "checker" || opts.pattern == "diamond")) {
        const float dx = cell.localU - 0.5f;
        const float dy = cell.localV - 0.5f;
        float       m  = 0.f;
        if (opts.pattern == "diamond") {
            m = 1.f - (std::fabs(dx) + std::fabs(dy)) * 1.6f;
        } else {
            m = 1.f - std::sqrt(dx * dx + dy * dy) * 2.2f;
        }
        m = smoothstep(0.15f, 0.55f, m) * opts.motif;
        body = mix(body, b, m * 0.55f);
        // Thin motif ring.
        const float ring = smoothstep(0.02f, 0.f, std::fabs(m - 0.45f));
        body             = mix(body, mix(a, b, 0.7f), ring * opts.motif * 0.4f);
    }

    // Speckles / glaze mottling.
    const float speck = n.valueNoise(u * 22.f + float(cell.idX), v * 22.f + float(cell.idY));
    body              = mix(body, mix(body, Rgb{1.f, 1.f, 1.f}, 0.35f),
                            smoothstep(0.62f, 0.88f, speck) * opts.speckles * 0.5f);
    const float glazeNoise = n.fbm(u * 3.5f, v * 3.5f, 3);
    body                   = mix(body, mix(body, Rgb{1.f, 1.f, 1.f}, 0.25f), glazeNoise * opts.glaze * 0.35f);

    // Wear darkens center slightly, lightens edges.
    const float wearEdge = (1.f - cell.edge) * opts.wear;
    body                 = mix(body, mix(body, Rgb{0.95f, 0.93f, 0.90f}, 0.4f), wearEdge * 0.45f);

    Rgb grout = groutRgb(opts.palette);
    if (cell.inGrout) {
        const float grit = n.valueNoise(u * 40.f, v * 40.f);
        grout            = mix(grout, Rgb{grout.r * 0.7f, grout.g * 0.7f, grout.b * 0.7f}, grit * 0.35f);
        return grout;
    }

    const float bevelAmt = smoothstep(0.f, opts.bevel + 1e-4f, cell.edge);
    return mix(grout, body, 0.35f + 0.65f * bevelAmt);
}

std::unique_ptr<image::ImageData> makeTileImage(const Params& params, std::string& error) {
    const int w = std::clamp(params.getWidth() > 0 ? params.getWidth() : 256, 8, 4096);
    const int h = std::clamp(params.getHeight() > 0 ? params.getHeight() : 256, 8, 4096);
    if (w > 4096 || h > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    const TileOpts opts = tileFromParams(params);
    NoiseField     n;
    n.seed = params.getSeed();
    const bool seamless = params.getInt("seamless", 1) != 0;
    if (seamless) {
        n.periodX = std::max(1, opts.tilesX);
        n.periodY = std::max(1, opts.tilesY);
    }

    auto img = std::make_unique<image::ImageData>(w, h, "RGBA8");
    const int px = opts.pixelSize;
    for (int y = 0; y < h; ++y) {
        const int by = (y / px) * px;
        for (int x = 0; x < w; ++x) {
            const int   bx = (x / px) * px;
            const float u  = (float(bx) + 0.5f) / float(w);
            const float v  = (float(by) + 0.5f) / float(h);

            TileCell cell;
            if (opts.pattern == "diamond") {
                cell = sampleDiamond(u, v, opts.tilesX, opts.tilesY, opts.grout);
            } else if (opts.pattern == "hex") {
                cell = sampleHex(u, v, opts.tilesX, opts.tilesY, opts.grout);
            } else if (opts.pattern == "subway") {
                cell = sampleSubway(u, v, opts.tilesX, opts.tilesY, opts.grout);
            } else if (opts.pattern == "mosaic") {
                cell = sampleMosaic(u, v, opts.tilesX, opts.tilesY, opts.grout, n);
            } else if (opts.pattern == "basket" || opts.pattern == "basketweave") {
                cell = sampleBasketTile(u, v, opts.tilesX, opts.tilesY, opts.grout);
            } else if (opts.pattern == "herringbone") {
                // Reuse herringbone lattice as tile layout.
                const PlankCell p = sampleHerringbone(u, v, opts.tilesY, opts.tilesX, opts.grout);
                cell.idX          = p.idX;
                cell.idY          = p.idY;
                cell.localU       = p.localU;
                cell.localV       = p.localV;
                cell.edge         = p.edge;
                cell.inGrout      = p.inGap;
                cell.colorAlt     = (p.idX + p.idY) & 1;
            } else {
                // square / checker / ceramic default
                cell = sampleSquare(u, v, opts.tilesX, opts.tilesY, opts.grout);
            }

            Rgb col = shadeTile(opts, cell, n, u, v);
            if (opts.colors < 64) {
                const float bands = float(opts.colors);
                col.r             = std::floor(col.r * bands) / bands;
                col.g             = std::floor(col.g * bands) / bands;
                col.b             = std::floor(col.b * bands) / bands;
            }
            writePixel(*img, x, y, col);
        }
    }
    return img;
}

std::unique_ptr<image::ImageData> woodRecipe(const Params& params, std::string& error) {
    return makeWoodImage(params, error);
}

std::unique_ptr<image::ImageData> tileRecipe(const Params& params, std::string& error) {
    return makeTileImage(params, error);
}

RecipeDescriptor woodDescriptor() {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.floor.wood", "Wood Floor", "Floor", 8, 8, 4096, 4096);
    schema.params.push_back(ParamDescriptor::choice(
        "layout", "Layout", "planks", {"planks", "staggered", "herringbone", "chevron", "parquet", "basket"}));
    schema.params.push_back(
        ParamDescriptor::choice("tone", "Wood Tone", "oak", {"oak", "walnut", "pine", "cherry", "ebony", "ash"}));
    schema.params.push_back(ParamDescriptor::integer("rows", "Plank Rows", 6, 1, 64));
    schema.params.push_back(ParamDescriptor::integer("cols", "Plank Columns", 4, 1, 64));
    schema.params.push_back(ParamDescriptor::floating("gap", "Groove Width", 0.04f, 0.f, 0.35f, 0.005f));
    schema.params.push_back(ParamDescriptor::floating("grain", "Grain Density", 8.f, 0.5f, 64.f, 0.1f));
    schema.params.push_back(ParamDescriptor::floating("warp", "Grain Warp", 1.4f, 0.f, 8.f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("wear", "Surface Wear", 0.25f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("stain", "Stain", 0.15f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("bevel", "Edge Bevel", 0.08f, 0.f, 0.4f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 8, 2, 64));
    schema.params.push_back(ParamDescriptor::integer("pixelSize", "Pixel Size", 1, 1, 64));
    schema.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
    return schema;
}

RecipeDescriptor tileDescriptor() {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.floor.tile", "Floor Tile", "Floor", 8, 8, 4096, 4096);
    schema.params.push_back(ParamDescriptor::choice(
        "pattern", "Pattern", "square",
        {"square", "checker", "diamond", "hex", "subway", "mosaic", "basket", "basketweave", "herringbone"}));
    schema.params.push_back(ParamDescriptor::choice(
        "palette", "Palette", "ceramic",
        {"ceramic", "terracotta", "slate", "porcelain", "marble", "black", "mosaic", "subway"}));
    schema.params.push_back(ParamDescriptor::integer("tilesX", "Tiles X", 4, 1, 64));
    schema.params.push_back(ParamDescriptor::integer("tilesY", "Tiles Y", 4, 1, 64));
    schema.params.push_back(ParamDescriptor::floating("grout", "Grout Width", 0.06f, 0.f, 0.4f, 0.005f));
    schema.params.push_back(ParamDescriptor::floating("bevel", "Edge Bevel", 0.10f, 0.f, 0.45f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("glaze", "Glaze", 0.35f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("wear", "Wear", 0.20f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("speckles", "Speckles", 0.15f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("motif", "Motif Strength", 0.55f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 8, 2, 64));
    schema.params.push_back(ParamDescriptor::integer("pixelSize", "Pixel Size", 1, 1, 64));
    schema.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
    return schema;
}

}  // namespace

eve::Result<std::unique_ptr<image::ImageData>> generateWoodFloorTexture(const Params& params) {
    std::string error;
    auto        img = makeWoodImage(params, error);
    if (!img) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "wood floor texture generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(img));
}

eve::Result<std::unique_ptr<image::ImageData>> generateTileFloorTexture(const Params& params) {
    std::string error;
    auto        img = makeTileImage(params, error);
    if (!img) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "tile floor texture generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(img));
}

void registerFloorTextureRecipes(TextureRecipeRegistry& registry) {
    registry.registerRecipe(woodDescriptor(), woodRecipe);
    registry.registerRecipe(tileDescriptor(), tileRecipe);
}

}  // namespace eve::procgen
