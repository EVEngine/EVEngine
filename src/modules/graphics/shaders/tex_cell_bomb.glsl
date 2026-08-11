// Texture cell bombing — break obvious tiling by blending randomly
// offset/rotated samples from a 2×2 cell neighborhood.
// Include with: #extension GL_GOOGLE_include_directive : enable
//               #include "tex_cell_bomb.glsl"

#ifndef TEX_CELL_BOMB_GLSL
#define TEX_CELL_BOMB_GLSL

vec2 texBombHash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec2 texBombRotate(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

/**
 * Sample `tex` with cell bombing.
 * cellScale — cells per UV unit (typical 2..16). Ignored when strength≈0.
 * strength  — 0 = plain texture(uv); 1 = full random offset + rotation.
 * rotAmount — 0..1 scales per-cell rotation (multiplied by strength).
 *
 * Each cell applies a random UV rotation about the cell center plus a random
 * offset. Pure translation of a periodic texture would look identical; rotation
 * is what breaks the repeating lattice.
 */
vec4 textureCellBomb(sampler2D tex, vec2 uv, float cellScale, float strength,
                     float rotAmount) {
    if (strength < 1e-4)
        return texture(tex, uv);

    float scale = max(cellScale, 1e-3);
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);

    vec2 p = uv * scale;
    vec2 cell = floor(p);
    vec2 f = fract(p);
    // Quintic smoothstep — softer seams than hermite.
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    vec4 acc = vec4(0.0);
    for (int j = 0; j <= 1; ++j) {
        for (int i = 0; i <= 1; ++i) {
            vec2 cij = cell + vec2(float(i), float(j));
            vec2 rnd = texBombHash22(cij);
            vec2 rndB = texBombHash22(cij + vec2(19.0, 47.0));

            // Offset in UV units (fraction of a cell width, scaled by strength).
            vec2 offset = (rnd * 2.0 - 1.0) * (strength / scale);
            float ang = (rndB.x * 2.0 - 1.0) * 3.14159265 * clamp(rotAmount, 0.0, 1.0) *
                        strength;

            // Rotate UV about this cell's center, then add offset.
            vec2 cellCenter = (cij + vec2(0.5)) / scale;
            vec2 sampleUV = cellCenter + texBombRotate(uv - cellCenter, ang) + offset;

            float wij = mix(1.0 - w.x, w.x, float(i)) * mix(1.0 - w.y, w.y, float(j));
            acc += textureGrad(tex, sampleUV, dx, dy) * wij;
        }
    }
    return acc;
}

#endif
