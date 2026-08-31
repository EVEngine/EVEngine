#pragma once

#include "common/Module.h"
#include "map/TileLayer.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/Fov.h"
#include "map/TileCollision.h"

#include <string>
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
     */
    TileLayer *newLayerFromFile(const std::string &path);

    /** @brief Same as newLayerFromFile but returns how many layers were created (0 on failure). */
    int loadFromFile(const std::string &path);

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
