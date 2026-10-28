#version 450
layout(location=0) in vec4 screenClip;
layout(set=0,binding=18) uniform sampler2D groundColor;
layout(push_constant) uniform Controls { float data[32]; } controls;
layout(location=0) out vec4 outColor;
void main() {
    vec2 uv=(screenClip.xy/screenClip.w)*0.5+0.5;
    float luminance=dot(texture(groundColor,clamp(uv,vec2(0.0),vec2(1.0))).rgb,vec3(0.2126,0.7152,0.0722));
    int band=luminance<controls.data[2]?0:(luminance<controls.data[3]?1:2);
    vec3 dark=vec3(controls.data[4],controls.data[5],controls.data[6]);
    vec3 green=vec3(controls.data[7],controls.data[8],controls.data[9]);
    vec3 young=vec3(controls.data[10],controls.data[11],controls.data[12]);
    outColor=vec4(band==0?dark:(band==1?green:young),1.0);
}
