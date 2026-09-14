#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;

struct Light3D{vec4 posRadius;vec4 color;};
layout(set=0,binding=0,std140) uniform Frame{mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;vec4 cameraPos;vec4 ambient;Light3D lights[8];vec4 texBomb;vec4 parallax;mat4 view;vec4 clipInfo;vec4 cloud;vec4 cloudWind;vec4 bindlessEnv;vec4 envProbeCenter;vec4 envProbeExtent;vec4 skinInfo;vec4 reflectionProbeCenter[2];vec4 reflectionProbeExtent[2];}ubo;
layout(push_constant)uniform Externals{float data[32];}u;

layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vWorldPos;
layout(location=4) out vec3 vCameraPos;
layout(location=5) out vec3 vViewPos;

float waveHeight(vec2 worldXZ){
    float scale=max(u.data[3],.01)*.72;
    float time=u.data[0]*u.data[1];
    float primary=sin(dot(worldXZ,vec2(.91,.31))*scale+time*1.45);
    float crossWave=sin(dot(worldXZ,vec2(-.36,.93))*scale*.58-time*.92);
    float swell=sin(dot(worldXZ,vec2(.22,.98))*scale*.24+time*.37);
    return u.data[2]*(primary*.48+crossWave*.34+swell*.18);
}

void main(){
    vec4 baseWorld=ubo.model*vec4(inPos,1);
    float eps=.025;
    float height=waveHeight(baseWorld.xz);
    float dx=(waveHeight(baseWorld.xz+vec2(eps,0))-waveHeight(baseWorld.xz-vec2(eps,0)))/(2*eps);
    float dz=(waveHeight(baseWorld.xz+vec2(0,eps))-waveHeight(baseWorld.xz-vec2(0,eps)))/(2*eps);
    vec3 localPos=inPos;
    localPos.y+=height;
    vec4 world=ubo.model*vec4(localPos,1);
    gl_Position=ubo.mvp*vec4(localPos,1);
    vWorldPos=world.xyz;
    vViewPos=(ubo.view*world).xyz;
    vNormal=normalize(transpose(inverse(mat3(ubo.model)))*normalize(vec3(-dx,1,-dz)));
    vUV=inUV;
    vTint=ubo.tint;
    vCameraPos=ubo.cameraPos.xyz;
}
