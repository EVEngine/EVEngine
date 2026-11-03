#version 450
// Multi-mode Ultra Elements HD volume — Fireball/Inferno + Fire Tornado, Black Flame,
// Judgment, Portal, Cyclone, Ball Lightning, Radiant Lance, Supernova, Aegis.
// Push constants (declareFloat order):
// 0 time  1 size  2 temperature  3 intensity  4 soot  5 smoke
// 6 haze  7 speed  8 orbit  9 steps
// 10 px 11 py 12 pz  13 dx 14 dy 15 dz
// 16 tintR 17 tintG 18 tintB  19 mode
// 20 phaseT  21 seed  22 uiLeft  23 viewW  24 viewH  25 height
// 26 spin  27 absorb  28 rimR  29 rimG  30 rimB  31 unused
// mode: 0 ball 1 impact 2 inferno 3 tornado 4 black-flame 5 judgment
//       6 portal 7 cyclone 8 ball-lightning 9 radiant-lance 10 supernova 11 aegis
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
    float e = 0.15;
    float n1 = noise(q + vec3(0.0, e, 0.0)) - noise(q - vec3(0.0, e, 0.0));
    float n2 = noise(q + vec3(e, 0.0, 0.0)) - noise(q - vec3(e, 0.0, 0.0));
    float n3 = noise(q + vec3(0.0, 0.0, e)) - noise(q - vec3(0.0, 0.0, e));
    return normalize(vec3(n1 - n3, n2 - n1, n3 - n2) + vec3(1e-4));
}

vec3 blackbody(float kelvin) {
    float t = clamp(kelvin, 800.0, 12000.0);
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
    return mat3(r, f, uax);
}

vec4 sampleFireball(vec3 pw, vec3 ballPos, vec3 dir, float R, float Tcore, float intensity,
                    float sootAmt, float flow, float time, float seed, vec3 tint) {
    mat3 rot = transpose(basisFromDir(dir));
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
    emission += vec3(0.035, 0.03, 0.028) * smokeAmt * billow * cool * 0.35;
    return vec4(emission, max(sigma, 0.0));
}

// Rankine-ish fire tornado: helical flame sheath + rising core.
vec4 sampleTornado(vec3 pw, vec3 base, float height, float radius, float Tcore, float intensity,
                   float sootAmt, float spin, float time, float seed, vec3 tint) {
    vec3 q = pw - base;
    float h = q.y / max(height, 0.1);
    if (h < -0.05 || h > 1.25) return vec4(0.0);
    float hh = clamp(h, 0.0, 1.0);
    float ang = atan(q.z, q.x) - time * (3.5 + spin * 2.5) - hh * 10.0 * spin;
    float r = length(q.xz);
    float foot = radius * (1.85 - 1.15 * pow(hh, 1.15));
    float helix = 0.55 + 0.45 * sin(ang * 2.0 + noise(vec3(q.xz * 1.8, hh * 3.0 - time * 1.4 + seed)) * 6.283);
    float shellR = foot * helix;
    float shell = exp(-pow((r - shellR * 0.55) / max(0.14, shellR * 0.55), 2.0));
    float core = exp(-r * r / max(0.06, foot * foot * 0.28));
    vec3 np = vec3(q.x * cos(ang) - q.z * sin(ang), q.y * 0.45 - time * 2.4, q.x * sin(ang) + q.z * cos(ang));
    np = np * (0.55 / max(radius, 0.15)) + seed;
    np += curlApprox(np * 0.55) * 0.35;
    float n1 = noise(np), n2 = noise(np * 2.2 + 1.7);
    float tongue = smoothstep(0.2, 0.75, n1 * 0.55 + n2 * 0.45 + (1.0 - hh) * 0.3);
    float body = (0.7 * shell + 1.05 * core) * tongue
               * smoothstep(0.0, 0.04, hh) * (1.0 - smoothstep(0.78, 1.15, hh));
    float heat = clamp(core * 1.5 + shell * 0.7 + (n2 - 0.5) * 0.4, 0.0, 1.0);
    float T = mix(820.0, Tcore * 1.5, heat * (1.0 - 0.45 * hh));
    float soot = sootAmt * smoothstep(0.45, 0.95, hh) * shell * 0.55;
    float sigma = (body * 4.8 + soot * 2.2) / max(radius, 0.08);
    vec3 emission = body * fireL(T, Tcore) * tint * intensity * 1.75;
    emission *= mix(1.0, 0.4, clamp(soot, 0.0, 1.0));
    return vec4(emission, sigma);
}

// Absorbing black-flame capsule with hot rim (ports MASS_GLSL silhouette).
vec4 sampleBlackFlame(vec3 pw, vec3 base, float height, float radius, float intensity,
                      float absorb, float swirl, float time, float seed, vec3 rim, vec3 coreCol) {
    vec3 q = pw - base;
    float h = q.y / max(height, 0.1);
    if (h < -0.05 || h > 1.35) return vec4(0.0);
    float rad = radius * (0.62 - 0.42 * clamp(h, 0.0, 1.0) + 0.3 * exp(-h * 6.0));
    float amp = radius * (0.28 + 0.55 * clamp(h, 0.0, 1.0));
    float d0 = rad - length(q.xz);
    if (d0 < -amp) return vec4(0.0);
    float ang = swirl * time * (1.5 - 0.8 * clamp(h, 0.0, 1.0));
    float c = cos(ang), s = sin(ang);
    vec2 xz = vec2(c * q.x - s * q.z, s * q.x + c * q.z);
    float f = 0.36 / max(radius, 0.08);
    vec3 np = vec3(xz.x, q.y * 0.4 - time * 2.2, xz.y) * f + seed;
    np += curlApprox(np * 0.45 + vec3(0.0, -0.3 * time, seed)) * 0.35;
    float n1 = noise(np), n2 = noise(np * vec3(2.2, 1.6, 2.2) + vec3(0.31, -0.7 * f * time, 0.17));
    float d = d0 + (n1 - 0.5) * 2.2 * amp + (n2 - 0.5) * 0.7 * amp;
    float top = 1.0 - smoothstep(0.55, 1.0, h + (n1 - 0.5) * 1.1 + (n2 - 0.5) * 0.35);
    float body = smoothstep(0.0, 0.3 * radius, d) * top;
    float skin = (d < 0.0 ? exp(d / (0.1 * radius)) : exp(-d / (0.02 * radius))) * top;
    if (body + skin < 2e-3) return vec4(0.0);
    float sil = 1.0 - abs(dot(normalize(q.xz + 1e-4), normalize(vec2(0.7, 0.3))));
    float root = exp(-max(q.y, 0.0) / 0.22);
    float sil2 = sil * sil;
    float burn = 2.4 * sil2 * sil2 + 0.6 * sil2 * smoothstep(0.6, 1.0, h) + 1.6 * root;
    float sb = body * 11.0 * absorb;
    float ss = skin * 0.55;
    float sigma = (sb + ss) / max(radius, 0.08);
    // Near-zero albedo absorber; emission only on the burning skin.
    vec3 L = mix(rim, coreCol, 0.55 * root) * (0.35 + 1.35 * n2 * n2) * burn * intensity;
    vec3 emission = ss * L / max(radius, 0.08);
    // Extra dark density so the column reads charcoal.
    sigma += body * 6.0 * absorb / max(radius, 0.08);
    return vec4(emission, sigma);
}

// Judgment: gold god-ray pillar + ground bloom.
vec4 sampleJudgment(vec3 pw, vec3 base, float height, float radius, float intensity,
                    float time, float seed, vec3 tint, float rays) {
    vec3 q = pw - base;
    float h = q.y / max(height, 0.2);
    if (h < -0.1 || h > 1.35) return vec4(0.0);
    float r = length(q.xz);
    float ang = atan(q.z, q.x);
    float core = exp(-r * r / max(0.05, radius * radius * 0.28));
    float shaft = exp(-pow(r / max(0.18, radius * 1.05), 2.0)) * (1.0 - smoothstep(0.92, 1.2, h));
    float spokeN = mix(5.0, 10.0, clamp(rays, 0.0, 1.0));
    float spoke = pow(0.5 + 0.5 * cos(ang * spokeN + time * 0.35 + seed), 5.0);
    float crep = shaft * (0.35 + 0.65 * spoke) * smoothstep(0.0, 0.06, h);
    float sigil = exp(-pow((r - radius * 1.1) / max(0.12, radius * 0.35), 2.0))
                * exp(-q.y * q.y / 0.05) * (0.4 + 0.6 * spoke);
    float ground = exp(-r * r / max(0.25, radius * radius * 2.2)) * exp(-q.y * q.y / 0.1);
    float n = noise(vec3(q.xz * 1.2, h * 2.0 - time * 0.5 + seed));
    float body = (0.85 * core + 1.1 * crep + 0.7 * ground + 0.9 * sigil) * (0.75 + 0.25 * n);
    float sigma = body * 2.2 / max(radius, 0.1);
    vec3 gold = mix(vec3(1.0, 0.72, 0.28), tint, 0.25);
    vec3 emission = body * gold * intensity * (1.4 + 0.9 * crep + 0.6 * sigil);
    return vec4(emission, sigma);
}

// Portal / rift: vertical standing hoop (thin along +Z, faces default camera).
vec4 samplePortal(vec3 pw, vec3 center, float radius, float intensity, float time, float seed, vec3 tint) {
    vec3 q = pw - center;
    float radial = length(q.xy);
    float depth = abs(q.z);
    if (radial > radius * 1.55 || depth > radius * 0.85) return vec4(0.0);
    float rim = exp(-pow((radial - radius * 0.78) / max(0.07, radius * 0.18), 2.0));
    float hole = smoothstep(radius * 0.82, radius * 0.25, radial);
    float swirl = 0.5 + 0.5 * sin(atan(q.y, q.x) * 6.0 - time * 3.2 + seed);
    float n = noise(vec3(q.xy * 2.4, time * 0.9 + seed));
    float slab = exp(-depth * depth / max(0.04, radius * radius * 0.18));
    float body = (rim * (0.75 + 0.35 * swirl) + hole * 0.45 * n) * slab;
    float sigma = (hole * 10.0 + rim * 3.2) / max(radius, 0.1);
    vec3 emission = rim * tint * intensity * (2.2 + 1.4 * swirl) * slab;
    emission *= 1.0 - 0.9 * hole;
    return vec4(emission, sigma * max(intensity, 0.4));
}

// Pale wind cyclone funnel.
vec4 sampleCyclone(vec3 pw, vec3 base, float height, float radius, float intensity,
                   float spin, float time, float seed, vec3 tint) {
    vec3 q = pw - base;
    float h = q.y / max(height, 0.1);
    if (h < -0.05 || h > 1.2) return vec4(0.0);
    float hh = clamp(h, 0.0, 1.0);
    float ang = atan(q.z, q.x) - time * (2.8 + spin * 2.0) - hh * 8.0;
    float r = length(q.xz);
    float foot = radius * (1.05 - 0.7 * pow(hh, 1.2));
    float shell = exp(-pow((r - foot * 0.5) / max(0.1, foot * 0.55), 2.0));
    float core = exp(-r * r / max(0.06, foot * foot * 0.4));
    vec3 np = vec3(r * cos(ang), hh * 2.5 - time * 1.8, r * sin(ang)) + seed;
    float n = noise(np * 1.4);
    float body = (0.7 * shell + 0.45 * core) * (0.45 + 0.55 * n)
               * smoothstep(0.0, 0.05, hh) * (1.0 - smoothstep(0.85, 1.15, hh));
    float sigma = body * 2.4 / max(radius, 0.1);
    vec3 mist = mix(vec3(0.75, 0.88, 0.92), tint, 0.4);
    vec3 emission = body * mist * intensity * 1.1;
    return vec4(emission, sigma);
}

// Ball lightning plasma orb.
vec4 sampleBallLightning(vec3 pw, vec3 pos, float R, float intensity, float time, float seed, vec3 tint) {
    vec3 q = pw - pos;
    float rad = length(q);
    if (rad > R * 2.8) return vec4(0.0);
    float core = exp(-rad * rad / max(0.04, R * R * 0.35));
    float shell = exp(-pow((rad - R * 0.75) / max(0.08, R * 0.4), 2.0));
    vec3 np = q * (2.2 / max(R, 0.1)) + vec3(0.0, -time * 3.0, seed);
    np += curlApprox(np) * 0.4;
    float n = noise(np);
    float arc = pow(max(0.0, noise(np * 3.1 + time) - 0.55), 2.0);
    float body = (1.2 * core + 0.7 * shell) * (0.65 + 0.35 * n) + arc * 1.4;
    float sigma = body * 3.5 / max(R, 0.08);
    vec3 emission = body * mix(vec3(0.65, 0.8, 1.0), tint, 0.5) * intensity * (1.5 + 3.0 * arc);
    return vec4(emission, sigma);
}

// Radiant lance / thick light beam along dir from a to b (pos=from, dir toward target, size=width).
vec4 sampleLance(vec3 pw, vec3 from, vec3 dir, float lengthHint, float width, float intensity,
                 float time, float seed, vec3 tint) {
    vec3 f = normalize(dir + vec3(1e-4));
    vec3 q = pw - from;
    float along = dot(q, f);
    if (along < -0.2 || along > lengthHint * 1.15) return vec4(0.0);
    float u = clamp(along / max(lengthHint, 0.2), 0.0, 1.0);
    vec3 radial = q - f * along;
    float r = length(radial);
    float thick = width * (0.75 + 0.35 * sin(u * 14.0 + time * 9.0));
    float core = exp(-r * r / max(0.01, thick * thick * 0.18));
    float sheath = exp(-r * r / max(0.03, thick * thick * 0.7));
    float tip = 1.0 - smoothstep(0.85, 1.05, u);
    float n = noise(vec3(along * 1.5, r * 4.0, time * 2.0 + seed));
    float body = (1.35 * core + 0.55 * sheath) * tip * (0.8 + 0.2 * n);
    float sigma = body * 3.2 / max(width, 0.08);
    vec3 emission = body * mix(vec3(1.0, 0.92, 0.7), tint, 0.4) * intensity * 2.0;
    return vec4(emission, sigma);
}

// Supernova expanding shells.
vec4 sampleSupernova(vec3 pw, vec3 center, float age, float blast, float intensity,
                     float time, float seed, vec3 tint) {
    vec3 q = pw - center;
    float rad = length(q);
    float grow = mix(0.4, 3.4, smoothstep(0.0, 1.2, age)) * blast;
    float shell = exp(-pow((rad - grow * 0.7) / max(0.18, grow * 0.28), 2.0));
    float core = exp(-rad * rad / max(0.08, grow * grow * 0.12));
    float n = noise(q * (1.4 / max(blast, 0.2)) + vec3(0.0, -time * 0.6, seed));
    float body = shell * 1.2 + core * (1.1 - 0.5 * smoothstep(0.0, 1.5, age)) + n * shell * 0.35;
    float sigma = body * 2.6 / max(blast, 0.15);
    vec3 emission = body * mix(vec3(1.0, 0.85, 0.55), tint, 0.45) * intensity * 1.8;
    return vec4(emission, sigma);
}

// Aegis dome around caster.
vec4 sampleAegis(vec3 pw, vec3 center, float radius, float intensity, float time, float seed, vec3 tint) {
    vec3 q = pw - center;
    float rad = length(q / vec3(1.0, 0.85, 1.0));
    if (rad > radius * 1.35) return vec4(0.0);
    float shell = exp(-pow((rad - radius * 0.92) / max(0.06, radius * 0.12), 2.0));
    float hex = pow(0.5 + 0.5 * cos(atan(q.z, q.x) * 6.0 + time * 1.2), 4.0)
              * pow(0.5 + 0.5 * cos(q.y * 4.0 + seed), 3.0);
    float body = shell * (0.55 + 0.45 * hex);
    float sigma = body * 2.2 / max(radius, 0.15);
    vec3 emission = body * tint * intensity * (1.4 + 1.2 * hex);
    return vec4(emission, sigma);
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

    vec3 sky = mix(vec3(0.04, 0.06, 0.10), vec3(0.012, 0.018, 0.03), clamp(rd.y * 1.4 + 0.4, 0.0, 1.0));
    float ground = rd.y < 0.0 ? -ro.y / rd.y : 200.0;
    if (ground < 80.0) {
        vec3 gp = ro + rd * ground;
        float disc = length(gp.xz - vec2(1.5, 0.0));
        float plate = smoothstep(8.8, 7.2, disc);
        vec3 floorC = mix(vec3(0.02, 0.03, 0.05), vec3(0.06, 0.09, 0.12), plate);
        floorC *= 1.0 - 0.25 * exp(-disc * disc / 40.0);
        sky = mix(sky, floorC, 1.0 - exp(-ground * 0.02));
        float dmy = length((gp.xz - vec2(3.6, 0.15)) / vec2(0.45, 0.45));
        if (dmy < 1.0 && ground < 3.2) sky = mix(sky, vec3(0.45, 0.12, 0.65), 0.35 * (1.0 - dmy));
    }

    vec3 ballPos = vec3(u.p[10], u.p[11], u.p[12]);
    vec3 dir = normalize(vec3(u.p[13], u.p[14], u.p[15]) + vec3(1e-4));
    vec3 tint = max(vec3(u.p[16], u.p[17], u.p[18]), vec3(0.05));
    vec3 rim = max(vec3(u.p[28], u.p[29], u.p[30]), vec3(0.05));
    float R = max(0.14, 0.42 * u.p[1]);
    float Tcore = max(900.0, u.p[2]);
    float intensity = max(0.05, u.p[3]) * 4.5;
    float sootAmt = u.p[4];
    float smokeAmt = u.p[5];
    float flow = 2.8 + u.p[7] * 0.12;
    float time = u.p[0];
    float seed = u.p[21];
    float mode = u.p[19];
    float phaseT = u.p[20];
    float height = max(1.2, 3.2 * u.p[25] * max(u.p[1], 0.5));
    float spin = max(0.2, u.p[26]);
    float absorb = max(0.2, u.p[27]);

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
        } else if (mode < 2.5) {
            s = sampleInferno(pw, vec3(ballPos.x, 0.0, ballPos.z), height, R * 0.85, Tcore, intensity, sootAmt, time, seed, tint);
        } else if (mode < 3.5) {
            s = sampleTornado(pw, vec3(ballPos.x, 0.0, ballPos.z), height * 1.15, R * 1.1, Tcore, intensity, sootAmt, spin, time, seed, tint);
        } else if (mode < 4.5) {
            s = sampleBlackFlame(pw, vec3(ballPos.x, 0.0, ballPos.z), height, R * 1.05, intensity * 0.35, absorb, spin, time, seed, rim, tint);
        } else if (mode < 5.5) {
            s = sampleJudgment(pw, vec3(ballPos.x, 0.0, ballPos.z), height * 1.35, R * 1.35, intensity * 0.28, time, seed, tint, smokeAmt);
        } else if (mode < 6.5) {
            s = samplePortal(pw, ballPos, R * 1.6, intensity * 0.45, time, seed, tint);
        } else if (mode < 7.5) {
            s = sampleCyclone(pw, vec3(ballPos.x, 0.0, ballPos.z), height, R * 1.15, intensity * 0.4, spin, time, seed, tint);
        } else if (mode < 8.5) {
            s = sampleBallLightning(pw, ballPos, R * 1.15, intensity * 0.5, time, seed, tint);
        } else if (mode < 9.5) {
            float len = max(2.5, length(vec3(u.p[13], u.p[14], u.p[15])) > 0.5 ? 4.2 : 4.2);
            // from = ballPos - dir * 0 (ballPos is caster), dir toward target; length ~4.2 arena span
            s = sampleLance(pw, ballPos, dir, 4.4, R * 0.85, intensity * 0.45, time, seed, tint);
        } else if (mode < 10.5) {
            s = sampleSupernova(pw, ballPos, phaseT, max(0.6, u.p[1]), intensity * 0.5, time, seed, tint);
        } else {
            s = sampleAegis(pw, ballPos, R * 2.2, intensity * 0.4, time, seed, tint);
        }
        if (s.a < 0.01) continue;
        float sigma = s.a;
        float absorbStep = exp(-sigma * ds);
        vec3 emit = s.rgb * (1.0 - absorbStep) / max(sigma, 1e-3);
        color += trans * emit;
        trans *= absorbStep;
    }

    float haze = u.p[6];
    if (haze > 0.01) {
        float wob = (noise(vec3(uv * 8.0, time * 1.7)) - 0.5) * 0.015 * haze;
        color.r = mix(color.r, color.g, abs(wob) * 2.0);
    }

    color += trans * sky;
    color *= 6.5;
    color = color / (1.0 + color * 0.55);
    color *= 1.0 - 0.10 * dot(uv - 0.5, uv - 0.5);
    color = pow(max(color, vec3(0.0)), vec3(0.90));
    outColor = vec4(color, 1.0);
}
