#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp; mat4 model;
    vec4 lightDirIntensity; vec4 lightColor; vec4 tint; vec4 cameraPos;
    vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo;
} ubo;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;
layout(location = 6) out float vParticle;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    // 12 = particle kind: rain 0, snow 1, wind 2.
    float kind = u.data[12];
    float phase = hash12(inPos.xz * 0.5 + vec2(floor(inPos.y * 0.7), 13.0));
    float speed = max(u.data[3] * inNormal.z, 0.01);
    vec3 velocity = vec3(u.data[1], -speed, u.data[2]);
    vec3 base = inPos + vec3(u.data[13], -speed * u.data[0], u.data[14]);
    if (kind > 1.5) base = inPos; // Wind strands stay anchored; only their light moves.
    base.xz = mod(base.xz - ubo.cameraPos.xz + 26.0, 52.0) - 26.0 + ubo.cameraPos.xz;
    base.y = mod(base.y - ubo.cameraPos.y + 10.0, 20.0) - 10.0 + ubo.cameraPos.y;
    if (kind > 0.5 && kind < 1.5) {
        float flutter = u.data[0] * 1.7 + phase * 6.283185;
        base.x += sin(flutter) * 0.32;
        base.z += cos(flutter * 0.73) * 0.24;
    }
    vec3 cameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 cameraUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    vec3 up = -normalize(velocity);
    vec3 sight = ubo.cameraPos.xyz - base;
    vec3 side = cross(up, sight);
    vec3 right = dot(side, side) > 0.0001 ? normalize(side) : cameraRight;
    if (kind > 0.5 && kind < 1.5) {
        // Snow faces the view plane, independent of its fall direction.
        float angle = phase * 6.283185 + u.data[0] * 0.35;
        right = cameraRight * cos(angle) + cameraUp * sin(angle);
        up = -cameraRight * sin(angle) + cameraUp * cos(angle);
    }
    float size = inNormal.x;
    float widthScale = kind > 0.5 && kind < 1.5 ? size : inNormal.y;
    vec3 pos = base + right * inUV.x * u.data[5] * widthScale
                    + up * (inUV.y - 0.5) * u.data[4] * size;
    vec4 world = ubo.model * vec4(pos, 1.0);
    gl_Position = ubo.mvp * vec4(pos, 1.0);
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    // Geometry offsets and texture coordinates have different domains.
    vUV = vec2(inUV.x + 0.5, inUV.y);
    float distanceFade = smoothstep(1.0, 3.0, length(sight));
    float edgeFade = 1.0 - smoothstep(20.0, 26.0, max(abs(base.x - ubo.cameraPos.x), abs(base.z - ubo.cameraPos.z)));
    vTint = ubo.tint;
    vTint.a *= distanceFade * edgeFade;
    vCameraPos = ubo.cameraPos.xyz;
    vNormal = inNormal;
    vParticle = phase;
}
