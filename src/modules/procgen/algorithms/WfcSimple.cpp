#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

struct WfcTile {
    uint32_t semantic = Semantic::Empty;
    // Orthogonal adjacency: bit i set ⇒ may neighbor tile i (N/E/S/W all share same rule).
    uint64_t compat = 0;
};

struct WfcPreset {
    const char *name;
    std::vector<WfcTile> tiles;
};

WfcPreset makeDungeonPreset() {
    // 0 Wall, 1 Floor, 2 Corridor, 3 Door
    WfcPreset p;
    p.name = "dungeon";
    p.tiles = {
        {Semantic::Wall, 0},
        {Semantic::Floor, 0},
        {Semantic::Corridor, 0},
        {Semantic::Door, 0},
    };
    auto allow = [&](int a, int b) {
        p.tiles[size_t(a)].compat |= (1ull << b);
        p.tiles[size_t(b)].compat |= (1ull << a);
    };
    allow(0, 0);  // wall-wall
    allow(0, 1);  // wall-floor
    allow(0, 2);  // wall-corridor
    allow(0, 3);  // wall-door
    allow(1, 1);  // floor-floor
    allow(1, 2);  // floor-corridor
    allow(1, 3);  // floor-door
    allow(2, 2);  // corridor-corridor
    // door-door disallowed (keeps doors sparse)
    return p;
}

WfcPreset makeCavePreset() {
    WfcPreset p;
    p.name = "cave";
    p.tiles = {
        {Semantic::Wall, 0},
        {Semantic::Floor, 0},
    };
    auto allow = [&](int a, int b) {
        p.tiles[size_t(a)].compat |= (1ull << b);
        p.tiles[size_t(b)].compat |= (1ull << a);
    };
    allow(0, 0);
    allow(0, 1);
    allow(1, 1);
    return p;
}

WfcPreset makeTerrainPreset() {
    // Adjacent biomes only (ordered elevation band).
    // 0 Water 1 Sand 2 Grass 3 Dirt 4 Stone 5 Snow
    WfcPreset p;
    p.name = "terrain";
    p.tiles = {
        {Semantic::Water, 0}, {Semantic::Sand, 0},  {Semantic::Grass, 0},
        {Semantic::Dirt, 0},  {Semantic::Stone, 0}, {Semantic::Snow, 0},
    };
    auto allow = [&](int a, int b) {
        p.tiles[size_t(a)].compat |= (1ull << b);
        p.tiles[size_t(b)].compat |= (1ull << a);
    };
    for (int i = 0; i < 6; ++i) {
        allow(i, i);
        if (i + 1 < 6) allow(i, i + 1);
    }
    return p;
}

const WfcPreset &presetByName(const std::string &name) {
    static const WfcPreset dungeon = makeDungeonPreset();
    static const WfcPreset cave = makeCavePreset();
    static const WfcPreset terrain = makeTerrainPreset();
    if (name == "cave") return cave;
    if (name == "terrain") return terrain;
    return dungeon;
}

bool genWfcSimple(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 4 || h < 4) {
        error = "wfc.simple: size must be at least 4x4";
        return false;
    }
    if (w > 256 || h > 256) {
        error = "wfc.simple: size capped at 256x256";
        return false;
    }

    const std::string presetName = params.getString("preset", "dungeon");
    if (presetName != "dungeon" && presetName != "cave" && presetName != "terrain") {
        error = "wfc.simple: unknown preset '" + presetName + "' (use dungeon|cave|terrain)";
        return false;
    }
    const WfcPreset &preset = presetByName(presetName);
    const int tileCount = int(preset.tiles.size());
    if (tileCount <= 0 || tileCount > 63) {
        error = "wfc.simple: invalid tile set";
        return false;
    }

    const int maxAttempts = std::max(1, params.getInt("maxAttempts", 32));
    const uint64_t fullMask = (tileCount >= 63) ? ~0ull : ((1ull << tileCount) - 1ull);

    std::mt19937 rng(params.getSeed());

    auto trySolve = [&](Grid2D &grid) -> bool {
        std::vector<uint64_t> wave(size_t(w * h), fullMask);
        auto idx = [&](int x, int y) { return size_t(y * w + x); };
        auto countBits = [](uint64_t m) {
            int c = 0;
            while (m) {
                m &= m - 1;
                ++c;
            }
            return c;
        };

        auto pickTile = [&](uint64_t mask) -> int {
            int options[64];
            int n = 0;
            for (int t = 0; t < tileCount; ++t) {
                if (mask & (1ull << t)) options[n++] = t;
            }
            if (n <= 0) return -1;
            std::uniform_int_distribution<int> dist(0, n - 1);
            return options[dist(rng)];
        };

        // Border: force walls for dungeon/cave (keeps maps enclosed).
        if (presetName != "terrain") {
            for (int x = 0; x < w; ++x) {
                wave[idx(x, 0)] = 1ull << 0;
                wave[idx(x, h - 1)] = 1ull << 0;
            }
            for (int y = 0; y < h; ++y) {
                wave[idx(0, y)] = 1ull << 0;
                wave[idx(w - 1, y)] = 1ull << 0;
            }
        }

        auto propagate = [&](int sx, int sy) -> bool {
            std::vector<std::pair<int, int>> stack;
            stack.emplace_back(sx, sy);
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                const uint64_t self = wave[idx(cx, cy)];
                if (self == 0) return false;

                uint64_t allowedNeighbor = 0;
                for (int t = 0; t < tileCount; ++t) {
                    if (self & (1ull << t)) allowedNeighbor |= preset.tiles[size_t(t)].compat;
                }

                const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
                for (const auto &d : dirs) {
                    const int nx = cx + d[0];
                    const int ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    uint64_t &nb = wave[idx(nx, ny)];
                    const uint64_t before = nb;
                    nb &= allowedNeighbor;
                    if (nb == 0) return false;
                    if (nb != before) stack.emplace_back(nx, ny);
                }
            }
            return true;
        };

        // Seed propagate from border constraints.
        if (presetName != "terrain") {
            for (int x = 0; x < w; ++x) {
                if (!propagate(x, 0) || !propagate(x, h - 1)) return false;
            }
            for (int y = 0; y < h; ++y) {
                if (!propagate(0, y) || !propagate(w - 1, y)) return false;
            }
        }

        for (;;) {
            int bestX = -1, bestY = -1;
            int bestEntropy = std::numeric_limits<int>::max();
            bool anyUncollapsed = false;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int e = countBits(wave[idx(x, y)]);
                    if (e == 0) return false;
                    if (e == 1) continue;
                    anyUncollapsed = true;
                    // Prefer lower entropy; break ties randomly via salt.
                    const int salted = e * 1000 + int(rng() % 1000u);
                    if (salted < bestEntropy) {
                        bestEntropy = salted;
                        bestX = x;
                        bestY = y;
                    }
                }
            }
            if (!anyUncollapsed) break;
            if (bestX < 0) return false;

            const int chosen = pickTile(wave[idx(bestX, bestY)]);
            if (chosen < 0) return false;
            wave[idx(bestX, bestY)] = 1ull << chosen;
            if (!propagate(bestX, bestY)) return false;
        }

        grid.resize(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint64_t m = wave[idx(x, y)];
                int tile = 0;
                for (int t = 0; t < tileCount; ++t) {
                    if (m & (1ull << t)) {
                        tile = t;
                        break;
                    }
                }
                grid.setCell(x, y, int(preset.tiles[size_t(tile)].semantic));
            }
        }
        return true;
    };

    bool ok = false;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        // Advance RNG between attempts for a different collapse order.
        if (attempt > 0) (void)rng();
        Grid2D candidate;
        if (trySolve(candidate)) {
            out = std::move(candidate);
            ok = true;
            break;
        }
    }
    if (!ok) {
        error = "wfc.simple: failed to collapse (try different seed/preset or raise maxAttempts)";
        return false;
    }

    out.setMeta("algorithm", "wfc.simple");
    out.setMeta("preset", presetName);

    // Spawn/stairs on walkable cells when present.
    std::vector<std::pair<int, int>> walkable;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = uint32_t(out.getCell(x, y));
            if (c == Semantic::Floor || c == Semantic::Corridor || c == Semantic::Grass ||
                c == Semantic::Dirt || c == Semantic::Sand) {
                walkable.emplace_back(x, y);
            }
        }
    }
    if (!walkable.empty()) {
        std::uniform_int_distribution<size_t> pick(0, walkable.size() - 1);
        auto a = walkable[pick(rng)];
        auto b = walkable[pick(rng)];
        if (a == b && walkable.size() > 1) b = walkable[(pick(rng) + 1) % walkable.size()];
        out.clearObjects();
        out.addObjectAt("spawn", "spawn", float(a.first), float(a.second));
        out.addObjectAt("stairs", "stairs", float(b.first), float(b.second));
    }
    return true;
}

}  // namespace

void registerWfcSimple(GeneratorRegistry &registry) {
    registry.registerAlgorithm("wfc.simple", genWfcSimple);
}

}  // namespace eve::procgen
