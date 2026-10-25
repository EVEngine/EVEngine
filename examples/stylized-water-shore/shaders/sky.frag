#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
layout(location=3) in vec3 world;
layout(location=4) in vec3 camera;
layout(location=5) in vec3 viewPosition;
layout(location=0) out vec4 outColor;
float hash(vec2 p) {return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p) {
    vec2 i=floor(p),f=fract(p);f=f*f*(3-2*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
void main() {
    vec3 d=normalize(world-camera);
    float height=max(d.y,0);
    vec3 sky=mix(vec3(.43,.55,.65),vec3(.075,.20,.38),pow(height,.5));
    vec2 p=d.xz/max(d.y+.12,.08)*2;
    float cloud=noise(p)*.55+noise(p*2.1)*.28+noise(p*4.3)*.17;
    float wisps=smoothstep(.57,.79,cloud)*smoothstep(0,.15,d.y);
    sky=mix(sky,vec3(.75,.78,.77),wisps*.65);
    outColor=vec4(sky,1);
}
