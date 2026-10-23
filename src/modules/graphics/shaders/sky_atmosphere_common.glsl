layout(std140,set=1,binding=32) uniform Atmosphere {
    vec4 rayleigh;
    vec4 mie;
    vec4 mieAbsorption;
    vec4 ozone;
    vec4 geometry;
    vec4 fogDensity;
    vec4 fogColor;
    vec4 fogDirectional;
    vec4 fogGeometry;
    vec4 opticalInfo, opticalDay, opticalDusk, opticalNight, absorptionDay, absorptionNight;
#ifdef SKY_WISPS
    vec4 wispsBaseColor;
    vec4 wispsGradient;
    vec4 wispsMotion;
    vec4 wispsLighting;
    vec4 materialContrast;
    vec4 sunDiskColorRadius;
    vec4 sunDiskShape;
    vec4 sunDiskCurve[12];
    vec4 cycleSun, cycleMoon, cycleMoonOrbit;
    vec4 cycleDayTint, cycleDuskTint, cycleNightTint, cycleGlow;
    vec4 cycleControls, cycleStars, cycleStarUv, cycleTwinkle;
    vec4 cycleScattering[21];
    vec4 moonDiskColor, moonDiskShape, moonDiskLighting, moonDiskGlow;
#endif
} atmosphere;
layout(set=1,binding=0) uniform sampler3D transmittanceTable;
layout(set=1,binding=1) uniform sampler3D multiScatteringTable;
const float PI = 3.14159265358979323846;

vec2 sphere(vec3 origin, vec3 direction, float radius) {
    float b = dot(origin,direction);
    float discriminant = b*b - dot(origin,origin) + radius*radius;
    if (discriminant < 0) return vec2(-1);
    float root = sqrt(discriminant);
    return vec2(-b-root,-b+root);
}
float opticalSlice() {
    float count=atmosphere.opticalInfo.x;
    float fraction=clamp((frame.data[21]-atmosphere.opticalInfo.y)/(atmosphere.opticalInfo.z-atmosphere.opticalInfo.y),0.0,1.0);
    return (.5+fraction*(count-1.0))/count;
}
void opticalCoefficients(out vec3 ray, out vec3 absorption) {
    ray=atmosphere.rayleigh.rgb; absorption=atmosphere.ozone.rgb;
    if(atmosphere.opticalInfo.x<=1.0) return;
    float y=frame.data[21];
    float day=clamp((y+.03604)/(.03604+.06319),0.0,1.0),night=(1.0-day)*(1.0-day);
    ray=mix(mix(atmosphere.opticalDay.rgb,atmosphere.opticalDusk.rgb,clamp(1.0-y/.3,0.0,1.0)),atmosphere.opticalNight.rgb,night)*atmosphere.opticalDay.w;
    vec4 c=mix(atmosphere.absorptionDay,atmosphere.absorptionNight,clamp((.025-y)/.05,0.0,1.0));
    absorption=(max(max(c.r,c.g),c.b)+min(min(c.r,c.g),c.b)-c.rgb)*c.w;
}
void medium(vec3 point, out vec3 extinction, out vec3 rayleigh, out vec3 mie) {
    float altitude = max(0,length(point)-atmosphere.geometry.x);
    vec3 ray,absorption; opticalCoefficients(ray,absorption);
    rayleigh = ray * exp(-altitude/atmosphere.rayleigh.w);
    float mieDensity = exp(-altitude/atmosphere.mie.w);
    mie = atmosphere.mie.rgb * mieDensity;
    float ozoneDensity = max(0,1-abs(altitude-atmosphere.ozone.w)/atmosphere.geometry.z);
    extinction = rayleigh + mie + atmosphere.mieAbsorption.rgb * mieDensity +
                 absorption * ozoneDensity;
}
vec2 transmittanceUv(float height, float cosine) {
    float bottom=atmosphere.geometry.x, top=atmosphere.geometry.y;
    float h=sqrt(max(0,top*top-bottom*bottom));
    float rho=sqrt(max(0,height*height-bottom*bottom));
    float distance=max(0,-height*cosine+sqrt(max(0,height*height*(cosine*cosine-1)+top*top)));
    float minimum=top-height, maximum=rho+h;
    return clamp(vec2((distance-minimum)/(maximum-minimum),rho/h),0,1);
}
vec3 sunlight(vec3 point, vec3 direction) {
    if (sphere(point,direction,atmosphere.geometry.x).x > 0) return vec3(0);
    float height=length(point);
    return textureLod(transmittanceTable,vec3(transmittanceUv(height,dot(point,direction)/height),opticalSlice()),0).rgb;
}
vec3 multipleScattering(vec3 point, vec3 light) {
    float height=length(point);
    vec2 uv=vec2(dot(point,light)/height*.5+.5,
                 (height-atmosphere.geometry.x)/(atmosphere.geometry.y-atmosphere.geometry.x));
    return textureLod(multiScatteringTable,vec3(clamp(uv,0,1),opticalSlice()),0).rgb;
}
