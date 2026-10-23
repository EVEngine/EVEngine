#version 450
// Analytic area-light shadow and sphere occlusion for this fixed unit-sphere studio.
// This is a scene lighting probe, not a replacement for engine-wide shadow maps.
layout(location=0) in vec3 vNormal;
layout(location=3) in vec3 vWorldPos;
layout(set=0,binding=0,std140) uniform Frame {
 mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;
 vec4 tint;vec4 cameraPos;vec4 ambient;
} u;
layout(location=0) out vec4 outColor;
void main(){
 vec3 n=normalize(vNormal),center=u.model[3].xyz+vec3(0,1.15,0);
 vec3 delta=center-vWorldPos;
 float distanceSquared=max(dot(delta,delta),1.0001);
 float occlusion=clamp(max(dot(n,normalize(delta)),0)/distanceSquared,0,1);
 vec3 light=normalize(u.lightDirIntensity.xyz);
 vec3 tangent=normalize(cross(light,vec3(0,0,1)));
 vec3 bitangent=cross(light,tangent);
 float visibility=0;
 for(int i=0;i<32;++i){
  float angle=float(i)*2.39996323;
  float radius=sqrt((float(i)+.5)/32.0)*.28;
  vec3 ray=normalize(light+(cos(angle)*tangent+sin(angle)*bitangent)*radius);
  vec3 offset=center-(vWorldPos+n*.002);
  float along=dot(offset,ray);
  bool blocked=along>0 && dot(offset,offset)-along*along<1;
  visibility+=blocked?(1.0-u.tint.a)/32.0:1.0/32.0;
 }
 vec3 diffuse=vec3(.22)/3.14159265*u.lightColor.rgb*max(dot(n,light),0)*visibility;
 vec3 fill=vec3(.22)*(u.ambient.rgb+vec3(.12))*(1-.85*occlusion*u.tint.a);
 outColor=vec4(diffuse+fill,1);
}
