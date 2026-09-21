#pragma once

namespace eve::stylize::shaders {
inline constexpr const char *kYsaMesh = R"wgsl(
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
fn ramp(midpoint:f32,width:f32,value:f32)->f32 { return smoothstep(midpoint,midpoint+max(width,0.00001),value); }
fn toLinear(value:vec3f)->vec3f { let color=max(value,vec3f(0)); return select(pow((color+vec3f(0.055))/1.055,vec3f(2.4)),color/12.92,color<=vec3f(0.04045)); }
@fragment fn fs_main(i:MeshIn)->@location(0) vec4f {
    var base = textureSample(albedo, meshSamp, i.uv) * i.tint;
    if (base.a < mp(20)) { discard; }
    var N = normalize(i.normal);
    var L = normalize(frame.lightDir.xyz);
    var V = normalize(i.camera - i.world);
    var diffuse = ramp(mp(0), mp(1), dot(N, L));
    var visibility = animeShadow(i.world, N);
    var shadow = max(ramp(0.0, mp(6), visibility), 1.0 - mp(5));
    var light = diffuse * shadow;
    var ambient = vec3f(mp(2), mp(3), mp(4));
    var specularColor = vec3f(mp(10), mp(11), mp(12));
    var rimColor = vec3f(mp(17), mp(18), mp(19));
    // EVEngine albedo textures use UNORM sampling; presentation encodes sRGB.
    // Match the reference project's selected lighting space explicitly.
    if (mp(22) > 0.5) {
        base = vec4f(toLinear(base.rgb), base.a);
        ambient = toLinear(ambient);
        specularColor = toLinear(specularColor);
        rimColor = toLinear(rimColor);
    }
    var color = base.rgb * mix(ambient, frame.lightColor.rgb, light);

    var halfSum = V + L;
    var H = halfSum * inverseSqrt(max(dot(halfSum, halfSum), 0.000001));
    var specular = ramp(mp(8), mp(9), dot(N, H));
    var highlight = specular * mp(7) * specularColor;
    highlight *= frame.lightColor.rgb * light;

    var factor = mix(1.0, dot(L, N), mp(16));
    var midpoint = mix(1.0, mp(14), factor);
    var rim = ramp(midpoint, mp(15), 1.0 - dot(V, N));
    var edge = rim * mp(13) * rimColor;
    edge *= mix(1.0, light, mp(21));
    // The source graph combines these lobes with Maximum, not addition.
    color += max(highlight, edge);
    return vec4f(select(toLinear(color), color, mp(22) > 0.5), base.a);
}
)wgsl";
}
