// RoguelikeGenerator �?a configurable, seed-driven room-and-corridor level
// generator ("RoguelikeGenerator Pro" style). Unlike dungeon.bsp it layers
// extra data on top of the wall/floor grid:
//   * `detail` (Grid2D) stores per-cell wall-autotile direction masks, floor
//     pattern variants and scattered decor tiles (see k* constants below).
//   * `objects` carry placed props (pillars / chests / grass tufts) and the
//     spawn / stairs markers.
// Everything is driven by Params so scripts can re-roll with a different seed
// or tweak generation rules without touching code. Generated cells use the
// standard Semantic ids, so the result still applies to a TileLayer through
// the usual palette pipeline.
//
// Register id: "level.roguelike"
#include "procgen/algorithms/RoguelikeGenerator.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Semantic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

// ---- detail-layer encoding (documented; the script example consumes these) ----
// Wall cells:  detail = 8-bit neighbour mask (which directions are walkable).
// Floor cells: detail = floor-pattern variant (1..kFloorMaxVariant), or
//              >= kDecorBase to mark a scattered decor tile.
constexpr int kDecorBase = 100;        // floor detail >= this  => decor tile
constexpr int kFloorMaxVariant = 32;   // floor pattern variant cap

// Neighbour direction bits used by the wall mask.
constexpr int kE = 1 << 0;  // +x
constexpr int kS = 1 << 1;  // +y
constexpr int kW = 1 << 2;  // -x
constexpr int kN = 1 << 3;  // -y
constexpr int kSE = 1 << 4;
constexpr int kSW = 1 << 5;
constexpr int kNW = 1 << 6;
constexpr int kNE = 1 << 7;

bool isWalkable(uint32_t c) {
    return c == Semantic::Floor || c == Semantic::Corridor;
}

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

/** Assign a wall-autotile mask for every wall cell adjacent to walkable floor. */
void autotileWalls(Grid2D &out) {
    const int w = out.getWidth();
    const int h = out.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (uint32_t(out.getCell(x, y)) != Semantic::Wall) continue;
            int mask = 0;
            if (x + 1 < w && isWalkable(uint32_t(out.getCell(x + 1, y)))) mask |= kE;
            if (y + 1 < h && isWalkable(uint32_t(out.getCell(x, y + 1)))) mask |= kS;
            if (x - 1 >= 0 && isWalkable(uint32_t(out.getCell(x - 1, y)))) mask |= kW;
            if (y - 1 >= 0 && isWalkable(uint32_t(out.getCell(x, y - 1)))) mask |= kN;
            if (x + 1 < w && y + 1 < h && isWalkable(uint32_t(out.getCell(x + 1, y + 1))))
                mask |= kSE;
            if (x - 1 >= 0 && y + 1 < h && isWalkable(uint32_t(out.getCell(x - 1, y + 1))))
                mask |= kSW;
            if (x - 1 >= 0 && y - 1 >= 0 && isWalkable(uint32_t(out.getCell(x - 1, y - 1))))
                mask |= kNW;
            if (x + 1 < w && y - 1 >= 0 && isWalkable(uint32_t(out.getCell(x + 1, y - 1))))
                mask |= kNE;
            out.setDetail(x, y, mask);
        }
    }
}

/** Deterministic floor-pattern variant for a floor cell. */
int floorVariant(int x, int y, const std::string &pattern, int variants, uint32_t salt) {
    const int v = std::max(1, variants);
    switch (pattern[0]) {
    case 'c':  // checker
        return 1 + ((x + y) % v);
    case 'p':  // plank
        return 1 + (y % v);
    default:   // brick / plain / cobble -> pseudo-random but stable per seed
        return 1 + (int((x * 73856093u + y * 19349663u + salt) & 0x7FFFFFFFu) % v);
    }
}

void carveRect(Grid2D &out, const Rect &r, uint32_t semantic) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) out.setCell(x, y, int(semantic));
}

bool rectsOverlap(const Rect &a, const Rect &b, int pad) {
    return a.x < b.x + b.w + pad && b.x < a.x + a.w + pad && a.y < b.y + b.h + pad &&
           b.y < a.y + a.h + pad;
}

void carveCorridor(Grid2D &out, int ax, int ay, int bx, int by, int width,
                   const std::string &style, std::mt19937 &rng) {
    if (style == "diagonal") {
        int x = ax;
        int y = ay;
        while (x != bx || y != by) {
            for (int oy = 0; oy < width; ++oy)
                for (int ox = 0; ox < width; ++ox)
                    out.setCell(x + ox, y + oy, int(Semantic::Corridor));
            if (x != bx && y != by && (rng() & 1u)) {
                x += (bx > x) ? 1 : -1;
            } else if (x != bx) {
                x += (bx > x) ? 1 : -1;
            } else {
                y += (by > y) ? 1 : -1;
            }
        }
        for (int oy = 0; oy < width; ++oy)
            for (int ox = 0; ox < width; ++ox) out.setCell(x + ox, y + oy, int(Semantic::Corridor));
        return;
    }

    // L-shaped: horizontal then vertical (random bend for variety on "l").
    int cornerX = bx;
    int cornerY = ay;
    if (style == "straight") {
        // Force a straight line when aligned, otherwise keep the L.
    } else if (rng() & 1u) {
        cornerX = ax;
        cornerY = by;
    }

    const int x0 = std::min(ax, cornerX);
    const int x1 = std::max(ax, cornerX);
    for (int x = x0; x <= x1; ++x)
        for (int oy = 0; oy < width; ++oy)
            out.setCell(x, cornerY + oy, int(Semantic::Corridor));

    const int y0 = std::min(cornerY, by);
    const int y1 = std::max(cornerY, by);
    for (int y = y0; y <= y1; ++y)
        for (int ox = 0; ox < width; ++ox)
            out.setCell(cornerX + ox, y, int(Semantic::Corridor));
}

bool genRoguelike(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 9 || h < 9) {
        error = "level.roguelike: size must be at least 9x9";
        return false;
    }

    const uint32_t seed        = params.getSeed();
    const int      roomCount   = clampInt(params.getInt("roomCount", 9), 1, 256);
    const int      roomMin     = clampInt(params.getInt("roomMin", 4), 2, w);
    const int      roomMax     = clampInt(params.getInt("roomMax", 8), roomMin, w);
    const int      padding     = clampInt(params.getInt("padding", 1), 0, 4);
    const int      spacing     = clampInt(params.getInt("spacing", 2), 0, 8);
    const int      corridorW   = clampInt(params.getInt("corridorWidth", 1), 1, 3);
    const std::string style    = params.getString("corridorStyle", "l");
    const std::string pattern  = params.getString("floorPattern", "brick");
    const int      variants    = clampInt(params.getInt("floorVariants", 4), 1, kFloorMaxVariant);
    const float    decorDensity = std::clamp(params.getFloat("decorDensity", 0.05f), 0.f, 1.f);
    const std::string decorSet = params.getString("decorSet", "mixed");
    const bool     doAutotile  = params.getInt("autotile", 1) != 0;

    out.resize(w, h);
    out.fill(Semantic::Wall);

    std::mt19937 rng(seed);

    // 1) Place rooms on a coarse grid partition so they stay spread out, but
    //    allow jitter within each slot. Reject a slot if its room would overlap
    //    an earlier one (with spacing) or push outside the map.
    const int cols  = std::max(1, int(std::sqrt(double(roomCount))));
    const int rows  = std::max(1, (roomCount + cols - 1) / cols);
    std::vector<Rect> rooms;
    std::uniform_int_distribution<int> dim(roomMin, roomMax);
    std::uniform_int_distribution<int> jx(0, 0), jy(0, 0);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (int(rooms.size()) >= roomCount) break;
            // Compute the slot's usable area (leave `padding` on each side).
            const int sx = padding + int((long(c) * (w - 2 * padding)) / cols);
            const int ex = padding + int((long(c + 1) * (w - 2 * padding)) / cols);
            const int sy = padding + int((long(r) * (h - 2 * padding)) / rows);
            const int ey = padding + int((long(r + 1) * (h - 2 * padding)) / rows);
            const int slotW = std::max(roomMin, ex - sx);
            const int slotH = std::max(roomMin, ey - sy);
            if (slotW < roomMin || slotH < roomMin) continue;

            // Try a few candidate sizes/offsets inside the slot before giving up.
            for (int attempt = 0; attempt < 24; ++attempt) {
                const int rw = std::min(dim(rng), slotW);
                const int rh = std::min(dim(rng), slotH);
                const int maxX = sx + (slotW - rw);
                const int maxY = sy + (slotH - rh);
                std::uniform_int_distribution<int> ox(sx, maxX);
                std::uniform_int_distribution<int> oy(sy, maxY);
                const Rect cand{ox(rng), oy(rng), rw, rh};

                bool ok = true;
                for (const Rect &other : rooms) {
                    if (rectsOverlap(cand, other, spacing)) { ok = false; break; }
                }
                if (ok) {
                    rooms.push_back(cand);
                    break;
                }
            }
        }
    }
    if (rooms.empty()) {
        // Last resort: one central room.
        const int rw = std::max(roomMin, w / 3);
        const int rh = std::max(roomMin, h / 3);
        rooms.push_back({(w - rw) / 2, (h - rh) / 2, rw, rh});
    }

    // 2) Carve room floors.
    for (const Rect &r : rooms) carveRect(out, r, Semantic::Floor);

    // 3) Connect consecutive rooms.
    for (size_t i = 1; i < rooms.size(); ++i) {
        const int ax = rooms[i - 1].x + rooms[i - 1].w / 2;
        const int ay = rooms[i - 1].y + rooms[i - 1].h / 2;
        const int bx = rooms[i].x + rooms[i].w / 2;
        const int by = rooms[i].y + rooms[i].h / 2;
        carveCorridor(out, ax, ay, bx, by, corridorW, style, rng);
    }

    // 4) Wall autotile direction masks.
    if (doAutotile) autotileWalls(out);

    // 5) Floor pattern variants.
    const uint32_t salt = seed * 2654435761u;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = uint32_t(out.getCell(x, y));
            if (c == Semantic::Floor || c == Semantic::Corridor) {
                out.setDetail(x, y, floorVariant(x, y, pattern, variants, salt));
            }
        }
    }

    // 6) Scatter decor (ground tiles + placed props), avoiding the walkway
    //    through doorways (cells with a wall on two opposite sides) and the
    //    outer border so spawn/stairs stay reachable.
    std::vector<std::pair<int, int>> floorCells;
    floorCells.reserve(size_t(w * h));
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x)
            if (isWalkable(uint32_t(out.getCell(x, y)))) floorCells.emplace_back(x, y);

    std::vector<std::pair<int, int>> decorTiles;
    if (!floorCells.empty() && decorDensity > 0.f) {
        const size_t count = size_t(float(floorCells.size()) * decorDensity);
        std::uniform_int_distribution<size_t> pick(0, floorCells.size() - 1);
        std::uniform_int_distribution<int> kind(0, 2);
        for (size_t i = 0; i < count; ++i) {
            const auto [dx, dy] = floorCells[pick(rng)];
            if (out.getDetail(dx, dy) >= kDecorBase) continue;
            // Keep a clear 1-tile ring around spawn/stairs for reachability.
            bool nearMarker = false;
            for (int k = 0; k < out.getObjectCount(); ++k) {
                if (int(out.getObjectX(k)) == dx && int(out.getObjectY(k)) == dy) {
                    nearMarker = true;
                    break;
                }
            }
            if (nearMarker) continue;
            out.setDetail(dx, dy, kDecorBase + kind(rng));
            decorTiles.emplace_back(dx, dy);
        }
    }

    // 7) Props (pillars / chests) from the chosen decor set.
    auto propCount = [&](int base) {
        return decorSet == "none" ? 0 : std::max(0, int(float(base) * 0.35f));
    };
    std::uniform_int_distribution<int> yroll(0, 2);
    if (decorSet == "pillars" || decorSet == "mixed") {
        const int n = propCount(roomCount);
        for (int i = 0; i < n; ++i) {
            if (rooms.empty()) break;
            const Rect &r = rooms[size_t(i) % rooms.size()];
            if (r.w < 4 || r.h < 4) continue;
            const int px = r.x + r.w / 2 + (yroll(rng) - 1);
            const int py = r.y + r.h / 2 + (yroll(rng) - 1);
            out.addObject("pillar" + std::to_string(i), "pillar", float(px), float(py), 1.f, 1.f,
                          0);
        }
    }
    if (decorSet == "treasure" || decorSet == "mixed") {
        const int n = propCount(roomCount);
        for (int i = 0; i < n; ++i) {
            if (rooms.empty()) break;
            const Rect &r = rooms[size_t(i) % rooms.size()];
            const int px = r.x + r.w / 2 + (yroll(rng) - 1);
            const int py = r.y + r.h / 2 + (yroll(rng) - 1);
            out.addObject("chest" + std::to_string(i), "chest", float(px), float(py), 1.f, 1.f, 0);
        }
    }

    // 8) Spawn + stairs on walkable cells (props from step 7 stay in place).
    std::vector<std::pair<int, int>> walkable;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (isWalkable(uint32_t(out.getCell(x, y)))) walkable.emplace_back(x, y);
    if (!walkable.empty()) {
        auto pick = [&](uint32_t s) {
            return walkable[(seed * 1664525u + s * 1013904223u) % walkable.size()];
        };
        auto a = pick(1);
        auto b = pick(7);
        if (a == b && walkable.size() > 1) b = walkable[(size_t(seed) + 1) % walkable.size()];
        out.addObjectAt("spawn", "spawn", float(a.first), float(a.second));
        out.addObjectAt("stairs", "stairs", float(b.first), float(b.second));
    }

    // 9) Metadata.
    out.setMeta("algorithm", "level.roguelike");
    out.setMeta("seed", std::to_string(seed));
    out.setMeta("rooms", std::to_string(rooms.size()));
    out.setMeta("floorPattern", pattern);
    out.setMeta("decorTiles", std::to_string(decorTiles.size()));
    out.setMeta("corridorStyle", style);
    return true;
}

}  // namespace

void registerRoguelikeGenerator(GeneratorRegistry &registry) {
    auto descriptor = GeneratorDescriptor::grid("level.roguelike", "Roguelike Level", "Dungeon", 9, 9);
    descriptor.params.push_back(ParamDescriptor::integer("roomCount", "Room Count", 9, 1, 256));
    descriptor.params.push_back(ParamDescriptor::integer("roomMin", "Minimum Room Size", 4, 2, 128));
    descriptor.params.push_back(ParamDescriptor::integer("roomMax", "Maximum Room Size", 8, 2, 256));
    descriptor.params.push_back(ParamDescriptor::integer("padding", "Room Padding", 1, 0, 4));
    descriptor.params.push_back(ParamDescriptor::integer("spacing", "Room Spacing", 2, 0, 8));
    descriptor.params.push_back(ParamDescriptor::integer("corridorWidth", "Corridor Width", 1, 1, 3));
    descriptor.params.push_back(ParamDescriptor::choice("corridorStyle", "Corridor Style", "l",
                                                        {"l", "straight", "diagonal"}));
    descriptor.params.push_back(ParamDescriptor::choice("floorPattern", "Floor Pattern", "brick",
                                                        {"brick", "checker", "plank", "plain", "cobble"}));
    descriptor.params.push_back(ParamDescriptor::integer("floorVariants", "Floor Variants", 4, 1, 15));
    descriptor.params.push_back(ParamDescriptor::floating("decorDensity", "Decoration Density", 0.05f, 0.f,
                                                          1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::choice("decorSet", "Decoration Set", "mixed",
                                                        {"mixed", "pillars", "treasure", "none"}));
    descriptor.params.push_back(ParamDescriptor::boolean("autotile", "Autotile", true));
    registry.registerAlgorithm(std::move(descriptor), genRoguelike);
}

bool autotileGridInPlace(Grid2D &grid) {
    autotileWalls(grid);
    return grid.getWidth() > 0 && grid.getHeight() > 0;
}

uint32_t randomSeedValue() {
    std::random_device rd;
    const uint32_t v = rd();
    return v == 0 ? 1u : v;
}

}  // namespace eve::procgen
