#version 450
struct Light3D{vec4 posRadius;vec4 color;};
layout(set=0,binding=0,std140) uniform Frame{mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;vec4 cameraPos;vec4 ambient;Light3D lights[8];vec4 texBomb;vec4 parallax;mat4 view;vec4 clipInfo;vec4 cloud;vec4 cloudWind;vec4 bindlessEnv;vec4 envProbeCenter;vec4 envProbeExtent;vec4 skinInfo;vec4 reflectionProbeCenter[2];vec4 reflectionProbeExtent[2];}ubo;
layout(set=0,binding=3) uniform samplerCube environment;
layout(location=0) in vec3 normal;
layout(location=1) in vec2 surface;
layout(location=2) in vec3 world;
layout(location=0) out vec4 color;
layout(set=0,binding=2) uniform sampler2D hullMask;
float hullValue(int column, int row) {
    uvec4 bytes = uvec4(round(texelFetch(hullMask, ivec2(column,row),0)*255.0));
    return uintBitsToFloat(bytes.x | (bytes.y<<8) | (bytes.z<<16) | (bytes.w<<24));
}
bool dryInterior(vec3 point) {
    if (textureSize(hullMask,0) != ivec2(4,545)) return false;
    int count = clamp(int(hullValue(0,0)),0,32);
    for (int volume=0;volume<count;++volume) {
        int row = 1+volume*17;
        int planes = clamp(int(hullValue(0,row)),0,16);
        bool inside = planes > 0;
        for (int plane=0;plane<planes;++plane) {
            int r = row+1+plane;
            vec3 n = vec3(hullValue(0,r),hullValue(1,r),hullValue(2,r));
            if (dot(n,point)>hullValue(3,r)) { inside=false; break; }
        }
        if (inside) return true;
    }
    return false;
}
float foamNoise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    vec4 h = fract(sin(vec4(dot(cell,vec2(127.1,311.7)),
        dot(cell+vec2(1,0),vec2(127.1,311.7)),
        dot(cell+vec2(0,1),vec2(127.1,311.7)),
        dot(cell+vec2(1,1),vec2(127.1,311.7))))*43758.5453);
    return mix(mix(h.x,h.y,f.x),mix(h.z,h.w,f.x),f.y);
}
void main() {
    vec3 N = normalize(gl_FrontFacing ? normal : -normal);
    float variance = dot(dFdx(N),dFdx(N)) + dot(dFdy(N),dFdy(N));
    if (dryInterior(world)) discard;
    vec3 V = normalize(ubo.cameraPos.xyz - world);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 R = reflect(-V, N);
    float fresnel = 0.02037 + 0.97963 * pow(1.0 - max(dot(N,V), 0.0), 5.0);
    float coverage = clamp(max(surface.x, (0.72 - surface.y) * 2.5), 0.0, 1.0);
    // Break up transported coverage into foam patches; zero coverage stays clear.
    float threshold = 0.18 + 0.55 * foamNoise(world.xz * 0.4);
    float foam = smoothstep(threshold-0.15,threshold+0.15,coverage)
        * (0.55 + 0.45 * foamNoise(world.xz * 1.2));
    float roughness = mix(0.12, 0.65, foam);
    vec3 reflected = textureLod(environment, R, roughness * float(max(textureQueryLevels(environment)-1,0))).rgb * ubo.lightColor.w;
    // Directional water-body scattering makes crest slopes readable from above.
    vec3 water = vec3(0.008, 0.075, 0.095) * (0.35 + 0.65 * max(dot(N,L),0.0));
    // Filter the narrow sun lobe by the pixel's normal variance, retaining its
    // integrated energy instead of flashing individual far-field triangles white.
    float exponent = mix(160.0,24.0,foam);
    float filteredExponent = exponent / (1.0 + exponent * variance);
    float sun = (filteredExponent / exponent) * pow(max(dot(N,normalize(V+L)),0.0), filteredExponent);
    vec3 lit = mix(water, reflected, fresnel) + ubo.lightColor.rgb * sun * 0.15 * max(dot(N,L),0.0);
    vec3 foamLight = vec3(0.83,0.91,0.91) * (0.4 + 0.6 * max(dot(N,L),0.0));
    color = vec4(mix(lit, foamLight, foam) * ubo.tint.rgb, 1.0);
}
