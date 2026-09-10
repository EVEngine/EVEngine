#pragma once

namespace eve::weather::shaders {

inline constexpr const char *kWeatherVertWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame { mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f };
struct Params { data: array<vec4f,8> }; struct In{@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};
struct Out{@builtin(position) pos:vec4f,@location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f,@location(6) particle:f32};
@group(0) @binding(0) var<uniform> frame:Frame; @group(0) @binding(15) var<uniform> params:Params;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
fn hash12(q:vec2f)->f32{var v=fract(vec3f(q.x,q.y,q.x)*0.1031);v+=dot(v,v.yzx+vec3f(33.33));return fract((v.x+v.y)*v.z);}
@vertex fn vs_main(i:In)->Out {
    let kind=p(12u);
    let phase=hash12(i.pos.xz*0.5+vec2f(floor(i.pos.y*0.7),13.0));
    let speed=max(p(3u)*i.normal.z,0.01);
    let velocity=vec3f(p(1u),-speed,p(2u));
    var base=i.pos+vec3f(p(13u),-speed*p(0u),p(14u));
    if(kind>1.5) { base=i.pos; }
    let relative=base-frame.cameraPos.xyz+vec3f(26.0,10.0,26.0);
    base=relative-floor(relative/vec3f(52.0,20.0,52.0))*vec3f(52.0,20.0,52.0)-vec3f(26.0,10.0,26.0)+frame.cameraPos.xyz;
    if(kind>0.5 && kind<1.5) {
        let flutter=p(0u)*1.7+phase*6.283185;
        base.x+=sin(flutter)*0.32;
        base.z+=cos(flutter*0.73)*0.24;
    }
    let cameraRight=vec3f(frame.view[0][0],frame.view[1][0],frame.view[2][0]);
    let cameraUp=vec3f(frame.view[0][1],frame.view[1][1],frame.view[2][1]);
    var up=-normalize(velocity);
    let sight=frame.cameraPos.xyz-base;
    let side=cross(up,sight);
    var right=cameraRight;
    if(dot(side,side)>0.0001) { right=normalize(side); }
    var widthScale=i.normal.y;
    if(kind>0.5 && kind<1.5) {
        let angle=phase*6.283185+p(0u)*0.35;
        right=cameraRight*cos(angle)+cameraUp*sin(angle);
        up=-cameraRight*sin(angle)+cameraUp*cos(angle);
        widthScale=i.normal.x;
    }
    let position=base+right*i.uv.x*p(5u)*widthScale+up*(i.uv.y-0.5)*p(4u)*i.normal.x;
    let world=frame.model*vec4f(position,1);
    var o:Out;
    o.pos=frame.mvp*vec4f(position,1); o.pos.y=-o.pos.y;
    o.normal=i.normal; o.uv=vec2f(i.uv.x+0.5,i.uv.y); o.tint=frame.tint;
    let distanceFade=smoothstep(1.0,3.0,length(sight));
    let edgeFade=1.0-smoothstep(20.0,26.0,max(abs(base.x-frame.cameraPos.x),abs(base.z-frame.cameraPos.z)));
    o.tint.a*=distanceFade*edgeFade;
    o.world=world.xyz; o.camera=frame.cameraPos.xyz;
    o.view=(frame.view*world).xyz; o.particle=phase; return o;
}
)wgsl";

inline constexpr const char *kWeatherFragWgsl = R"wgsl(
struct Params{data:array<vec4f,8>};struct In{@builtin(position) pos:vec4f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f,@location(6) particle:f32};
@group(0) @binding(1) var albedo:texture_2d<f32>;@group(0) @binding(7) var samp:sampler;@group(0) @binding(15) var<uniform> params:Params;fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
@fragment fn fs_main(i:In)->@location(0) vec4f {
    var tex=textureSample(albedo,samp,i.uv)*i.tint;
    // Evaluate derivatives before particle discard for uniform control flow.
    let t=i.uv.y;
    let center=0.5+(0.045+i.particle*0.035)*sin(t*3.141593);
    let taper=smoothstep(0.0,0.08,t)*(1.0-smoothstep(0.92,1.0,t));
    let head=fract(p(0u)*0.22+i.particle*7.13)*1.5-0.25;
    let along=1.0-t;
    let light=smoothstep(head-0.32,head-0.12,along)*(1.0-smoothstep(head-0.035,head+0.025,along));
    let halfWidth=0.006;
    let aa=max(fwidth(i.uv.x-center),0.001);
    if(p(12u)>1.5) {
        let ribbon=1.0-smoothstep(max(halfWidth-aa,0.0),halfWidth+aa,abs(i.uv.x-center));
        tex=vec4f(vec3f(0.94,0.98,1.0)*i.tint.rgb,ribbon*taper*light*i.tint.a*0.16);
    }
    if(p(6u)<=0.0 || i.particle>=p(6u)){discard;}
    let coverage=sqrt(max(tex.a,0.0))*select(0.95,0.88,p(12u)>1.5);
    let threshold=fract(52.9829189*fract(dot(floor(i.pos.xy),vec2f(0.06711056,0.00583715))));
    if(coverage<=threshold){discard;}
    let fog=clamp(1.0-exp(-length(i.view)*p(10u)),0.0,1.0);
    let glint=0.72+0.28*pow(max(0.0,1.0-abs(i.uv.x-0.5)*2.0),2.0);
    return vec4f(mix(tex.rgb*glint,vec3f(p(7u),p(8u),p(9u)),fog),1);
}
)wgsl";

inline constexpr const char *kBoltVertWgsl = R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct In{@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};struct Out{@builtin(position) pos:vec4f,@location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f};@group(0) @binding(0) var<uniform> frame:Frame;@vertex fn vs_main(i:In)->Out{let w=frame.model*vec4f(i.pos,1);var o:Out;o.pos=frame.mvp*vec4f(i.pos,1);o.pos.y=-o.pos.y;o.normal=i.normal;o.uv=i.uv;o.tint=frame.tint;o.world=w.xyz;o.camera=frame.cameraPos.xyz;o.view=(frame.view*w).xyz;return o;}
)wgsl";

inline constexpr const char *kBoltFragWgsl = R"wgsl(
struct Params{data:array<vec4f,8>};struct In{@builtin(position) pos:vec4f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(5) view:vec3f};@group(0) @binding(15) var<uniform> params:Params;fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
@fragment fn fs_main(i:In)->@location(0) vec4f{let flash=clamp(p(11u),0.0,1.0);if(flash<=0.01){discard;}let across=abs(i.uv.x);let core=1.0-smoothstep(0.08,0.34,across);let halo=pow(max(0.0,1.0-across),2.2)*flash;let bayer=array<f32,16>(0.0,0.5,0.125,0.625,0.75,0.25,0.875,0.375,0.1875,0.6875,0.0625,0.5625,0.9375,0.4375,0.8125,0.3125);let q=vec2u(i.pos.xy)%vec2u(4);if(max(core,halo*0.72)<=bayer[q.y*4u+q.x]){discard;}let color=mix(vec3f(0.20,0.38,0.95),vec3f(0.92,0.97,1.0),core)*(0.55+0.85*flash);let fog=clamp(1.0-exp(-length(i.view)*p(10u)),0.0,1.0);return vec4f(mix(color,vec3f(p(7u),p(8u),p(9u)),fog*0.6),1);}
)wgsl";

}  // namespace eve::weather::shaders
