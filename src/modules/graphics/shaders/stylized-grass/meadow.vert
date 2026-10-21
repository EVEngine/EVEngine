#version 450
// Shared four-vertex card, one immutable affine record per gl_InstanceIndex.
layout(location=0) in vec3 inPosition;
layout(location=2) in vec2 inUV;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDir; vec4 lightColor; vec4 tint; vec4 cameraPos;
} frame;
layout(set=1,binding=33,std430) readonly buffer Instances { mat4 values[]; } instances;
layout(push_constant) uniform Controls { float data[32]; } controls;
layout(location=0) out vec2 uv;
layout(location=1) flat out vec2 rootUV;
layout(location=2) flat out float variant;
layout(location=3) flat out float accent;
layout(location=4) flat out float phase;
layout(location=5) flat out int swayGroup;
void main() {
    mat4 record=instances.values[gl_InstanceIndex];
    vec3 root=record[3].xyz;
    vec3 toCamera=frame.cameraPos.xyz-root;
    vec3 right=normalize(vec3(toCamera.z,0.0,-toCamera.x));
    vec3 position=root+right*(inUV.x-0.5)*record[0].x+vec3(0.0,inUV.y*record[1].y,0.0);
    gl_Position=frame.mvp*vec4(position,1.0);
    // Identical for every vertex: every visible pixel of this instance samples
    // exactly the same already-lit ground location, including its real shadows.
    vec4 clip=frame.mvp*vec4(root+vec3(0.0,0.025,0.0),1.0);
    rootUV=(clip.xy/clip.w)*0.5+0.5;
    uv=inUV;
    variant=record[2].x;
    accent=record[2].y;
    phase=fract(sin(float(gl_InstanceIndex)*12.9898)*43758.5453);
    // Stable two-metre patches share a period; individual phases stay varied.
    vec2 patchCell=floor(root.xz/2.0);
    swayGroup=int(floor(fract(sin(dot(patchCell,vec2(127.1,311.7)))*43758.5453)*4.0));
}
