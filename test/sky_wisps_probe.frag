#version 450
#extension GL_GOOGLE_include_directive : require
#include "../src/modules/graphics/shaders/sky_wisps.glsl"
#include "../src/modules/graphics/shaders/sky_moon.glsl"
#include "../src/modules/graphics/shaders/sky_sun_curve.glsl"
#include "../src/modules/graphics/shaders/sky_fog_lighting.glsl"
#include "../src/modules/graphics/shaders/sky_material_composite.glsl"

const float PI=3.141592653589793;
struct ProbeDaylight {
vec4 cycleSun;
vec4 cycleMoon;
vec4 cycleMoonOrbit;
vec4 cycleDayTint;
vec4 cycleDuskTint;
vec4 cycleNightTint;
vec4 cycleGlow;
vec4 cycleControls;
vec4 cycleStars;
vec4 cycleStarUv;
vec4 cycleTwinkle;
vec4 materialContrast;vec4 cycleScattering[21];
};
ProbeDaylight atmosphere;
#include "../src/modules/graphics/shaders/sky_sun_disk.glsl"
struct ProbeFrame { float data[32]; }; ProbeFrame frame;
#include "../src/modules/graphics/shaders/sky_daylight.glsl"
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(binding=0) uniform sampler2D source;
layout(location=0) out vec4 result;
void main() {
    atmosphere.cycleSun=vec4(1.0,1.0,1.0,5.0);
    atmosphere.cycleMoon=vec4(0.445801,0.557475,0.864583,0.15);
    atmosphere.cycleMoonOrbit=vec4(35.0,0.0,0.04,0.0);
    atmosphere.cycleDayTint=vec4(0.476966,0.473985,0.526042,1.0);
    atmosphere.cycleDuskTint=vec4(1.0,0.587119,0.395833,1.0);
    atmosphere.cycleNightTint=vec4(0.367187,0.428386,0.489583,1.0);
    atmosphere.cycleGlow=vec4(0.093045,0.141694,0.255208,0.6);
    atmosphere.cycleControls=vec4(1.0,2.0,1.0,1.0);
    atmosphere.cycleStars=vec4(0.484375,0.639062,1.0,0.75);
    atmosphere.cycleStarUv=vec4(2.5,5.0,0.0,0.0);
    atmosphere.cycleTwinkle=vec4(0.7,1.5,2.0,0.0);
    atmosphere.materialContrast=vec4(.1,1,1,1);
    atmosphere.cycleScattering[0]=vec4(81.0,0.0,0.0,0.0);
    atmosphere.cycleScattering[1]=vec4(82.5,0.001196,0.000683,0.000366);
    atmosphere.cycleScattering[2]=vec4(84.0,0.002388,0.001363,0.001097);
    atmosphere.cycleScattering[3]=vec4(86.25,0.005973,0.00409,0.003653);
    atmosphere.cycleScattering[4]=vec4(87.75,0.011942,0.006817,0.006238);
    atmosphere.cycleScattering[5]=vec4(88.5,0.017277,0.009863,0.00842);
    atmosphere.cycleScattering[6]=vec4(89.25,0.029506,0.015712,0.012434);
    atmosphere.cycleScattering[7]=vec4(90.0,0.051084,0.027597,0.020231);
    atmosphere.cycleScattering[8]=vec4(90.75,0.083489,0.045573,0.030318);
    atmosphere.cycleScattering[9]=vec4(92.25,0.166479,0.098237,0.061713);
    atmosphere.cycleScattering[10]=vec4(93.75,0.240982,0.162336,0.10412);
    atmosphere.cycleScattering[11]=vec4(97.5,0.411628,0.323737,0.238416);
    atmosphere.cycleScattering[12]=vec4(101.25,0.51204,0.437146,0.351205);
    atmosphere.cycleScattering[13]=vec4(105.0,0.567094,0.509806,0.429);
    atmosphere.cycleScattering[14]=vec4(112.5,0.66208,0.626445,0.563073);
    atmosphere.cycleScattering[15]=vec4(120.0,0.739011,0.705275,0.664457);
    atmosphere.cycleScattering[16]=vec4(127.5,0.793053,0.778758,0.734856);
    atmosphere.cycleScattering[17]=vec4(135.0,0.849338,0.845151,0.817966);
    atmosphere.cycleScattering[18]=vec4(150.0,0.938013,0.938643,0.933859);
    atmosphere.cycleScattering[19]=vec4(165.0,0.984285,0.987547,1.009665);
    atmosphere.cycleScattering[20]=vec4(180.0,1.0,1.0,1.0);
    if (fragColor.r>=12.0) {
        if(fragColor.r>=14.0) {
            result=vec4(skyMoonXAxis(skyCycleMoon(fragColor.g/24.0),vec2(35,0),0),1);
        } else {
            vec3 ray=normalize(vec3(fragColor.b,0,fragColor.r<13.0 ? 1 : -1));
            result=skyMoonSurface(source,source,ray,vec3(0,0,-1),vec3(1,0,0),.033161256,
                skyMoonPhase(fragColor.g,1.6),.005,vec2(0,.2));
        }
        return;
    }
    if (fragColor.r>=7.0) {
        vec3 moon=skyCycleMoon(fragColor.g/24.0),cloud,contrast,fog;vec4 glow;
        float sunY=.866025403784*cos((fragColor.g-12.0)*PI/12.0);
        skyDaylightMaterial(sunY,moon,cloud,contrast,glow,fog);
        if (fragColor.r<7.5) result=vec4(cloud,1);
        else if (fragColor.r<8.5) result=vec4(contrast.yz,glow.w,1);
        else if (fragColor.r<9.5) result=vec4(moon,1);
        else if (fragColor.r<10.5) result=vec4(glow.rgb,1);
        else result=vec4(fog,1);
        return;
    }

    if (fragColor.r < 0.5) {
        float alpha = skyWispsOpacity(source, vec2(0.25,0.5), 1.0, vec2(-1,0),
                                     fragColor.g, 1.0, 10.0, 0.4, 0.2);
        result = vec4(alpha,alpha,alpha,1);
    } else if (fragColor.r < 1.5) {
        vec3 sun = vec3(0,0,-1), moon = -sun;
        result = vec4(skySunCenteredGradient(vec3(0,0,1),sun),
                      skyMoonCenteredGradient(vec3(0,0,1),sun,moon,1.0,false),
                      skyWispsLitScale(vec2(1,0),0.8),1);
    } else if (fragColor.r < 2.5) {
        vec3 sun = vec3(0,0,1), moon = -sun;
        result = vec4(skyMoonCenteredGradient(vec3(0,0,1),sun,moon,1.0,false),
                      skyMoonCenteredGradient(vec3(0,0,1),sun,moon,0.0,false),
                      skySunCenteredGradient(vec3(0,0,1),sun),1);
    } else if (fragColor.r < 3.5) {
        result = vec4(skyWispsVertexColor(vec3(0,0,1),vec3(0.5),vec4(0,0,-1,5)),1);
    } else if (fragColor.r < 4.5) {
        vec3 composed = mix(vec3(.2,1,4),vec3(2,.2,0),.5);
        result = vec4(skyMaterialContrast(composed,vec3(.1,.5,.375)),1);
    }
    else if (fragColor.r < 5.5) {
        // Asymmetric slopes distinguish Hermite interpolation from a linear color ramp.
        vec4 keys[4]=vec4[4](vec4(0,0,0,0),vec4(.25,.5,0,2),vec4(.75,.75,-1,0),vec4(1,1,0,0));
        float v=skySunCurve(keys,fragColor.g);
        result=vec4(v,v,v,1);
    }
    else {
        result=vec4(skyDirectionalFogFade(fragColor.g,vec2(0,.2)),skyDirectionalFogFade(fragColor.g,vec2(0)),0,1);
    }
}
