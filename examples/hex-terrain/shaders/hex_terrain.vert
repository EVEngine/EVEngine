#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in uvec4 inJoints;
layout(location=4) in vec4 inWeights;

struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind; vec4 bindlessEnv;
    vec4 skinInfo; mat4 skinBones[128];
} ubo;

layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vWorldPos;
layout(location=4) out vec3 vCameraPos;
layout(location=5) out vec3 vViewPos;

float terrainHash(vec2 p) {
    p=fract(p*vec2(123.34,456.21));
    p+=dot(p,p+45.32);
    return fract(p.x*p.y);
}

float terrainNoise(vec2 p) {
    vec2 i=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(mix(terrainHash(i),terrainHash(i+vec2(1,0)),f.x),
               mix(terrainHash(i+vec2(0,1)),terrainHash(i+vec2(1,1)),f.x),f.y);
}

void main() {
    vec3 p=inPos;
    // Keep the authored mesh position identical to the engine depth/shadow passes.
    // Water and vegetation motion is shaded in the fragment stage.
    vec4 localPos=vec4(p,1.0);
    gl_Position=ubo.mvp*localPos;
    vec4 world=ubo.model*localPos;
    vWorldPos=world.xyz;
    vViewPos=(ubo.view*world).xyz;
    vNormal=normalize(transpose(inverse(mat3(ubo.model)))*inNormal);
    vUV=inUV;
    vTint=ubo.tint;
    vCameraPos=ubo.cameraPos.xyz;
}
