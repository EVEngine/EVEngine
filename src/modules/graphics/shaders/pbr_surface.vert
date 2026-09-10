#version 450
struct UvInfo { float rotation; uint offset; float present; float srgb; };
struct Light { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Params {
 mat4 mvp; mat4 model; mat4 view;
 vec4 camera; vec4 tint; vec4 ambient; vec4 material;
 vec4 emissive; vec4 specular; vec4 coat; vec4 misc;
 Light lights[8];
 vec4 uvTransform[11]; // offset xy, scale zw
 UvInfo uvInfo[11]; // rotation, storage offset (-1 uses vertex UV0), present, reserved
} u;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in uvec4 joints;
layout(location=4) in vec4 weights;
layout(set=0,binding=2,std430) readonly buffer UVs { vec2 values[]; } uv;
layout(set=0,binding=3,std430) readonly buffer Skin { mat4 values[]; } skin;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 texcoords[11];
void main() {
 mat4 pose=mat4(1);
 if(u.misc.w>0) {
  pose=mat4(0); float total=0;
  for(int i=0;i<4;i++) if(weights[i]>0 && joints[i]<uint(u.misc.w)) {
   pose+=weights[i]*skin.values[joints[i]]; total+=weights[i];
  }
  pose=total>0 ? pose/total : mat4(1);
 }
 vec4 p=pose*vec4(position,1);
 worldPosition=(u.model*p).xyz;
 worldNormal=transpose(inverse(mat3(u.model*pose)))*normal;
 gl_Position=u.mvp*p;
 for(int i=0;i<11;i++) {
  vec2 t=u.uvInfo[i].offset==0xffffffffu ? texcoord : uv.values[int(u.uvInfo[i].offset)+gl_VertexIndex];
  t*=u.uvTransform[i].zw;
  float c=cos(u.uvInfo[i].rotation),s=sin(u.uvInfo[i].rotation);
  texcoords[i]=mat2(c,s,-s,c)*t+u.uvTransform[i].xy;
 }
}
