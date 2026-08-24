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
    // Push-constant slots: 0=time 1=windX 2=windZ 3=speed 4=length 5=width 6=intensity.
    float H = 20.0;
    float phase = hash12(inPos.xz * 0.5 + vec2(floor(inPos.y * 0.7), 13.0));
    float t = u.data[0] * u.data[3] * inNormal.z + phase * H;

    // Recycle a camera-centred precipitation volume.  mod() avoids the old
    // half-empty band caused by subtracting a wrapped offset.
    vec3 base = inPos;
    base.xz += ubo.cameraPos.xz;
    base.y = mod(inPos.y - t, H);
    float age = 1.0 - base.y / H;
    base.x += u.data[1] * age * 0.075;
    base.z += u.data[2] * age * 0.075;
    // Gentle gust sway for snow.
    float sway = sin(u.data[0] * 1.2 + phase * 6.2831) * 0.06 * u.data[4];
    base.x += sway * u.data[1];
    base.z += sway * u.data[2];

    // Align streaks to their physical velocity and face their thin dimension
    // toward the camera (screen-space rain streak billboard).
    vec3 velocity = normalize(vec3(u.data[1] * 0.11, -u.data[3], u.data[2] * 0.11));
    vec3 toCamera = normalize(ubo.cameraPos.xyz - base);
    vec3 right = normalize(cross(velocity, toCamera));
    vec3 up = -velocity;
    float particleLength = u.data[4] * inNormal.x;
    float particleWidth = u.data[5] * inNormal.y;
    vec3 pos = base + right * inUV.x * particleWidth + up * inUV.y * particleLength;

    vec4 world = ubo.model * vec4(pos, 1.0);
    gl_Position = ubo.mvp * vec4(pos, 1.0);
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    vUV = inUV;
    vTint = ubo.tint;
    vCameraPos = ubo.cameraPos.xyz;
    vNormal = inNormal;
    vParticle = phase;
}
