#version 450

struct Particle {
    vec4 positionVelocity;
    vec4 lifeSizeRotation;
    vec4 motion;
    vec4 accelerationNoise;
};

struct Meta {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    uint alive;
    uint spawned;
    uint killed;
    uint dropped;
};

layout(std430, set = 0, binding = 1) readonly buffer Particles {
    Particle particles[];
} state;
layout(std430, set = 0, binding = 3) readonly buffer SortedIndices {
    uint indices[];
} sortedIndices;
layout(std430, set = 0, binding = 4) readonly buffer ParticleMeta {
    Meta value;
} meta;

layout(push_constant) uniform PushConstants {
    vec4 viewportCamera;
    vec4 cameraParticle;
    vec4 sizeMode;
    vec4 colorStart;
    vec4 colorEnd;
    uvec4 flipbook;
    vec4 soft;
} pc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;
layout(location = 2) out vec2 fragSceneUv;
layout(location = 3) flat out vec3 fragSoft;

void emitBillboard(Particle particle, float rotation, vec2 extent) {
    const vec2 corners[6] = vec2[6](vec2(-0.5, -0.5), vec2(0.5, -0.5),
                                      vec2(0.5, 0.5), vec2(-0.5, -0.5),
                                      vec2(0.5, 0.5), vec2(-0.5, 0.5));
    const vec2 baseUv[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                                     vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

    float lifetime = max(particle.lifeSizeRotation.y, 1e-6);
    float age = clamp(1.0 - particle.lifeSizeRotation.x / lifetime, 0.0, 1.0);

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
    fragSceneUv = screen / pc.viewportCamera.xy;
    fragSoft = pc.soft.xyz;

    uint columns = max(pc.flipbook.x, 1u);
    uint rows = max(pc.flipbook.y, 1u);
    uint total = columns * rows;
    uint frame = uint(max(floor(particle.motion.y), 0.0)) % total;
    vec2 cell = vec2(float(frame % columns), float(frame / columns));
    fragUv = (cell + baseUv[gl_VertexIndex]) / vec2(float(columns), float(rows));
    fragColor = mix(pc.colorStart, pc.colorEnd, age);
}

void emitDegenerate() {
    gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
    fragColor = vec4(0.0);
    fragUv = vec2(0.0);
    fragSceneUv = vec2(0.0);
    fragSoft = vec3(0.0);
}

void main() {
    uint facingMode = uint(pc.sizeMode.w + 0.5);

    if (facingMode == 3u) {
        // Ribbon: connect birth-sorted neighbors into oriented quads.
        if (pc.flipbook.w == 0u || gl_InstanceIndex + 1u >= meta.value.alive) {
            emitDegenerate();
            return;
        }
        Particle previous = state.particles[sortedIndices.indices[gl_InstanceIndex]];
        Particle current = state.particles[sortedIndices.indices[gl_InstanceIndex + 1u]];
        vec2 delta = current.positionVelocity.xy - previous.positionVelocity.xy;
        float segmentLength = length(delta);
        if (segmentLength < max(pc.soft.w, 0.0)) {
            emitDegenerate();
            return;
        }

        float lifetime = max(current.lifeSizeRotation.y, 1e-6);
        float age = clamp(1.0 - current.lifeSizeRotation.x / lifetime, 0.0, 1.0);
        float scale = mix(pc.sizeMode.x, pc.sizeMode.y, age) * current.lifeSizeRotation.z;
        float thickness = pc.cameraParticle.w * scale * max(pc.sizeMode.z, 0.0);
        float rotation = atan(delta.y, delta.x);
        Particle proxy = current;
        proxy.positionVelocity.xy = (previous.positionVelocity.xy + current.positionVelocity.xy) * 0.5;
        emitBillboard(proxy, rotation, vec2(segmentLength, thickness));
        return;
    }

    uint particleIndex = gl_InstanceIndex;
    if (pc.flipbook.w != 0u) particleIndex = sortedIndices.indices[gl_InstanceIndex];
    Particle particle = state.particles[particleIndex];
    float lifetime = max(particle.lifeSizeRotation.y, 1e-6);
    float age = clamp(1.0 - particle.lifeSizeRotation.x / lifetime, 0.0, 1.0);
    float scale = mix(pc.sizeMode.x, pc.sizeMode.y, age) * particle.lifeSizeRotation.z;
    vec2 extent = pc.cameraParticle.zw * scale;

    float rotation = particle.lifeSizeRotation.w;
    if (facingMode == 1u) {
        float speed = length(particle.positionVelocity.zw);
        extent.x = max(extent.x, speed * pc.sizeMode.z);
        rotation = atan(particle.positionVelocity.w, particle.positionVelocity.z);
    } else if (facingMode == 2u) {
        rotation = uintBitsToFloat(pc.flipbook.z);
    }

    emitBillboard(particle, rotation, extent);
}
