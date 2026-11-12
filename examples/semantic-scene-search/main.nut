// Names and tags are the authored semantic description; no inferred gameplay permissions.
local objects = [
    { id="mine-door", name="废弃矿场入口的木门 Wooden entrance to an abandoned mine", x=2.0, z=1.0, tag="door", interactive=true },
    { id="stone-gate", name="城堡的石头拱门 Stone castle archway", x=5.0, z=2.0, tag="door", interactive=true },
    { id="far-door", name="远处矿场的木门 Distant wooden mine door", x=60.0, z=1.0, tag="door", interactive=true },
    { id="painted-door", name="墙上画的木门 Painted wooden door decoration", x=1.0, z=2.0, tag="door", interactive=false },
    { id="chest", name="装有金币的宝箱 Treasure chest containing gold coins", x=4.0, z=4.0, tag="chest", interactive=true },
    { id="tree", name="一棵古老的橡树 An ancient oak tree", x=7.0, z=4.0, tag="tree", interactive=false },
    { id="observer", name="观察者 Observer looking toward world positive X", x=0.0, z=0.0, tag="observer", interactive=false }
];

eve_init <- function() {
    gfx.setBackgroundColor(0.07, 0.09, 0.12, 1.0);
    scene.beginBuild();
    scene.beginNode("root", "矿场场景 Mine scene");
    foreach (object in objects) {
        scene.addNode(object.id, object.name);
        scene.setBuildPosition(object.x, 0.0, object.z);
        if (object.id == "observer") scene.setBuildRotation(1.57079632679, 0.0, 0.0);
    }
    scene.end();
    if (!scene.mountBuildAs("semantic-demo")) throw "Scene mount failed";
    foreach (object in objects) {
        if (!scene.addNodeTag(object.id, object.tag)) throw "Scene tag failed";
        if (object.interactive && !scene.addNodeTag(object.id, "interactable")) throw "Scene tag failed";
    }
    print("Semantic scene search ready: semantic-demo\n");
};

eve_update <- function(dt) {};
eve_render <- function() {
    gfx.clear();
    // A lightweight map preview; query coordinates remain real scene world units.
    foreach (object in objects) {
        local x = 60.0 + object.x * 10.0;
        local y = 70.0 + object.z * 70.0;
        local r = object.tag == "chest" ? 0.9 : 0.35;
        local g = object.tag == "tree" ? 0.75 : 0.5;
        gfx.drawSolidRect(x, y, 30.0, 35.0, r, g, 0.3, 1.0);
    }
};
