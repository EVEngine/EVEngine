persist snowWeather = null
persist snowCamera = null
persist snowScene = []
function snowBlock(mesh,x,y,z,sx,sy,sz,r,g,b){local o=eve.Renderable3D();o.setMesh(mesh);o.setPosition(x,y,z);o.setScale(sx,sy,sz);o.setTint(r,g,b,1.0);o.setRoughness(0.84);snowScene.push(o);}
eve_init=function(){
    gfx.setBackgroundColor(0.035,0.055,0.09,1.0);
    snowCamera=eve.Camera3D();snowCamera.setEye(0.0,5.5,13.0);snowCamera.setTarget(0.0,1.5,-10.0);
    snowCamera.setAmbient(0.24,0.27,0.34);snowCamera.setActive(true);
    gfx.setDirectionalLight(-0.5,-1.0,-0.2,2.2,2.3,2.6);
    local cube=gfx.newMeshCube(1.0);
    snowBlock(cube,0.0,-1.0,-11.0,24.0,0.5,30.0,0.18,0.22,0.25);
    snowBlock(cube,-4.0,1.0,-9.0,2.0,4.0,2.0,0.22,0.32,0.38);
    snowBlock(cube,3.5,1.5,-13.0,2.5,5.0,2.5,0.30,0.24,0.20);
    snowWeather=eve.Weather();snowWeather.setPreset("snow");snowWeather.setIntensity(1.0);
    local result=snowWeather.setSnowWind(4.5,-2.8,1.5);if(!result.ok)throw result.status.summary;
    snowWeather.init(gfx);
    print("PCG_SNOW_WIND_READY x="+snowWeather.getSnowWindX()+" y="+snowWeather.getSnowWindY()+" z="+snowWeather.getSnowWindZ()+" world=true");
};
eve_update=function(dt){snowWeather.update(dt,gfx);};
eve_render=function(){gfx.clear();gfx.render3D();};
