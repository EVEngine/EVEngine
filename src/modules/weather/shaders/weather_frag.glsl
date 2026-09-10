#version 450

layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 6) in float vParticle;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

void main() {
    // Push-constant slots: 6=intensity 7=fogR 8=fogG 9=fogB 10=fogDensity.
    vec4 tex = texture(albedoSampler, vUV);
    float intensity = u.data[6];
    vec4 col = tex * vTint;
    if (u.data[12] > 1.5) {
        // A fixed shallow arc. A short illuminated section travels downwind.
        float t = vUV.y;
        float center = 0.5 + (0.045 + vParticle * 0.035) * sin(t * 3.141593);
        float taper = smoothstep(0.0, 0.08, t) * (1.0 - smoothstep(0.92, 1.0, t));
        float head = fract(u.data[0] * 0.22 + vParticle * 7.13) * 1.5 - 0.25;
        float along = 1.0 - t;
        float light = smoothstep(head - 0.32, head - 0.12, along)
                    * (1.0 - smoothstep(head - 0.035, head + 0.025, along));
        float halfWidth = 0.006;
        float aa = max(fwidth(vUV.x - center), 0.001);
        float ribbon = 1.0 - smoothstep(max(halfWidth - aa, 0.0), halfWidth + aa, abs(vUV.x - center));
        col = vec4(vec3(0.94, 0.98, 1.0) * vTint.rgb, ribbon * taper * light * vTint.a * 0.16);
    }
    // Select whole particles to vary density independently of edge coverage.
    if (intensity <= 0.0 || vParticle >= intensity) discard;
    // Coverage transparency for the custom opaque pipeline; preserve soft
    // texture edges instead of promoting every surviving texel to solid white.
    float coverage = sqrt(max(col.a, 0.0)) * (u.data[12] > 1.5 ? 0.88 : 0.95);
    float threshold = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
    if (coverage <= threshold) discard;

    float viewDist = length(vViewPos);
    float fogAmt = clamp(1.0 - exp(-viewDist * u.data[10]), 0.0, 1.0);
    vec3 fogCol = vec3(u.data[7], u.data[8], u.data[9]);
    float glint = 0.72 + 0.28 * pow(max(0.0, 1.0 - abs(vUV.x - 0.5) * 2.0), 2.0);
    vec3 rgb = mix(col.rgb * glint, fogCol, fogAmt);

    // Coverage is represented by surviving samples in the opaque pipeline.
    outColor = vec4(rgb, 1.0);
}
