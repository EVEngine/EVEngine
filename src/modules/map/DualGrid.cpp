#include "map/DualGrid.h"
#include "map/TileOrientation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::map {
namespace {

// SpriteCook / common dual-grid 4×4 sheet: index by 4-bit corner mask.
// mask 0 → empty; other values map to atlas local ids 0..15.
constexpr std::array<int, 16> kDefaultFrameByMask = {
    -1, 15, 8, 9, 0, 11, 14, 7, 13, 4, 1, 10, 3, 2, 5, 6,
};

int resolveFirstGid(TileLayer* display, int firstDisplayGid) {
    if (firstDisplayGid > 0) return firstDisplayGid;
    const int ts = display->getTilesetFirstGid();
    return ts > 0 ? ts : 1;
}

float staggerPitchY(const TileLayer::Config& cfg) {
    const bool hex = cfg.orientation == MapOrientation::Hexagonal && cfg.hexSideLength > 0.f;
    return hex ? (cfg.tileH + cfg.hexSideLength) * 0.5f : cfg.tileH * 0.5f;
}

float staggerPitchX(const TileLayer::Config& cfg) {
    const bool hex = cfg.orientation == MapOrientation::Hexagonal && cfg.hexSideLength > 0.f;
    return hex ? (cfg.tileW + cfg.hexSideLength) * 0.5f : cfg.tileW * 0.5f;
}

float smoothstep(float a, float b, float x) {
    if (a == b) return x < a ? 0.f : 1.f;
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

uint32_t hash2(uint32_t seed, int x, int y) {
    uint32_t h = seed ^ uint32_t(x) * 0x9e3779b9u ^ uint32_t(y) * 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    return h ^ (h >> 16);
}

float valueNoise(uint32_t seed, float x, float y) {
    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const float sx = fx * fx * (3.f - 2.f * fx), sy = fy * fy * (3.f - 2.f * fy);
    auto        sample = [seed](int px, int py) { return float(hash2(seed, px, py) & 0xffffu) / 32767.5f - 1.f; };
    const float a      = std::lerp(sample(x0, y0), sample(x0 + 1, y0), sx);
    const float b      = std::lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), sx);
    return std::lerp(a, b, sy);
}

void distanceTransform(const std::vector<uint8_t>& binary, int width, int height, uint8_t target,
                       std::vector<float>& out) {
    constexpr float diagonal = 1.41421356237f;
    const float     infinity = std::numeric_limits<float>::max() / 4.f;
    out.resize(binary.size());
    for (size_t i = 0; i < binary.size(); ++i) out[i] = binary[i] == target ? 0.f : infinity;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float& d = out[size_t(y * width + x)];
            if (x > 0) d = std::min(d, out[size_t(y * width + x - 1)] + 1.f);
            if (y > 0) d = std::min(d, out[size_t((y - 1) * width + x)] + 1.f);
            if (x > 0 && y > 0) d = std::min(d, out[size_t((y - 1) * width + x - 1)] + diagonal);
            if (x + 1 < width && y > 0) d = std::min(d, out[size_t((y - 1) * width + x + 1)] + diagonal);
        }
    for (int y = height - 1; y >= 0; --y)
        for (int x = width - 1; x >= 0; --x) {
            float& d = out[size_t(y * width + x)];
            if (x + 1 < width) d = std::min(d, out[size_t(y * width + x + 1)] + 1.f);
            if (y + 1 < height) d = std::min(d, out[size_t((y + 1) * width + x)] + 1.f);
            if (x + 1 < width && y + 1 < height) d = std::min(d, out[size_t((y + 1) * width + x + 1)] + diagonal);
            if (x > 0 && y + 1 < height) d = std::min(d, out[size_t((y + 1) * width + x - 1)] + diagonal);
        }
}

}  // namespace

float DualGridMaskAtlas::coverageAt(int mask, int x, int y) const {
    if (mask < 0 || mask > 15 || x < 0 || y < 0 || x >= width || y >= height) return 0.f;
    return coverage[(size_t(mask) * size_t(height) + size_t(y)) * size_t(width) + size_t(x)];
}

float DualGridMaskAtlas::signedDistanceAt(int mask, int x, int y) const {
    if (mask < 0 || mask > 15 || x < 0 || y < 0 || x >= width || y >= height) return 0.f;
    return signedDistance[(size_t(mask) * size_t(height) + size_t(y)) * size_t(width) + size_t(x)];
}

float DualGridMaskAtlas::bandAt(int mask, int x, int y, float center, float halfWidth, float softness) const {
    if (halfWidth < 0.f || softness < 0.f) return 0.f;
    const float d = std::abs(signedDistanceAt(mask, x, y) - center);
    return 1.f - smoothstep(halfWidth, halfWidth + softness, d);
}

eve::Result<DualGridMaskAtlas> generateDualGridMaskAtlas(const DualGridMaskConfig& config) {
    if (config.width < 2 || config.height < 2 || config.width > 2048 || config.height > 2048 ||
        !std::isfinite(config.edgeWidth) || config.edgeWidth <= 0.f || config.edgeWidth > 0.5f ||
        !std::isfinite(config.noiseScale) || config.noiseScale < 0.f || !std::isfinite(config.noiseStrength) ||
        config.noiseStrength < 0.f || config.noiseStrength > 0.5f) {
        return eve::Result<DualGridMaskAtlas>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "invalid dual-grid mask dimensions or shaping parameters",
            "map.dual_grid_mask.config"));
    }

    DualGridMaskAtlas atlas;
    atlas.width              = config.width;
    atlas.height             = config.height;
    const size_t framePixels = size_t(config.width) * size_t(config.height);
    atlas.coverage.resize(framePixels * 16u);
    atlas.signedDistance.resize(framePixels * 16u);
    std::vector<uint8_t> binary(framePixels);
    std::vector<float>   insideDistance, outsideDistance;
    const float          distanceNorm = float(std::max(config.width, config.height));

    for (int mask = 0; mask < 16; ++mask) {
        const float top    = (mask & 1) ? 1.f : 0.f;
        const float right  = (mask & 2) ? 1.f : 0.f;
        const float left   = (mask & 4) ? 1.f : 0.f;
        const float bottom = (mask & 8) ? 1.f : 0.f;
        for (int y = 0; y < config.height; ++y)
            for (int x = 0; x < config.width; ++x) {
                const float u = (float(x) + 0.5f) / float(config.width);
                const float v = (float(y) + 0.5f) / float(config.height);
                // Isometric diamond vertices map the logical TL/TR/BL/BR bits to visual T/R/L/B.
                const float topWeight    = (1.f - v) * (1.f - std::abs(2.f * u - 1.f));
                const float bottomWeight = v * (1.f - std::abs(2.f * u - 1.f));
                const float leftWeight   = (1.f - u) * (1.f - std::abs(2.f * v - 1.f));
                const float rightWeight  = u * (1.f - std::abs(2.f * v - 1.f));
                const float sum          = std::max(topWeight + rightWeight + leftWeight + bottomWeight, 1e-6f);
                float field = (top * topWeight + right * rightWeight + left * leftWeight + bottom * bottomWeight) / sum;
                field += valueNoise(config.seed ^ uint32_t(mask) * 0x9e3779b9u, u * config.noiseScale,
                                    v * config.noiseScale) *
                         config.noiseStrength;
                binary[size_t(y * config.width + x)] = field >= 0.5f ? 1u : 0u;
            }
        distanceTransform(binary, config.width, config.height, 0u, outsideDistance);
        distanceTransform(binary, config.width, config.height, 1u, insideDistance);
        const size_t base = size_t(mask) * framePixels;
        for (size_t i = 0; i < framePixels; ++i) {
            float sd;
            if (mask == 0)
                sd = -1.f;
            else if (mask == 15)
                sd = 1.f;
            else
                sd = binary[i] ? outsideDistance[i] / distanceNorm : -insideDistance[i] / distanceNorm;
            sd                             = std::clamp(sd, -1.f, 1.f);
            atlas.signedDistance[base + i] = sd;
            atlas.coverage[base + i]       = smoothstep(-config.edgeWidth, config.edgeWidth, sd);
        }
    }
    return eve::Result<DualGridMaskAtlas>::success(std::move(atlas));
}

eve::Result<DualGridRgbaImage> bakeDualGridTransitionAtlas(const DualGridRgbaImage&  terrainA,
                                                           const DualGridRgbaImage&  terrainB,
                                                           const DualGridMaskConfig& config) {
    const size_t expected = size_t(config.width) * size_t(config.height) * 4u;
    if (terrainA.width != config.width || terrainA.height != config.height || terrainB.width != config.width ||
        terrainB.height != config.height || terrainA.pixels.size() != expected || terrainB.pixels.size() != expected) {
        return eve::Result<DualGridRgbaImage>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "transition tiles must be same-size RGBA8 images matching mask config", "map.dual_grid_mask.tiles"));
    }
    auto generated = generateDualGridMaskAtlas(config);
    if (!generated) return eve::Result<DualGridRgbaImage>::failure(generated.status());

    DualGridRgbaImage result;
    result.width  = config.width * 4;
    result.height = config.height * 4;
    result.pixels.resize(size_t(result.width) * size_t(result.height) * 4u);
    for (int mask = 0; mask < 16; ++mask) {
        const int frameX = (mask % 4) * config.width;
        const int frameY = (mask / 4) * config.height;
        for (int y = 0; y < config.height; ++y)
            for (int x = 0; x < config.width; ++x) {
                const float  coverage = generated.value().coverageAt(mask, x, y);
                const size_t source   = size_t(y * config.width + x) * 4u;
                const size_t target   = size_t((frameY + y) * result.width + frameX + x) * 4u;
                for (size_t channel = 0; channel < 4; ++channel) {
                    const float mixed               = std::lerp(float(terrainA.pixels[source + channel]),
                                                                float(terrainB.pixels[source + channel]), coverage);
                    result.pixels[target + channel] = uint8_t(std::clamp(std::lround(mixed), 0l, 255l));
                }
            }
    }
    return eve::Result<DualGridRgbaImage>::success(std::move(result));
}

const std::array<int, 16>& dualGridDefaultFrameTable() { return kDefaultFrameByMask; }

int dualGridDefaultFrame(int mask) {
    if (mask < 0 || mask > 15) return -1;
    return kDefaultFrameByMask[size_t(mask)];
}

void dualGridHalfOffset(const TileLayer::Config& cfg, float& offX, float& offY) {
    switch (cfg.orientation) {
        case MapOrientation::Isometric:
            // tileToWorld(tx-0.5, ty-0.5) with same iso formula ⇒ origin (ox, oy - th/2).
            offX = 0.f;
            offY = -cfg.tileH * 0.5f;
            break;
        case MapOrientation::Staggered:
        case MapOrientation::Hexagonal:
            if (cfg.staggerAxis == StaggerAxis::Y) {
                offX = -cfg.tileW * 0.5f;
                offY = -staggerPitchY(cfg) * 0.5f;
            } else {
                offX = -staggerPitchX(cfg) * 0.5f;
                offY = -cfg.tileH * 0.5f;
            }
            break;
        case MapOrientation::Orthogonal:
        default:
            offX = -cfg.tileW * 0.5f;
            offY = -cfg.tileH * 0.5f;
            break;
    }
}

bool dualGridLogicFilled(TileLayer& logic, int tx, int ty, int filledGid) {
    auto cfg = logic.config();
    if (tx < 0 || ty < 0 || tx >= cfg->mapW || ty >= cfg->mapH) return false;
    const int gid = int(tileGid(uint32_t(logic.getTile(tx, ty))));
    if (gid == 0) return false;
    if (filledGid == 0) return true;
    return gid == filledGid;
}

int dualGridMaskAt(TileLayer& logic, int dx, int dy, int filledGid) {
    const bool tl = dualGridLogicFilled(logic, dx - 1, dy - 1, filledGid);
    const bool tr = dualGridLogicFilled(logic, dx, dy - 1, filledGid);
    const bool bl = dualGridLogicFilled(logic, dx - 1, dy, filledGid);
    const bool br = dualGridLogicFilled(logic, dx, dy, filledGid);
    return dualGridMaskFromCorners(tl, tr, bl, br);
}

bool resolveDualGrid(TileLayer* logic, TileLayer* display, const DualGridOptions& opts, std::string* error) {
    if (!logic || !display) {
        if (error) *error = "resolveDualGrid: logic and display layers required";
        return false;
    }
    if (logic == display) {
        if (error) *error = "resolveDualGrid: logic and display must be different layers";
        return false;
    }

    const int logicW = logic->getMapWidth();
    const int logicH = logic->getMapHeight();
    if (logicW <= 0 || logicH <= 0) {
        if (error) *error = "resolveDualGrid: logic layer has empty size";
        return false;
    }

    const float tileW = logic->getTileWidth();
    const float tileH = logic->getTileHeight();
    display->setTileSize(tileW, tileH);
    display->resize(logicW + 1, logicH + 1);

    auto lc           = logic->config();
    auto dc           = display->config();
    dc->orientation   = lc->orientation;
    dc->staggerAxis   = lc->staggerAxis;
    dc->staggerIndex  = lc->staggerIndex;
    dc->hexSideLength = lc->hexSideLength;

    if (opts.applyHalfOffset) {
        float offX = 0.f, offY = 0.f;
        dualGridHalfOffset(*lc, offX, offY);
        display->setOrigin(logic->getX() + offX, logic->getY() + offY);
    } else {
        display->setOrigin(logic->getX(), logic->getY());
    }

    // Keep draw bookkeeping in sync when display is freshly created.
    display->setLayer(logic->getLayer());
    display->setCamera(logic->draw()->camera);
    display->setCanvas(logic->draw()->canvas);

    const int firstGid  = resolveFirstGid(display, opts.firstDisplayGid);
    const int filledGid = opts.filledGid;

    for (int dy = 0; dy < logicH + 1; ++dy) {
        for (int dx = 0; dx < logicW + 1; ++dx) {
            const int mask  = dualGridMaskAt(*logic, dx, dy, filledGid);
            int       frame = -1;
            if (opts.useDefaultFrameTable) {
                frame = dualGridDefaultFrame(mask);
            } else if (mask != 0) {
                frame = mask;
            }
            const int gid = (frame < 0) ? 0 : firstGid + frame;
            display->setTile(dx, dy, gid);
        }
    }

    if (opts.hideLogic) logic->setVisible(false);
    display->setVisible(true);

    if (error) error->clear();
    return true;
}

bool resolveDualGrid(TileLayer* logic, TileLayer* display, std::string* error) {
    return resolveDualGrid(logic, display, DualGridOptions{}, error);
}

}  // namespace eve::map
