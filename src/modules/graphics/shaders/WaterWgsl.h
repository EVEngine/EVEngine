#pragma once

namespace eve::graphics::shaders {

/** @brief Vertex-displaced low-frequency water layers for the WebGPU backend. */
inline constexpr char kWaterVertWgsl[] = R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f,envProbeCenter:vec4f,envProbeExtent:vec4f,skinInfo:vec4f,reflectionProbeCenter:array<vec4f,2>,reflectionProbeExtent:array<vec4f,2>};
struct Params{data:array<vec4f,8>};
struct VSIn{@location(0)pos:vec3f,@location(1)normal:vec3f,@location(2)uv:vec2f};
struct VSOut{@builtin(position)pos:vec4f,@location(0)normal:vec3f,@location(1)uv:vec2f,@location(2)tint:vec4f,@location(3)worldPos:vec3f,@location(4)cameraPos:vec3f,@location(5)viewPos:vec3f};
@group(0)@binding(0)var<uniform>frame:Frame;
@group(0)@binding(15)var<uniform>params:Params;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
fn waveHeight(worldXZ:vec2f)->f32{
 let scale=max(p(3u),0.01)*0.72;let time=p(0u)*p(1u);
 let primary=sin(dot(worldXZ,vec2f(0.91,0.31))*scale+time*1.45);
 let crossWave=sin(dot(worldXZ,vec2f(-0.36,0.93))*scale*0.58-time*0.92);
 let swell=sin(dot(worldXZ,vec2f(0.22,0.98))*scale*0.24+time*0.37);
 return p(2u)*(primary*0.48+crossWave*0.34+swell*0.18);
}
@vertex fn vs_main(in:VSIn)->VSOut{
 let baseWorld=frame.model*vec4f(in.pos,1.0);let eps=0.025;let height=waveHeight(baseWorld.xz);
 let dx=(waveHeight(baseWorld.xz+vec2f(eps,0))-waveHeight(baseWorld.xz-vec2f(eps,0)))/(2.0*eps);
 let dz=(waveHeight(baseWorld.xz+vec2f(0,eps))-waveHeight(baseWorld.xz-vec2f(0,eps)))/(2.0*eps);
 let localPos=in.pos+vec3f(0,height,0);let world=frame.model*vec4f(localPos,1.0);
 var out:VSOut;out.pos=frame.mvp*vec4f(localPos,1.0);out.pos.y=-out.pos.y;
 out.normal=normalize(mat3x3f(frame.model[0].xyz,frame.model[1].xyz,frame.model[2].xyz)*normalize(vec3f(-dx,1,-dz)));
 out.uv=in.uv;out.tint=frame.tint;out.worldPos=world.xyz;out.cameraPos=frame.cameraPos.xyz;out.viewPos=(frame.view*world).xyz;return out;
}
)wgsl";

/** @brief Backend-parity stylized-water WGSL source with depth foam and scene-color refraction. */
inline constexpr char kWaterFragWgsl[] = R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f,envProbeCenter:vec4f,envProbeExtent:vec4f,skinInfo:vec4f,reflectionProbeCenter:array<vec4f,2>,reflectionProbeExtent:array<vec4f,2>};
struct FSIn{@builtin(position)pos:vec4f,@location(0)normal:vec3f,@location(1)uv:vec2f,@location(2)tint:vec4f,@location(3)worldPos:vec3f,@location(4)cameraPos:vec3f,@location(5)viewPos:vec3f};
struct Params{data:array<vec4f,8>};
@group(0)@binding(0)var<uniform>frame:Frame;@group(0)@binding(1)var sceneColorTex:texture_2d<f32>;@group(0)@binding(3)var environment:texture_cube<f32>;@group(0)@binding(6)var ssrTex:texture_2d<f32>;@group(0)@binding(7)var samp:sampler;@group(0)@binding(9)var sceneDepthTex:texture_2d<f32>;@group(0)@binding(15)var<uniform>params:Params;@group(0)@binding(16)var reflectionProbe0:texture_cube<f32>;@group(0)@binding(17)var reflectionProbe1:texture_cube<f32>;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}fn hash(n:f32)->f32{return fract(sin(n)*43758.5453123);}fn hash2(i:i32)->vec2f{return vec2f(hash(f32(i)*7.31),hash(f32(i)*13.17+1.0));}
fn shapedWave(phase:f32)->f32{let w=sin(phase);return sign(w)*pow(abs(w),max(p(31u),0.1));}
fn rippleRing(uv:vec2f,i:i32)->f32{let period=max(p(6u),0.05);let age=p(0u)-floor(p(0u)/period)*period-hash(f32(i)*3.7)*period;if(age<0.0||age>period*0.8){return 0.0;}let center=mix(vec2f(0.5),hash2(i),vec2f(0.72));let x=(length(uv-center)-age*0.22)/0.10;return p(4u)*exp(-(x*x)*0.9)*cos(x*6.2831853)*exp(-age*1.6);}
fn waterHeight(uv:vec2f)->f32{let scale=p(3u);let time=p(0u)*p(1u);let q=uv+vec2f(sin(uv.y*scale*0.31+time*0.37),sin(uv.x*scale*0.27-time*0.29))*0.018;let phase0=dot(q,vec2f(1.0,0.23))*scale*6.2831853+time*6.2831853;let phase1=dot(q,vec2f(-0.48,0.88))*scale*0.73*6.2831853-time*1.17*6.2831853;let phase2=dot(q,vec2f(0.37,0.93))*scale*1.31*6.2831853+time*0.61*6.2831853;var h=p(2u)*(shapedWave(phase0)*0.42+shapedWave(phase1)*0.34+shapedWave(phase2)*0.24);for(var i=0;i<8;i++){if(f32(i)>=p(5u)+0.5){break;}h+=rippleRing(q,i);}return h;}
fn caustics(pos:vec2f)->f32{let q=pos*max(p(30u),0.01);let t=p(0u)*p(1u);let a=sin(q.x*1.37+q.y*0.73+t*1.2);let b=sin(q.x*-0.62+q.y*1.51-t*0.93);let c=sin((q.x+q.y)*1.11+t*0.57);let ridge=1.0-abs((a+b+c)/3.0);return pow(clamp(ridge,0.0,1.0),5.0);}
fn foamCells(pos:vec2f)->f32{let cell=floor(pos);let f=fract(pos);var first=10.0;var second=10.0;for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){let g=vec2f(f32(x),f32(y));let point=hash2(i32(cell.x+g.x)*131+i32(cell.y+g.y)*197);let distance=length(g+point-f);if(distance<first){second=first;first=distance;}else if(distance<second){second=distance;}}}return 1.0-smoothstep(0.025,0.18,second-first);}
fn flowNoise(pos:vec2f)->f32{let t=p(0u)*p(1u);let a=sin(pos.x*1.31+pos.y*0.73+t*1.17);let b=sin(pos.x*-0.67+pos.y*1.57-t*0.83);return 0.5+0.25*(a+b);}
fn crestLines(pos:vec2f)->f32{let t=p(0u)*p(1u);let scale=max(p(3u),0.01)*0.43;let phaseA=dot(pos,vec2f(0.96,0.28))*scale+sin(pos.y*0.72-t*0.31)*1.35+t*1.35;let phaseB=dot(pos,vec2f(-0.38,0.92))*scale*0.67+sin(pos.x*0.58+t*0.27)*1.15-t*0.82;let a=smoothstep(0.91,0.985,sin(phaseA));let b=smoothstep(0.935,0.992,sin(phaseB));let broken=smoothstep(0.52,0.70,flowNoise(pos*0.66+vec2f(4.7)));return max(a*0.58,b*0.38)*broken;}
fn probeWeight(i:u32,pos:vec3f)->f32{let edge=frame.reflectionProbeExtent[i].xyz-abs(pos-frame.reflectionProbeCenter[i].xyz);let inside=min(edge.x,min(edge.y,edge.z));return select(0.0,clamp(inside/max(frame.reflectionProbeExtent[i].w,0.0001),0.0,1.0),inside>0.0&&frame.reflectionProbeCenter[i].w>0.0);}
fn probeDirection(i:u32,direction:vec3f,pos:vec3f)->vec3f{let center=frame.reflectionProbeCenter[i].xyz;let extent=frame.reflectionProbeExtent[i].xyz;let safe=select(vec3f(0.00001),direction,abs(direction)>vec3f(0.00001));let exitT=max((center-extent-pos)/safe,(center+extent-pos)/safe);let distance=min(exitT.x,min(exitT.y,exitT.z));return select(normalize(direction),normalize(pos+direction*distance-center),distance>0.0);}
@fragment fn fs_main(in:FSIn)->@location(0)vec4f{
 let eps=0.001;let grad=vec2f(waterHeight(in.uv+vec2f(eps,0))-waterHeight(in.uv-vec2f(eps,0)),waterHeight(in.uv+vec2f(0,eps))-waterHeight(in.uv-vec2f(0,eps)))/(2.0*eps);let flowTime=p(0u)*p(1u);let normalLayerA=vec2f(sin(in.worldPos.x*1.7+in.worldPos.z*0.8+flowTime),cos(in.worldPos.z*1.5-in.worldPos.x*0.6-flowTime*0.83));let normalLayerB=vec2f(cos(in.worldPos.x*3.1-in.worldPos.z*1.2-flowTime*1.31),sin(in.worldPos.z*2.7+in.worldPos.x*0.9+flowTime*0.57));let layeredNormal=grad*0.11+normalLayerA*0.055+normalLayerB*0.025;let normal=normalize(in.normal+vec3f(-layeredNormal.x,0,-layeredNormal.y));let view=normalize(frame.cameraPos.xyz-in.worldPos);let reflected=reflect(-view,normal);let ndv=max(dot(view,normal),0.0);
 let size=vec2f(textureDimensions(sceneColorTex));let suv=in.pos.xy/max(size,vec2f(1));let nearZ=max(frame.clipInfo.x,0.0001);let farZ=max(frame.clipInfo.y,nearZ+0.001);let surface=clamp((-in.viewPos.z-nearZ)/(farZ-nearZ),0.0,1.0);let hasDepth=p(29u)>=0.0;let opacity=select(-p(29u)-1.0,p(29u),hasDepth);let sceneSample=textureSample(sceneColorTex,samp,suv);var scene=surface+1.0;if(hasDepth){scene=textureSample(sceneDepthTex,samp,suv).r;}if(sceneSample.a>surface+0.00001&&sceneSample.a<0.9999){scene=sceneSample.a;}let depth=max((scene-surface)*(farZ-nearZ),0.0);let shallow=1.0-smoothstep(0.0,max(p(16u),0.0001),depth);let shallowColor=vec3f(p(13u),p(14u),p(15u));let water=mix(vec3f(p(10u),p(11u),p(12u)),shallowColor,vec3f(shallow));
 let flow=vec2f(flowNoise(in.worldPos.xz*1.8),flowNoise(in.worldPos.zx*2.3+vec2f(17.0)))-vec2f(0.5);let ruv=clamp(suv+(layeredNormal*0.75+flow*0.35)*p(28u)*(0.25+shallow*0.75),vec2f(0.001),vec2f(0.999));let behind=textureSample(sceneColorTex,samp,ruv).rgb;let absorption=select(1.0,1.0-exp(-depth/max(p(16u),0.001)*3.2),hasDepth);var waterAlpha=clamp(opacity*mix(0.35,1.0,absorption),0.0,1.0);var color=water;let causticMask=select(0.0,smoothstep(0.02,max(p(16u),0.03),depth)*shallow,hasDepth);color+=shallowColor*caustics(in.worldPos.xz)*causticMask*p(26u);
 let rough=clamp(0.08+length(layeredNormal)*0.35,0.05,0.34);let envLod=rough*f32(max(i32(textureNumLevels(environment))-2,0));var w0=probeWeight(0u,in.worldPos);var w1=probeWeight(1u,in.worldPos);var sum=w0+w1;if(sum>1.0){w0/=sum;w1/=sum;sum=1.0;}var reflection=textureSampleLevel(environment,samp,reflected,envLod).rgb*frame.lightColor.w*(1.0-sum);if(w0>0.0){reflection+=textureSampleLevel(reflectionProbe0,samp,probeDirection(0u,reflected,in.worldPos),rough*4.0).rgb*frame.reflectionProbeCenter[0].w*w0;}if(w1>0.0){reflection+=textureSampleLevel(reflectionProbe1,samp,probeDirection(1u,reflected,in.worldPos),rough*4.0).rgb*frame.reflectionProbeCenter[1].w*w1;}reflection*=vec3f(p(21u),p(22u),p(23u));let fresnel=pow(1.0-ndv,max(p(25u),0.01));waterAlpha=max(waterAlpha,opacity*mix(0.35,1.0,fresnel));let reflectionWeight=clamp((0.08+0.92*fresnel)*p(20u),0.0,0.78);color=mix(color,reflection,vec3f(reflectionWeight));
 let light=normalize(frame.lightDir.xyz);let glint=pow(max(dot(normal,normalize(view+light)),0.0),mix(24.0,160.0,1.0-rough));color+=frame.lightColor.rgb*glint*max(dot(normal,light),0.0)*p(24u);
 let foamUv=in.worldPos.xz*0.80;let cells=foamCells(foamUv+vec2f(p(0u)*0.08,-p(0u)*0.05));let cells2=foamCells(foamUv*0.61+vec2f(-p(0u)*0.04,p(0u)*0.06));let curvedLines=smoothstep(0.72,0.96,max(cells,cells2*0.72));let crest=smoothstep(p(2u)*0.34,p(2u)*0.80,waterHeight(in.uv));let surfaceTint=mix(shallowColor,vec3f(p(17u),p(18u),p(19u)),vec3f(0.28));let surfacePattern=curvedLines*(0.07+crest*0.11)*p(9u);color=mix(color,surfaceTint,vec3f(surfacePattern));waterAlpha=max(waterAlpha,surfacePattern*0.42);
 let normalizedDepth=clamp(depth/max(p(7u),0.0001),0.0,1.0);let edge=1.0-smoothstep(max(p(7u)-p(8u),0.0),p(7u)+p(8u),depth);let bandCoord=normalizedDepth*3.4+flowNoise(foamUv*2.1)*0.34;let shoreBands=(1.0-smoothstep(0.035,0.12,abs(fract(bandCoord)-0.5)))*(1.0-normalizedDepth);let shoreFoam=max(edge*(0.52+0.48*cells),shoreBands*0.82)*p(9u);color=mix(color,vec3f(p(17u),p(18u),p(19u)),vec3f(clamp(shoreFoam,0.0,1.0)));waterAlpha=max(waterAlpha,clamp(shoreFoam,0.0,1.0));
 if(p(27u)>0.0){let externalReflection=textureSample(ssrTex,samp,suv);let externalWeight=clamp(externalReflection.a*p(27u),0.0,1.0);color=mix(color,externalReflection.rgb,vec3f(externalWeight));waterAlpha=max(waterAlpha,externalWeight);}
 waterAlpha=max(waterAlpha,reflectionWeight*0.72);let transmission=(1.0-waterAlpha)*0.38;color=mix(color,behind,vec3f(transmission));return vec4f(color*in.tint.rgb,waterAlpha);
}
)wgsl";

}  // namespace eve::graphics::shaders
