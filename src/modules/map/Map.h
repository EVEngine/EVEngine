#pragma once

#include "common/Module.h"
#include "map/TileLayer.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/Fov.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/**
 * Map module — factory + script binding for 2D tilemaps.
 * Per-frame: TileConfigSystem (hot reload) → unified sprite+tile render.
 * Pathfinding: newPathfinder / newPathfinderSize → A* + Flow Field group paths.
 * FOV: newFov / newFovSize → shadowcast visibility + explored memory.
 */
class Map : public Module {
public:
    Module_REG(Map);
    Map() = default;
    ~Map() override = default;

    TileLayer *newLayer(int mapW, int mapH, float tileW = 32.f, float tileH = 32.f);

    /** Pathfinder bound to a TileLayer (syncs walkability from GIDs). */
    Pathfinder *newPathfinder(TileLayer *layer);
    /** Custom grid pathfinder (no layer); fill with setBlocked / setCellCost. */
    Pathfinder *newPathfinderSize(int mapW, int mapH);

    /** FOV bound to a TileLayer (syncs opacity from opaque GIDs). */
    Fov *newFov(TileLayer *layer);
    /** Custom grid FOV (no layer); fill with setOpaque. */
    Fov *newFovSize(int mapW, int mapH);

    /**
     * Load map JSON (Tiled-compatible subset or EVEngine simplified format).
     * Creates one TileLayer per tile layer; returns the first (nullptr on failure).
     * Refreshes the module object cache from objectgroup layers.
     */
    TileLayer *newLayerFromFile(const std::string &path);

    /** Same as newLayerFromFile but returns how many layers were created (0 on failure). */
    int loadFromFile(const std::string &path);

    void update(float dt);
    void render(graphics::Graphics *gfx);
    int pollConfigs();

    int getLayerCount() const;

    int getObjectCount() const;
    std::string getObjectName(int i) const;
    std::string getObjectType(int i) const;
    float getObjectX(int i) const;
    float getObjectY(int i) const;
    float getObjectWidth(int i) const;
    float getObjectHeight(int i) const;
    int getObjectGid(int i) const;

    /** Replace object cache (used by load / hot reload). */
    void setObjects(std::vector<MapObject> objects);

    /**
     * Dual-grid resolve: paint logic (filled/empty), fill display with 15-tile
     * autotiles on a half-offset grid. See DualGrid.h.
     * filledGid 0 = any non-zero logic cell counts as filled.
     */
    bool resolveDualGrid(TileLayer *logic, TileLayer *display);
    bool resolveDualGridFilled(TileLayer *logic, TileLayer *display, int filledGid);
    /** 4-bit corner mask at display cell (dx,dy); see DualGrid.h. */
    int dualGridMaskAt(TileLayer *logic, int dx, int dy, int filledGid);
    /** Default atlas frame for mask (-1 = empty). */
    int dualGridFrame(int mask);
    /** Projection-correct half-step origin delta for a logic layer. */
    float dualGridOffsetX(TileLayer *logic);
    float dualGridOffsetY(TileLayer *logic);
    std::string lastDualGridError() const;

private:
    std::vector<MapObject> objects_;
    std::string dualGridError_;
};

}  // namespace eve::map
