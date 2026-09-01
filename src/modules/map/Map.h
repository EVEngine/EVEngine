#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "map/TileLayer.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/Fov.h"
#include "map/TileCollision.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/**
 * @brief Map module — factory + script binding for 2D tilemaps.
 * Per-frame: TileConfigSystem (hot reload) → unified sprite+tile render.
 * Pathfinding: newPathfinder / newPathfinderSize → A* + Flow Field group paths.
 * FOV: newFov / newFovSize → shadowcast visibility + explored memory.
 */
class Map : public Module {
public:
    Module_REG(Map);
    Map();
    ~Map() override = default;

    TileLayer *newLayer(int mapW, int mapH, float tileW = 32.f, float tileH = 32.f);

    /** @brief Pathfinder bound to a TileLayer (syncs walkability from GIDs). */
    Pathfinder *newPathfinder(TileLayer *layer);
    /** @brief Custom grid pathfinder (no layer); fill with setBlocked / setCellCost. */
    Pathfinder *newPathfinderSize(int mapW, int mapH);

    /** @brief FOV bound to a TileLayer (syncs opacity from opaque GIDs). */
    Fov *newFov(TileLayer *layer);
    /** @brief Custom grid FOV (no layer); fill with setOpaque. */
    Fov *newFovSize(int mapW, int mapH);
    /** @brief Volume FOV (W×H×D voxels); mode defaults to volume. */
    Fov *newFovVolume(int mapW, int mapH, int depth);

    /**
     * @brief Load map JSON (Tiled-compatible subset or EVEngine simplified format).
     * Creates one TileLayer per tile layer; returns the first (nullptr on failure).
     * Refreshes the module object cache from objectgroup layers.
     * @remarks Replacement is transactional at the module boundary: failure preserves the current layers
     * and object cache; success clears and hides replaced layers before publishing the new cache. Returned
     * layers are module-owned and remain borrowed until a later successful replacement or module teardown.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    TileLayer *newLayerFromFile(const std::string &path);

    /**
     * @brief Same transactional replacement as newLayerFromFile, returning the published layer count.
     * @return Positive published layer count, or zero with the previous map preserved on failure.
     */
    int loadFromFile(const std::string &path);

    /**
     * @brief Parse a map and validate its objects before atomically publishing it.
     * @param path Map JSON path borrowed for this call.
     * @param contractJson Versioned `eve.map.object-contract` JSON borrowed for this call.
     * @return Published layer count, or a structured failure with the prior map preserved.
     * @ownership Published layers remain module-owned; path and contract text are not retained.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] eve::Result<int> loadFromFileWithObjectContract(const std::string &path,
                                                                  std::string_view contractJson);

    /**
     * @brief Parse map JSON text and atomically publish it after object-contract admission.
     * @param mapJson Candidate map document borrowed for this call.
     * @param contractJson Versioned object contract borrowed for this call.
     * @return Published layer count, or a structured failure with the prior map preserved.
     * @ownership Candidate text is not retained; successful layers remain module-owned.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] eve::Result<int> loadFromTextWithObjectContract(std::string_view mapJson,
                                                                  std::string_view contractJson);

    void update(float dt);
    void render(graphics::Graphics *gfx);
    int pollConfigs();
    /** @brief Last tile collection counters for project-owned diagnostics UIs. */
    int getLastVisibleTileCount() const;
    int getLastCustomVisualCount() const;
    int getLastAtlasCount() const;
    int getLastVisitedChunkCount() const;
    int getLastVisitedCellCount() const;

    /** @brief Generate merged collision from non-walkable tile metadata and notify adapters. */
    int publishCollision(TileLayer *layer);
    int getCollisionRectCount() const;
    float getCollisionRectX(int index) const;
    float getCollisionRectY(int index) const;
    float getCollisionRectWidth(int index) const;
    float getCollisionRectHeight(int index) const;

    int getLayerCount() const;
    /** @brief Layer created by the most recent loadFromFile/newLayerFromFile call. */
    TileLayer *getLayer(int index) const;

    int getObjectCount() const;
    std::string getObjectName(int i) const;
    std::string getObjectType(int i) const;
    float getObjectX(int i) const;
    float getObjectY(int i) const;
    float getObjectWidth(int i) const;
    float getObjectHeight(int i) const;
    int getObjectGid(int i) const;

    /**
     * @brief Return the number of sorted custom properties on one cached object.
     * @param objectIndex Transient object-cache index.
     * @return Property count, or zero for an invalid object index.
     * @remarks The count and subsequent property indices are invalidated whenever the object cache is replaced.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    int getObjectPropertyCount(int objectIndex) const;

    /**
     * @brief Return an owning custom-property name at deterministic sorted position.
     * @param objectIndex Transient object-cache index.
     * @param propertyIndex Property position in [0, getObjectPropertyCount()).
     * @return Owning property name, or an empty string for an invalid index.
     * @remarks Property indices are transient; persist property names and stable object names instead.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    std::string getObjectPropertyName(int objectIndex, int propertyIndex) const;

    /**
     * @brief Query whether a cached object declares an exact custom-property name.
     * @param objectIndex Transient object-cache index.
     * @param name Property name borrowed only for this call.
     * @return True only for a valid object index with that property.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] bool hasObjectProperty(int objectIndex, std::string_view name) const;

    /**
     * @brief Read the owning canonical text projection of an object's custom property.
     * @param objectIndex Transient object-cache index.
     * @param name Exact property name borrowed only for this call.
     * @param defaultValue Owning default value returned when the object or property is absent.
     * @return Owning property text or the supplied default; no reference into the reloadable cache escapes.
     * @remarks Tiled scalar values are projected to canonical text at load admission. Consumers parse and
     * validate domain-specific values before publishing gameplay state. Duplicate names keep the first value.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] std::string getObjectProperty(int objectIndex, std::string_view name,
                                                std::string defaultValue = {}) const;

    /**
     * @brief Find the first loaded map object with an exact stable name.
     * @param name Object name to match; the input is borrowed only for this call.
     * @return Owning cache index when present, or nullopt when no object matches.
     * @remarks The index is a transient projection and may identify another object after
     * loadFromFile, newLayerFromFile, hot reload, or setObjects. Persist object names, not indices.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] std::optional<std::size_t> findObjectByName(std::string_view name) const;

    /**
     * @brief Find the first loaded object containing a point, optionally filtered by type.
     * @param x Query coordinate in the same space as MapObject::x.
     * @param y Query coordinate in the same space as MapObject::y.
     * @param type Exact object type, or empty to accept any type.
     * @return Owning cache index when present, or nullopt when no object matches.
     * @remarks Positive-size objects use an inclusive rectangle. Point objects require an exact
     * finite coordinate match. The transient index has the same invalidation rules as findObjectByName.
     * @thread Call on the Map module's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] std::optional<std::size_t> findObjectAt(float x, float y,
                                                          std::string_view type = {}) const;

    /** @brief Replace object cache (used by load / hot reload). */
    void setObjects(std::vector<MapObject> objects);

    /**
     * @brief Dual-grid resolve: paint logic (filled/empty), fill display with 15-tile
     * autotiles on a half-offset grid. See DualGrid.h.
     * filledGid 0 = any non-zero logic cell counts as filled.
     */
    bool resolveDualGrid(TileLayer *logic, TileLayer *display);
    bool resolveDualGridFilled(TileLayer *logic, TileLayer *display, int filledGid);
    /** @brief 4-bit corner mask at display cell (dx,dy); see DualGrid.h. */
    int dualGridMaskAt(TileLayer *logic, int dx, int dy, int filledGid);
    /** @brief Default atlas frame for mask (-1 = empty). */
    int dualGridFrame(int mask);
    /** @brief Projection-correct half-step origin delta for a logic layer. */
    float dualGridOffsetX(TileLayer *logic);
    float dualGridOffsetY(TileLayer *logic);
    std::string lastDualGridError() const;

private:
    std::vector<MapObject> objects_;
    std::vector<TileLayer *> loadedLayers_;
    std::string dualGridError_;
    std::vector<TileCollisionRect> collisionRects_;
};

}  // namespace eve::map
