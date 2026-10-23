#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldRay;
layout(location=0) out vec4 color;
layout(location=1) flat in vec3 fogAmbient;
layout(location=2) flat in vec3 fogSun;
layout(push_constant) uniform SkyFrame { float data[32]; } frame;
#include "sky_atmosphere_common.glsl"
#include "sky_height_fog.glsl"
#include "sky_view_radiance.glsl"
void main() {
    vec3 direction = normalize(worldRay);
    color = vec4(applyHeightFog(skyViewRadiance(direction),direction),1);
}
