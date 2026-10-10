#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 position;
// Custom sky stream: normal.x carries the authored vertex-red morph mask.
layout(location=1) in vec3 attributes;
layout(location=2) in vec2 authoredUV;
layout(location=0) out vec3 worldRay;
layout(location=1) flat out vec3 fogAmbient;
layout(location=2) flat out vec3 fogSun;
layout(location=3) out vec2 wispsUV;
layout(location=4) out float vertexRed;
layout(location=5) out vec3 wispsColor;
layout(location=6) out vec2 celestialGradients;
layout(location=7) flat out vec3 sunDiskColor;
layout(location=8) flat out vec3 cycleContrast;
layout(location=9) flat out vec4 glowNight;
layout(location=10) out vec2 starsUV;
layout(location=11) flat out vec3 cycleFogBase;
layout(location=12) flat out vec3 lunarForward;
layout(location=13) flat out vec3 lunarXAxis;
layout(location=14) flat out vec2 lunarScaleIntensity;
layout(location=15) flat out vec3 lunarPhase;
layout(push_constant) uniform SkyFrame { float data[32]; } frame;
#define SKY_WISPS
#include "sky_atmosphere_common.glsl"
#include "sky_ambient.glsl"
#include "sky_fog_lighting.glsl"
#include "sky_wisps.glsl"
#include "sky_sun_curve.glsl"
#include "sky_sun_disk.glsl"
#include "sky_daylight.glsl"
#include "sky_moon.glsl"
void main() {
    mat4 vp = mat4(vec4(frame.data[0],frame.data[1],frame.data[2],frame.data[3]),
                   vec4(frame.data[4],frame.data[5],frame.data[6],frame.data[7]),
                   vec4(frame.data[8],frame.data[9],frame.data[10],frame.data[11]),
                   vec4(frame.data[12],frame.data[13],frame.data[14],frame.data[15]));
    vec3 eye = vec3(frame.data[16],frame.data[17],frame.data[18]);
    vec4 clip = vp * vec4(position,1);
    gl_Position = vec4(clip.xy,clip.w,clip.w);
    worldRay = position-eye;
    wispsUV = authoredUV;
    vertexRed = attributes.x;
    vec3 light = vec3(frame.data[20],frame.data[21],frame.data[22]);
    sunDiskColor = atmosphere.sunDiskColorRadius.rgb;
    if (atmosphere.sunDiskShape.z>0.5) {
        for (int channel=0;channel<3;++channel) {
            vec4 keys[4];
            for (int i=0;i<4;++i) keys[i]=atmosphere.sunDiskCurve[channel*4+i];
            sunDiskColor[channel] *= skySunCurve(keys,0.5+0.5*light.y)/keys[3].y;
        }
    }
    vec3 energy = vec3(frame.data[24],frame.data[25],frame.data[26]);
    vec3 uePosition = vec3(position.x,position.z,position.y);
    vec3 ueSunForward = vec3(-light.x,-light.z,-light.y);
    vec3 ueMoonForward = vec3(frame.data[19],frame.data[23],frame.data[27]);
    vec3 cloudBase=atmosphere.wispsBaseColor.rgb;
    vec4 gradient=atmosphere.wispsGradient;
    cycleContrast=atmosphere.materialContrast.xyz;
    glowNight=vec4(0);starsUV=vec2(0);cycleFogBase=atmosphere.fogColor.rgb;
    if (atmosphere.sunDiskShape.w>0.5) {
        ueMoonForward=skyCycleMoon(frame.data[19]);
        skyDaylightMaterial(light.y,ueMoonForward,cloudBase,cycleContrast,glowNight,cycleFogBase);
        float threshold=(-light.y-.05)*10.0;
        gradient=vec4(threshold<0.0 ? ueSunForward : ueMoonForward,
            clamp(abs(threshold),0.0,1.0)*(threshold<0.0 ? 5.0 : 3.0));
        starsUV=skyTilingStarsUV(uePosition,frame.data[19]);
    }
    lunarForward=ueMoonForward;
    lunarXAxis=vec3(0); lunarScaleIntensity=vec2(1,0); lunarPhase=vec3(0);
    if(atmosphere.moonDiskColor.w>0.5) {
        lunarXAxis=skyMoonXAxis(ueMoonForward,atmosphere.cycleMoonOrbit.xy,atmosphere.moonDiskShape.z);
        lunarScaleIntensity.x=atmosphere.moonDiskShape.x*mix(atmosphere.moonDiskShape.y,1.0,
            clamp(-ueMoonForward.z/.6,0,1));
        float day=pow(clamp(1.0+light.y/.11,0,1),9);
        lunarScaleIntensity.y=mix(atmosphere.moonDiskLighting.x,atmosphere.moonDiskLighting.y,day)*
            atmosphere.cycleMoon.w*8.33*atmosphere.cycleControls.x;
        lunarPhase=skyMoonPhase(atmosphere.moonDiskLighting.z,atmosphere.moonDiskLighting.w);
    }
    wispsColor = skyWispsVertexColor(uePosition,cloudBase,gradient);
    celestialGradients = vec2(skySunCenteredGradient(uePosition,ueSunForward),
        skyMoonCenteredGradient(uePosition,ueSunForward,ueMoonForward,atmosphere.wispsLighting.y,atmosphere.wispsLighting.z>0.5));
    fogAmbient = vec3(0);
    fogSun = vec3(0);
    if (atmosphere.fogDensity.x>0) {
        fogAmbient = distantSkyAmbient(light)*energy*atmosphere.fogGeometry.y;
        if(atmosphere.opticalInfo.x>1.0) {
            vec3 moonDirection,moonEnergy; skyMoonLight(moonDirection,moonEnergy);
            fogAmbient+=distantSkyAmbient(moonDirection)*moonEnergy*atmosphere.fogGeometry.y;
        }
        fogSun = atmosphere.fogDirectional.rgb*skyDirectionalFogFade(light.y,vec2(atmosphere.geometry.w,atmosphere.fogGeometry.w))*dot(energy,vec3(.2126,.7152,.0722)) +
            sunlight(vec3(0,atmosphere.geometry.x+.001,0),light)*energy*atmosphere.fogGeometry.y;
    }
}
