#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=5) in vec4 inColor;
layout(set=0,binding=0) uniform Frame {
 mat4 mvp;mat4 model;vec4 lightDir;vec4 lightColor;vec4 tint;vec4 cameraPos;
} f;
layout(push_constant) uniform Params {float data[32];} u;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vLightDir;
layout(location=4) out vec3 vLightColor;
layout(location=5) out vec3 vWorldPos;
layout(location=6) out vec3 vCameraPos;
layout(location=7) out vec3 vSmoothNormal;
void main(){
 vec3 smoothN=inNormal;
 if(u.data[15]>1.5) smoothN=normalize(inColor.rgb*2.0-1.0);
 else if(u.data[15]>0.5) smoothN=normalize(inPos+vec3(0.00001));
 vec3 p=inPos;
 if(u.data[12]>0.5){
  p+=smoothN*u.data[13];
  p+=vec3(0.57,0.83,0.21)*u.data[14];
 }
 mat3 nm=transpose(inverse(mat3(f.model)));
 vNormal=normalize(nm*inNormal);vSmoothNormal=normalize(nm*smoothN);
 vWorldPos=(f.model*vec4(p,1)).xyz;vUV=inUV;vTint=f.tint;
 vLightDir=normalize(f.lightDir.xyz);vLightColor=f.lightColor.rgb;vCameraPos=f.cameraPos.xyz;
 gl_Position=f.mvp*vec4(p,1);
}
