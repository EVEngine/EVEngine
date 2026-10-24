#version 450
layout(location=0) in vec2 uv;
layout(location=1) flat in vec2 rootUV;
layout(location=2) flat in float variant;
layout(location=3) flat in float accent;
layout(location=4) flat in float phase;
layout(location=5) flat in int swayGroup;
layout(set=0,binding=18) uniform sampler2D groundColor;
layout(set=1,binding=0) uniform sampler2D swayAtlas;
layout(push_constant) uniform Controls { float data[32]; } controls;
layout(location=0) out vec4 outColor;
float silhouette(int frameIndex) {
    // Half-texel inset prevents neighboring atlas frames leaking into a card.
    vec2 size=vec2(controls.data[14],controls.data[15]);
    vec2 tile=size/vec2(4.0,2.0);
    vec2 local=vec2(uv.x,1.0-uv.y)*(tile-1.0)+0.5;
    return texture(swayAtlas,(vec2(float(frameIndex),variant)*tile+local)/size).a;
}
void main() {
    float frameSeconds=controls.data[1]*controls.data[16+swayGroup];
    float clock=controls.data[0]/max(frameSeconds,0.01)+phase*4.0;
    int current=int(floor(clock))%4;
    float mask=mix(silhouette(current),silhouette((current+1)%4),smoothstep(0.0,1.0,fract(clock)));
    if(mask<0.5) discard;
    float luminance=dot(texture(groundColor,clamp(rootUV,vec2(0.0),vec2(1.0))).rgb,vec3(0.2126,0.7152,0.0722));
    int band=luminance<controls.data[2]?0:(luminance<controls.data[3]?1:2);
    // Rare, separately Poisson-spaced roots: dark -> green, light -> green,
    // green -> dark. This is a stable spatial decision, never per-frame RNG.
    if(accent>0.5 && controls.data[13]>0.5) band=band==1?0:1;
    vec3 dark=vec3(controls.data[4],controls.data[5],controls.data[6]);
    vec3 green=vec3(controls.data[7],controls.data[8],controls.data[9]);
    vec3 young=vec3(controls.data[10],controls.data[11],controls.data[12]);
    // RGB comes exclusively from the selected palette entry, never the mask.
    outColor=vec4(band==0?dark:(band==1?green:young),1.0);
}
