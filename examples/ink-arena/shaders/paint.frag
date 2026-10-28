#version 450
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(binding=0) uniform sampler2D brushAtlas;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Projector {
    vec4 brushU;
    vec4 brushV;
    vec4 depth;
    vec4 ink;
} p;
void main() {
    vec3 pixel=vec3(1.0,gl_FragCoord.xy);
    float depth=abs(dot(p.depth.xyz,pixel));
    vec2 uv=vec2(dot(p.brushU.xyz,pixel),dot(p.brushV.xyz,pixel));
    if(depth>=0.85 || any(lessThanEqual(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) discard;
    ivec2 texel=ivec2(p.brushU.w,p.brushV.w)*256+ivec2(uv*255.0);
    float fade=clamp((0.85-depth)/0.40,0.0,1.0);
    float coverage=texelFetch(brushAtlas,texel,0).r*fade*fade*(3.0-2.0*fade);
    // SrcAlpha / OneMinusSrcAlpha RGB; One / OneMinusSrcAlpha alpha.
    // Hardware blending implements the same RGBA recurrence as SplatPaint.
    outColor=vec4(p.ink.rgb,coverage);
}
