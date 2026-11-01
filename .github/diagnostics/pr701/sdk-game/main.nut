eve_init=function(){
    eve.Plugins().load("pr701_probe.dylib");
    local probe=eve.GpuProbe();
    local failures=probe.run();
    failures+=probe.runOcean();
    print("ENGINE_PROBE_DONE failures="+failures+"\n");
};
eve_update=function(dt){};
eve_render=function(){gfx.clear();};
