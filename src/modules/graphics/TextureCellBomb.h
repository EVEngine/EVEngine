#pragma once

#include <cmath>
#include <cstdint>

namespace eve::graphics {

/**
 * CPU reference for texture cell-bombing math (matches tex_cell_bomb.glsl).
 * Used by unit tests; the GPU path lives in mesh3d*.frag.
 */
struct TexCellBombParams {
    float cellScale = 4.f;  // cells per UV unit
    float strength = 0.f;   // 0 = off (plain UV)
    float rotAmount = 1.f;  // 0..1 rotation scale
};

inline void texBombHash22(float x, float y, float &ox, float &oy) {
    float px = x * 0.1031f;
    float py = y * 0.1030f;
    float pz = x * 0.0973f;
    px -= std::floor(px);
    py -= std::floor(py);
    pz -= std::floor(pz);
    float d = px * (py + 33.33f) + py * (pz + 33.33f) + pz * (px + 33.33f);
    px += d;
    py += d;
    pz += d;
    ox = (px + py) * pz;
    oy = (px + pz) * py;
    ox -= std::floor(ox);
    oy -= std::floor(oy);
}

/** Map UV → bombed sample UV for the dominant (floor) cell; strength≤0 → identity. */
inline void texCellBombSampleUV(float u, float v, const TexCellBombParams &p, float &ou,
                                float &ov) {
    if (p.strength < 1e-4f) {
        ou = u;
        ov = v;
        return;
    }
    const float scale = p.cellScale < 1e-3f ? 1e-3f : p.cellScale;
    const float cx = std::floor(u * scale);
    const float cy = std::floor(v * scale);
    float rndx = 0.f, rndy = 0.f;
    texBombHash22(cx, cy, rndx, rndy);
    float rndBx = 0.f, rndBy = 0.f;
    texBombHash22(cx + 19.f, cy + 47.f, rndBx, rndBy);
    const float ox = (rndx * 2.f - 1.f) * (p.strength / scale);
    const float oy = (rndy * 2.f - 1.f) * (p.strength / scale);
    float rot = p.rotAmount;
    if (rot < 0.f) rot = 0.f;
    if (rot > 1.f) rot = 1.f;
    const float ang = (rndBx * 2.f - 1.f) * 3.14159265f * rot * p.strength;
    const float s = std::sin(ang);
    const float c = std::cos(ang);
    const float cellCenterU = (cx + 0.5f) / scale;
    const float cellCenterV = (cy + 0.5f) / scale;
    const float lu = u - cellCenterU;
    const float lv = v - cellCenterV;
    ou = cellCenterU + (c * lu - s * lv) + ox;
    ov = cellCenterV + (s * lu + c * lv) + oy;
}

}  // namespace eve::graphics
