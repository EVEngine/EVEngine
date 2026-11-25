// eve-hot-path
// Native adaptation of pixelfx pickups/chest-open.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include <numeric>
#include "stylize/pixelfx/PickupLoot.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double               OPEN = .8, COLLECT = 2.55, POOF = 3.3, CS = .94, SN = .34, HB = 9 / CS, LA = 6, LB = 5.5;
constexpr int                  HW = 12, FH = 9;
constexpr std::array<Point, 4> RATTLES{{{.1, .4}, {.32, .62}, {.5, .8}, {.64, 1}}};
constexpr std::array<int, 2>   STRAPS{{-9, 7}};
const std::array<uint32_t, 6>  IRON{
    {hex(0x0e0d16), hex(0x1d1c28), hex(0x2f2e3e), hex(0x474658), hex(0x6a6a7e), hex(0x9898ac)}};
const std::array<std::array<uint32_t, 6>, 4> GEMS{
    {{{hex(0x2a0612), hex(0x6e0f2a), hex(0xb81c3c), hex(0xf0405a), hex(0xff8a96), hex(0xffd2d6)}},
     {{hex(0x06200f), hex(0x0f4a26), hex(0x1a8040), hex(0x34c060), hex(0x8af0a0), hex(0xd8ffe0)}},
     {{hex(0x0a1030), hex(0x142a70), hex(0x2050c0), hex(0x3a8cf0), hex(0x8ccaff), hex(0xdcf0ff)}},
     {{hex(0x1a0a2a), hex(0x3e1466), hex(0x6a28a8), hex(0xa050e0), hex(0xd09cff), hex(0xf0e0ff)}}}};
const uint32_t DUST = hex(0x5e5068);
struct LidRow {
    int    row;
    double angle, depth;
    bool   outside;
    double light;
    bool   crest = false, seam = false;
};
std::vector<LidRow> lidRows(double th) {
    std::vector<LidRow> rows;
    rows.reserve(44);
    const double ct = std::cos(th), st = std::sin(th);
    for (int i = 0; i <= 260; ++i) {
        const double angle = (double(i) / 260) * PI, dz = LA + LA * std::cos(angle), dy = LB * std::sin(angle),
                     z = -LA + dz * ct - dy * st, y = HB + dz * st + dy * ct;
        if (y < HB - .05) continue;
        double       nz = std::cos(angle) / LA, ny = std::sin(angle) / LB;
        const double nl = jsHypot2(nz, ny);
        nz /= nl;
        ny /= nl;
        const double Nz = nz * ct - ny * st, Ny = nz * st + ny * ct, v = y * CS + (LA - z) * SN, dep = z * CS + y * SN;
        const int    row = int(std::floor(-v));
        auto         old = std::find_if(rows.begin(), rows.end(), [&](const LidRow& q) { return q.row == row; });
        const LidRow q{row, angle, dep, Nz * CS + Ny * SN > 0, Ny * .95 + Nz * .3 - .2};
        if (old == rows.end())
            rows.push_back(q);
        else if (dep > old->depth)
            *old = q;
    }
    std::sort(rows.begin(), rows.end(), [](const LidRow& a, const LidRow& b) { return a.row < b.row; });
    auto hi = rows.end();
    for (auto it = rows.begin(); it != rows.end(); ++it)
        if (it->outside && (hi == rows.end() || it->light > hi->light)) hi = it;
    if (hi != rows.end() && hi->light > .5) hi->crest = true;
    for (double seam : {PI / 3, (2 * PI) / 3}) {
        auto k = rows.end();
        for (auto it = rows.begin(); it != rows.end(); ++it)
            if (k == rows.end() || std::abs(it->angle - seam) < std::abs(k->angle - seam)) k = it;
        if (k != rows.end() && std::abs(k->angle - seam) < .4) k->seam = true;
    }
    return rows;
}
uint32_t tone(std::span<const uint32_t> ramp, double light, const std::array<size_t, 5>& steps) {
    return ramp[steps[light > .6 ? 0 : light > .25 ? 1 : light > -.1 ? 2 : light > -.45 ? 3 : 4]];
}
uint32_t heat(std::span<const uint32_t> C, double k, double g) { return C[size_t(clamp(jsRound(k + g * 2.2), 0, 7))]; }
void     paintHoard(pickup::Layer<40, 44>& L, std::span<const uint32_t> C, std::span<const uint32_t> W, double g) {
    for (int y = -13; y <= -10; ++y)
        for (int x = -HW; x < HW; ++x) {
            const double h = rand({uint32_t(x), uint32_t(y), 7});
            const int    k = h < .18 ? 5 : h < .55 ? 4 : 3;
            L.set(x, y, y == -13 && std::abs(x + .5) > 9 ? W[2] : heat(C, k - (y == -13 ? 1 : 0), g));
        }
    for (int x = -9; x < 9; ++x) {
        const double top = -13 - jsRound(2.2 * std::pow(std::cos(((x + .5) / 10) * (PI / 2)), 1.4));
        for (int y = int(top); y < -13; ++y)
            L.set(x, y, heat(C, y == top ? 5 : 4 + (rand({uint32_t(x), uint32_t(y), 3}) < .3 ? 1 : 0), g));
    }
}
void paintLid(pickup::Layer<40, 44>& L, std::span<const uint32_t> C, std::span<const uint32_t> W, double th, double g) {
    const auto rows = lidRows(th);
    const int  top  = rows.empty() ? 0 : rows.front().row;
    for (const auto& r : rows)
        for (int x = -HW; x < HW; ++x) {
            const double u = r.angle, side = (x + HW + .5) / (2 * HW);
            const bool   strap = std::any_of(STRAPS.begin(), STRAPS.end(), [&](int s) { return x == s || x == s + 1; }),
                         left  = std::find(STRAPS.begin(), STRAPS.end(), x) != STRAPS.end(),
                         corner = x == -HW || x == HW - 1;
            uint32_t     c;
            if (r.outside) {
                if (r.row == top && corner && th < .6) continue;
                const double light = r.light + .2 - side * .35 - (r.row == top ? .45 : 0);
                if (u < .3 || (u < .55 && x >= -2 && x <= 1))
                    c = tone(C, light + (x >= -2 && x <= 1 ? .15 : .1), {6, 5, 5, 4, 3});
                else if (corner)
                    c = x < 0 ? IRON[4] : IRON[2];
                else if (strap)
                    c = tone(IRON, light + (left ? .3 : -.3), {5, 4, 3, 2, 1});
                else if (r.seam)
                    c = W[0];
                else {
                    c = r.crest && side < .62 ? W[6] : tone(W, light, {5, 4, 3, 2, 1});
                    if (rand({uint32_t(x >> 1), jsUint(jsRound(u * 6)), 11}) < .2) {
                        const auto index = std::find(W.begin(), W.end(), c) - W.begin();
                        c                = W[size_t(std::max<std::ptrdiff_t>(0, index - 1))];
                    }
                }
            } else {
                const double k = g * std::pow(u / PI, 1.3);
                if (u < .3)
                    c = heat(C, 1, g);
                else if (r.seam)
                    c = W[0];
                else
                    c = k > .6 ? mix(W[4], C[5], .3) : k > .35 ? W[3] : k > .15 ? W[2] : W[1];
                if (corner) c = W[0];
            }
            L.set(x, r.row, c);
        }
}
void paintFront(pickup::Layer<40, 44>& L, std::span<const uint32_t> C, std::span<const uint32_t> W, double g) {
    for (int y = -FH; y <= -1; ++y)
        for (int x = -HW; x < HW; ++x) {
            const double side = (x + HW + .5) / (2 * HW);
            uint32_t     c;
            if (y == -FH)
                c = side < .3 ? C[6] : side < .7 ? C[5] : C[4];
            else if (y == -1)
                c = W[1];
            else if (y == -5)
                c = W[0];
            else {
                const bool top = y == -8 || y == -4, bot = y == -6 || y == -2;
                c = W[top ? (side < .45 ? 4 : 3) : bot ? 2 : 3];
                if (!top && rand({uint32_t(x >> 1), uint32_t(y), 5}) < .2) c = W[bot ? 1 : 2];
            }
            if (x == -HW) c = y == -FH ? C[5] : IRON[4];
            if (x == HW - 1) c = y == -FH ? C[3] : IRON[2];
            for (int s : STRAPS) {
                if (x == s && y > -FH) c = y == -7 || y == -3 ? IRON[5] : IRON[4];
                if (x == s + 1 && y > -FH) c = IRON[2];
            }
            L.set(x, y, c);
        }
    for (int y = -FH; y <= -4; ++y)
        for (int x = -2; x <= 1; ++x) L.set(x, y, x == 1 || y == -4 ? C[3] : x == -2 && y == -FH ? C[7] : C[5]);
    const auto kh = g < .15 ? W[0] : g < .4 ? C[4] : g < .7 ? C[6] : WHITE;
    L.set(-1, -7, kh);
    L.set(0, -7, kh);
    L.set(-1, -6, kh);
    L.set(0, -6, scale(kh, .85));
}
void paintChest(pickup::Layer<40, 44>& L, std::span<const uint32_t> C, std::span<const uint32_t> W, double th,
                double g) {
    if (th > 1.2) {
        paintLid(L, C, W, th, g);
        paintHoard(L, C, W, g);
    } else {
        paintHoard(L, C, W, g);
        paintLid(L, C, W, th, g);
    }
    paintFront(L, C, W, g);
}
struct Ray {
    double angle, length, width, delay, frequency, phase;
};
struct Loot {
    bool   gem;
    size_t material;
    double x, y, vx, vy, t0;
    bool   up = false, rest = false;
    double radius, axis, spin, spinSpeed, settle = 0, phase, pop = 0;
    bool   gone = false;
};
struct Ring {
    double x, y, t0;
};
struct Bit {
    double   x, y;
    uint32_t color;
    double   td;
    bool     mote, done = false;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct ChestOpen final : Kernel {
    std::vector<uint32_t> C, W;
    int64_t               woodDark = 0;
    Rng                   R;
    Particles             ps, dust;
    Lights                lights;
    std::array<Ray, 9>    rays;
    std::vector<Loot>     loot;
    std::vector<Ring>     rings;
    std::vector<Bit>      bits;
    double                t      = 0;
    bool                  opened = false, poofed = false;
    explicit ChestOpen(const PixelFxSettings& o)
        : C(o.palette), W(o.pickup.wood.size()), woodDark(o.pickup.wood[0]), R(o.seed) {
        std::transform(o.pickup.wood.begin(), o.pickup.wood.end(), W.begin(), [](int64_t c) { return uint32_t(c); });
        ps.sparks   = {C[7], C[6], C[5], C[4]};
        ps.haze     = C[4];
        dust.sparks = {C[5], C[5], C[4], C[3]};
        for (size_t i = 0; i < rays.size(); ++i) {
            const double u = (double(i) / 8) * 2 - 1, a = u * 1.05 + R.range(-.06, .06),
                         len = R.range(46, 64) * (1.2 - .4 * std::abs(u)), w = R.range(.035, .08), d = R.range(0, .06),
                         f = R.range(5, 10), ph = R.range(0, TAU);
            rays[i] = {a, len, w, d, f, ph};
        }
        const int n = o.pickup.coins;
        loot.reserve(size_t(n + 4));
        for (int i = 0; i < n + 4; ++i) {
            const bool   gem  = i >= n;
            const int    sign = i % 2 ? 1 : -1;
            const double a = sign * R.range(.1, .5), sp = R.range(190, 270), x = R.range(-7, 7),
                         t0 = OPEN + .03 + R.range(0, .2), ax = R.range(-.6, .6), spin = R.range(0, TAU),
                         vs = R.signedRange(14, 24), ph = R.next();
            loot.push_back({gem, gem ? size_t(i - n) % GEMS.size() : 0, x, -12, std::sin(a) * sp, -std::cos(a) * sp, t0,
                            false, false, gem ? 2.6 : 3, ax, spin, vs, 0, ph});
        }
        std::vector<size_t> order(loot.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return std::abs(loot[a].vx) > std::abs(loot[b].vx); });
        for (size_t k = 0; k < order.size(); ++k) loot[order[k]].pop = COLLECT + double(k) * .04 + R.range(0, .02);
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    struct Shake {
        double dx, hop, lid;
    };
    Shake shake() const {
        double dx = 0, hop = 0, lid = 0;
        for (auto q : RATTLES) {
            const double u = t - q.x;
            if (u < 0 || u > .18) continue;
            const double e = 1 - u / .18;
            dx += std::sin(u * 90) * 1.6 * q.y * e;
            if (u < .06 && q.y > .5) hop = 1;
            lid = std::max(lid, std::max(0., std::sin(u * 38)) * .13 * q.y * e);
        }
        if (t >= OPEN && t < OPEN + .08) hop = 1;
        const double strain = smooth(OPEN - .14, OPEN, t);
        if (t < OPEN) {
            lid = std::max(lid, .07 * strain);
            dx += strain > .2 ? (std::fmod(std::floor(t * 60), 2) != 0 ? 1 : -1) * .6 : 0;
        }
        return {jsRound(dx), hop, lid};
    }
    double lidAngle(double lid) const {
        if (t < OPEN) return lid;
        const double u = t - OPEN, w = 30, z = .55, wd = w * std::sqrt(1 - z * z);
        return 1.98 * (1 - std::exp(-z * w * u) * (std::cos(wd * u) + ((z * w) / wd) * std::sin(wd * u)));
    }
    double inner() const {
        if (t < OPEN) {
            double k = .1 + .45 * smooth(0, OPEN, t) + .5 * smooth(OPEN - .14, OPEN, t);
            for (auto q : RATTLES)
                if (t > q.x) k += .45 * q.y * std::exp(-(t - q.x) * 9);
            return k;
        }
        return (.62 + .3 * std::exp(-(t - OPEN) * 5)) * (1 - smooth(COLLECT - .3, POOF, t)) + .05 * std::sin(t * 9);
    }
    double rayK(const Ray& r) const {
        const double u = t - OPEN - r.delay;
        if (u < 0) return 0;
        return ease::outCubic(clamp(u / .1)) * (1 - smooth(1, 2.1, u)) *
               (.72 + .28 * std::sin(t * r.frequency + r.phase));
    }
    void update(double dt) override {
        t += dt;
        const double g = inner();
        for (auto q : RATTLES)
            if (q.y > .5 && t - dt < q.x + .05 && t >= q.x + .05)
                for (int i = 0; i < 4; ++i) {
                    const int sd = i % 2 ? 1 : -1;
                    emit({ParticleKind::Dust, sd * R.range(9, 13), -1, sd * R.range(20, 50), -R.range(2, 8),
                          R.range(.3, .5), DUST, R.range(1.2, 2.2) * q.y});
                }
        if (t < OPEN && R.isChance(g * .08)) {
            const double x = R.range(-11, 11);
            emit({ParticleKind::Star, x, -10, x * 1.5, -R.range(8, 30), R.range(.3, .6), R.isChance(.5) ? C[6] : C[5],
                  R.range(.4, 1.2)});
        }
        if (!opened && t >= OPEN) {
            opened = true;
            lights.add(0, -18, 64, C[5], .45, .7);
            lights.add(0, -16, 22, WHITE, .35, .1);
            for (int i = 0; i < 7; ++i) {
                const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(120, 260);
                emit({ParticleKind::Spark, R.range(-8, 8), -13, std::cos(a) * sp, std::sin(a) * sp, R.range(.12, .26)});
            }
            for (int i = 0; i < 8; ++i) {
                const int sign = R.sign();
                emit({ParticleKind::Dust, sign * R.range(10, 14), -1, sign * R.range(30, 80), -R.range(2, 10),
                      R.range(.4, .7), DUST, R.range(1.6, 3)});
            }
        }
        if (t > OPEN && t < COLLECT) {
            if (R.isChance(.22))
                emit({ParticleKind::Ember, R.range(-9, 9), -13, R.range(-12, 12), -R.range(25, 60), R.range(.8, 1.5),
                      C[5], R.range(.8, 1.6)});
            if (R.isChance(.12)) {
                const double a = R.range(-1, 1), d = R.range(10, 40);
                emit({ParticleKind::Star, std::sin(a) * d, -12 - std::cos(a) * d, 0, -R.range(4, 12), R.range(.3, .7),
                      R.isChance(.5) ? WHITE : C[6], R.range(.5, 2)});
            }
        }
        for (auto& it : loot) {
            if (it.gone || t < it.t0) continue;
            it.up = true;
            if (t >= it.pop + .13) {
                it.gone        = true;
                const double y = it.y - 6;
                const auto&  G = GEMS[it.material];
                if (it.gem) rings.push_back({it.x, y, t});
                lights.add(it.x, y, 18, it.gem ? G[4] : C[5], .65, .25);
                for (int i = 0; i < 4; ++i) {
                    const double a = R.range(0, TAU), sp = R.range(25, 55);
                    emit({ParticleKind::Star, it.x, y, std::cos(a) * sp - it.x * .25, std::sin(a) * sp - 20,
                          R.range(.3, .55), i ? (it.gem ? G[5] : C[6]) : WHITE, R.range(.6, 1.5)});
                }
                continue;
            }
            if (it.rest) {
                it.settle += dt;
                it.vx -= it.vx * 9 * dt;
                it.x = clamp(it.x + it.vx * dt, -74, 74);
                continue;
            }
            it.vy += 950 * dt;
            it.x += it.vx * dt;
            if (std::abs(it.x) > 74) {
                it.x = (it.x < 0 ? -1 : 1) * 74;
                it.vx *= -.3;
            }
            it.y += it.vy * dt;
            it.spin += it.spinSpeed * dt;
            if (it.y + it.radius >= -.5 && it.vy > 0) {
                it.y = -.5 - it.radius;
                if (it.vy > 110) {
                    emit({ParticleKind::Dust, it.x, -1, R.range(-20, 20), -R.range(4, 12), R.range(.25, .45), DUST,
                          R.range(1, 1.8)});
                    if (it.vy > 220) {
                        for (int i = 0; i < 2; ++i)
                            emit(
                                {ParticleKind::Spark, it.x, -1, R.range(-90, 90), -R.range(60, 140), R.range(.1, .22)});
                        lights.add(it.x, -2, 9, C[5], .5, .12);
                    }
                    it.vy = -it.vy * .42;
                    it.vx *= .5;
                    it.spinSpeed *= .55;
                } else {
                    it.rest = true;
                    it.vy   = 0;
                }
            }
        }
        std::erase_if(rings, [&](const Ring& q) { return t - q.t0 > .3; });
        if (!poofed && t >= POOF) {
            poofed = true;
            lights.add(0, -12, 40, C[5], .5, .6);
            pickup::Layer<40, 44> L;
            L.begin(-20, -32);
            paintChest(L, C, W, lidAngle(0), .3);
            L.outline(W[0]);
            int top = 0;
            for (size_t i = 0; i < L.colors.size(); ++i)
                if (L.colors[i]) top = std::min(top, int(i / 40) - L.info.originY);
            bits.reserve(L.colors.size());
            for (size_t i = 0; i < L.colors.size(); ++i) {
                const auto c = L.colors[i];
                if (!c) continue;
                const int x = int(i % 40) - L.info.originX, y = int(i / 40) - L.info.originY;
                bits.push_back({double(x), double(y), c,
                                POOF + (double(y - top) / -top) * .32 + rand({uint32_t(x), uint32_t(y), 21}) * .05,
                                int64_t(c) != woodDark && rand({uint32_t(x), uint32_t(y), 23}) < .14});
            }
        }
        for (auto& b : bits)
            if (!b.done && t >= b.td) {
                b.done = true;
                if (b.mote) {
                    const double vx = R.range(-28, 28), vy = -R.range(14, 50), life = R.range(.4, .8),
                                 size = R.range(.5, 1.1);
                    dust.spawn(R, ParticleKind::Ember, b.x + .5, b.y + .5, vx, vy, life, C[5], size);
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        dust.update(dt, R);
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        if (t < POOF + .2) pickup::blobShadow(p, 0, 15, 1 - smooth(POOF, POOF + .2, t));
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double g = inner(), open = t >= OPEN ? 1 : 0;
        const auto   sh = shake();
        const double th = lidAngle(sh.lid);
        pickup::pool(p, 0, 0, 30 + 10 * g, 4, C[4], (.1 + .18 * open) * g);
        p.glow(0, -16, 24 + 8 * g, C[4], (.1 + .2 * open) * g);
        if (t < POOF) {
            pickup::Layer<40, 44> L;
            L.begin(-20, -32);
            paintChest(L, C, W, th, std::min(1.2, g));
            L.outline(W[0]);
            L.draw(p, sh.dx, -sh.hop);
        } else
            for (const auto& b : bits)
                if (!b.done) p.px(b.x, b.y, b.td - t < .04 ? (int64_t(b.color) == woodDark ? C[3] : C[6]) : b.color);
        if (t < OPEN) {
            p.glowLine(-11, -9.5 - sh.hop, 11, -9.5 - sh.hop, 5, C[5], .22 * g);
            if (g > .35 && sh.lid < .015)
                p.lineFn(
                    -10 + sh.dx, -10 - sh.hop, 9 + sh.dx, -10 - sh.hop,
                    [&](double u) { return std::abs(u - .5) < .2 ? WHITE : C[6]; },
                    [&](double u) { return (g - .35) * 2.2 * (1 - std::abs(u - .5) * .9); });
            p.glow(sh.dx, -7 - sh.hop, 6, C[6], .35 * g);
            if (sh.lid > .015)
                for (uint32_t i = 0; i < 8; ++i) {
                    const double x = -10.5 + double(i) * 3 + sh.dx + (rand({i, 3}) - .5) * 1.5,
                                 a = -PI / 2 + (x / 11) * 1.25,
                                 length =
                                     (7 + rand({i, jsUint(std::floor(t * 30)), 1}) * 12) * std::min(1., sh.lid / .05),
                                 y0 = -10 - sh.hop;
                    p.lineFn(
                        x, y0, x + std::cos(a) * length, y0 + std::sin(a) * length,
                        [&](double u) { return u < .25  ? WHITE
                                               : u < .6 ? C[6]
                                                        : C[5]; },
                        [](double u) { return 1.15 - u; });
                    p.glowLine(x, y0, x + std::cos(a) * length * .7, y0 + std::sin(a) * length * .7, 3, C[5], .25);
                }
        }
        for (const auto& r : rays) {
            const double k = rayK(r);
            if (k <= .01) continue;
            const double ox = std::sin(r.angle) * 7, oy = -12, dx = std::sin(r.angle), dy = -std::cos(r.angle),
                         length = r.length * (.88 + .12 * std::sin(t * r.frequency * .7 + r.phase)),
                         tw = std::tan(r.width), x1 = ox + dx * length, y1 = oy + dy * length;
            p.shade(std::min(ox, x1) - 8, std::min(oy, y1) - 8, std::max(ox, x1) + 8, std::max(oy, y1) + 8,
                    [&](double x, double y, int sx, int sy) {
                        const double rx = x - ox, ry = y - oy, d = rx * dx + ry * dy;
                        if (d < 0 || d > length) return;
                        const double q = std::abs(rx * dy - ry * dx), hw = .6 + d * tw,
                                     f = q < hw ? 1 - .35 * (q / hw) : .65 * (1 - (q - hw) / 1.6);
                        if (f <= 0) return;
                        p.add(sx, sy, q < hw * .45 ? C[6] : C[5],
                              k * .55 * f * std::pow(1 - d / length, 1.4) * smooth(5, 14, d));
                    });
        }
        if (t >= OPEN && t < OPEN + .2) {
            const double a = (t - OPEN) / .2;
            flare(p, 0, -14, 22 * std::pow(1 - a, 1.4), 1 - a, C);
        }
        pickup::Layer<16, 16> IL;
        for (const auto& it : loot) {
            if (!it.up || it.gone) continue;
            const double pu = t - it.pop, lift = pu > 0 ? 6 * ease::outCubic(clamp(pu / .1)) : 0;
            const bool   white = pu > .095;
            double       y = it.y - lift, ax = it.axis, spin = it.spin;
            bool         flat = false;
            if (it.rest && !it.gem) {
                const double w = std::exp(-it.settle * 8) * std::abs(std::cos(it.settle * 22));
                ax             = 0;
                spin           = std::acos(clamp(.5 + .5 * w));
                flat           = w < .12;
                y              = flat ? -1.5 - lift : -it.radius * std::cos(spin) - lift;
            }
            if (it.y > -10) pickup::blobShadow(p, it.x, 3, .7 * (1 - clamp(-it.y / 30)));
            const auto& G = GEMS[it.material];
            if (it.gem)
                pickup::gemlet(IL, p, it.x, y, it.radius,
                               it.rest ? .4 * (it.spinSpeed < 0   ? -1
                                               : it.spinSpeed > 0 ? 1
                                                                  : 0)
                                       : it.spin,
                               G, white);
            else
                pickup::coin(IL, p, flat ? jsRound(it.x) + .5 : it.x, y, it.radius, ax, flat ? std::acos(.5) : spin, C,
                             white, 1, !flat);
            p.glow(it.x, y, 9, it.gem ? G[4] : C[5], it.rest ? .26 : .18);
            p.glow(it.x, y, 3, WHITE, .12);
            if (it.rest && t < it.pop) {
                const double u = std::fmod(t * .9 + it.phase, 1);
                if (u < .16)
                    pickup::glint(p, it.x - 1, y - 1.5, 2.6 * std::sin((u / .16) * PI), it.gem ? G[5] : C[6], .5);
            }
        }
        for (const auto& r : rings) {
            const double k = (t - r.t0) / .3, e = ease::outCubic(k);
            p.ring(r.x, r.y, 2 + 5 * e, 1, k < .3 ? WHITE : C[6], 1.15 - k);
            p.glow(r.x, r.y, 12, C[5], .45 * (1 - k));
            if (k < .5) pickup::glint(p, r.x, r.y, 3.4 * (1 - k / .5), C[6], .55);
        }
        ps.draw(p, Paint::Layer::Front);
        dust.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& chestOpenDescriptor() {
    static const std::array<Option, 2> options{{{"wood", 1, 64, false, 7, &PixelFxPickupSettings::wood},
                                                {"coins", 0, 64, false, 0, &PixelFxPickupSettings::coins}}};
    static const Descriptor            d{
        {PixelFxKind::ChestOpen, "pickups/chest-open", 176, 112, 88, 96, 4.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.pickup.coins = 12;
            s.palette      = {hex(0x1e1206), hex(0x4a2c08), hex(0x86520c), hex(0xc48412),
                              hex(0xf0b424), hex(0xffe066), hex(0xfff6c4), WHITE};
            s.pickup.wood  = {int64_t(hex(0x110a10)) - 4294967296LL, int64_t(hex(0x24131a)) - 4294967296LL,
                              int64_t(hex(0x3a1f1f)) - 4294967296LL, int64_t(hex(0x552d24)) - 4294967296LL,
                              int64_t(hex(0x73402c)) - 4294967296LL, int64_t(hex(0x925737)) - 4294967296LL,
                              int64_t(hex(0xb07246)) - 4294967296LL};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ChestOpen>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
