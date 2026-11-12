#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(set=0,binding=1) uniform sampler2D splatSampler;
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
layout(location=0) out vec4 outColor;

float hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p)));
    uint n = q.x * 1597334677u ^ q.y * 3812015801u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float terrainShadow(vec3 worldPos, float viewDepth) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;
    int cascade = viewDepth < shadow.splits.x ? 0 : (viewDepth < shadow.splits.y ? 1 : 2);
    vec4 clip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 ||
        ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;
    float compareBias = cascade == 0 ? shadow.cascadeBias.x :
                        (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    if (compareBias <= 1e-8) compareBias = shadow.bias.x;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
        visibility += texture(shadowMap,
            vec4(uv + vec2(x, y) * texel * 0.65, float(cascade), ndc.z - compareBias));
    return visibility / 9.0;
}
// Coordinates derive from panel UV and height, so both columns share the same geology.
float detailNoise(vec3 p) {
    return (valueNoise(p.xy)+valueNoise(p.yz)+valueNoise(p.zx))/3.0;
}
void main() {
    vec3 p=vec3(vUV.x*32.0,vWorldPos.y,vUV.y*32.0);
    vec3 N=normalize(vNormal);
    vec3 V=normalize(vCameraPos-vWorldPos);
    if(dot(N,V)<0.0) N=-N;
    float slope=1.0-clamp(N.y,0.0,1.0);
    float macro=valueNoise(p.xz*.23);
    float grain=detailNoise(p*6.5);
    float warpedHeight=p.y+p.x*.10+valueNoise(p.xz*.28)*.65;
    float strata=.5+.5*sin(warpedHeight*5.2);
    float bed=smoothstep(.25,.78,strata);
    vec3 stone=mix(vec3(.115,.13,.14),vec3(.175,.16,.135),.35+.35*bed);
    float seams=1.0-smoothstep(.02,.12,abs(sin(warpedHeight*2.6)));
    stone*=mix(.75,1.12,grain)*(1.0-seams*.10);
    float grass=(1.0-smoothstep(.22,.48,slope))*(1.0-smoothstep(6.5,10.5,p.y));
    grass*=smoothstep(.20,.65,macro);
    vec3 meadow=mix(vec3(.035,.065,.025),vec3(.095,.135,.055),macro)*mix(.85,1.1,grain);
    vec3 albedo=mix(stone,meadow,grass);
    float scree=(1.0-grass)*(1.0-smoothstep(.18,.4,slope));
    albedo=mix(albedo,vec3(.14,.13,.11)*mix(.85,1.15,grain),scree*.5);
    float snow=smoothstep(14.5,18.5,p.y)*(1.0-smoothstep(.25,.65,slope));
    albedo=mix(albedo,vec3(.62,.66,.65),snow*.8);
    float dx=detailNoise(p*6.5+vec3(.12,0,0))-grain;
    float dz=detailNoise(p*6.5+vec3(0,0,.12))-grain;
    N=normalize(N+vec3(-dx,0,-dz)*.24);
    vec3 L=normalize(ubo.lightDirIntensity.xyz);
    float visibility=terrainShadow(vWorldPos,max(-vViewPos.z,0.0));
    float diffuse=max(dot(N,L),0.0);
    vec3 sky=ubo.ambient.rgb*mix(vec3(.55,.64,.76),vec3(1.0),N.y*.5+.5);
    vec3 color=albedo*vTint.rgb*(sky+ubo.lightColor.rgb*(.12+.88*diffuse)*visibility);
    outColor=vec4(color,1.0);
}
