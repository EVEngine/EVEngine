// Editable architectural policy. The engine owns geometric constraints and component selection.
export function houseSettings() { return {
    seed = 20260815, width = 8, depth = 7, floors = 2,
    moduleSize = 1.0, floorHeight = 3.0,
    footprint = "rectangle", roof = "gable", entrance = "auto",
    rooms = ["living,kitchen,bedroom", "living,kitchen,bedroom"],
    // Zero preserves the legacy floor masks. Set to 1 to erode upper floors through GridGraph.
    upperInset = 0
}; }

local stageNeed = function(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
};

export function generateHouse(kit, settings) {
    local request = kit.newRequest();
    request.setSeed(settings.seed); request.setPlot(settings.width, settings.depth);
    request.setFloors(settings.floors); request.setModuleSize(settings.moduleSize);
    request.setFloorHeight(settings.floorHeight); request.setFootprint(settings.footprint);
    request.setRoof(settings.roof); request.setEntrance(settings.entrance);
    local generation = eve.HouseGeneration();
    stageNeed(generation.configure(request, kit));
    for (local floor = 0; floor < settings.floors; ++floor) {
        local mask = stageNeed(generation.nextFloorGrid());
        if (floor > 0 && settings.upperInset > 0) {
            local graph = stageNeed(procgen.newGridGraph());
            stageNeed(graph.addNode("plot", "grid.input"));
            stageNeed(graph.addNode("inset", "grid.shrink"));
            stageNeed(graph.connect("plot", "inset", 0));
            stageNeed(graph.setNodeGrid("plot", mask));
            stageNeed(graph.setNodeInt("inset", "radius", settings.upperInset));
            mask = stageNeed(graph.execute("inset"));
        }
        // Change room purposes per floor, or insert a grid operation before emission.
        stageNeed(generation.emitFloor(mask, settings.rooms[floor % settings.rooms.len()]));
    }
    return stageNeed(generation.finish());
}
