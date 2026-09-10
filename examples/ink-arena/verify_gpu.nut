// CPU oracle exists only in verification. Runtime has no CPU paint mirror.
local maximum=0.0, compared=0;
foreach(s in surfaces) {
    local read=s.canvas.readPixels();
    checkInk(read.ok,"GPU surface readback");
    local pixels=read.value;
    for(local gy=0;gy<9;gy++) for(local gx=0;gx<9;gx++) {
        local x=((gx+0.37)*s.w/9).tointeger(), y=((gy+0.63)*s.h/9).tointeger();
        local world=add(s.p,add(mul(s.u,(x+0.5)/s.w),mul(s.v,(y+0.5)/s.h)));
        local rgba=[0.0,0.0,0.0,0.0];
        foreach(c in paintCommands) {
            if(dot(s.n,c.normal)<=0.15) continue;
            local d=sub(world,c.center);
            local depth=fabs(dot(d,c.normal))/c.radius;
            if(depth>=0.85) continue;
            local u=dot(d,c.tu)/(c.radius*2)+0.5;
            local v=dot(d,c.tv)/(c.radius*2)+0.5;
            if(u<=0 || u>=1 || v<=0 || v>=1) continue;
            local fade=clamp((0.85-depth)/0.4,0.0,1.0);
            local coverage=brush.getPixelR((c.tile%4)*256+(u*255).tointeger(),
                (c.tile/4)*256+(v*255).tointeger())*fade*fade*(3-2*fade);
            for(local i=0;i<4;i++) {
                local value=rgba[i]*(1-coverage)+(i==3?1.0:c.color[i])*coverage;
                rgba[i]=floor(value*255+0.5)/255.0;
            }
        }
        local actual=[pixels.getPixelR(x,y),pixels.getPixelG(x,y),pixels.getPixelB(x,y),pixels.getPixelA(x,y)];
        for(local i=0;i<4;i++) { local error=fabs(actual[i]-rgba[i]); if(error>maximum) maximum=error; }
        compared++;
    }
}
print(format("INK_GPU_PARITY samples=%d maxError=%.6f\n",compared,maximum));
checkInk(maximum<=0.025,"GPU paint matches independent world-space CPU oracle within RGBA8 tolerance");
