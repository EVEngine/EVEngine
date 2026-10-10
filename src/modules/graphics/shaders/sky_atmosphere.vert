#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 position;
layout(location=0) out vec3 worldRay;
layout(location=1) flat out vec3 fogAmbient;
layout(location=2) flat out vec3 fogSun;
layout(push_constant) uniform SkyFrame { float data[32]; } frame;
#include "sky_atmosphere_common.glsl"
#include "sky_ambient.glsl"
#include "sky_fog_lighting.glsl"
void main() {
    mat4 inverseVP = mat4(vec4(frame.data[0],frame.data[1],frame.data[2],frame.data[3]),
                          vec4(frame.data[4],frame.data[5],frame.data[6],frame.data[7]),
                          vec4(frame.data[8],frame.data[9],frame.data[10],frame.data[11]),
                          vec4(frame.data[12],frame.data[13],frame.data[14],frame.data[15]));
    vec4 farPoint = inverseVP * vec4(position.xy,1,1);
    worldRay = farPoint.xyz - vec3(frame.data[16],frame.data[17],frame.data[18]) * farPoint.w;
    gl_Position = vec4(position.xy,1,1);
    fogAmbient = vec3(0);
    fogSun = vec3(0);
    if (atmosphere.fogDensity.x > 0) {
        vec3 light = vec3(frame.data[20],frame.data[21],frame.data[22]);
        vec3 energy = vec3(frame.data[24],frame.data[25],frame.data[26]);
        fogAmbient = distantSkyAmbient(light) * energy * atmosphere.fogGeometry.y;
        fogSun = atmosphere.fogDirectional.rgb * skyDirectionalFogFade(light.y,vec2(atmosphere.geometry.w,atmosphere.fogGeometry.w)) * dot(energy,vec3(.2126,.7152,.0722)) +
                 sunlight(vec3(0,atmosphere.geometry.x + .001,0),light) * energy * atmosphere.fogGeometry.y;
    }
}
