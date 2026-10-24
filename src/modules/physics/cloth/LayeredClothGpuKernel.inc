#pragma once
namespace eve::physics::detail {
inline constexpr const char* layeredClothKernel = R"glsl(#version 450
layout(local_size_x = 64) in;
layout(binding = 0) readonly buffer A { vec4 a[]; };
layout(binding = 1) writeonly buffer B { vec4 b[]; };
layout(binding = 2) buffer H { vec4 history[]; };
layout(binding = 3) buffer L { vec4 links[]; };
layout(binding = 4) buffer Heads { uint heads[]; };
struct Ref {
    ivec4 cellID;
    uvec4 next;
};
layout(binding = 5) buffer Refs { Ref refs[]; };
layout(binding = 6) buffer Stats { uint stats[]; };
layout(binding = 7) buffer Output { float vertices[]; };
layout(push_constant) uniform Params { float d[32]; }
pc;
const uint NONE = 0xffffffffu;
uint       N() { return uint(pc.d[1]); }
uint       C() { return uint(pc.d[2]); }
uint       R() { return uint(pc.d[3]); }
uint       layer(uint i) { return i / (C() * R()); }
uvec3      triangle(uint id) {
    uint per = (C() - 1) * (R() - 1) * 2, sheet = id / per, local = id % per, cell = local / 2;
    uint t = sheet * C() * R() + (cell / (C() - 1)) * C() + cell % (C() - 1);
    return local % 2 == 0 ? uvec3(t, t + C(), t + 1) : uvec3(t + C(), t + C() + 1, t + 1);
}
ivec3 cellOf(vec3 p) { return ivec3(floor(p / pc.d[24])); }
uint  bucket(ivec3 c) {
    uvec3 u = uvec3(c);
    return ((u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u)) & (uint(pc.d[19]) - 1);
}
void insertRef(uint slot, ivec3 cell, uint id) {
    refs[slot].cellID = ivec4(cell, int(id));
    refs[slot].next   = uvec4(atomicExchange(heads[bucket(cell)], slot), 0, 0, 0);
}
// All broad-phase boxes include the substep trajectory. Never use end-pose boxes for CCD.
void pointBounds(uint i, out ivec3 lo, out ivec3 hi) {
    lo = cellOf(min(a[i].xyz, history[i].xyz) - vec3(pc.d[6]));
    hi = cellOf(max(a[i].xyz, history[i].xyz) + vec3(pc.d[6]));
}
void bounds(uint t, out ivec3 lo, out ivec3 hi) {
    uvec3 ids = triangle(t);
    lo        = ivec3(2147483647);
    hi        = ivec3(-2147483647);
    for (uint k = 0; k < 3; k++) {
        ivec3 l, h;
        pointBounds(ids[k], l, h);
        lo = min(lo, l);
        hi = max(hi, h);
    }
}
bool boundedCells(ivec3 lo, ivec3 hi) {
    vec3 span = vec3(hi) - vec3(lo) + 1;
    return all(greaterThan(span, vec3(0))) && all(lessThanEqual(span, vec3(64))) && span.x * span.y * span.z <= 64;
}
void insertBox(uint id, ivec3 lo, ivec3 hi) {
    if (!boundedCells(lo, hi)) {
        atomicOr(stats[1], 1);
        atomicAdd(stats[3], 1);
        return;
    }
    for (int z = lo.z; z <= hi.z; z++)
        for (int y = lo.y; y <= hi.y; y++)
            for (int x = lo.x; x <= hi.x; x++) {
                uint slot = atomicAdd(stats[0], 1);
                if (slot >= uint(pc.d[20])) {
                    atomicOr(stats[1], 1);
                    atomicAdd(stats[3], 1);
                    continue;
                }
                insertRef(slot, ivec3(x, y, z), id);
            }
}
bool allowed(uint p, uint q) {
    if (layer(p) != layer(q)) return pc.d[16] > 0.5;
    if (pc.d[18] < 0.5) return false;
    int dr = abs(int((p % (C() * R())) / C()) - int((q % (C() * R())) / C()));
    int dc = abs(int(p % C()) - int(q % C()));
    return dr > 2 || dc > 2;
}
vec3 at(uint i, float t) { return mix(history[i].xyz, a[i].xyz, t); }
// A unique triangle owns each topological edge, including boundary edges.
uint edgeOwner(uvec2 e) {
    uint low = min(e.x, e.y), high = max(e.x, e.y), r = (low % (C() * R())) / C(), c = low % C();
    uint sheet = layer(low) * (C() - 1) * (R() - 1) * 2;
    if (high / C() == low / C())
        return sheet + (r < R() - 1 ? (r * (C() - 1) + c) * 2 : ((r - 1) * (C() - 1) + c) * 2 + 1);
    if (high - low == C()) return sheet + (c < C() - 1 ? (r * (C() - 1) + c) * 2 : (r * (C() - 1) + c - 1) * 2 + 1);
    return sheet + (r * (C() - 1) + c - 1) * 2;
}
uvec2 triangleEdge(uint t, uint k) {
    uvec3 v = triangle(t);
    return uvec2(v[k], v[(k + 1) % 3]);
}
void edgeBounds(uvec2 e, out ivec3 lo, out ivec3 hi) {
    ivec3 l, h;
    pointBounds(e.x, lo, hi);
    pointBounds(e.y, l, h);
    lo = min(lo, l);
    hi = max(hi, h);
}
vec3 closestWeights(vec3 p, vec3 x, vec3 y, vec3 z) {
    vec3  ab = y - x, ac = z - x, ap = p - x;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return vec3(1, 0, 0);
    vec3  bp = p - y;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return vec3(0, 1, 0);
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / max(d1 - d3, 1e-20);
        return vec3(1 - v, v, 0);
    }
    vec3  cp = p - z;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return vec3(0, 0, 1);
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / max(d2 - d6, 1e-20);
        return vec3(1 - w, 0, w);
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        float w = (d4 - d3) / max(d4 - d3 + d5 - d6, 1e-20);
        return vec3(0, 1 - w, w);
    }
    float det = va + vb + vc;
    if (abs(det) < 1e-20) return vec3(1, 0, 0);
    return vec3(1 - (vb + vc) / det, vb / det, vc / det);
}
// Conservative advancement uses a Lipschitz bound on relative vertex displacement.
// The iteration cap returns a conservative contact instead of silently missing a crossing.
// This is tolerance-bounded float CCD, not an exact arithmetic intersection predicate.
float pointSample(uint point, uvec3 ids, float time, out vec3 weights, out vec3 delta) {
    vec3 x = at(ids.x, time), y = at(ids.y, time), z = at(ids.z, time), p = at(point, time);
    weights = closestWeights(p, x, y, z);
    delta   = p - x * weights.x - y * weights.y - z * weights.z;
    return length(delta);
}
bool contact(uint point, uint t, out vec3 weights, out vec3 impulse) {
    uvec3 ids = triangle(t);
    for (uint k = 0; k < 3; k++)
        if (!allowed(point, ids[k])) return false;
    vec3  velocity = a[point].xyz - history[point].xyz;
    float speed    = 0;
    for (uint k = 0; k < 3; k++) speed = max(speed, length(velocity - (a[ids[k]].xyz - history[ids[k]].xyz)));
    float h = pc.d[6], tolerance = max(h * 1e-4, 1e-7), time = 0;
    vec3  delta;
    float dist = pointSample(point, ids, 0, weights, delta);
    // Retain the starting side even if a contact-shell point crosses this substep.
    if (dist > h + tolerance) {
        for (int step = 0; step < 64; step++) {
            if (dist <= h + tolerance) break;
            if (speed < 1e-12) return false;
            float advance = 0.9 * (dist - h) / speed;
            if (time + advance > 1) return false;
            time += advance;
            dist = pointSample(point, ids, time, weights, delta);
        }
    }
    vec3 normal;
    if (dist > 1e-8)
        normal = delta / dist;
    else {
        normal = cross(at(ids.y, time) - at(ids.x, time), at(ids.z, time) - at(ids.x, time));
        if (dot(normal, normal) < 1e-20) return false;
        normal     = normalize(normal);
        vec3 oldcp = vec3(0);
        for (uint k = 0; k < 3; k++) oldcp += history[ids[k]].xyz * weights[k];
        float side = dot(history[point].xyz - oldcp, normal);
        if (abs(side) < 1e-8) side = layer(point) > layer(ids.x) ? 1 : -1;
        normal *= side >= 0 ? 1 : -1;
    }
    vec3  separation = a[point].xyz, rel = velocity;
    float denom = a[point].w;
    for (uint k = 0; k < 3; k++) {
        separation -= a[ids[k]].xyz * weights[k];
        rel -= (a[ids[k]].xyz - history[ids[k]].xyz) * weights[k];
        denom += a[ids[k]].w * weights[k] * weights[k];
    }
    float depth = h - dot(separation, normal);
    if (depth <= 0 || denom < 1e-12) return false;
    vec3 tangent = rel - normal * dot(rel, normal);
    tangent *= min(1.0, pc.d[17] * depth / max(length(tangent), 1e-8));
    impulse = (normal * depth - tangent) / denom;
    return true;
}
vec2 segmentWeights(vec3 x, vec3 y, vec3 u, vec3 v) {
    vec3  d = y - x, e = v - u, r = x - u;
    float aa = dot(d, d), bb = dot(d, e), cc = dot(e, e), dd = dot(d, r), ee = dot(e, r);
    float s = 0, t = 0;
    if (aa <= 1e-20)
        t = cc > 1e-20 ? clamp(ee / cc, 0, 1) : 0;
    else if (cc <= 1e-20)
        s = clamp(-dd / aa, 0, 1);
    else {
        float det = aa * cc - bb * bb;
        s         = det > 1e-20 ? clamp((bb * ee - cc * dd) / det, 0, 1) : 0;
        t         = (bb * s + ee) / cc;
        if (t < 0) {
            t = 0;
            s = clamp(-dd / aa, 0, 1);
        } else if (t > 1) {
            t = 1;
            s = clamp((bb - dd) / aa, 0, 1);
        }
    }
    return vec2(s, t);
}
float edgeSample(uvec2 e, uvec2 f, float time, out vec2 weights, out vec3 delta) {
    vec3 x = at(e.x, time), y = at(e.y, time), u = at(f.x, time), v = at(f.y, time);
    weights = segmentWeights(x, y, u, v);
    delta   = mix(x, y, weights.x) - mix(u, v, weights.y);
    return length(delta);
}
bool orderedEdgeContact(uvec2 e, uvec2 f, out vec2 weights, out vec3 impulse) {
    for (uint j = 0; j < 2; j++)
        for (uint k = 0; k < 2; k++)
            if (!allowed(e[j], f[k])) return false;
    float speed = 0;
    for (uint j = 0; j < 2; j++)
        for (uint k = 0; k < 2; k++)
            speed = max(speed, length((a[e[j]].xyz - history[e[j]].xyz) - (a[f[k]].xyz - history[f[k]].xyz)));
    float h = pc.d[6], tolerance = max(h * 1e-4, 1e-7), time = 0;
    vec3  delta;
    float dist = edgeSample(e, f, 0, weights, delta);
    if (dist > h + tolerance)
        for (int step = 0; step < 64; step++) {
            if (dist <= h + tolerance) break;
            if (speed < 1e-12) return false;
            float advance = 0.9 * (dist - h) / speed;
            if (time + advance > 1) return false;
            time += advance;
            dist = edgeSample(e, f, time, weights, delta);
        }
    vec3 normal;
    if (dist > 1e-8)
        normal = delta / dist;
    else {
        normal =
            mix(history[e.x].xyz, history[e.y].xyz, weights.x) - mix(history[f.x].xyz, history[f.y].xyz, weights.y);
        if (dot(normal, normal) < 1e-20) normal = cross(at(e.y, time) - at(e.x, time), at(f.y, time) - at(f.x, time));
        if (dot(normal, normal) < 1e-20) return false;
        normal = normalize(normal);
    }
    vec2  ew = vec2(1 - weights.x, weights.x), fw = vec2(1 - weights.y, weights.y);
    float denom      = 0;
    vec3  separation = vec3(0), rel = vec3(0);
    for (uint k = 0; k < 2; k++) {
        denom += a[e[k]].w * ew[k] * ew[k] + a[f[k]].w * fw[k] * fw[k];
        separation += a[e[k]].xyz * ew[k] - a[f[k]].xyz * fw[k];
        rel += (a[e[k]].xyz - history[e[k]].xyz) * ew[k] - (a[f[k]].xyz - history[f[k]].xyz) * fw[k];
    }
    float depth = h - dot(separation, normal);
    if (depth <= 0 || denom < 1e-12) return false;
    vec3 tangent = rel - normal * dot(rel, normal);
    tangent *= min(1.0, pc.d[17] * depth / max(length(tangent), 1e-8));
    impulse = (normal * depth - tangent) / denom;
    return true;
}
// Both endpoint gathers evaluate the same ordered pair. This also fixes the
// closest-point ambiguity for parallel/degenerate segments and keeps reactions paired.
bool edgeContact(uvec2 e, uvec2 f, out vec2 weights, out vec3 impulse) {
    if (min(e.x, e.y) < min(f.x, f.y)) return orderedEdgeContact(e, f, weights, impulse);
    bool hit = orderedEdgeContact(f, e, weights, impulse);
    weights  = weights.yx;
    impulse  = -impulse;
    return hit;
}
// Block row-sum bound in mass-scaled coordinates. Every participant in a
// constraint uses the same denominator; one global scale preserves paired reactions.
float pointRow(uint point, uint t, vec3 w, float ownMass, float coefficient) {
    uvec3 ids   = triangle(t);
    float denom = a[point].w, total = sqrt(a[point].w);
    for (uint k = 0; k < 3; k++) {
        denom += a[ids[k]].w * w[k] * w[k];
        total += sqrt(a[ids[k]].w) * abs(w[k]);
    }
    return sqrt(ownMass) * abs(coefficient) * total / max(denom, 1e-12);
}
float edgeRow(uint i, uvec2 e, uvec2 f, vec2 w) {
    vec2  ew = vec2(1 - w.x, w.x), fw = vec2(1 - w.y, w.y);
    float denom = 0, total = 0;
    for (uint k = 0; k < 2; k++) {
        denom += a[e[k]].w * ew[k] * ew[k] + a[f[k]].w * fw[k] * fw[k];
        total += sqrt(a[e[k]].w) * ew[k] + sqrt(a[f[k]].w) * fw[k];
    }
    return sqrt(a[i].w) * (i == e.x ? ew.x : ew.y) * total / max(denom, 1e-12);
}
vec4 outgoing(uint i) {
    vec4 sum = vec4(0);
    vec3 w, j;
    if (stats[1] != 0) {
        for (uint t = 0; t < uint(pc.d[21]); t++)
            if (contact(i, t, w, j)) {
                sum += vec4(j * a[i].w, pointRow(i, t, w, a[i].w, 1));
                atomicAdd(stats[2], 1);
            }
    } else {
        ivec3 lo, hi;
        pointBounds(i, lo, hi);
        for (int z = lo.z; z <= hi.z; z++)
            for (int y = lo.y; y <= hi.y; y++)
                for (int x = lo.x; x <= hi.x; x++) {
                    ivec3 cell = ivec3(x, y, z);
                    uint  slot = heads[bucket(cell)];
                    while (slot != NONE) {
                        ivec4 ref = refs[slot].cellID;
                        ivec3 tl, th;
                        if (uint(ref.w) >= N())
                            bounds(uint(ref.w) - N(), tl, th);
                        else
                            tl = lo;
                        if (all(equal(ref.xyz, cell)) && uint(ref.w) >= N() && all(equal(cell, max(lo, tl))) &&
                            contact(i, uint(ref.w) - N(), w, j)) {
                            sum += vec4(j * a[i].w, pointRow(i, uint(ref.w) - N(), w, a[i].w, 1));
                            atomicAdd(stats[2], 1);
                        }
                        slot = refs[slot].next.x;
                    }
                }
    }
    return sum;
}
vec4 incomingTriangle(uint i, uint t) {
    uvec3 ids    = triangle(t);
    int   corner = ids.x == i ? 0 : (ids.y == i ? 1 : (ids.z == i ? 2 : -1));
    if (corner < 0) return vec4(0);
    vec4  sum = vec4(0);
    vec3  w, j;
    ivec3 lo, hi;
    bounds(t, lo, hi);
    if (stats[1] != 0) {
        for (uint point = 0; point < N(); point++)
            if (contact(point, t, w, j)) sum += vec4(-j * w[corner] * a[i].w, pointRow(point, t, w, a[i].w, w[corner]));
    } else {
        for (int z = lo.z; z <= hi.z; z++)
            for (int y = lo.y; y <= hi.y; y++)
                for (int x = lo.x; x <= hi.x; x++) {
                    ivec3 cell = ivec3(x, y, z);
                    uint  slot = heads[bucket(cell)];
                    while (slot != NONE) {
                        ivec4 ref = refs[slot].cellID;
                        ivec3 pl, ph;
                        if (uint(ref.w) < N())
                            pointBounds(uint(ref.w), pl, ph);
                        else
                            pl = lo;
                        if (all(equal(ref.xyz, cell)) && uint(ref.w) < N() && all(equal(cell, max(lo, pl))) &&
                            contact(uint(ref.w), t, w, j))
                            sum += vec4(-j * w[corner] * a[i].w, pointRow(uint(ref.w), t, w, a[i].w, w[corner]));
                        slot = refs[slot].next.x;
                    }
                }
    }
    return sum;
}
vec4 edgeAgainstTriangle(uint i, uvec2 e, uint t) {
    vec4 sum = vec4(0);
    vec3 j;
    vec2 w;
    for (uint k = 0; k < 3; k++) {
        uvec2 f = triangleEdge(t, k);
        if (edgeOwner(f) != t) continue;
        if (edgeContact(e, f, w, j)) {
            sum += vec4(j * a[i].w * (i == e.x ? 1 - w.x : w.x), edgeRow(i, e, f, w));
            if (i == min(e.x, e.y) && min(e.x, e.y) < min(f.x, f.y)) atomicAdd(stats[2], 1);
        }
    }
    return sum;
}
vec4 edgeCorrection(uint i, uvec2 e) {
    vec4 sum = vec4(0);
    if (stats[1] != 0) {
        for (uint t = 0; t < uint(pc.d[21]); t++) sum += edgeAgainstTriangle(i, e, t);
    } else {
        ivec3 lo, hi;
        edgeBounds(e, lo, hi);
        // Edge boxes can cover more cells than either endpoint or a triangle.
        if (!boundedCells(lo, hi)) {
            for (uint t = 0; t < uint(pc.d[21]); t++) sum += edgeAgainstTriangle(i, e, t);
        } else
            for (int z = lo.z; z <= hi.z; z++)
                for (int y = lo.y; y <= hi.y; y++)
                    for (int x = lo.x; x <= hi.x; x++) {
                        ivec3 cell = ivec3(x, y, z);
                        uint  slot = heads[bucket(cell)];
                        while (slot != NONE) {
                            ivec4 ref = refs[slot].cellID;
                            if (uint(ref.w) >= N() && all(equal(ref.xyz, cell))) {
                                uint  t = uint(ref.w) - N();
                                ivec3 tl, th;
                                bounds(t, tl, th);
                                if (all(equal(cell, max(lo, tl)))) sum += edgeAgainstTriangle(i, e, t);
                            }
                            slot = refs[slot].next.x;
                        }
                    }
    }
    return sum;
}
void main() {
    uint i = gl_GlobalInvocationID.x, mode = uint(pc.d[7]);
    if (mode == 2) {
        if (i < uint(pc.d[19])) heads[i] = NONE;
        if (i == 0) {
            stats[0] = 0;
            stats[1] = 0;
            stats[4] = 0;
            if (pc.d[26] > 0.5) {
                stats[2] = 0;
                stats[3] = 0;
            }
        }
        return;
    }
    if (mode == 3) {
        ivec3 lo, hi;
        if (i < N()) {
            pointBounds(i, lo, hi);
            insertBox(i, lo, hi);
        }
        if (i < uint(pc.d[21])) {
            bounds(i, lo, hi);
            insertBox(N() + i, lo, hi);
        }
        return;
    }
    if (i >= N()) return;
    vec4  p  = a[i];
    float dt = pc.d[0];
    if (mode == 0) {
        vec3 old = p.xyz, v = (old - history[i].xyz) * (1.0 - pc.d[15]);
        p.w = history[i].w;
        if (p.w > 0) p.xyz += v + vec3(pc.d[9] + pc.d[12], pc.d[10] + pc.d[13], pc.d[11] + pc.d[14]) * dt * dt;
        history[i].xyz = old;
        for (uint k = 0; k < 12; k++) links[i * 12 + k].w = 0;
    } else if (mode == 1 && p.w > 0) {
        vec3 correction = vec3(0);
        for (uint k = 0; k < 12; k++) {
            uint slot = i * 12 + k;
            vec4 edge = links[slot];
            if (edge.x < 0) continue;
            vec4  q     = a[uint(edge.x)];
            vec3  delta = p.xyz - q.xyz;
            float dist  = length(delta);
            if (dist < 1e-8) continue;
            float alpha = edge.z * pc.d[22] / (dt * dt),
                  dl    = (-(dist - edge.y) - alpha * edge.w) / (p.w + q.w + alpha) * 0.2;
            links[slot].w += dl;
            correction += delta / dist * (p.w * dl);
        }
        p.xyz += correction;
        p.y = max(p.y, pc.d[27]);
    } else if (mode == 6) {
        vec4 correction = outgoing(i);
        if (p.w > 0) {
            uint local = i % (C() * R()), row = local / C(), col = local % C();
            for (int rr = int(row) - 1; rr <= int(row); rr++)
                for (int cc = int(col) - 1; cc <= int(col); cc++) {
                    if (rr < 0 || cc < 0 || rr >= int(R()) - 1 || cc >= int(C()) - 1) continue;
                    uint t = (layer(i) * (C() - 1) * (R() - 1) + uint(rr) * (C() - 1) + uint(cc)) * 2;
                    correction += incomingTriangle(i, t) + incomingTriangle(i, t + 1);
                    for (uint tri = t; tri < t + 2; tri++)
                        for (uint k = 0; k < 3; k++) {
                            uvec2 e = triangleEdge(tri, k);
                            if ((e.x == i || e.y == i) && edgeOwner(e) == tri) correction += edgeCorrection(i, e);
                        }
                }
        }
        // Reuse the vertex output as scratch before the final packing dispatch.
        // No draw can consume it during update (same-queue, pre-pass contract).
        for (uint k = 0; k < 4; k++) vertices[i * 4 + k] = correction[k];
        atomicMax(stats[4], floatBitsToUint(correction.w));
        return;
    } else if (mode == 4) {
        if (p.w > 0) {
            float rowMax = uintBitsToFloat(stats[4]);
            float scale  = min(pc.d[23], 0.9 / (max(1.0, rowMax) * (1.0 + pc.d[17])));
            p.xyz += vec3(vertices[i * 4], vertices[i * 4 + 1], vertices[i * 4 + 2]) * scale;
            p.y = max(p.y, pc.d[27]);
        }
    } else if (mode == 5) {
        uint local = i % (C() * R()), row = local / C(), col = local % C();
        uint left = col > 0 ? i - 1 : i, right = col + 1 < C() ? i + 1 : i, up = row > 0 ? i - C() : i,
             down   = row + 1 < R() ? i + C() : i;
        vec3 normal = cross(a[down].xyz - a[up].xyz, a[right].xyz - a[left].xyz);
        normal      = dot(normal, normal) > 1e-12 ? normalize(normal) : vec3(0, 1, 0);
        float t     = pc.d[4] > 1 ? float(layer(i)) / (pc.d[4] - 1) : 0;
        for (uint side = 0; side < 2; side++) {
            uint base = (i + side * N()) * 22;
            for (uint k = 0; k < 22; k++) vertices[base + k] = 0;
            for (uint k = 0; k < 3; k++) {
                vertices[base + k]     = p[k];
                vertices[base + 3 + k] = normal[k] * (side == 0 ? 1 : -1);
            }
            vertices[base + 6]  = float(col) / float(C() - 1);
            vertices[base + 7]  = float(row) / float(R() - 1);
            vertices[base + 14] = 0.2 + 0.7 * t;
            vertices[base + 15] = 0.65 - 0.3 * t;
            vertices[base + 16] = 0.9 - 0.65 * t;
            vertices[base + 17] = 1;
        }
        return;
    }
    b[i] = p;
}
)glsl";
}
