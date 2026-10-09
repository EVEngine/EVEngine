#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 color;
layout(push_constant) uniform SkyFrame { float data[32]; } frame;
#define SKY_WISPS
#include "sky_atmosphere_common.glsl"
#include "sky_sun_disk.glsl"
#include "sky_daylight.glsl"
#define SKY_VIEW_PRODUCER
#include "sky_view_radiance.glsl"
void main() {
    vec3 origin=vec3(frame.data[16],frame.data[17],frame.data[18])*.001+vec3(0,atmosphere.geometry.x,0);
    float rawHeight=length(origin);
    float height=max(rawHeight,atmosphere.geometry.x+.005);
    vec3 up=rawHeight>0.0 ? origin/rawHeight : vec3(0,1,0);
    vec3 ray=skyViewBasis(up)*skyViewDirection(gl_FragCoord.xy-.5,height,atmosphere.geometry.x);
    color=vec4(skyViewQuantize(skyIntegrateRadiance(ray,true)),1);
}
