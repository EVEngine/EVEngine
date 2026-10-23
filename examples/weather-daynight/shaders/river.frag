#version 450
// Continuous spline ribbon: U across the channel, V in metres downstream.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient; Light3D lights[8];
} frame;
layout(push_constant) uniform Externals { float data[32]; } p;
layout(location=0) out vec4 outColor;

float hash(vec2 q) { return fract(sin(dot(q,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 q) {
    vec2 i=floor(q), f=fract(q); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
               mix(hash(i+vec2(0,1)),hash(i+1.0),f.x),f.y);
}
float waterHeight(vec2 q) {
    return noise(q*vec2(24.0,1.5))*0.6+noise(q*vec2(51.0,3.3)+9.0)*0.27;
}
void main() {
    float time=p.data[0], flow=p.data[1];
    vec2 q=vec2(vUV.x,vUV.y-time*2.1*flow);
    float n=waterHeight(q);
    float slope=1.0-clamp(normalize(vNormal).y,0.0,1.0);
    // Derivative basis follows the actual curved ribbon, including the drop.
    vec3 dpdx=dFdx(vWorldPos), dpdy=dFdy(vWorldPos);
    vec2 du=dFdx(vUV), dv=dFdy(vUV);
    vec3 tangent=normalize(dpdx*dv.y-dpdy*du.y);
    vec3 base=normalize(vNormal);
    vec3 along=normalize(cross(base,tangent));
    vec2 grad=vec2(waterHeight(q+vec2(0.002,0))-waterHeight(q-vec2(0.002,0)),
                   waterHeight(q+vec2(0,0.02))-waterHeight(q-vec2(0,0.02)));
    vec3 N=normalize(base-tangent*grad.x*2.8-along*grad.y*1.5);
    vec3 V=normalize(frame.cameraPos.xyz-vWorldPos);
    vec3 L=normalize(frame.lightDirIntensity.xyz);
    float diffuse=max(dot(N,L),0.0);
    vec3 illumination=frame.ambient.rgb*1.5+frame.lightColor.rgb*(0.18+diffuse*0.65);
    float fresnel=pow(1.0-clamp(dot(N,V),0.0,1.0),5.0);
    vec3 water=mix(vec3(0.015,0.16,0.19),vec3(0.055,0.37,0.36),n);
    water=mix(water,vec3(0.28,0.48,0.56),0.18+fresnel*0.45);
    float bank=smoothstep(0.40,0.50,abs(vUV.x-0.5));
    float threads=smoothstep(0.50,0.76,n+noise(q*vec2(8,0.7))*0.20);
    float impact=exp(-dot(vWorldPos.xz-vec2(0,-4.2),vWorldPos.xz-vec2(0,-4.2))*0.20);
    float rings=0.5+0.5*sin(length(vWorldPos.xz-vec2(0,-4.4))*14.0-time*6.0);
    float foam=clamp(threads*(0.08+slope*0.95)+bank*smoothstep(0.3,0.7,noise(q*vec2(18,3)))*0.65+
                     impact*(0.23+threads*0.6+rings*0.18),0.0,0.96);
    vec3 color=mix(water,vec3(0.83,0.94,0.92),foam)*illumination;
    float glint=pow(max(dot(N,normalize(L+V)),0.0),100.0);
    color+=frame.lightColor.rgb*glint*0.6;
    outColor=vec4(color,1.0);
}
