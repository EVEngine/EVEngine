#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldRay;
layout(location=1) flat in vec3 fogAmbient;
layout(location=2) flat in vec3 fogSun;
layout(location=3) in vec2 wispsUV;
layout(location=4) in float vertexRed;
layout(location=5) in vec3 wispsColor;
layout(location=6) in vec2 celestialGradients;
layout(location=7) flat in vec3 sunDiskColor;
layout(location=8) flat in vec3 cycleContrast;
layout(location=9) flat in vec4 glowNight;
layout(location=10) in vec2 starsUV;
layout(location=11) flat in vec3 cycleFogBase;
layout(location=12) flat in vec3 lunarForward;
layout(location=13) flat in vec3 lunarXAxis;
layout(location=14) flat in vec2 lunarScaleIntensity;
layout(location=15) flat in vec3 lunarPhase;
layout(location=0) out vec4 color;
layout(push_constant) uniform SkyFrame { float data[32]; } frame;
#define SKY_WISPS
#include "sky_atmosphere_common.glsl"
#include "sky_height_fog.glsl"
#include "sky_wisps.glsl"
#include "sky_view_radiance.glsl"
#include "sky_material_composite.glsl"
#include "sky_sun_disk.glsl"
#include "sky_moon.glsl"
layout(set=1,binding=2) uniform sampler2D wispsTexture;
layout(set=1,binding=3) uniform sampler2D starsTexture;
layout(set=1,binding=4) uniform sampler2D starsNoiseTexture;
layout(set=1,binding=5) uniform sampler2D moonColorTexture;
layout(set=1,binding=6) uniform sampler2D moonNormalTexture;
void main() {
    float alpha = skyWispsOpacityAtPhase(wispsTexture,wispsUV,vertexRed,atmosphere.wispsMotion.xy,
        abs(frame.data[31]),atmosphere.wispsBaseColor.w,atmosphere.wispsLighting.w);
    vec3 radiance = wispsColor*skyWispsLitScale(celestialGradients,atmosphere.wispsLighting.x);
    vec3 direction = normalize(worldRay);
    float skyScale = mix(1.0,skyWispsLitScale(celestialGradients,atmosphere.wispsLighting.x),
                         atmosphere.materialContrast.w);
    vec3 light = vec3(frame.data[20],frame.data[21],frame.data[22]);
    float disk = skySunDiskMask(dot(direction,light),atmosphere.sunDiskColorRadius.w,atmosphere.sunDiskShape.x);
    bool reflection = (floatBitsToUint(frame.data[31]) & 0x80000000u) != 0u;
    vec3 solar = sunDiskColor*disk*(reflection ? atmosphere.sunDiskShape.y : 1.0);
    vec3 stars=vec3(0),glow=vec3(0);
    if (atmosphere.sunDiskShape.w>0.5) {
        float noise=textureLod(starsNoiseTexture,starsUV*2.0,0.0).r;
        stars=texture(starsTexture,starsUV,-.5).rgb*(atmosphere.cycleTwinkle.x+noise*atmosphere.cycleTwinkle.y)*
            atmosphere.cycleStars.rgb*atmosphere.cycleStars.w*.62*glowNight.w*atmosphere.cycleControls.x;
        glow=glowNight.rgb*mix(1.3,.6,vertexRed)*(reflection ? atmosphere.cycleTwinkle.z : 1.0);
    }
    vec4 lunar=vec4(0);
    if(atmosphere.moonDiskColor.w>0.5) {
        lunar=skyMoonSurface(moonColorTexture,moonNormalTexture,direction.xzy,lunarForward,
            lunarXAxis,lunarScaleIntensity.x,lunarPhase,atmosphere.moonDiskShape.w,atmosphere.moonDiskGlow.xy);
        lunar.rgb*=atmosphere.moonDiskColor.rgb*lunarScaleIntensity.y;
    }
    vec3 composed = mix(skyViewRadiance(direction)*skyScale+(solar+stars)*(1-lunar.a)+lunar.rgb,radiance,alpha)+glow;
    composed = skyMaterialContrast(composed,cycleContrast);
    color = vec4(applyHeightFogColor(composed,direction,cycleFogBase),1);
}
