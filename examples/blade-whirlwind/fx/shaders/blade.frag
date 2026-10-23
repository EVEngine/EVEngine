#version 450
// Native mesh vertex interface. No screen coordinates or private camera model.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=1) uniform sampler2D bladeMask;
layout(push_constant) uniform Params { float p[32]; } u;
// time, scroll, base RGB, edge RGB. Independent Shader per pooled instance.
layout(location=0) out vec4 outColor;
void main() {
    bool shard=vUV.y>1.5;
    vec2 uv=shard?vec2(vUV.x,vUV.y-2.):vUV;
    float mask=texture(bladeMask,vec2(fract(uv.x-u.p[0]*u.p[1]),uv.y)).a;
    float feather=smoothstep(0.,.07,uv.y)*(1.-smoothstep(.82,1.,uv.y));
    float streak=sin(uv.x*110.-u.p[0]*12.+uv.y*12.);
    float edge=smoothstep(.36,.50,uv.y)*(1.-smoothstep(.50,.67,uv.y));
    vec3 base=vec3(u.p[2],u.p[3],u.p[4]);
    vec3 highlight=vec3(u.p[5],u.p[6],u.p[7]);
    vec3 color=mix(base,highlight,clamp(edge*.8+max(0.,streak)*.20,0.,1.));
    float alpha=mask*feather*vTint.a;
    if(shard) { color=highlight; alpha=vTint.a*.82*feather; }
    if(alpha<.015) discard;
    // Invert the renderer's established display curve to keep the authored palette.
    color=clamp(color,vec3(0.0),vec3(.96));
    color=.139*color/max(vec3(1.0)-color,vec3(.001));
    outColor=vec4(color,alpha);
}
