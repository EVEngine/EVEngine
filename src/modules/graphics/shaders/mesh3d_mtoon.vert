#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity; // xyz = primary dir; w = lightCount
    vec4 lightColor;        // rgb = primary; w = metallic
    vec4 tint;
    vec4 cameraPos;         // xyz = eye; w = roughness
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo; // near, far
    vec4 cloud;
    vec4 cloudWind;
    vec4 virtualTexture;
    vec4 virtualAtlas;
    vec4 bindlessEnv;
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    mat4 skinBones[128];
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;

layout(set=0,binding=6) uniform sampler2D metadata;
layout(push_constant) uniform Externals { float data[32]; } u;
float word(int i) { uvec4 b=uvec4(round(texelFetch(metadata,ivec2(i,0),0)*255)); return uintBitsToFloat(b.x|(b.y<<8)|(b.z<<16)|(b.w<<24)); }
layout(set=0,binding=1) uniform sampler2D atlas;
vec4 info(int i) { return vec4(word(i*4),word(i*4+1),word(i*4+2),word(i*4+3)); }
int wrapped(int n,int size,int mode) {
    if(mode==33071) return clamp(n,0,size-1);
    int period=mode==33648?2*size:size;
    n=((n%period)+period)%period;
    return n>=size?2*size-1-n:n;
}
vec4 sampleSlot(int slot,vec2 uv,vec4 missing) {
    vec4 rect=info(5+slot*3), transform=info(6+slot*3), params=info(7+slot*3);
    if(params.w<.5) return missing;
    float c=cos(params.x),s=sin(params.x);
    uv=mat2(c,s,-s,c)*(uv*transform.zw)+transform.xy;
    vec2 p=uv*rect.zw-.5; ivec2 base=ivec2(floor(p)); vec2 f=fract(p);
    ivec2 size=ivec2(rect.zw), origin=ivec2(rect.xy);
    ivec2 a=ivec2(wrapped(base.x,size.x,int(params.y)),wrapped(base.y,size.y,int(params.z)));
    ivec2 b=ivec2(wrapped(base.x+1,size.x,int(params.y)),wrapped(base.y+1,size.y,int(params.z)));
    return mix(mix(texelFetch(atlas,origin+a,0),texelFetch(atlas,origin+ivec2(b.x,a.y),0),f.x),
               mix(texelFetch(atlas,origin+ivec2(a.x,b.y),0),texelFetch(atlas,origin+b,0),f.x),f.y);
}

void main() {
    vec4 localPos = vec4(inPos, 1.0);
    vec3 localNormal = inNormal;
    if (ubo.skinInfo.x > 0.5) {
        mat4 skin = inWeights.x * ubo.skinBones[inJoints.x]
                  + inWeights.y * ubo.skinBones[inJoints.y]
                  + inWeights.z * ubo.skinBones[inJoints.z]
                  + inWeights.w * ubo.skinBones[inJoints.w];
        localPos = skin * localPos;
        localNormal = mat3(skin) * localNormal;
    }
    gl_Position = ubo.mvp * localPos;
    if(u.data[24]>.5) {
        float width=word(7)*sampleSlot(7,inUV,vec4(1)).r;
        float mode=word(9);
        vec3 normal=normalize(mat3(transpose(inverse(ubo.model)))*localNormal);
        if(mode==1) gl_Position=ubo.mvp*(localPos+vec4(normalize(localNormal)*width,0));
        else if(mode==2) {
            vec4 direction=ubo.mvp*vec4(normalize(localNormal),0);
            gl_Position.xy+=normalize(direction.xy+vec2(1e-8))*width*gl_Position.w;
        }
    }
    vec4 world = ubo.model * localPos;
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    vNormal = normalize(normalMat * localNormal);
    vUV = inUV;
    vTint = ubo.tint;
    vCameraPos = ubo.cameraPos.xyz;
}
