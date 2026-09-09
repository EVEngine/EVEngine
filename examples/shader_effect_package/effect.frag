#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vLightDir;
layout(location=4) in vec3 vLightColor;
layout(location=5) in vec3 vWorldPos;
layout(location=6) in vec3 vCameraPos;
layout(push_constant) uniform Externals { float data[32]; } u;
layout(location=0) out vec4 color;
void main() {
    float phase = u.data[0];
    float strength = u.data[1];
    float rim = pow(1.0-abs(dot(normalize(vNormal),normalize(vCameraPos-vWorldPos))),2.5);
    float wave = 0.5+0.5*sin(vWorldPos.y*25.0-phase*9.0);
    float grid = pow(wave,12.0);
    vec3 cyan = mix(vec3(0.015,0.07,0.14),vec3(0.12,0.85,1.0),rim);
    vec3 glow = cyan + vec3(0.03,0.35,0.8)*grid;
    color = vec4(glow*strength*vTint.rgb,vTint.a);
}
