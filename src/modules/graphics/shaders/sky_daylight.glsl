// Clear, non-astronomical UDS material chain. One injected day phase drives all branches.
float skyNightFilter(float sunY) {
    float daylight = clamp((sunY + .03604) / (.03604 + .06319), 0.0, 1.0);
    return (1.0-daylight)*(1.0-daylight);
}
float skyTwilight(float forwardZ) { return pow(clamp((.25-forwardZ)/.26,0.0,1.0),11.0); }
vec3 skyScatteringColor(float forwardZ) {
    float angle=degrees(acos(clamp(forwardZ,-1.0,1.0)));
    if (angle<=atmosphere.cycleScattering[0].x) return atmosphere.cycleScattering[0].yzw;
    for (int i=1;i<21;++i) {
        vec4 a=atmosphere.cycleScattering[i-1], b=atmosphere.cycleScattering[i];
        if (angle<=b.x) return mix(a.yzw,b.yzw,(angle-a.x)/(b.x-a.x));
    }
    return atmosphere.cycleScattering[20].yzw;
}
vec3 skyCycleMoon(float dayPhase) {
    vec4 orbit=atmosphere.cycleMoonOrbit;
    float phase=(dayPhase+orbit.w)*2.0*PI;
    float pitch=radians(orbit.x),yaw=radians(orbit.y);
    vec3 v=vec3(sin(pitch)*cos(phase),sin(phase),-cos(pitch)*cos(phase)-orbit.z);
    return normalize(vec3(cos(yaw)*v.x-sin(yaw)*v.y,sin(yaw)*v.x+cos(yaw)*v.y,v.z));
}
void skyDaylightMaterial(float sunY, vec3 moonForward, out vec3 cloudColor,
                        out vec3 contrast, out vec4 glowNight, out vec3 fogBase) {
    float night=skyNightFilter(sunY);
    float sunTwilight=skyTwilight(-sunY);
    float moonEffective=clamp((moonForward.z+.035)*-2.35,0.0,1.0)*atmosphere.cycleControls.z;
    float absent=1.0+(1.0-max(sunTwilight,moonEffective))*atmosphere.cycleControls.y;
    float adjustedSun=atmosphere.cycleSun.w*mix(min(5.0/max(atmosphere.cycleSun.w,1e-8),1.0),1.0,
        pow(clamp(1.0+sunY/.11,0.0,1.0),8.0));
    vec3 sunBase=skyScatteringColor(-sunY)*atmosphere.cycleSun.rgb*(12.0/33.0)*adjustedSun*sunTwilight;
    vec3 moonBase=skyScatteringColor(moonForward.z)*atmosphere.cycleMoon.rgb*(12.0/23.25)*
        atmosphere.cycleMoon.w*atmosphere.cycleControls.z*skyTwilight(moonForward.z);
    vec3 tint=mix(atmosphere.cycleDayTint.rgb,atmosphere.cycleDuskTint.rgb,clamp(1.0-sunY/.3,0.0,1.0));
    tint=mix(tint,vec3(.403922,.423529,.478431),night);
    cloudColor=(sunBase*tint+moonBase*atmosphere.cycleNightTint.rgb*.6)*vec3(.437516,.493875,.526042)*
        atmosphere.cycleDayTint.w*mix(1.0,atmosphere.cycleControls.x,night);
    vec3 glow=atmosphere.cycleGlow.rgb*atmosphere.cycleGlow.w*.02*night*atmosphere.cycleControls.x;
    contrast=vec3(atmosphere.materialContrast.x,max(.002,dot(sunBase+moonBase+glow,vec3(.2126,.7152,.0722))*.5),
        atmosphere.cycleControls.w*mix(.375,.45,night)*absent);
    glowNight=vec4(glow,night);
    fogBase=glow*.4*absent*atmosphere.cycleControls.w;
}
vec2 skyTilingStarsUV(vec3 uePosition,float dayPhase) {
    vec3 n=normalize(uePosition);
    vec2 xy=n.xy*vec2(-1,1);
    float len=length(xy);
    vec2 radial=len>1e-8 ? xy/len : vec2(0);
    // Material ArccosineFast uses UE's degree-one approximation, shared with solar disk.
    float angle=skySunAcos(n.z);
    vec2 uv=radial*angle*.35*atmosphere.cycleStarUv.x;
    float rotation=atmosphere.cycleStarUv.z*2.0*PI;
    uv=mat2(cos(rotation),-sin(rotation),sin(rotation),cos(rotation))*uv;
    return uv+vec2(0,dayPhase*atmosphere.cycleStarUv.y+atmosphere.cycleStarUv.w*.01);
}

// DirectionalLight stores an 8-bit sRGB FColor, then decodes it for the renderer.
vec3 skyLightColor(vec3 c) {
    vec3 encoded=mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-.055,greaterThan(c,vec3(.0031308)));
    encoded=floor(clamp(encoded,0.0,1.0)*255.0+.5)/255.0;
    return mix(encoded/12.92,pow((encoded+.055)/1.055,vec3(2.4)),greaterThan(encoded,vec3(.04045)));
}
void skyMoonLight(out vec3 direction,out vec3 energy) {
    vec3 forward=skyCycleMoon(frame.data[19]);
    direction=-forward.xzy;
    energy=skyLightColor(atmosphere.cycleMoon.rgb)*atmosphere.cycleMoon.w*
        clamp(1.0-forward.z/.195,0.0,1.0)*atmosphere.cycleControls.z*atmosphere.cycleControls.x;
}
