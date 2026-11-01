eve_init=function(){eve.Plugins().load("pr701_probe.dylib");local p=eve.GpuProbe();local failures=p.run();print("ENGINE_PROBE_DONE failures="+failures+"\n");};
eve_update=function(dt){};eve_render=function(){gfx.clear();};
