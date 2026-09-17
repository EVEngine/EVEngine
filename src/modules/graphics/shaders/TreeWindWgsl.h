#pragma once
namespace eve::graphics::shaders {
/** @brief Immutable WGSL source. @ownership Program image. @lifetime Entire process. */
inline constexpr const char* kTreeWindVertWgsl=R"wgsl(
struct Light3D { posRadius:vec4f,color:vec4f };
struct Frame {
 mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,
 lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,
 cloud:vec4f,cloudWind:vec4f,virtualTexture:vec4f,virtualAtlas:vec4f,envProbeCenter:vec4f,
 envProbeExtent:vec4f,skinInfo:vec4f,reflectionProbeCenter:array<vec4f,2>,reflectionProbeExtent:array<vec4f,2>,
};
struct Externals { data:array<vec4f,4> };
struct VSIn { @location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f };
struct VSOut { @builtin(position) pos:vec4f,@location(0) normal:vec3f,@location(1) uv:vec2f,
 @location(2) tint:vec4f,@location(3) worldPos:vec3f,@location(4) cameraPos:vec3f,@location(5) viewPos:vec3f };
@group(0) @binding(0) var<uniform> frame:Frame;
@group(0) @binding(15) var<uniform> params:Externals;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
fn inverse3(m:mat3x3f)->mat3x3f{
 let a=m[0].x;let b=m[1].x;let c=m[2].x;let d=m[0].y;let e=m[1].y;let f=m[2].y;
 let g=m[0].z;let h=m[1].z;let i=m[2].z;let q=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
 return mat3x3f(vec3f(e*i-f*h,f*g-d*i,d*h-e*g)/q,vec3f(c*h-b*i,a*i-c*g,b*g-a*h)/q,
 vec3f(b*f-c*e,c*d-a*f,a*e-b*d)/q);
}
fn deform(local:vec3f,root:vec3f)->vec3f{
 let range=abs(p(5u));let main=p(3u);if(range==0.0||main==0.0){return local;}
 let distance=(root-frame.cameraPos.xyz)/range;var attenuation=1.0-clamp(dot(distance,distance),0.0,1.0);
 attenuation*=attenuation;let dimensions=vec2f(p(14u),p(15u));
 let flex=vec3f(p(6u),p(7u),p(8u))*vec3f(clamp(main*3.0,0.0,1.0),clamp(main*2.0,0.0,1.0),1.0-main*main*0.5)*main*attenuation;
 let frequency=vec3f(p(9u),p(10u),p(11u));let direction=vec3f(p(0u),p(1u),p(2u));
 let time=-fract(p(4u)*6.0)*6.283185;let norm=local/vec3f(dimensions.x,dimensions.y,dimensions.x);
 let branch=dot(norm.xz,norm.xz);let stem=clamp(norm.y,0.0,1.0);let lengthA=dot(local,local);let world=root+local;
 var gust=((sin(time+frequency.x*(root.x+root.y+root.z))*0.3+main*0.5)+(p(12u)*0.4+main)*main)*(p(13u)*0.3+0.7);
 var tally=vec3f(direction.x,0.0,direction.z)*stem*stem*gust*flex.x;gust=gust*0.7+0.3;
 if(p(5u)>0.0){let displaced=world+tally*0.25;tally+=direction*stem*stem*(sin(time*2.0+dot(displaced,vec3f(frequency.y)))*branch*0.7+0.3)*gust*flex.y;}
 let offset=local+tally;let denominator=dot(offset,offset);var normalization=0.0;
 if(denominator!=0.0){normalization=clamp(lengthA/denominator,0.0,1.0);} tally=offset*normalization;
 if(p(5u)>0.0&&flex.z!=0.0){let wave=sin(vec3f(time*5.0)+(world+tally)*frequency.z);
 tally+=(wave*direction+direction)*vec3f(branch,branch*0.75,branch)*(stem*gust*flex.z)*(normalization+0.5);}
 return tally;
}
@vertex fn vs_main(input:VSIn)->VSOut{
 let model3=mat3x3f(frame.model[0].xyz,frame.model[1].xyz,frame.model[2].xyz);let inv=inverse3(model3);
 let objectPos=inv*deform(model3*input.pos,frame.model[3].xyz);let world=frame.model*vec4f(objectPos,1.0);
 var out:VSOut;out.pos=frame.mvp*vec4f(objectPos,1.0);out.pos.y=-out.pos.y;out.worldPos=world.xyz;
 out.viewPos=(frame.view*world).xyz;out.normal=normalize(transpose(inv)*input.normal);out.uv=input.uv;
 out.tint=frame.tint;out.cameraPos=frame.cameraPos.xyz;return out;
}
)wgsl";
}  // namespace eve::graphics::shaders
