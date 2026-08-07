# Tiled Isometric/Hex Tilemap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load usable Tiled JSON (base64+zlib/gzip), render orthogonal/isometric/staggered/hexagonal tile layers, parse object groups, and depth-sort tiles with sprites in one 2D queue.

**Architecture:** Extend `TileLayer::Config` with Tiled orientation fields; add pure `TileProjection` helpers; decode compressed layer data in `TileConfig`; cache `MapObject`s on `Map`; introduce `graphics::DrawItem2D` so `Map::render` collects sprites + tiles, sorts by `(canvas, layer, depthY)`, draws without `present()` (existing `RenderSystem::render` keeps present for sprite-only tests).

**Tech Stack:** C++17, Poco JSON, `eve::data` base64/zlib/gzip, ECS `TileLayer` / `Renderable2D`, zeroerr tests, Squirrel bindings via simplesquirrel.

**Spec:** `docs/2026-08-07-tiled-isometric-hex-design.md`

## Global Constraints

- No infinite maps, external `.tsx`, TMX XML, flip/rotate UV drawing, or `zstd` in v1
- Unknown `orientation` → load failure (no silent orthogonal fallback)
- `loadMapFile` failure must not leave half-created `TileLayer` entities
- Particles/UI stay outside the unified 2D queue
- `Map::render` must **not** call `gfx.present()` (example draws more after map)
- Graphics must not depend on map (map may include graphics headers)
- Keep existing orthogonal `demo.json` / `test/map.cpp` behaviors

## File structure

| File | Responsibility |
|------|----------------|
| `src/modules/map/TileProjection.h` (+ `.cpp` if non-inline) | Orientation enums + `tileToWorld` / `tileToDepthY` / `worldToTile` |
| `src/modules/map/TileLayer.h` | `Config` fields; thin wrappers calling projection |
| `src/modules/map/TileConfig.cpp` / `.h` | Decode layer data; parse orientation; parse objectgroups; `loadMapFile` rollback |
| `src/modules/map/MapObject.h` | `MapObject` POD |
| `src/modules/map/Map.h` / `.cpp` | Object cache API; `render` = unified collect+draw |
| `src/modules/map/TileSystem.h` / `.cpp` | `collect(std::vector<DrawItem2D>&)`; keep null-safe `render` for tests |
| `src/modules/graphics/DrawItem2D.h` | Shared draw item + sort comparator helpers |
| `src/modules/graphics/RenderSystem.h` / `.cpp` | `collectSprites` / `drawItems` / `render` = sprites+draw+present |
| `test/map.cpp` | Projection, decode, objects, depth sort smoke |

---

### Task 1: TileProjection + Config fields

**Files:**
- Create: `src/modules/map/TileProjection.h`
- Create: `src/modules/map/TileProjection.cpp`
- Modify: `src/modules/map/TileLayer.h` (`Config` struct)
- Modify: `src/modules/map/TileLayer.cpp` (wrappers)
- Test: `test/map.cpp`

**Interfaces:**
- Produces:
  - `enum class MapOrientation { Orthogonal, Isometric, Staggered, Hexagonal };`
  - `enum class StaggerAxis { X, Y };`
  - `enum class StaggerIndex { Odd, Even };`
  - `void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy);`
  - `float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty);`
  - `void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty);`

- [ ] **Step 1: Write failing projection tests**

Append to `test/map.cpp`:

```cpp
#include "map/TileProjection.h"

TEST_CASE("map.projection.orthogonal") {
    TileLayer::Config cfg;
    cfg.mapW = 10; cfg.mapH = 10;
    cfg.tileW = 32.f; cfg.tileH = 32.f;
    cfg.originX = 10.f; cfg.originY = 20.f;
    cfg.orientation = MapOrientation::Orthogonal;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 2, 3, wx, wy);
    CHECK_EQ(wx, 10.f + 2 * 32.f);
    CHECK_EQ(wy, 20.f + 3 * 32.f);
    CHECK_EQ(tileToDepthY(cfg, 2, 3), 20.f + 4 * 32.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 1.f, wy + 1.f, tx, ty);
    CHECK_EQ(tx, 2);
    CHECK_EQ(ty, 3);
}

TEST_CASE("map.projection.isometric") {
    TileLayer::Config cfg;
    cfg.tileW = 64.f; cfg.tileH = 32.f;
    cfg.originX = 0.f; cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 1, 0, wx, wy);
    // Tiled: x = (tx - ty) * tw/2, y = (tx + ty) * th/2
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, -32.f);
    CHECK_EQ(wy, 16.f);
    CHECK_EQ(tileToDepthY(cfg, 1, 0), 16.f + 32.f);
}

TEST_CASE("map.projection.staggeredYOdd") {
    TileLayer::Config cfg;
    cfg.tileW = 64.f; cfg.tileH = 32.f;
    cfg.originX = 0.f; cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Staggered;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 0.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);  // odd row shifted by tw/2
    CHECK_EQ(wy, 16.f);  // th/2 per row
}
```

- [ ] **Step 2: Run tests — expect fail (types missing)**

```powershell
cmake --build build --config Debug --target eve_test
.\build\Debug\eve_test.exe "map.projection"
```

Expected: compile error or FAIL (symbols not found).

- [ ] **Step 3: Implement Config fields + projection**

In `TileLayer.h` `Config`:

```cpp
MapOrientation orientation = MapOrientation::Orthogonal;
StaggerAxis staggerAxis = StaggerAxis::Y;
StaggerIndex staggerIndex = StaggerIndex::Odd;
float hexSideLength = 0.f;
```

In `TileProjection.cpp` (formulas match Tiled):

```cpp
static bool staggerDoShift(const TileLayer::Config &cfg, int tx, int ty) {
    const int major = (cfg.staggerAxis == StaggerAxis::Y) ? ty : tx;
    const bool odd = (major & 1) != 0;
    return cfg.staggerIndex == StaggerIndex::Odd ? odd : !odd;
}

void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy) {
    const float tw = cfg.tileW, th = cfg.tileH;
    switch (cfg.orientation) {
    case MapOrientation::Orthogonal:
        wx = cfg.originX + float(tx) * tw;
        wy = cfg.originY + float(ty) * th;
        break;
    case MapOrientation::Isometric:
        wx = cfg.originX + float(tx - ty) * tw * 0.5f;
        wy = cfg.originY + float(tx + ty) * th * 0.5f;
        break;
    case MapOrientation::Staggered:
    case MapOrientation::Hexagonal: {
        // Match Tiled HexagonalRenderer / StaggeredRenderer:
        // sideOffset = (tileExtent - hexSideLength) / 2
        // pitch = tileExtent - sideOffset  (= (tileExtent + hexSideLength) / 2 for hex)
        const bool hex = cfg.orientation == MapOrientation::Hexagonal && cfg.hexSideLength > 0.f;
        if (cfg.staggerAxis == StaggerAxis::Y) {
            const float pitchY = hex ? (th + cfg.hexSideLength) * 0.5f : th * 0.5f;
            wx = cfg.originX + float(tx) * tw + (staggerDoShift(cfg, tx, ty) ? tw * 0.5f : 0.f);
            wy = cfg.originY + float(ty) * pitchY;
        } else {
            const float pitchX = hex ? (tw + cfg.hexSideLength) * 0.5f : tw * 0.5f;
            wx = cfg.originX + float(tx) * pitchX;
            wy = cfg.originY + float(ty) * th + (staggerDoShift(cfg, tx, ty) ? th * 0.5f : 0.f);
        }
        break;
    }
    }
}

float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty) {
    float wx = 0, wy = 0;
    tileToWorld(cfg, tx, ty, wx, wy);
    return wy + cfg.tileH;  // foot = bottom of tile sprite rect
}

void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty) {
    wx -= cfg.originX;
    wy -= cfg.originY;
    switch (cfg.orientation) {
    case MapOrientation::Orthogonal:
        tx = int(std::floor(wx / cfg.tileW));
        ty = int(std::floor(wy / cfg.tileH));
        break;
    case MapOrientation::Isometric: {
        // Inverse of (tx-ty)*tw/2, (tx+ty)*th/2
        const float a = wx / (cfg.tileW * 0.5f);
        const float b = wy / (cfg.tileH * 0.5f);
        tx = int(std::floor((b + a) * 0.5f));
        ty = int(std::floor((b - a) * 0.5f));
        break;
    }
    default: {
        // Brute nearest in map bounds (correct for stagger/hex v1)
        tx = 0; ty = 0;
        float best = 1e30f;
        for (int y = 0; y < std::max(1, cfg.mapH); ++y) {
            for (int x = 0; x < std::max(1, cfg.mapW); ++x) {
                float cx, cy;
                tileToWorld(cfg, x, y, cx, cy);
                cx += cfg.tileW * 0.5f;
                cy += cfg.tileH * 0.5f;
                const float d = (cx - (wx + cfg.originX)) * (cx - (wx + cfg.originX)) +
                                (cy - (wy + cfg.originY)) * (cy - (wy + cfg.originY));
                if (d < best) { best = d; tx = x; ty = y; }
            }
        }
        break;
    }
    }
}
```

Fix `worldToTile` staggered/hex nearest-search to use absolute world coords consistently (pass full `wx,wy` without double-subtracting origin — implement carefully when coding).

Add `TileLayer` methods:

```cpp
void tileToWorld(int tx, int ty, float &wx, float &wy);
float depthY(int tx, int ty);
void worldToTile(float wx, float wy, int &tx, int &ty);
```

- [ ] **Step 4: Run tests — expect pass**

```powershell
cmake --build build --config Debug --target eve_test
.\build\Debug\eve_test.exe "map.projection"
```

Expected: all `map.projection.*` PASS. Also run `.\build\Debug\eve_test.exe "map."` for regression.

- [ ] **Step 5: Commit**

```powershell
git add src/modules/map/TileProjection.h src/modules/map/TileProjection.cpp src/modules/map/TileLayer.h src/modules/map/TileLayer.cpp test/map.cpp
git commit -m "feat(map): add tile projection for ortho/iso/stagger/hex"
```

---

### Task 2: Decode Tiled layer data (array + base64 + zlib/gzip)

**Files:**
- Modify: `src/modules/map/TileConfig.cpp`
- Modify: `src/modules/map/TileConfig.h` (declare helper if tested externally)
- Test: `test/map.cpp`

**Interfaces:**
- Consumes: `eve::data::decode("base64", ...)`, `eve::data::decompress("zlib"|"gzip", ...)`
- Produces: `bool decodeLayerData(Poco::JSON::Object::Ptr layerObj, size_t expectedCount, std::vector<uint32_t> &out, std::string *error);`

- [ ] **Step 1: Write failing decode tests**

```cpp
#include "data/DataModule.h"
#include <memory>
#include <cstring>

TEST_CASE("map.decode.base64ZlibRoundTrip") {
    // 2x2 GIDs: 1,2,3,4 little-endian uint32
    uint32_t gids[4] = {1, 2, 3, 4};
    size_t rawn = sizeof(gids);
    size_t csz = 0;
    std::unique_ptr<eve::data::CompressedData> cdata(
        eve::data::compress("zlib", reinterpret_cast<const char *>(gids), rawn, -1));
    REQUIRE(cdata);
    size_t b64n = 0;
    std::unique_ptr<char[]> b64(eve::data::encode(
        "base64", static_cast<const char *>(cdata->getData()), cdata->getSize(), b64n));
    REQUIRE(b64);

    std::string json = std::string("{\"width\":2,\"height\":2,\"tilewidth\":8,\"tileheight\":8,") +
        "\"encoding\":\"base64\",\"compression\":\"zlib\",\"data\":\"" +
        std::string(b64.get(), b64n) + "\"}";
    // Flat root with encoding at root — applyConfig must honor encoding/compression on the object that owns data
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 8.f, 8.f);
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(1, 0), 2);
    CHECK_EQ(layer->getTile(0, 1), 3);
    CHECK_EQ(layer->getTile(1, 1), 4);
}

TEST_CASE("map.decode.zstdFails") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 8.f, 8.f);
    const char *json = R"({"width":1,"height":1,"tilewidth":8,"tileheight":8,
        "encoding":"base64","compression":"zstd","data":"AAAA"})";
    CHECK(!layer->applyConfig(json));
}
```

- [ ] **Step 2: Run — expect fail**

```powershell
.\build\Debug\eve_test.exe "map.decode"
```

Expected: FAIL (`applyConfig` ignores encoding).

- [ ] **Step 3: Implement `decodeLayerData` and wire into `readDataArray` / apply paths**

Replace/extend current `readDataArray` usage:

```cpp
bool decodeLayerData(Poco::JSON::Object::Ptr layerObj, size_t expectedCount,
                     std::vector<uint32_t> &out, std::string *error) {
    if (!layerObj || !layerObj->has("data")) {
        if (error) *error = "missing data";
        return false;
    }
    // Path A: JSON array
    try {
        if (layerObj->isArray("data")) {
            auto arr = layerObj->getArray("data");
            out.resize(arr->size());
            for (size_t i = 0; i < arr->size(); ++i)
                out[i] = uint32_t(asInt(arr->get(i), 0));
            if (out.size() != expectedCount) {
                if (error) *error = "gid count mismatch";
                return false;
            }
            return true;
        }
    } catch (...) {}

    std::string encoding = layerObj->has("encoding") ? asString(layerObj->get("encoding")) : "";
    std::string compression = layerObj->has("compression") ? asString(layerObj->get("compression")) : "";
    if (encoding != "base64") {
        if (error) *error = "unsupported encoding";
        return false;
    }
    if (compression == "zstd") {
        if (error) *error = "zstd not supported; export as zlib";
        return false;
    }
    const std::string b64 = asString(layerObj->get("data"));
    size_t decodedLen = 0;
    std::unique_ptr<char[]> decoded(
        eve::data::decode("base64", b64.data(), b64.size(), decodedLen));
    if (!decoded) {
        if (error) *error = "base64 decode failed";
        return false;
    }
    const char *bytes = decoded.get();
    size_t nbytes = decodedLen;
    std::unique_ptr<char[]> inflated;
    if (!compression.empty()) {
        if (compression != "zlib" && compression != "gzip") {
            if (error) *error = "unsupported compression";
            return false;
        }
        size_t rawsize = expectedCount * 4;
        inflated.reset(eve::data::decompress(compression, bytes, nbytes, rawsize));
        if (!inflated) {
            if (error) *error = "decompress failed";
            return false;
        }
        bytes = inflated.get();
        nbytes = rawsize;
    }
    if (nbytes != expectedCount * 4) {
        if (error) *error = "gid byte length mismatch";
        return false;
    }
    out.resize(expectedCount);
    for (size_t i = 0; i < expectedCount; ++i) {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(bytes) + i * 4;
        out[i] = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
                 (uint32_t(p[3]) << 24);
    }
    return true;
}
```

Use in `applyFlatLayerData` / `applyOneLayerObject` with `expectedCount = mapW*mapH`. Propagate failure to `applyConfigText` error string when possible.

- [ ] **Step 4: Run — expect pass**

```powershell
cmake --build build --config Debug --target eve_test
.\build\Debug\eve_test.exe "map.decode"
.\build\Debug\eve_test.exe "map."
```

- [ ] **Step 5: Commit**

```powershell
git add src/modules/map/TileConfig.cpp src/modules/map/TileConfig.h test/map.cpp
git commit -m "feat(map): decode Tiled base64 zlib/gzip layer data"
```

---

### Task 3: Parse orientation globals into Config

**Files:**
- Modify: `src/modules/map/TileConfig.cpp` (`applyMapGlobals`, `loadMapFile`)
- Test: `test/map.cpp`

**Interfaces:**
- Consumes: Task 1 enums
- Produces: Config filled from JSON keys `orientation`, `staggeraxis`, `staggerindex`, `hexsidelength`

- [ ] **Step 1: Write failing test**

```cpp
TEST_CASE("map.config.orientationIsometric") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 64.f, 32.f);
    const char *json = R"({
        "width": 2, "height": 2, "tilewidth": 64, "tileheight": 32,
        "orientation": "isometric",
        "data": [1, 0, 0, 1]
    })";
    CHECK(layer->applyConfig(json));
    CHECK(layer->config()->orientation == MapOrientation::Isometric);
}

TEST_CASE("map.config.unknownOrientationFails") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 8.f, 8.f);
    CHECK(!layer->applyConfig(R"({"width":1,"height":1,"orientation":"banana","data":[1]})"));
}
```

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement parse helper**

```cpp
bool parseOrientation(Poco::JSON::Object::Ptr root, TileLayer::Config *cfg, std::string *error) {
    if (!root || !cfg) return false;
    if (!root->has("orientation")) return true;
    const std::string o = asString(root->get("orientation"));
    if (o.empty() || o == "orthogonal") cfg->orientation = MapOrientation::Orthogonal;
    else if (o == "isometric") cfg->orientation = MapOrientation::Isometric;
    else if (o == "staggered") cfg->orientation = MapOrientation::Staggered;
    else if (o == "hexagonal") cfg->orientation = MapOrientation::Hexagonal;
    else {
        if (error) *error = "unknown orientation: " + o;
        return false;
    }
    if (root->has("staggeraxis")) {
        const std::string a = asString(root->get("staggeraxis"));
        cfg->staggerAxis = (a == "x") ? StaggerAxis::X : StaggerAxis::Y;
    }
    if (root->has("staggerindex")) {
        const std::string i = asString(root->get("staggerindex"));
        cfg->staggerIndex = (i == "even") ? StaggerIndex::Even : StaggerIndex::Odd;
    }
    if (root->has("hexsidelength"))
        cfg->hexSideLength = asFloat(root->get("hexsidelength"), 0.f);
    return true;
}
```

Call from `applyMapGlobals`; if false, `applyConfigDocument` returns false. Same in `loadMapFile` before creating layers.

- [ ] **Step 4: Run — expect pass**

- [ ] **Step 5: Commit**

```powershell
git commit -am "feat(map): parse Tiled orientation into TileLayer config"
```

---

### Task 4: MapObject cache + objectgroup loading

**Files:**
- Create: `src/modules/map/MapObject.h`
- Modify: `src/modules/map/TileConfig.h` / `.cpp` (`loadMapFile` signature or side-channel)
- Modify: `src/modules/map/Map.h` / `.cpp`
- Test: `test/map.cpp`

**Interfaces:**
- Produces:
  - `struct MapObject { std::string name, type; float x,y,width,height; uint32_t gid; };`
  - `std::vector<TileLayer *> loadMapFile(const std::string &path, std::vector<MapObject> *objects, std::string *error);`  
    (keep overload or default `objects=nullptr` for compatibility)
  - `Map::getObjectCount()`, `getObjectName(i)`, `getObjectType(i)`, `getObjectX/Y/Width/Height(i)`, `getObjectGid(i)`
  - On `Map::loadFromFile` / `newLayerFromFile`: replace module object cache

- [ ] **Step 1: Write failing test**

Prefer in-memory path: add `loadMapText` or test via temp file. Simplest: extend `applyConfig` is not enough for objects — add:

```cpp
// In TileConfig.h
std::vector<TileLayer *> loadMapText(const std::string &json, std::vector<MapObject> *objects,
                                     std::string *error = nullptr);
```

Test:

```cpp
TEST_CASE("map.objects.parseObjectGroup") {
    const char *json = R"({
      "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
      "layers": [
        { "type": "tilelayer", "width": 2, "height": 1, "data": [1, 0] },
        { "type": "objectgroup", "objects": [
            { "name": "spawn", "type": "player", "x": 8, "y": 24, "width": 16, "height": 16 }
        ]}
      ]
    })";
    std::vector<MapObject> objs;
    std::string err;
    auto layers = loadMapText(json, &objs, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    REQUIRE(objs.size() == 1);
    CHECK_EQ(objs[0].name, "spawn");
    CHECK_EQ(objs[0].type, "player");
    CHECK_EQ(objs[0].x, 8.f);
    CHECK_EQ(objs[0].y, 24.f);
}
```

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement**

- Parse objectgroup in `loadMapText` / `loadMapFile` loop.
- Skip tilesets with `source` field when selecting default tileset.
- On any failure after creating layers: destroy each created `TileLayer` (use ECS destroy / `release` pattern already in project — check how entities are deleted; if no destroy API, document and clear gids + hide as fallback; prefer proper entity destroy if available).
- `Map` stores `std::vector<MapObject> objects_; std::string objectsPath_;`
- `loadFromFile` fills `objects_` from `loadMapFile`
- Hot reload: in `TileConfigSystem::poll` / `HotReload` tilemap path, when reloading, also refresh `Map` objects if path matches (call `Map::create()->reloadObjectsFromPath(path)` or store objects on first layer resource path and re-parse).

Minimal hot-reload: `Map::loadFromFile` / dedicated `Map::reloadObjects(path)` called from `reloadConfigFile` when Map singleton exists.

- [ ] **Step 4: Run — expect pass**

- [ ] **Step 5: Commit**

```powershell
git commit -am "feat(map): parse Tiled object groups into Map object cache"
```

---

### Task 5: DrawItem2D + depthY sort (sprites)

**Files:**
- Create: `src/modules/graphics/DrawItem2D.h`
- Modify: `src/modules/graphics/RenderSystem.h`
- Modify: `src/modules/graphics/RenderSystem.cpp`
- Test: `test/RenderSystem.cpp` (add depth order unit test if feasible without GPU assert) **or** pure sort test in `test/map.cpp`

**Interfaces:**
- Produces:
  - `struct DrawItem2D { ...; float depthY; int layer; Canvas *canvas; ... existing Item fields ... };`
  - `void RenderSystem::collectSprites(std::vector<DrawItem2D> &out);`
  - `void RenderSystem::drawItems(Graphics &gfx, std::vector<DrawItem2D> &items, bool present);`
  - `RenderSystem::render` = collectSprites + drawItems(..., true)
  - Sort: canvas offscreen → canvas ptr → layer → depthY → lit/shader/texture

- [ ] **Step 1: Write failing sort test**

```cpp
TEST_CASE("map.drawItem.sortByDepthY") {
    using eve::graphics::DrawItem2D;
    std::vector<DrawItem2D> items(2);
    items[0].layer = 0; items[0].depthY = 100.f; items[0].canvas = nullptr;
    items[1].layer = 0; items[1].depthY = 50.f;  items[1].canvas = nullptr;
    eve::graphics::sortDrawItems2D(items);
    CHECK_EQ(items[0].depthY, 50.f);
    CHECK_EQ(items[1].depthY, 100.f);
}
```

Declare `sortDrawItems2D` in `DrawItem2D.h`.

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Refactor RenderSystem**

Extract current `Item` into `DrawItem2D` (add `depthY`, `u0,v0,u1,v1`, `bool solid`, `Color solidColor` for tile debug path).

Sprite collect:

```cpp
item.depthY = xf->y + sp->height * xf->sy;
```

`drawItems`: move existing draw loop; if `present` then `gfx.present()` else skip.

`render(Graphics& gfx)`:

```cpp
std::vector<DrawItem2D> items;
collectSprites(items);
drawItems(gfx, items, true);
```

- [ ] **Step 4: Run existing RenderSystem + new sort tests**

```powershell
.\build\Debug\eve_test.exe "RenderSystem"
.\build\Debug\eve_test.exe "map.drawItem"
```

Expected: PASS (no sprite regression).

- [ ] **Step 5: Commit**

```powershell
git commit -am "refactor(graphics): DrawItem2D queue with depthY sort"
```

---

### Task 6: Tile collect + Map::render unified queue

**Files:**
- Modify: `src/modules/map/TileSystem.h` / `.cpp`
- Modify: `src/modules/map/Map.cpp`
- Test: `test/map.cpp`

**Interfaces:**
- Consumes: `tileToWorld`, `tileToDepthY`, `DrawItem2D`, `RenderSystem::collectSprites/drawItems`
- Produces: `TileRenderSystem::collect(std::vector<graphics::DrawItem2D> &out);`
- `Map::render(gfx)` = collectSprites + collect tiles + drawItems(..., false)
- `TileRenderSystem::render(gfx)` for tests: collect tiles only + drawItems(..., false); null gfx no-op

- [ ] **Step 1: Write failing interleave test (logic-level)**

```cpp
TEST_CASE("map.render.tilesContributeDepthY") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 2, 32.f, 32.f);
    layer->config()->orientation = MapOrientation::Orthogonal;
    layer->setOrigin(0, 0);
    layer->setTile(0, 0, 1);
    layer->setTile(0, 1, 1);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 2);
    CHECK(items[0].depthY < items[1].depthY); // after sort or check raw then sort
    eve::graphics::sortDrawItems2D(items);
    CHECK(items[0].depthY < items[1].depthY);
}
```

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement collect**

For each visible tile with `tileGid(raw)!=0`:

```cpp
DrawItem2D it;
tileToWorld(*cfg, tx, ty, it.x, it.y);
it.w = cfg->tileW; it.h = cfg->tileH;
it.depthY = tileToDepthY(*cfg, tx, ty);
it.layer = draw->layer;
it.canvas = draw->canvas;
it.cam = fromEntity(draw->camera);
// UV via atlasUV or solid color path flags
```

`Map::render`:

```cpp
void Map::render(graphics::Graphics *gfx) {
    if (!gfx) return;
    std::vector<graphics::DrawItem2D> items;
    graphics::RenderSystem::collectSprites(items);
    TileRenderSystem::collect(items);
    graphics::RenderSystem::drawItems(*gfx, items, false);
}
```

Update isometric placement: use `tileToWorld` instead of ortho multiply in collect (orthogonal becomes special case of projection).

- [ ] **Step 4: Run**

```powershell
.\build\Debug\eve_test.exe "map."
.\build\Debug\eve_test.exe "RenderSystem"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git commit -am "feat(map): unify tile and sprite 2D draw queue"
```

---

### Task 7: Script bindings + docs touch-up

**Files:**
- Modify: `src/modules/map/Map.cpp` (`expose`)
- Modify: `src/modules/map/TileLayer.cpp` / `.h` if wrappers need binding-friendly signatures
- Modify: `docs/模块设计.md` or `docs/2D渲染API设计.md` — one short bullet that map supports Tiled orientations + object cache
- Optional: small isometric example JSON under `example/maps/` (no external image required)

**Interfaces:**
- TileLayer: `tileToWorldX/Y`, `depthY`, `worldToTileX/Y` (split returns if Squirrel can't do out-params easily)
- Map: `getObjectCount`, `getObjectName`, `getObjectType`, `getObjectX`, `getObjectY`, `getObjectWidth`, `getObjectHeight`, `getObjectGid`

- [ ] **Step 1: Add binding-friendly wrappers**

```cpp
// TileLayer
float tileToWorldX(int tx, int ty);
float tileToWorldY(int tx, int ty);
float depthYAt(int tx, int ty);
int worldToTileX(float wx, float wy);
int worldToTileY(float wx, float wy);
```

- [ ] **Step 2: Expose on Map / TileLayer in `Map::expose`**

- [ ] **Step 3: Manual sanity** — load ortho demo still works (`example/maps/demo.json`)

- [ ] **Step 4: Commit**

```powershell
git commit -am "feat(map): expose projection and map objects to scripts"
```

---

### Task 8: Final verification

- [ ] **Step 1: Run full map + related suites**

```powershell
cmake --build build --config Debug --target eve_test
.\build\Debug\eve_test.exe "map."
.\build\Debug\eve_test.exe "RenderSystem"
.\build\Debug\eve_test.exe "Lighting2D"
```

Expected: PASS (or pre-existing failures unrelated — do not ignore new failures in map/RenderSystem).

- [ ] **Step 2: Spec checklist**

Confirm against `docs/2026-08-07-tiled-isometric-hex-design.md`:

1. Tiled JSON base64+zlib load — Task 2
2. isometric / staggered / hexagonal projection — Task 1+3
3. Unified depth queue — Task 5+6
4. Object layers enumerable — Task 4+7
5. Orthogonal regression — Task 8

- [ ] **Step 3: Commit any leftover fixes** (only if needed)

---

## Spec coverage self-review

| Spec requirement | Task |
|------------------|------|
| Config orientation fields | 1, 3 |
| tileToWorld / depthY / worldToTile | 1, 7 |
| base64 + zlib/gzip; zstd fails | 2 |
| Unknown orientation fails | 3 |
| Objectgroup → MapObject cache | 4, 7 |
| Unified DrawItem2D queue + depthY | 5, 6 |
| Map::render no present | 6 |
| No infinite/tsx/tmx/flip/zstd/particles | Global Constraints |
| loadMapFile rollback | 4 |
| Tests listed in spec | 1–6, 8 |

## Placeholder / consistency notes

- Enum names: `MapOrientation`, `StaggerAxis`, `StaggerIndex` (use everywhere)
- Draw API: `collectSprites` / `drawItems` / `sortDrawItems2D` / `TileRenderSystem::collect`
- Object API: `getObjectCount` + field getters (not a single struct return to Squirrel)
- Hex row spacing: implement with `hexSideLength` as in Task 1; adjust tests if Tiled reference numbers differ slightly — prefer matching [Tiled isometric docs](https://doc.mapeditor.org/en/stable/reference/tmx-map-format/#tmx-tileset) / libtiled placement
