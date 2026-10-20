#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
layout(location=3) in vec3 world;
layout(location=4) in vec3 camera;
layout(location=5) in vec3 viewPosition;
layout(location=0) out vec4 outColor;
void main() {
    vec3 N=normalize(normal),L=normalize(vec3(-.6,.9,-.8));
    float height=smoothstep(5.0,15.0,world.y);
    vec3 forest=mix(vec3(.045,.070,.045),vec3(.16,.18,.17),height);
    float grain=sin(world.x*3.1+world.y)*sin(world.z*2.3)*.025;
    forest*=.50+.5*max(dot(N,L),0.0)+grain;
    float haze=clamp(length(camera-world)/240.0,.10,.60);
    outColor=vec4(mix(forest,vec3(.43,.55,.65),haze),1);
}
