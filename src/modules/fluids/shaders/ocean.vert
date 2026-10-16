#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
struct Light3D{vec4 posRadius;vec4 color;};
layout(set=0,binding=0,std140) uniform Frame{mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;vec4 cameraPos;vec4 ambient;Light3D lights[8];vec4 texBomb;vec4 parallax;mat4 view;vec4 clipInfo;vec4 cloud;vec4 cloudWind;vec4 bindlessEnv;vec4 envProbeCenter;vec4 envProbeExtent;vec4 skinInfo;vec4 reflectionProbeCenter[2];vec4 reflectionProbeExtent[2];}ubo;
layout(location=0) out vec3 normal;
layout(location=1) out vec2 surface;
layout(location=2) out vec3 world;
void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    world = (ubo.model * vec4(inPos, 1.0)).xyz;
    normal = normalize(mat3(ubo.model) * inNormal);
    surface = inUV;
}
