#pragma once

namespace eve::stylize::shaders {
// Same material equations and uniform indices as anime_mesh.frag.
inline constexpr const char *kAnimeMesh = R"wgsl(
struct ShadowFrame {
    lightVP:array<mat4x4f,3>,splits:vec4f,bias:vec4f,cascadeBias:vec4f,cascadeTexel:vec4f,
};
@group(0) @binding(4) var<uniform> shadowFrame:ShadowFrame;
@group(0) @binding(5) var shadowMap:texture_depth_2d_array;
@group(0) @binding(8) var shadowSampler:sampler_comparison;
fn animeCascade(world:vec3f,normal:vec3f,cascade:i32)->f32 {
    let receiver=world+normal*max(shadowFrame.cascadeTexel[cascade],0.000001)*0.75;
    let clip=shadowFrame.lightVP[cascade]*vec4f(receiver,1);
    let ndc=clip.xyz/max(clip.w,0.000001);
    // WebGPU's shadow writer flips clip Y relative to Vulkan.
    let uv=vec2f(ndc.x,-ndc.y)*0.5+vec2f(0.5);
    if(any(uv<vec2f(0)) || any(uv>vec2f(1)) || ndc.z<0 || ndc.z>1) {return 1;}
    var bias=shadowFrame.cascadeBias[cascade];
    if(bias<0.00000001) {bias=shadowFrame.bias.x;}
    let texel=vec2f(1)/vec2f(textureDimensions(shadowMap));
    var sum=0.0;
    for(var y=-1;y<=1;y++) {for(var x=-1;x<=1;x++) {
        sum+=textureSampleCompareLevel(shadowMap,shadowSampler,uv+vec2f(f32(x),f32(y))*texel*0.65,cascade,ndc.z-bias);
    }}
    return sum/9;
}
fn animeShadow(world:vec3f,normal:vec3f)->f32 {
    if(shadowFrame.bias.y<0.5 || shadowFrame.bias.z<0.5 || shadowFrame.splits.w<0.0001) {return 1;}
    let depth=abs((frame.view*vec4f(world,1)).z);
    var cascade=2;
    if(depth<shadowFrame.splits.x) {cascade=0;} else if(depth<shadowFrame.splits.y) {cascade=1;}
    var visibility=animeCascade(world,normal,cascade);
    if(cascade<2) {
        let low=select(shadowFrame.splits.x,0.0,cascade==0);
        let high=shadowFrame.splits[cascade];
        let blend=smoothstep(high-max((high-low)*0.1,0.01),high,depth);
        if(blend>0) {visibility=mix(visibility,animeCascade(world,normal,cascade+1),blend);}
    }
    return mix(1.0,visibility,clamp(shadowFrame.splits.w,0,1));
}
@fragment fn fs_main(i:MeshIn)->@location(0) vec4f {
    var base=textureSample(albedo,meshSamp,i.uv)*i.tint;
    let n=normalize(i.normal); let l=normalize(frame.lightDir.xyz);
    let v=normalize(i.camera-i.world);
    let skin=mp(4); let hair=mp(5);
    base=vec4f(mix(base.rgb,mix(vec3f(1.0,0.88,0.79),base.rgb,clamp(mp(6),0,1)),skin),base.a);
    let ndl=dot(n,l);
    let width=max(fwidth(ndl)*1.5,max(mp(1),0.001));
    var lit=smoothstep(mp(0)-width,mp(0)+width,ndl);
    lit=min(lit,mix(1.0,animeShadow(i.world,n),clamp(mp(15),0,1)));
    var shadow=mix(vec3f(mp(9),mp(10),mp(11)),vec3f(0.88,0.68,0.65),skin);
    shadow=mix(vec3f(1),shadow,clamp(mp(2),0,1));
    var color=base.rgb*mix(shadow,vec3f(1,0.985,0.975),lit);
    color*=mix(1-clamp(mp(12),0,1),1,clamp(ndl*0.5+0.5,0,1));
    let halfVector=l+v;
    let h=halfVector*inverseSqrt(max(dot(halfVector,halfVector),0.000001));
    let strand=normalize(vec3f(0,1,0)-n*n.y+vec3f(0.001,0,0));
    let th=dot(strand,h);
    let spec=pow(max(1-th*th,0),max(mp(8),1));
    let highlight=smoothstep(0.4,0.7,spec)*hair*lit*smoothstep(0.15,0.65,dot(n,h));
    color+=vec3f(1,0.52,0.45)*mp(7)*highlight;
    let surfaceSpec=pow(max(dot(n,h),0),max(mp(8),1));
    color+=mix(vec3f(1),base.rgb,0.65)*mp(14)*lit*smoothstep(0.25,0.65,surfaceSpec);
    var rim=pow(1-max(dot(n,v),0),4);
    rim*=smoothstep(-0.2,0.5,ndl)*(1-skin*0.65);
    color+=vec3f(0.63,0.79,1)*mp(3)*rim;
    color*=clamp(frame.lightColor.rgb,vec3f(0),vec3f(2));
    color=mix(color,base.rgb,clamp(mp(13),0,1));
    return vec4f(color,base.a);
}
)wgsl";
}
