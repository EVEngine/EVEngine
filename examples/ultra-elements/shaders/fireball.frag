#version 450
// High-fidelity Fireball / Inferno volume — formula adapted from
// Humpalumps/ultra-elements src/spells/fire.js (FIREBALL_GLSL + fireL).
// Push constants (declareFloat order):
// 0 time  1 size  2 temperature  3 intensity  4 soot  5 smoke
// 6 haze  7 speed  8 orbit  9 steps
// 10 px 11 py 12 pz  13 dx 14 dy 15 dz
// 16 tintR 17 tintG 18 tintB  19 mode(0 ball,1 impact,2 inferno)
// 20 phaseT  21 seed  22 uiLeft  23 viewW  24 viewH  25 height(inferno)
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Params { float p[32]; } u;

float hash(vec3 q) {
    q = fract(q * 0.3183099 + vec3(0.11, 0.27, 0.39));
    q *= 17.0;
    return fract(q.x * q.y * q.z * (q.x + q.y + q.z));
}
float noise(vec3 q) {
    vec3 i = floor(q), f = fract(q);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x),
            mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
            mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
vec3 curlApprox(vec3 q) {
    // Cheap divergence-free-ish field from offset noise samples (stand-in for curl3 tex).
    float e = 0.15;
    float n1 = noise(q + vec3(0.0, e, 0.0)) - noise(q - vec3(0.0, e, 0.0));
    float n2 = noise(q + vec3(e, 0.0, 0.0)) - noise(q - vec3(e, 0.0, 0.0));
    float n3 = noise(q + vec3(0.0, 0.0, e)) - noise(q - vec3(0.0, 0.0, e));
    return normalize(vec3(n1 - n3, n2 - n1, n3 - n2) + vec3(1e-4));
}

// Compact blackbody chromaticity (luminance-normalised), log-mapped like ultra-elements noise.js.
vec3 blackbody(float kelvin) {
    float t = clamp(kelvin, 800.0, 12000.0);
    // Piecewise CIE-ish approximation, then luminance-normalise (matches BB LUT contract).
    float x = t / 1000.0;
    vec3 c;
    if (x < 6.6) {
        c.r = 1.0;
        c.g = clamp(0.390081 * log(x) - 0.631841, 0.0, 1.0);
        c.b = x < 1.9 ? 0.0 : clamp(0.543206 * log(x - 0.85) - 0.950976, 0.0, 1.0);
    } else {
        c.r = clamp(1.292936 * pow(x - 6.0, -0.1332048), 0.0, 1.0);
        c.g = clamp(1.129890 * pow(x - 6.0, -0.0755148), 0.0, 1.0);
        c.b = 1.0;
    }
    // Nudge low-T toward filmed orange and high-T toward cool white-blue (blue-flame preset).
    if (t < 2500.0) c = mix(c, vec3(1.0, 0.55, 0.12), smoothstep(900.0, 2200.0, t) * 0.35);
    if (t > 4000.0) c = mix(c, vec3(0.55, 0.78, 1.0), smoothstep(4000.0, 6500.0, t) * 0.85);
    float y = max(1e-4, 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b);
    return c / y;
}

vec3 fireL(float T, float Tref) {
    float k = T / max(Tref, 1.0);
    k *= k;
    return blackbody(T) * (k * k * smoothstep(700.0, 1000.0, T));
}

mat3 basisFromDir(vec3 d) {
    vec3 f = normalize(d);
    vec3 up = abs(f.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 r = normalize(cross(up, f));
    vec3 uax = cross(f, r);
    // Columns map local (x,y,z) with +Y = flight direction.
    return mat3(r, f, uax);
}

// Returns emission RGB (linear), density for alpha.
vec4 sampleFireball(vec3 pw, vec3 ballPos, vec3 dir, float R, float Tcore, float intensity,
                    float sootAmt, float flow, float time, float seed, vec3 tint) {
    mat3 rot = transpose(basisFromDir(dir)); // world -> local
    float L = R * 2.6;
    vec3 q = rot * (pw - ballPos);
    float uu = -q.y / L;
    float uc = clamp(uu, 0.0, 1.0);
    float d0 = R * sqrt(max(1.0 - 0.9 * uc, 0.0)) - length(vec3(q.x, max(q.y, 0.0), q.z));
    float amp = R * (0.32 + 0.7 * uc);
    if (d0 < -amp || uu > 1.3) return vec4(0.0);
    float s = 0.34 / max(R, 0.05);
    vec3 np = vec3(q.x, q.y * mix(1.0, 0.6, uc) + flow * time, q.z) * s;
    np += curlApprox(np * 0.4 + vec3(0.0, 0.1 * time, seed)) * 0.3 + seed;
    float n1 = noise(np);
    float n2 = noise(np * 1.9 + vec3(0.37, 0.5 * s * time, 0.11));
    float detail = 0.85;
    float d = d0 + (n1 - 0.5) * 2.0 * amp + (n2 - 0.5) * 0.7 * amp * detail - 0.3 * R * uc;
    float tail = 1.0 - smoothstep(0.8, 1.25, uu);
    float body = smoothstep(0.0, 0.5 * R, d) * tail;
    float cell = mix(0.5, noise(np * 1.5 + vec3(0.0, 0.35 * s * time, 0.0)), detail);
    float fold = 1.0 - cell;
    float heat = clamp(pow(clamp(d / max(R, 1e-3), 0.0, 1.0), 1.15) * 1.9 * (1.0 - 0.55 * uc)
                     + 0.2 * (n2 - 0.5) + 0.45 * (cell - 0.5), 0.0, 1.0);
    float T = mix(760.0, Tcore * 1.45, heat);
    float sf = body * (1.0 + 4.0 * cell * cell) / max(R, 0.05);
    float soot = smoothstep(-0.3 * R, 0.2 * R, d) * (1.0 - smoothstep(0.0, 0.45, heat))
               * max(smoothstep(0.42, 0.75, n2), 0.7 * fold * fold)
               * smoothstep(-0.25, 0.7, uu) * tail;
    float ss = soot * sootAmt * 8.0 / max(R, 0.05);
    float sigma = sf + ss;
    vec3 emission = sf * fireL(T, Tcore) * tint * intensity;
    // Smoke albedo contribution as darkening / soft glow cancel.
    emission *= mix(1.0, 0.55, clamp(ss / max(sigma, 1e-4), 0.0, 1.0));
    return vec4(emission, sigma);
}

vec4 sampleInferno(vec3 pw, vec3 base, float height, float radius, float Tcore, float intensity,
                   float sootAmt, float time, float seed, vec3 tint) {
    vec3 q = pw - base;
    float h = clamp(q.y / max(height, 0.1), 0.0, 1.0);
    float ang = atan(q.z, q.x) + time * 2.2 + h * 6.0;
    vec2 xz = q.xz;
    float r = length(xz);
    float swirl = 0.55 + 0.45 * sin(ang * 3.0 + noise(vec3(xz * 1.4, h * 4.0 - time)));
    float rad = radius * (0.85 + 1.1 * (1.0 - h)) * swirl;
    float shell = exp(-pow((r - rad * 0.45) / max(0.18, rad * 0.55), 2.0));
    float core = exp(-r * r / max(0.08, rad * rad * 0.45));
    vec3 np = vec3(xz * 1.6 + vec2(sin(ang), cos(ang)) * 0.55, h * 2.4 - time * 1.6 + seed);
    np += curlApprox(np * 0.5) * 0.25;
    float n = noise(np);
    float n2 = noise(np * 2.1 + 3.7);
    float body = (0.45 * shell + 1.05 * core) * (0.35 + 0.65 * n) * (0.7 + 0.3 * n2)
               * smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.72, 1.05, h));
    float heat = clamp(core * 1.45 + shell * 0.55 + (n - 0.5) * 0.35, 0.0, 1.0);
    float T = mix(860.0, Tcore * 1.4, heat);
    float sigma = body * (3.2 + sootAmt * 1.6) / max(radius, 0.08);
    vec3 emission = body * fireL(T, Tcore) * tint * intensity * 1.35;
    return vec4(emission, sigma);
}

vec4 sampleImpact(vec3 pw, vec3 hit, float age, float R, float Tcore, float intensity,
                  float smokeAmt, float time, float seed, vec3 tint) {
    vec3 q = pw - hit;
    float rad = length(q);
    float grow = mix(0.55, 3.2, smoothstep(0.0, 0.65, age)) * R;
    float shell = exp(-pow((rad - grow * 0.55) / max(0.22, grow * 0.42), 2.0));
    float core = exp(-rad * rad / max(0.08, grow * grow * 0.18));
    vec3 np = q * (1.5 / max(R, 0.2)) + vec3(0.0, -time * 0.95, seed);
    np += curlApprox(np * 0.45) * 0.3;
    float billow = noise(np);
    float billow2 = noise(np * 2.0 + 1.7);
    float cool = smoothstep(0.0, 1.6, age);
    float heat = clamp((1.0 - cool * 0.85) * (shell * 1.2 + core * 1.1 + billow * 0.45), 0.0, 1.0);
    float T = mix(780.0, Tcore * 1.25, heat);
    float sigma = (shell * (1.6 - 0.55 * cool) + core * (1.1 - 0.7 * cool)
                 + smokeAmt * billow * cool * 1.1) / max(R, 0.12);
    vec3 emission = (shell * 0.85 + core * 1.25) * (1.0 - cool * 0.75)
                  * fireL(T, Tcore) * tint * intensity * (0.7 + 0.3 * billow2);
    // Rising dark smoke after the flash.
    emission += vec3(0.035, 0.03, 0.028) * smokeAmt * billow * cool * 0.35;
    return vec4(emission, max(sigma, 0.0));
}

void main() {
    float uiLeft = u.p[22];
    float viewW = max(u.p[23], 1.0);
    float viewH = max(u.p[24], 1.0);
    vec2 uv = (gl_FragCoord.xy - vec2(uiLeft, 0.0)) / vec2(viewW, viewH);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        outColor = vec4(0.0);
        return;
    }
    vec2 xy = (uv * 2.0 - 1.0) * vec2(viewW / viewH, -1.0);

    float angle = u.p[8];
    vec3 target = vec3(1.4, 1.15, 0.0);
    vec3 ro = target + vec3(sin(angle) * 12.5, 5.8, cos(angle) * 12.5);
    vec3 ww = normalize(target - ro);
    vec3 right = normalize(cross(ww, vec3(0.0, 1.0, 0.0)));
    vec3 upv = cross(right, ww);
    vec3 rd = normalize(ww * 1.85 + right * xy.x + upv * xy.y);

    // Arena backdrop (matches lab palette).
    vec3 sky = mix(vec3(0.04, 0.06, 0.10), vec3(0.012, 0.018, 0.03), clamp(rd.y * 1.4 + 0.4, 0.0, 1.0));
    float ground = rd.y < 0.0 ? -ro.y / rd.y : 200.0;
    if (ground < 80.0) {
        vec3 gp = ro + rd * ground;
        float disc = length(gp.xz - vec2(1.5, 0.0));
        float plate = smoothstep(8.8, 7.2, disc);
        vec3 floorC = mix(vec3(0.02, 0.03, 0.05), vec3(0.06, 0.09, 0.12), plate);
        floorC *= 1.0 - 0.25 * exp(-disc * disc / 40.0);
        sky = mix(sky, floorC, 1.0 - exp(-ground * 0.02));
        // Purple dummy marker.
        float dmy = length((gp.xz - vec2(3.6, 0.15)) / vec2(0.45, 0.45));
        if (dmy < 1.0 && ground < 3.2) sky = mix(sky, vec3(0.45, 0.12, 0.65), 0.35 * (1.0 - dmy));
    }

    vec3 ballPos = vec3(u.p[10], u.p[11], u.p[12]);
    vec3 dir = normalize(vec3(u.p[13], u.p[14], u.p[15]) + vec3(1e-4));
    vec3 tint = max(vec3(u.p[16], u.p[17], u.p[18]), vec3(0.05));
    // Upstream placeBall uses R = 0.4 * size for the formed ball.
    float R = max(0.14, 0.42 * u.p[1]);
    float Tcore = max(900.0, u.p[2]);
    float intensity = max(0.05, u.p[3]) * 4.5; // emission gain ≈ FluidVolume look.emission
    float sootAmt = u.p[4];
    float smokeAmt = u.p[5];
    float flow = 2.8 + u.p[7] * 0.12;
    float time = u.p[0];
    float seed = u.p[21];
    float mode = u.p[19];
    float phaseT = u.p[20];
    float height = max(1.2, 3.2 * u.p[25] * max(u.p[1], 0.5));

    int steps = int(clamp(u.p[9], 48.0, 128.0));
    float start = 3.5;
    float finish = min(30.0, ground);
    float ds = max(0.0, finish - start) / float(steps);
    float trans = 1.0;
    vec3 color = vec3(0.0);
    float jitter = hash(vec3(gl_FragCoord.xy, time * 0.01));

    for (int i = 0; i < 128; ++i) {
        if (i >= steps || trans < 0.01) break;
        vec3 pw = ro + rd * (start + (float(i) + jitter) * ds);
        vec4 s = vec4(0.0);
        if (mode < 0.5) {
            s = sampleFireball(pw, ballPos, dir, R, Tcore, intensity, sootAmt, flow, time, seed, tint);
        } else if (mode < 1.5) {
            s = sampleImpact(pw, ballPos, phaseT, R, Tcore, intensity, smokeAmt, time, seed, tint);
        } else {
            s = sampleInferno(pw, vec3(ballPos.x, 0.0, ballPos.z), height, R * 0.85, Tcore, intensity, sootAmt, time, seed, tint);
        }
        if (s.a < 0.01) continue;
        // Volume integrator (Volumes.js): T * E * (1 - exp(-sigma dt)) / sigma
        float sigma = s.a;
        float absorb = exp(-sigma * ds);
        vec3 emit = s.rgb * (1.0 - absorb) / max(sigma, 1e-3);
        color += trans * emit;
        trans *= absorb;
    }

    // Soft heat haze as chromatic wobble when haze > 0 (screen-space stand-in).
    float haze = u.p[6];
    if (haze > 0.01) {
        float wob = (noise(vec3(uv * 8.0, time * 1.7)) - 0.5) * 0.015 * haze;
        color.r = mix(color.r, color.g, abs(wob) * 2.0);
    }

    color += trans * sky;
    // Night-arena exposure (~dusk×10 feel from upstream) then soft filmic roll-off.
    color *= 6.5;
    color = color / (1.0 + color * 0.55);
    color *= 1.0 - 0.10 * dot(uv - 0.5, uv - 0.5);
    color = pow(max(color, vec3(0.0)), vec3(0.90));
    outColor = vec4(color, 1.0);
}
