#version 450
// Linear eye depth, packed in RGB24 for the portable RGBA8 canvas path.
layout(location=5) in vec3 vViewPos;
layout(location=0) out vec4 outColor;
void main() {
    float depth=clamp(-vViewPos.z/64.0,0.0,0.99999);
    vec3 packed=fract(depth*vec3(1.0,255.0,65025.0));
    packed.xy-=packed.yz/255.0;
    outColor=vec4(packed,1.0);
}
