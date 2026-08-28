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
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
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

std::vector<std::string> assetPool(const Params &params, const std::string &role,
                                   const std::string &fallback) {
    std::vector<std::string> result;
    std::stringstream input(params.getString("assets." + role, fallback));
    std::string item;
    while (std::getline(input, item, ',')) {
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string::npos) result.push_back(item.substr(first, last - first + 1));
    }
    return result;
}

std::string pickAsset(const std::vector<std::string> &pool, std::mt19937 &rng) {
    if (pool.empty()) return {};
    return pool[size_t(rng()) % pool.size()];
}

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
    auto carve = [&](int x, int y) {
        if (uint32_t(out.getCell(x, y)) == Semantic::Wall)
            out.setCell(x, y, int(Semantic::Corridor));
    };
    if (style == "diagonal") {
        int x = ax;
        int y = ay;
        while (x != bx || y != by) {
            for (int oy = 0; oy < width; ++oy)
                for (int ox = 0; ox < width; ++ox)
                    carve(x + ox, y + oy);
            if (x != bx && y != by && (rng() & 1u)) {
                x += (bx > x) ? 1 : -1;
            } else if (x != bx) {
                x += (bx > x) ? 1 : -1;
            } else {
                y += (by > y) ? 1 : -1;
            }
        }
        for (int oy = 0; oy < width; ++oy)
            for (int ox = 0; ox < width; ++ox) carve(x + ox, y + oy);
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
            carve(x, cornerY + oy);

    const int y0 = std::min(cornerY, by);
    const int y1 = std::max(cornerY, by);
    for (int y = y0; y <= y1; ++y)
        for (int ox = 0; ox < width; ++ox)
            carve(cornerX + ox, y);
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
    const int      clusterGapMin = clampInt(params.getInt("clusterGapMin", spacing + 1), 1, 8);
    const int      clusterGapMax = clampInt(params.getInt("clusterGapMax", spacing + 3),
                                             clusterGapMin, 12);
    const int      corridorW   = clampInt(params.getInt("corridorWidth", 1), 1, 3);
    const std::string layout   = params.getString("layoutStyle", "grid");
    const std::string style    = params.getString("corridorStyle", "l");
    const std::string connections = params.getString("connectionStyle", "sequential");
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
    std::vector<size_t> roomParents;
    std::uniform_int_distribution<int> dim(roomMin, roomMax);
    std::uniform_int_distribution<int> jx(0, 0), jy(0, 0);

    if (layout == "clustered") {
        const int firstW = std::min(dim(rng), w - padding * 2);
        const int firstH = std::min(dim(rng), h - padding * 2);
        rooms.push_back({(w - firstW) / 2, (h - firstH) / 2, firstW, firstH});
        roomParents.push_back(0);
        std::uniform_int_distribution<int> jitter(-1, 1);
        std::uniform_int_distribution<int> clusterGap(clusterGapMin, clusterGapMax);
        const int maxAttempts = roomCount * 160;
        for (int attempt = 0; attempt < maxAttempts && int(rooms.size()) < roomCount; ++attempt) {
            const size_t parentIndex = size_t(rng()) % rooms.size();
            const Rect &parent = rooms[parentIndex];
            const int rw = std::min(dim(rng), w - padding * 2);
            const int rh = std::min(dim(rng), h - padding * 2);
            const int gap = clusterGap(rng);
            Rect cand{0, 0, rw, rh};
            switch (rng() % 4u) {
            case 0: // east
                cand.x = parent.x + parent.w + gap;
                cand.y = parent.y + parent.h / 2 - rh / 2 + jitter(rng);
                break;
            case 1: // west
                cand.x = parent.x - rw - gap;
                cand.y = parent.y + parent.h / 2 - rh / 2 + jitter(rng);
                break;
            case 2: // south
                cand.x = parent.x + parent.w / 2 - rw / 2 + jitter(rng);
                cand.y = parent.y + parent.h + gap;
                break;
            default: // north
                cand.x = parent.x + parent.w / 2 - rw / 2 + jitter(rng);
                cand.y = parent.y - rh - gap;
                break;
            }
            if (cand.x < padding || cand.y < padding ||
                cand.x + cand.w > w - padding || cand.y + cand.h > h - padding) continue;
            bool ok = true;
            for (const Rect &other : rooms) {
                if (rectsOverlap(cand, other, spacing)) { ok = false; break; }
            }
            if (ok) {
                rooms.push_back(cand);
                roomParents.push_back(parentIndex);
            }
        }
    } else {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (int(rooms.size()) >= roomCount) break;
                const int sx = padding + int((long(c) * (w - 2 * padding)) / cols);
                const int ex = padding + int((long(c + 1) * (w - 2 * padding)) / cols);
                const int sy = padding + int((long(r) * (h - 2 * padding)) / rows);
                const int ey = padding + int((long(r + 1) * (h - 2 * padding)) / rows);
                const int slotW = std::max(roomMin, ex - sx);
                const int slotH = std::max(roomMin, ey - sy);
                if (slotW < roomMin || slotH < roomMin) continue;
                for (int attempt = 0; attempt < 24; ++attempt) {
                    const int rw = std::min(dim(rng), slotW);
                    const int rh = std::min(dim(rng), slotH);
                    std::uniform_int_distribution<int> ox(sx, sx + slotW - rw);
                    std::uniform_int_distribution<int> oy(sy, sy + slotH - rh);
                    const Rect cand{ox(rng), oy(rng), rw, rh};
                    bool ok = true;
                    for (const Rect &other : rooms) {
                        if (rectsOverlap(cand, other, spacing)) { ok = false; break; }
                    }
                    if (ok) {
                        roomParents.push_back(rooms.empty() ? 0 : rooms.size() - 1);
                        rooms.push_back(cand);
                        break;
                    }
                }
            }
        }
    }
    if (rooms.empty()) {
        // Last resort: one central room.
        const int rw = std::max(roomMin, w / 3);
        const int rh = std::max(roomMin, h / 3);
        rooms.push_back({(w - rw) / 2, (h - rh) / 2, rw, rh});
        roomParents.push_back(0);
    }

    // 2) Carve room floors.
    for (const Rect &r : rooms) carveRect(out, r, Semantic::Floor);

    // 3) Connect rooms. Nearest-parent mode produces a compact branching tree
    // instead of long row-wrap corridors while preserving deterministic output.
    for (size_t i = 1; i < rooms.size(); ++i) {
        size_t parent = i - 1;
        if (connections == "growth" && i < roomParents.size()) {
            parent = roomParents[i];
        } else if (connections == "nearest") {
            int bestDistance = w + h + 1;
            const int bx = rooms[i].x + rooms[i].w / 2;
            const int by = rooms[i].y + rooms[i].h / 2;
            for (size_t candidate = 0; candidate < i; ++candidate) {
                const int ax = rooms[candidate].x + rooms[candidate].w / 2;
                const int ay = rooms[candidate].y + rooms[candidate].h / 2;
                const int distance = std::abs(ax - bx) + std::abs(ay - by);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    parent = candidate;
                }
            }
        }
        const int ax = rooms[parent].x + rooms[parent].w / 2;
        const int ay = rooms[parent].y + rooms[parent].h / 2;
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

    // 6) Scatter non-blocking ground detail, avoiding narrow doorways.
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

    // 7) Semantic asset dressing. Pools are comma-separated asset ids supplied
    // by the caller. Generic fallbacks deliberately contain no KayKit names.
    // Renderers resolve `getObjectAsset()` however they like (GLTF, prefab, sprite…).
    const auto containers = assetPool(params, "container", "container");
    const auto treasure = assetPool(params, "treasure", "treasure");
    const auto columns = assetPool(params, "column", "column");
    const auto tables = assetPool(params, "table", "table");
    const auto seating = assetPool(params, "seating", "seat");
    const auto beds = assetPool(params, "bed", "bed");
    const auto shelves = assetPool(params, "shelf", "shelf");
    const auto lights = assetPool(params, "light", "light");
    const auto wallLights = assetPool(params, "wallLight",
                                      params.getString("assets.light", "light"));
    const auto wallShelves = assetPool(params, "wallShelf",
                                       params.getString("assets.shelf", "shelf"));
    const auto banners = assetPool(params, "banner", "banner");
    const auto weapons = assetPool(params, "weapon", "weapon_display");
    const auto traps = assetPool(params, "trap", "trap");
    const auto food = assetPool(params, "food", "food");
    const auto tavern = assetPool(params, "tavern", "tavern_prop");
    const auto clutter = assetPool(params, "clutter", "clutter");
    const auto stairsAssets = assetPool(params, "stairs", "stairs");
    const float propDensity = std::clamp(params.getFloat("propDensity", 0.16f), 0.f, 1.f);
    const float corridorLightDensity =
        std::clamp(params.getFloat("corridorLightDensity", 0.035f), 0.f, 0.25f);
    std::unordered_set<int> occupied;
    int objectSerial = 0;
    int minimumRoomProps = std::numeric_limits<int>::max();
    auto cellKey = [w](int x, int y) { return y * w + x; };
    auto place = [&](const std::string &role, const std::vector<std::string> &pool, float x, float y,
                     float rotation, int flags, float ow = 1.f, float oh = 1.f) {
        const int cellX = clampInt(int(std::round(x)), 0, w - 1);
        const int cellY = clampInt(int(std::round(y)), 0, h - 1);
        if (pool.empty() || !isWalkable(uint32_t(out.getCell(cellX, cellY)))) return false;
        if ((flags & 1) && occupied.count(cellKey(cellX, cellY))) return false;
        out.addAssetObject(role + std::to_string(objectSerial++), role, pickAsset(pool, rng),
                           x, y, ow, oh, rotation, flags);
        if (flags & 1) occupied.insert(cellKey(cellX, cellY));
        return true;
    };

    if (decorSet != "none") {
        for (size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
            const Rect &r = rooms[roomIndex];
            if (r.w < 3 || r.h < 3) continue;
            const int left = r.x, right = r.x + r.w - 1;
            const int top = r.y, bottom = r.y + r.h - 1;
            const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
            // The clustered layout grows from room zero, making it the visual
            // hub. Give that hub a dense dining composition instead of a quiet
            // edge-oriented theme; remaining rooms still cycle deterministically.
            const int theme = roomIndex == 0 ? 2 : int((roomIndex + seed) % 7);
            static constexpr const char *kThemeNames[] = {
                "storage", "quarters", "dining", "armory", "treasury", "shrine", "tavern"};
            out.addAssetObject("room" + std::to_string(roomIndex), "room",
                               kThemeNames[theme], float(r.x), float(r.y),
                               float(r.w), float(r.h), 0.f, 32);
            const int propsBeforeRoom = objectSerial;

            // Every furnished room receives wall-mounted light and occasional banner.
            place("light", wallLights, left, cy, 90.f, 2 | 4);
            if (r.w >= 6) place("light", wallLights, right, cy, 270.f, 2 | 4);
            if ((rng() % 3u) != 0) place("banner", banners, left, cy - 1, 90.f, 2);

            if (decorSet == "pillars") {
                place("column", columns, left + 1, top + 1, 0.f, 1);
                place("column", columns, right - 1, bottom - 1, 0.f, 1);
                continue;
            }
            if (decorSet == "treasure" || theme == 4) {
                place("container", containers, right, cy, 270.f, 1);
                place("treasure", treasure, right - 1, cy, 0.f, 16);
                place("treasure", treasure, right - 1.35f, cy + 0.45f, 35.f, 16);
                place("treasure", treasure, right - 0.65f, cy - 0.45f, 320.f, 16);
                place("column", columns, left + 1, top + 1, 0.f, 1);
                place("column", columns, left + 1, bottom - 1, 0.f, 1);
            } else if (theme == 0) {
                place("container", containers, right, top + 1, 270.f, 1);
                place("container", containers, right, bottom - 1, 270.f, 1);
                place("container", containers, right - 0.75f, top + 1.35f, 245.f, 16);
                place("container", containers, right - 0.55f, bottom - 1.25f, 300.f, 16);
                place("shelf", wallShelves, cx, bottom, 0.f, 1 | 2);
                if (r.h >= 7) place("shelf", wallShelves, left, cy + 1, 90.f, 1 | 2);
                if (r.w >= 8) place("container", containers, left + 2, bottom, 0.f, 1);
                place("clutter", clutter, left + 1, bottom, 0.f, 16);
            } else if (theme == 1) {
                place("bed", beds, right, cy, 270.f, 1, 1.f, 2.f);
                if (r.h >= 7) place("bed", beds, right, cy - 2, 270.f, 1, 1.f, 2.f);
                place("container", containers, right - 1, top, 180.f, 1);
                place("light", lights, right - 1, cy, 0.f, 4);
                place("seating", seating, left + 1, bottom - 1, 45.f, 1);
            } else if (theme == 2) {
                place("table", tables, cx, cy, (rng() & 1u) ? 0.f : 90.f, 1, 2.f, 1.f);
                place("seating", seating, cx - 1, cy, 270.f, 1);
                place("seating", seating, cx + 1, cy, 90.f, 1);
                place("seating", seating, cx, cy - 1, 0.f, 1);
                place("seating", seating, cx, cy + 1, 180.f, 1);
                place("food", food, cx, cy, 0.f, 16);
                place("food", food, cx - 0.38f, cy + 0.18f, 25.f, 16);
                place("food", food, cx + 0.38f, cy - 0.18f, 205.f, 16);
                if (r.w >= 8) {
                    place("table", tables, cx - 2, cy, 0.f, 1, 2.f, 1.f);
                    place("seating", seating, cx - 2, cy - 1, 0.f, 1);
                    place("seating", seating, cx - 2, cy + 1, 180.f, 1);
                    place("food", food, cx - 2, cy, 70.f, 16);
                }
            } else if (theme == 3) {
                place("weapon", weapons, left, cy, 90.f, 2);
                place("weapon", weapons, cx, bottom, 0.f, 2);
                place("column", columns, cx, cy, 0.f, 1);
                if (r.w >= 7) place("column", columns, left + 1, bottom - 1, 0.f, 1);
                place("container", containers, right, top + 1, 270.f, 1);
                if ((rng() & 1u) != 0) place("trap", traps, cx - 1, cy, 0.f, 8);
            } else if (theme == 5) {
                place("table", tables, cx, cy, 0.f, 1, 2.f, 1.f);
                place("light", lights, cx - 0.45f, cy, 0.f, 4);
                place("light", lights, cx + 0.45f, cy, 0.f, 4);
                place("food", food, cx, cy, 0.f, 16);
                place("seating", seating, cx, bottom - 1, 180.f, 1);
                if (r.w >= 7) {
                    place("seating", seating, cx - 2, bottom - 1, 180.f, 1);
                    place("seating", seating, cx + 2, bottom - 1, 180.f, 1);
                }
            } else {
                place("tavern", tavern, right, cy, 270.f, 1);
                place("table", tables, cx, cy, 0.f, 1, 2.f, 1.f);
                place("seating", seating, cx - 1, cy, 270.f, 1);
                place("seating", seating, cx + 1, cy, 90.f, 1);
                place("seating", seating, cx, cy + 1, 180.f, 1);
                place("food", food, cx, cy, 0.f, 16);
                place("food", food, cx - 0.4f, cy, 20.f, 16);
                place("food", food, cx + 0.4f, cy, 340.f, 16);
                place("container", containers, right, bottom - 1, 270.f, 1);
                place("tavern", tavern, right - 0.7f, bottom - 1.2f, 250.f, 16);
            }

            // High-density presets add a secondary wall-side storage vignette.
            // This is intentionally shared across themes: barrels, trunks, and
            // crates are the visual glue that makes modular rooms feel occupied.
            if (propDensity >= 0.45f && theme != 0 && theme != 4 && theme != 6) {
                place("container", containers, right, bottom - 1, 270.f, 1);
                place("container", containers, right - 0.65f, bottom - 1.3f, 245.f, 16);
                if (r.w >= 7 && theme != 2)
                    place("seating", seating, left + 1, top + 1, 45.f, 1);
            }

            // Sparse edge clutter gives the dense, lived-in reference look without
            // turning room centres and corridors into an obstacle field.
            const int clutterAttempts = clampInt(int(std::ceil(propDensity * 1.5f)), 0, 2);
            const std::pair<int, int> clutterSpots[] = {
                {left, top + 1}, {right, bottom - 1}, {left + 1, bottom}, {right - 1, top}};
            for (int i = 0; i < clutterAttempts; ++i) {
                const auto &spot = clutterSpots[(size_t(i) + roomIndex) % 4];
                place("clutter", clutter, spot.first, spot.second,
                      float((i + int(roomIndex)) % 4) * 90.f, 16);
            }

            // Enforce a visible minimum for dense showcase presets. Failed
            // blocking placements simply advance to another interior edge;
            // non-blocking clutter remains cosmetic and navigation-safe.
            if (propDensity >= 0.35f) {
                // Scale the visual budget with room area. A fixed count makes
                // large chambers look abandoned even at high density.
                const int roomArea = r.w * r.h;
                const int targetProps = clampInt(
                    4 + int(std::ceil(propDensity * float(roomArea) * 0.28f)), 4, 16);
                const std::pair<int, int> fillerSpots[] = {
                    {left + 1, top + 1}, {right - 1, top + 1},
                    {left + 1, bottom - 1}, {right - 1, bottom - 1},
                    {cx - 1, bottom - 1}, {cx + 1, top + 1}};
                for (int attempt = 0;
                     attempt < 24 && objectSerial - propsBeforeRoom < targetProps; ++attempt) {
                    const auto &spot = fillerSpots[(size_t(attempt) + roomIndex) % 6];
                    if ((attempt & 1) == 0)
                        place("container", containers, spot.first, spot.second,
                              float((attempt + int(roomIndex)) % 4) * 90.f, 1);
                    else
                        place("container", containers, float(spot.first) + 0.35f,
                              float(spot.second) - 0.25f,
                              float((attempt * 37) % 360), 16);
                }
            }
            minimumRoomProps = std::min(minimumRoomProps, objectSerial - propsBeforeRoom);
        }

        // Long connectors in the reference are punctuated by sparse sconces.
        // Keep this semantic and density-driven so packs can substitute any
        // wall-mounted light prefab without changing the generator.
        const uint32_t lightThreshold = uint32_t(corridorLightDensity * 1000.f);
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                if (uint32_t(out.getCell(x, y)) != Semantic::Corridor) continue;
                const uint32_t hash = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ seed;
                if ((hash % 1000u) >= lightThreshold) continue;
                if (uint32_t(out.getCell(x - 1, y)) == Semantic::Wall)
                    place("light", wallLights, float(x), float(y), 90.f, 2 | 4);
                else if (uint32_t(out.getCell(x + 1, y)) == Semantic::Wall)
                    place("light", wallLights, float(x), float(y), 270.f, 2 | 4);
                else if (uint32_t(out.getCell(x, y - 1)) == Semantic::Wall)
                    place("light", wallLights, float(x), float(y), 180.f, 2 | 4);
                else if (uint32_t(out.getCell(x, y + 1)) == Semantic::Wall)
                    place("light", wallLights, float(x), float(y), 0.f, 2 | 4);
            }
        }
    }

    // 8) Spawn + an outward-facing perimeter stair. Stairs are architecture,
    // not arbitrary floor clutter: orientation identifies the wall opening the
    // renderer should replace (0 north, 180 south, 90 west, 270 east).
    struct StairCandidate { int x, y; float rotation; };
    std::vector<StairCandidate> stairCandidates;
    for (const Rect &r : rooms) {
        for (int x = r.x + 1; x < r.x + r.w - 1; ++x) {
            if (uint32_t(out.getCell(x, r.y - 1)) == Semantic::Wall)
                stairCandidates.push_back({x, r.y, 180.f});
            if (uint32_t(out.getCell(x, r.y + r.h)) == Semantic::Wall)
                stairCandidates.push_back({x, r.y + r.h - 1, 0.f});
        }
        for (int y = r.y + 1; y < r.y + r.h - 1; ++y) {
            if (uint32_t(out.getCell(r.x - 1, y)) == Semantic::Wall)
                stairCandidates.push_back({r.x, y, 270.f});
            if (uint32_t(out.getCell(r.x + r.w, y)) == Semantic::Wall)
                stairCandidates.push_back({r.x + r.w - 1, y, 90.f});
        }
    }
    if (!stairCandidates.empty()) {
        const StairCandidate &stairs = stairCandidates[size_t(seed) % stairCandidates.size()];
        out.addAssetObject("stairs", "stairs", pickAsset(stairsAssets, rng),
                           float(stairs.x), float(stairs.y), 1.f, 1.f,
                           stairs.rotation, 64);
        occupied.insert(cellKey(stairs.x, stairs.y));
    }

    std::vector<std::pair<int, int>> walkable;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (isWalkable(uint32_t(out.getCell(x, y))) && !occupied.count(cellKey(x, y)))
                walkable.emplace_back(x, y);
    if (!walkable.empty()) {
        auto pick = [&](uint32_t s) {
            return walkable[(seed * 1664525u + s * 1013904223u) % walkable.size()];
        };
        auto a = pick(1);
        out.addObjectAt("spawn", "spawn", float(a.first), float(a.second));
    }

    // 9) Metadata.
    out.setMeta("algorithm", "level.roguelike");
    out.setMeta("seed", std::to_string(seed));
    out.setMeta("rooms", std::to_string(rooms.size()));
    out.setMeta("floorPattern", pattern);
    out.setMeta("decorTiles", std::to_string(decorTiles.size()));
    out.setMeta("corridorStyle", style);
    out.setMeta("layoutStyle", layout);
    out.setMeta("connectionStyle", connections);
    out.setMeta("assetPack", params.getString("assetPack", "semantic-default"));
    out.setMeta("placedProps", std::to_string(objectSerial));
    out.setMeta("minimumRoomProps",
                std::to_string(minimumRoomProps == std::numeric_limits<int>::max()
                                   ? 0 : minimumRoomProps));
    // Tile renderers use these semantic pools to resolve every architecture cell.
    // Copying them into metadata keeps the generated artifact self-describing.
    static const char *architectureRoles[] = {
        "wall", "wallCorner", "wallJunction", "wallDoor", "wallWindow", "wallHalf",
        "wallBroken", "wallScaffold", "floor", "floorBroken", "floorDirt", "floorWood",
        "floorGrate", "floorFoundation", "ceiling", "stairs", "stairsRail", "door", "barrier"};
    for (const char *role : architectureRoles) {
        const std::string key = std::string("assets.") + role;
        if (params.has(key)) out.setMeta(key, params.getString(key, ""));
    }
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
    descriptor.params.push_back(ParamDescriptor::integer(
        "clusterGapMin", "Cluster Minimum Gap", 2, 1, 8));
    descriptor.params.push_back(ParamDescriptor::integer(
        "clusterGapMax", "Cluster Maximum Gap", 4, 1, 12));
    descriptor.params.push_back(ParamDescriptor::integer("corridorWidth", "Corridor Width", 1, 1, 3));
    descriptor.params.push_back(ParamDescriptor::choice("layoutStyle", "Room Layout", "grid",
                                                        {"grid", "clustered"}));
    descriptor.params.push_back(ParamDescriptor::choice("connectionStyle", "Room Connections",
                                                        "sequential",
                                                        {"sequential", "nearest", "growth"}));
    descriptor.params.push_back(ParamDescriptor::choice("corridorStyle", "Corridor Style", "l",
                                                        {"l", "straight", "diagonal"}));
    descriptor.params.push_back(ParamDescriptor::choice("floorPattern", "Floor Pattern", "brick",
                                                        {"brick", "checker", "plank", "plain", "cobble"}));
    descriptor.params.push_back(ParamDescriptor::integer("floorVariants", "Floor Variants", 4, 1, 15));
    descriptor.params.push_back(ParamDescriptor::floating("decorDensity", "Decoration Density", 0.05f, 0.f,
                                                          1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::choice("decorSet", "Decoration Set", "mixed",
                                                        {"mixed", "pillars", "treasure", "none"}));
    descriptor.params.push_back(ParamDescriptor::floating("propDensity", "Prop Density", 0.16f,
                                                          0.f, 1.f, 0.01f));
    descriptor.params.push_back(ParamDescriptor::floating("corridorLightDensity",
                                                          "Corridor Light Density", 0.035f,
                                                          0.f, 0.25f, 0.005f));
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
