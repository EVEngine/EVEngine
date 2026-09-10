#version 450
struct UvInfo { float rotation; uint offset; float present; float srgb; };
struct Light { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Params {
 mat4 mvp; mat4 model; mat4 view;
 vec4 camera; vec4 tint; vec4 ambient; vec4 material;
 vec4 emissive; vec4 specular; vec4 coat; vec4 misc;
 Light lights[8];
 vec4 uvTransform[11]; // offset xy, scale zw
 UvInfo uvInfo[11]; // rotation, stream offset (-1 uses vertex UV0), present, reserved
} u;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in uvec4 joints;
layout(location=4) in vec4 weights;
layout(location=5) in vec2 sourceUv0;
layout(location=6) in vec2 sourceUv1;
layout(location=7) in vec2 sourceUv2;
layout(location=8) in vec2 sourceUv3;
layout(location=9) in vec2 sourceUv4;
layout(location=10) in vec2 sourceUv5;
layout(location=11) in vec2 sourceUv6;
layout(location=12) in vec2 sourceUv7;
layout(location=13) in vec2 sourceUv8;
layout(location=14) in vec2 sourceUv9;
layout(location=15) in vec2 sourceUv10;
layout(set=0,binding=3,std430) readonly buffer Skin { mat4 values[]; } skin;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 texcoord0;
layout(location=3) out vec2 texcoord1;
layout(location=4) out vec2 texcoord2;
layout(location=5) out vec2 texcoord3;
layout(location=6) out vec2 texcoord4;
layout(location=7) out vec2 texcoord5;
layout(location=8) out vec2 texcoord6;
layout(location=9) out vec2 texcoord7;
layout(location=10) out vec2 texcoord8;
layout(location=11) out vec2 texcoord9;
layout(location=12) out vec2 texcoord10;
vec2 materialUv(int slot, vec2 source) {
 vec2 t=texcoord;
 if(u.uvInfo[slot].offset!=0xffffffffu)
  t=source;
 t*=u.uvTransform[slot].zw;
 float c=cos(u.uvInfo[slot].rotation),s=sin(u.uvInfo[slot].rotation);
 return mat2(c,s,-s,c)*t+u.uvTransform[slot].xy;
}
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
 texcoord0=materialUv(0,sourceUv0);
 texcoord1=materialUv(1,sourceUv1);
 texcoord2=materialUv(2,sourceUv2);
 texcoord3=materialUv(3,sourceUv3);
 texcoord4=materialUv(4,sourceUv4);
 texcoord5=materialUv(5,sourceUv5);
 texcoord6=materialUv(6,sourceUv6);
 texcoord7=materialUv(7,sourceUv7);
 texcoord8=materialUv(8,sourceUv8);
 texcoord9=materialUv(9,sourceUv9);
 texcoord10=materialUv(10,sourceUv10);
}
