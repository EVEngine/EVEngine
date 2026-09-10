// Uses the existing Mesh3D shadow descriptors; owns no additional GPU resources.
struct AnimeLight { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform AnimeFrame {
    mat4 mvp; mat4 model;
    vec4 lightDir; vec4 lightColor; vec4 tint; vec4 camera; vec4 ambient;
    AnimeLight lights[8]; vec4 texBomb; vec4 parallax; mat4 view;
} frame;
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadowFrame;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;

float animeCascade(vec3 world,vec3 normal,int cascade) {
    vec3 receiver=world+normal*max(shadowFrame.cascadeTexel[cascade],0.000001)*0.75;
    vec4 clip=shadowFrame.lightVP[cascade]*vec4(receiver,1.0);
    vec3 ndc=clip.xyz/max(clip.w,0.000001);
    vec2 uv=ndc.xy*0.5+0.5;
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) || ndc.z<0 || ndc.z>1)
        return 1.0;
    float bias=shadowFrame.cascadeBias[cascade];
    if(bias<0.00000001)bias=shadowFrame.bias.x;
    vec2 texel=1.0/vec2(textureSize(shadowMap,0).xy);
    float sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        sum+=texture(shadowMap,vec4(uv+vec2(x,y)*texel*0.65,float(cascade),ndc.z-bias));
    return sum/9.0;
}
float animeShadow(vec3 world,vec3 normal) {
    if(shadowFrame.bias.y<0.5 || shadowFrame.bias.z<0.5 || shadowFrame.splits.w<0.0001)
        return 1.0;
    float depth=abs((frame.view*vec4(world,1.0)).z);
    int cascade=depth<shadowFrame.splits.x?0:(depth<shadowFrame.splits.y?1:2);
    float visibility=animeCascade(world,normal,cascade);
    if(cascade<2) {
        float low=cascade==0?0.0:shadowFrame.splits.x;
        float high=shadowFrame.splits[cascade];
        float blend=smoothstep(high-max((high-low)*0.1,0.01),high,depth);
        if(blend>0.0)visibility=mix(visibility,animeCascade(world,normal,cascade+1),blend);
    }
    return mix(1.0,visibility,clamp(shadowFrame.splits.w,0.0,1.0));
}
