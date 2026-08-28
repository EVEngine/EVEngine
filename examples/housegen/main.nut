// Runtime API smoke example. Replace model paths in the JSON with converted CC0 GLB assets.
persist housegen = null
persist layout = null

eve_init = function() {
    housegen = eve.HouseGen();
    local kit = @"[
      {""id"":""foundation"",""model"":""assets/foundation.glb"",""category"":""foundation""},
      {""id"":""floor"",""model"":""assets/floor.glb"",""category"":""floor""},
      {""id"":""wall"",""model"":""assets/wall.glb"",""category"":""wall""},
      {""id"":""door"",""model"":""assets/door.glb"",""category"":""door""},
      {""id"":""roof"",""model"":""assets/roof.glb"",""category"":""roof""}
    ]";
    local componentResult = housegen.loadComponentsFromJson(kit);
    if (!componentResult.ok) {
        print("house component error: " + componentResult.status.summary + "\n");
        return;
    }
    local request = housegen.newRequest();
    request.setSeed(20260815);
    request.setPlot(7, 6);
    request.setFloors(2);
    layout = housegen.newLayout();
    local generationResult = housegen.generate(request, layout);
    if (!generationResult.ok)
        print("house generation failed: " + generationResult.status.summary + "\n");
    else
        print("generated " + layout.getInstanceCount() + " component instances\n" + layout.toJson() + "\n");
};

eve_render = function() { gfx.clear(); };
