// Existing Mesh3D shadow descriptor prefix. No additional runtime binding.
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3];vec4 splits;vec4 bias;vec4 cascadeBias;vec4 cascadeTexel;
} shadows;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
float sceneShadow(vec3 world,vec3 normal,float viewDepth) {
    if(shadows.bias.y<.5 || shadows.bias.z<.5 || shadows.splits.w<.001) return 1;
    int cascade=viewDepth<shadows.splits.x?0:(viewDepth<shadows.splits.y?1:2);
    float texelWorld=shadows.cascadeTexel[cascade];
    vec4 clip=shadows.lightVP[cascade]*vec4(world+normal*max(texelWorld,.001)*1.2,1);
    vec3 ndc=clip.xyz/clip.w;vec2 uv=ndc.xy*.5+.5;
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) || ndc.z<0 || ndc.z>1) return 1;
    float bias=shadows.cascadeBias[cascade];if(bias<1e-8) bias=shadows.bias.x;
    vec2 texel=1.0/vec2(textureSize(shadowMap,0).xy);float sum=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        sum+=texture(shadowMap,vec4(uv+vec2(x,y)*texel,float(cascade),ndc.z-bias));
    return mix(1.0,sum/9.0,shadows.splits.w);
}
