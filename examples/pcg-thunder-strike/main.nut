persist pcgThunderCamera = null
persist pcgThunderLight = null
persist pcgThunderState = null
persist pcgThunderObjects = []
persist pcgThunderWeather = null

function thunderCube(mesh,x,y,z,sx,sy,sz,r,g,b) {
    local object=eve.Renderable3D(); object.setMesh(mesh); object.setPosition(x,y,z);
    object.setScale(sx,sy,sz); object.setTint(r,g,b,1.0); object.setRoughness(0.85);
    pcgThunderObjects.push(object);
}

eve_init=function() {
    gfx.setBackgroundColor(0.018,0.025,0.055,1.0);
    pcgThunderCamera=eve.Camera3D(); pcgThunderCamera.setEye(12.0,8.0,18.0);
    pcgThunderCamera.setTarget(0.0,1.0,-8.0); pcgThunderCamera.setAmbient(0.06,0.07,0.11);
    pcgThunderLight=eve.Light3D(); pcgThunderLight.setType("point");
    pcgThunderLight.setColor(0.40,0.58,1.0,2.5);
    pcgThunderState=eve.ThunderStrikeState(); local settings=eve.ThunderStrikeSettings();
    settings.intensity=2.5; settings.radius=420.0; settings.volume=0.8; settings.audioClipCount=4;
    local strike=eve.triggerThunderStrike(pcgThunderState,settings,0.0,0.0,-8.0,20260912);
    assert(strike.ok); pcgThunderLight.setPosition(strike.value.x,strike.value.y,strike.value.z);
    pcgThunderLight.setRadius(strike.value.radius);
    pcgThunderWeather=eve.Weather(); pcgThunderWeather.init(gfx); pcgThunderWeather.setPreset("storm");
    pcgThunderWeather.setIntensity(0.72); pcgThunderWeather.strike();
    local cube=gfx.newMeshCube(1.0); thunderCube(cube,0.0,-1.0,-8.0,24.0,0.5,28.0,0.08,0.11,0.16);
    thunderCube(cube,-4.0,1.0,-8.0,2.5,4.0,2.5,0.15,0.28,0.38);
    thunderCube(cube,2.5,2.0,-10.0,3.0,6.0,3.0,0.18,0.24,0.34);
    print("PCG_THUNDER_STRIKE_READY audioIndex="+strike.value.audioClipIndex+
          " radius="+strike.value.radius+" seed=20260912\n");
};
eve_update=function(dt) {
    pcgThunderWeather.update(dt,gfx); eve.advanceThunderStrike(pcgThunderState,dt);
    pcgThunderLight.setEnabled(pcgThunderState.playing);
    pcgThunderLight.setColor(0.40,0.58,1.0,pcgThunderState.intensity);
};
eve_render=function(){gfx.clear();gfx.render3D();};
