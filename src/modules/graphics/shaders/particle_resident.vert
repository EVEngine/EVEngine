#version 450

struct Particle {
    vec4 positionVelocity;
    vec4 lifeSizeRotation;
    vec4 motion;
    vec4 accelerationNoise;
};

layout(std430, set = 0, binding = 1) readonly buffer Particles {
    Particle particles[];
} state;

layout(push_constant) uniform PushConstants {
    vec4 viewportCamera;
    vec4 cameraParticle;
    vec4 sizeMode;
    vec4 colorStart;
    vec4 colorEnd;
    uvec4 flipbook;
} pc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-0.5, -0.5), vec2(0.5, -0.5),
                                      vec2(0.5, 0.5), vec2(-0.5, -0.5),
                                      vec2(0.5, 0.5), vec2(-0.5, 0.5));
    const vec2 baseUv[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                                     vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

    Particle particle = state.particles[gl_InstanceIndex];
    float lifetime = max(particle.lifeSizeRotation.y, 1e-6);
    float age = clamp(1.0 - particle.lifeSizeRotation.x / lifetime, 0.0, 1.0);
    float scale = mix(pc.sizeMode.x, pc.sizeMode.y, age) * particle.lifeSizeRotation.z;
    vec2 extent = pc.cameraParticle.zw * scale;

    float rotation = particle.lifeSizeRotation.w;
    if (pc.sizeMode.w > 0.5) {
        float speed = length(particle.positionVelocity.zw);
        extent.x = max(extent.x, speed * pc.sizeMode.z);
        rotation = atan(particle.positionVelocity.w, particle.positionVelocity.z);
    }

    float c = cos(rotation);
    float s = sin(rotation);
    vec2 corner = corners[gl_VertexIndex] * extent;
    corner = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);

    vec2 center = particle.positionVelocity.xy;
    if (pc.cameraParticle.y > 0.5) {
        center = (center - pc.viewportCamera.zw) * pc.cameraParticle.x +
                 pc.viewportCamera.xy * 0.5;
    }
    vec2 screen = center + corner;
    vec2 ndc = vec2(screen.x / pc.viewportCamera.x * 2.0 - 1.0,
                    1.0 - screen.y / pc.viewportCamera.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);

    uint columns = max(pc.flipbook.x, 1u);
    uint rows = max(pc.flipbook.y, 1u);
    uint total = columns * rows;
    uint frame = uint(max(floor(particle.motion.y), 0.0)) % total;
    vec2 cell = vec2(float(frame % columns), float(frame / columns));
    fragUv = (cell + baseUv[gl_VertexIndex]) / vec2(float(columns), float(rows));
    fragColor = mix(pc.colorStart, pc.colorEnd, age);
}
