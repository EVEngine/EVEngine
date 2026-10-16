struct Light3D{posRadius:vec4f,color:vec4f};
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f,envProbeCenter:vec4f,envProbeExtent:vec4f,skinInfo:vec4f,reflectionProbeCenter:array<vec4f,2>,reflectionProbeExtent:array<vec4f,2>};
@group(0) @binding(0) var<uniform> frame:Frame;
struct Varying {
 @builtin(position) pos:vec4f,
 @location(0) normal:vec3f,
 @location(1) surface:vec2f,
 @location(2) world:vec3f,
};
@group(0) @binding(3) var environment:texture_cube<f32>;
@group(0) @binding(7) var samp:sampler;
@group(0) @binding(2) var hullMask:texture_2d<f32>;
fn hullValue(column:i32,row:i32)->f32 {
 let bytes=vec4u(round(textureLoad(hullMask,vec2i(column,row),0)*255.0));
 return bitcast<f32>(bytes.x | (bytes.y<<8u) | (bytes.z<<16u) | (bytes.w<<24u));
}
fn dryInterior(point:vec3f)->bool {
 if (any(textureDimensions(hullMask)!=vec2u(4u,545u))) {return false;}
 let count=clamp(i32(hullValue(0,0)),0,32);
 for(var volume=0;volume<count;volume++) {
  let row=1+volume*17;let planes=clamp(i32(hullValue(0,row)),0,16);
  var inside=planes>0;
  for(var plane=0;plane<planes;plane++) {
   let r=row+1+plane;
   let n=vec3f(hullValue(0,r),hullValue(1,r),hullValue(2,r));
   if(dot(n,point)>hullValue(3,r)) {inside=false;break;}
  }
  if(inside) {return true;}
 }
 return false;
}
@fragment fn fs_main(input:Varying,@builtin(front_facing) front:bool)->@location(0) vec4f {
 if(dryInterior(input.world)) {discard;}
 let N=normalize(select(-input.normal,input.normal,front));
 let V=normalize(frame.cameraPos.xyz-input.world);
 let L=normalize(frame.lightDir.xyz);let R=reflect(-V,N);
 let fresnel=0.02037+0.97963*pow(1.0-max(dot(N,V),0.0),5.0);
 let foam=clamp(max(input.surface.x,(0.72-input.surface.y)*2.5),0.0,1.0);
 let roughness=mix(0.12,0.65,foam);
 let reflected=textureSampleLevel(environment,samp,R,roughness*f32(textureNumLevels(environment)-1u)).rgb*frame.lightColor.w;
 let water=vec3f(0.006,0.055,0.075)*(0.6+0.4*max(N.y,0.0));
 let sun=pow(max(dot(N,normalize(V+L)),0.0),mix(420.0,20.0,roughness));
 let lit=mix(water,reflected,vec3f(fresnel))+frame.lightColor.rgb*sun*2.0*max(dot(N,L),0.0);
 let foamLight=vec3f(0.83,0.91,0.91)*(0.4+0.6*max(dot(N,L),0.0));
 return vec4f(mix(lit,foamLight,vec3f(foam))*frame.tint.rgb,1.0);
}
