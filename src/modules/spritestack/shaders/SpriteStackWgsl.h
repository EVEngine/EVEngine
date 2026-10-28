#pragma once
namespace eve::spritestack::shaders {
inline constexpr const char *kVert = R"wgsl(
struct Light{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct Params{data:array<vec4f,8>};struct In{@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};struct Out{@builtin(position) pos:vec4f,@location(1) uv:vec2f,@location(2) tint:vec4f};@group(0) @binding(0)var<uniform> frame:Frame;@group(0) @binding(15)var<uniform> params:Params;@vertex fn vs_main(i:In)->Out{var o:Out;o.pos=frame.mvp*vec4f(i.pos,1);o.pos.y=-o.pos.y;let r=params.data[0];o.uv=r.xy+i.uv*(r.zw-r.xy);o.tint=frame.tint;return o;}
)wgsl";
inline constexpr const char *kFrag = R"wgsl(
struct Params{data:array<vec4f,8>};struct In{@location(1) uv:vec2f,@location(2) tint:vec4f};@group(0) @binding(1)var tex:texture_2d<f32>;@group(0) @binding(7)var samp:sampler;@group(0) @binding(15)var<uniform> params:Params;@fragment fn fs_main(i:In)->@location(0) vec4f{let c=textureSample(tex,samp,i.uv)*i.tint;if(c.a<params.data[1].x){discard;}return c;}
)wgsl";
}  // namespace eve::spritestack::shaders
