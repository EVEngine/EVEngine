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
    if (!housegen.loadComponentsFromJson(kit)) {
        print("house component error: " + housegen.lastError() + "\n");
        return;
    }
    local request = housegen.newRequest();
    request.setSeed(20260815);
    request.setPlot(7, 6);
    request.setFloors(2);
    layout = housegen.newLayout();
    if (!housegen.generate(request, layout))
        print("house generation failed: " + housegen.lastError() + "\n");
    else
        print("generated " + layout.getInstanceCount() + " component instances\n" + layout.toJson() + "\n");
};

eve_render = function() { gfx.clear(); };
