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
void main() {
    if (dryInterior(world)) discard;
    vec3 N = normalize(gl_FrontFacing ? normal : -normal);
    vec3 V = normalize(ubo.cameraPos.xyz - world);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 R = reflect(-V, N);
    float fresnel = 0.02037 + 0.97963 * pow(1.0 - max(dot(N,V), 0.0), 5.0);
    float foam = clamp(max(surface.x, (0.72 - surface.y) * 2.5), 0.0, 1.0);
    float roughness = mix(0.12, 0.65, foam);
    vec3 reflected = textureLod(environment, R, roughness * float(max(textureQueryLevels(environment)-1,0))).rgb * ubo.lightColor.w;
    vec3 water = vec3(0.006, 0.055, 0.075) * (0.6 + 0.4 * max(N.y,0.0));
    float sun = pow(max(dot(N,normalize(V+L)),0.0), mix(420.0,20.0,roughness));
    vec3 lit = mix(water, reflected, fresnel) + ubo.lightColor.rgb * sun * 2.0 * max(dot(N,L),0.0);
    vec3 foamLight = vec3(0.83,0.91,0.91) * (0.4 + 0.6 * max(dot(N,L),0.0));
    color = vec4(mix(lit, foamLight, foam) * ubo.tint.rgb, 1.0);
}
