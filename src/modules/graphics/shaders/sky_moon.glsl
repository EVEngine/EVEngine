// Authored manual-orbit moon projection and phase shading, in UE XYZ coordinates.
vec3 skyMoonXAxis(vec3 forward, vec2 pitchYaw, float rotationDegrees) {
    vec2 angle=radians(pitchYaw);
    vec3 alignment=vec3(cos(angle.x)*cos(angle.y),cos(angle.x)*sin(angle.y),sin(angle.x));
    vec3 axis=cross(forward,alignment);
    float squared=dot(axis,axis);
    axis=squared>=.0001 ? axis*inversesqrt(squared) : vec3(0);
    float rotation=radians(rotationDegrees);
    return axis*cos(rotation)+cross(forward,axis)*sin(rotation)+forward*dot(forward,axis)*(1-cos(rotation));
}
vec3 skyMoonPhase(float days,float contrast) {
    float angle=clamp(days/29.53,0.0,1.0)*6.283185307179586;
    return vec3(sin(angle),-cos(angle),0)*contrast;
}
// RGB is unscaled emission, A occludes the space layer only (not atmospheric sky).
vec4 skyMoonSurface(sampler2D surface,sampler2D phaseNormal,vec3 ray,vec3 forward,
                    vec3 axis,float diameter,vec3 phase,float earthlight,vec2 glow) {
    vec2 projected=vec2(dot(axis,ray),dot(cross(axis,forward),ray));
    vec2 centered=projected/vec2(diameter,-diameter);
    vec2 uv=clamp(centered+.5,0,1);
    vec4 texel=texture(surface,uv);
    vec3 normal=texture(phaseNormal,uv).rbg*2-1;
    float visibility=clamp(dot(forward,-ray)*1000,0,1);
    float mask=texel.a*clamp((1-2*length(centered))*70,0,1)*visibility;
    float lit=clamp(clamp(dot(normal,phase),0,1)+earthlight,0,1);
    float halo=pow(1-clamp(length(projected)/glow.y,0,1),2)*glow.x*visibility;
    return vec4(texel.rgb*lit*mask+halo,mask);
}
