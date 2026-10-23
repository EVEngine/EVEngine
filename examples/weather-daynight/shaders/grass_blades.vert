#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient;
} frame;
layout(push_constant) uniform Externals { float data[32]; } p;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vWorldPos;
void main() {
    vec3 position=inPos;
    float phase=position.x*0.73+position.z*0.49;
    float wave=sin(p.data[0]*1.4+phase)+0.3*sin(p.data[0]*2.3-phase*1.7);
    // Root stays fixed; bending increases toward the tapered tip.
    float bend=inUV.y*inUV.y*wave*p.data[1];
    position.x+=bend;
    position.z+=bend*0.43;
    gl_Position=frame.mvp*vec4(position,1.0);
    vNormal=normalize(mat3(frame.model)*inNormal);
    vUV=inUV;
    vTint=frame.tint;
    vWorldPos=(frame.model*vec4(position,1.0)).xyz;
}
