// eve-hot-path
// Native adaptation of pixelfx shadow/curse.js, MIT.
// Sprite Fusion poison, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowSimulation.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           SY = -60, STAMP = .55, CRACK = 1.72, BREAK = 1.98;
constexpr int                              SW = 17, SH = 18;
constexpr std::array<std::string_view, SH> SKULL{
    "......kkkkk......", "....kkhhhhlkk....", "...khhhhhhlllk...", "..khhhhhllllllk..", ".khhhhlllllllmmk.",
    ".khhhllllllllmmk.", "khhllllllllllmmdk", "khlldddllldddmmdk", "khleeeeelleeeeedk", "kleeeeeelleeeeedk",
    "kleeeeeemleeeeedk", "kmleeeelmmleeedkk", ".kmmlllmnnmlmddk.", "..kmmlmnnnnmddk..", "..kdmlllmmlmddk..",
    "...kgtgtgtgtgk...", "...kdgtgtgtgdk...", "....kkkkkkkkk...."};
constexpr std::array<Point, 2> EYES{{{-3.5, 1.5}, {3.5, 1.5}}};
bool inSkull(int u, int v) { return u >= 0 && v >= 0 && u < SW && v < SH && SKULL[size_t(v)][size_t(u)] != '.'; }
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct PuffEmission {
    shadow::PuffKind kind;
    double           x, y, vx, vy, life, size, delay = 0;
    bool             smokes = false;
};
struct Chip {
    double x, y, vx, vy, life, angle, va, size;
};
struct Curse final : Kernel {
    const std::vector<uint32_t> C, K;
    const PixelFxActor          actor;
    Rng                         R;
    Particles                   ps;
    shadow::Puffs               puffs, dust;
    Bits                        bits;
    Lights                      lights;
    std::array<double, SW * SH> cracks{};
    double                      t = 0, acc = 0, head;
    bool                        stamped = false, broke = false;
    explicit Curse(const PixelFxSettings& o)
        : C(o.palette),
          K(o.shadow.bone),
          actor(o.actor),
          R(o.seed),
          head(actor.source == PixelFxActorSource::None ? -38 : actor.y) {
        ps.sparks   = {WHITE, C[7], C[6], C[5]};
        ps.haze     = C[4];
        puffs.flame = {WHITE, C[7], C[6], C[5], C[4], C[3], C[2], C[1], C[0]};
        puffs.soot  = {mix(C[3], C[2], .5), mix(C[1], C[0], .3)};
        puffs.cap   = mix(C[4], C[2], .5);
        puffs.rim   = {C[6], C[5], C[4]};
        dust.flame  = std::vector<uint32_t>(9, WHITE);
        dust.soot   = {mix(K[2], K[1], .4), mix(K[1], K[0], .5)};
        dust.cap    = K[3];
        dust.rim    = {K[4], K[3], K[2]};
        cracks.fill(-1);
        const std::array<std::vector<Point>, 4> paths{{{{8, 1}, {7, 3}, {8, 5}, {6, 6}, {5, 7}},
                                                       {{9, 1}, {10, 3}, {10, 4}, {12, 6}, {12, 7}},
                                                       {{4, 3}, {3, 5}, {2, 6}},
                                                       {{13, 4}, {14, 6}}}};
        for (const auto& path : paths) {
            const int len = int(path.size()) - 1;
            for (int k = 0; k < len; ++k) {
                const auto a = path[size_t(k)], b = path[size_t(k + 1)];
                const int  steps = int(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)));
                for (int s = 0; s <= steps; ++s) {
                    const int x     = int(jsRound(a.x + (b.x - a.x) * s / steps)),
                              y     = int(jsRound(a.y + (b.y - a.y) * s / steps));
                    auto&     value = cracks[size_t(y * SW + x)];
                    if (value < 0) value = (k + double(s) / std::max(1, steps)) / len;
                }
            }
        }
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void puff(const PuffEmission& e) {
        puffs.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.size, e.delay, e.smokes);
    }
    void dustPuff(const PuffEmission& e) {
        dust.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.size, e.delay, e.smokes);
    }
    void chip(const Chip& e) {
        Debris b;
        b.kind            = DebrisKind::Chunk;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = .85;
        b.drag            = .4;
        b.bounce          = .35;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.va;
        b.size            = e.size;
        b.ramp            = K;
        bits.add(std::move(b));
    }
    void update(double dt) override {
        t += dt;
        if (t < STAMP - .05) {
            const double k = t / STAMP;
            for (acc += dt * (40 + 60 * k); acc > 1; --acc) {
                const double a = R.range(0, TAU), D = R.range(22, 44);
                emit({ParticleKind::Mote, std::cos(a) * D, SY + std::sin(a) * D * .8, -std::sin(a) * 60,
                      std::cos(a) * 60, .8, R.pick<uint32_t>(std::array<uint32_t, 4>{WHITE, C[7], C[6], C[5]}),
                      R.range(.6, 1)});
            }
            if (R.isChance(dt * 14)) {
                const double a = R.range(0, TAU), D = R.range(20, 34);
                puff({shadow::PuffKind::Soot, std::cos(a) * D, SY + std::sin(a) * D * .7, -std::sin(a) * 40,
                      std::cos(a) * 30, R.range(.5, .7), R.range(3, 5.5)});
            }
            (void)shadow::attract(
                ps.list, 0, SY, 9e5 * (.4 + k), 70, .6, 2.5, dt,
                [](const auto& q) { return q.kind == ParticleKind::Mote ? 1. : 0.; }, 300);
            (void)shadow::attract(
                puffs.list, 0, SY, 5e5 * (.4 + k), 60, .7, 3.5, dt, [](const auto&) { return 1.; }, 120);
        }
        if (t >= STAMP && !stamped) {
            stamped = true;
            ps.list.clear();
            puffs.list.clear();
            lights.add(0, SY, 64, C[5], 1, .35);
            lights.add(0, SY, 30, WHITE, 1, .1);
            lights.add(0, head + 12, 40, C[4], .6, .5);
            for (int i = 0; i < 22; ++i) {
                const double a = R.range(0, TAU), sp = R.range(80, 160);
                emit({ParticleKind::Spark, std::cos(a) * 6, SY + std::sin(a) * 6, std::cos(a) * sp,
                      std::sin(a) * sp - 30, R.range(.2, .5)});
            }
            for (int i = 0; i < 6; ++i) {
                const double a = R.range(0, TAU), sp = R.range(20, 45);
                puff({shadow::PuffKind::Soot, std::cos(a) * 8, SY + std::sin(a) * 6, std::cos(a) * sp,
                      std::sin(a) * sp * .6 - 10, R.range(.6, .9), R.range(2.5, 4.5)});
            }
        }
        if (stamped && !broke) {
            for (const auto eye : EYES) {
                if (R.isChance(dt * 9))
                    emit({ParticleKind::Ember, eye.x, SY + eye.y - 3, R.range(-10, 10), -R.range(20, 45),
                          R.range(.5, .9), C[6]});
                if (R.isChance(dt * 7))
                    puff({shadow::PuffKind::Soot, eye.x + R.range(-1, 1), SY + eye.y - 3, R.range(-5, 5),
                          -R.range(14, 26), R.range(.5, .8), R.range(1.4, 2.4)});
            }
            if (R.isChance(dt * 8))
                emit({ParticleKind::Vapour, R.range(-7, 7), SY + R.range(-6, 6), R.range(-12, 12), R.range(-12, 4),
                      R.range(.5, .8), R.isChance(.5) ? C[3] : C[4], R.range(5, 8)});
            if (R.isChance(dt * 12))
                emit({ParticleKind::Mote, R.range(-4, 4), SY + 9, R.range(-6, 6), R.range(40, 70), R.range(.25, .4),
                      R.isChance(.5) ? C[6] : C[5], .8});
            if (R.isChance(dt * 10))
                puff({shadow::PuffKind::Soot, R.range(-3.5, 3.5), SY + 9, R.range(-3, 3), R.range(14, 26),
                      R.range(.45, .7), R.range(1.2, 2.2)});
            if (t > STAMP + .3 && R.isChance(dt * 5)) {
                const double side = R.range(-1, 1);
                emit({ParticleKind::Star, side * (actor.source != PixelFxActorSource::None ? actor.width / 2. : 10),
                      head + R.range(4, actor.source != PixelFxActorSource::None ? actor.height - 4 : 30), 0, -6,
                      R.range(.3, .5), C[6], R.range(1, 2)});
            }
        }
        if (t >= BREAK && !broke) {
            broke = true;
            for (int i = 0; i < 24; ++i) {
                double x, y;
                do {
                    x = R.range(-8.5, 8.5);
                    y = R.range(-9, 9);
                } while (!inSkull(int(std::floor(x + SW / 2.)), int(std::floor(y + SH / 2.))));
                const double speed = R.range(2.5, 6), jitter = R.range(-12, 12);
                chip({x, SY + y, x * speed + jitter, R.range(-70, 10), R.range(.8, 1.2), R.range(0, TAU),
                      R.range(-14, 14), R.range(1, 2.4)});
            }
            for (int i = 0; i < 26; ++i)
                emit({ParticleKind::Grit, R.range(-7, 7), SY + R.range(-8, 8), R.range(-32, 32), R.range(-60, 20),
                      R.range(.8, 1.3), R.pick<uint32_t>(std::array<uint32_t, 4>{K[2], K[3], K[3], K[4]}),
                      R.isChance(.3) ? 2. : 1.});
            for (int i = 0; i < 7; ++i)
                dustPuff({shadow::PuffKind::Soot, R.range(-7, 7), SY + R.range(-8, 6), R.range(-25, 25),
                          R.range(-20, 5), R.range(.5, .8), R.range(2, 4), R.range(0, .1)});
            for (const auto eye : EYES)
                puff({shadow::PuffKind::Flame, eye.x, SY + eye.y, (eye.x < 0 ? -1. : 1.) * 6, -30, .5, 2.6});
            lights.add(0, SY, 40, C[5], .5, .25);
        }
        lights.update(dt);
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted        = true;
                const double vx = R.range(-12, 12), vy = -R.range(4, 10);
                dustPuff({shadow::PuffKind::Soot, b.x, -1.5, vx, vy, R.range(.35, .6), R.range(1.5, 2.5)});
            }
        ps.update(dt, R, floor, [](double, double) {});
        puffs.update(dt, R, floor);
        dust.update(dt, R, floor);
    }
    void skull(Paint& p, double s, double sq, double ox, double oy, bool white, double crack) const {
        const double cx = ox, cy = SY + oy, hw = (SW / 2.) * s + 1, hh = (SH / 2.) * s * sq + 1;
        p.shader(cx - hw, cy - hh, cx + hw, cy + hh, [&](double x, double y, ShadeOutput& out) {
            const int u = int(std::floor((x - cx) / s + SW / 2.)), v = int(std::floor((y - cy) / (s * sq) + SH / 2.));
            if (!inSkull(u, v)) return;
            const char ch = SKULL[size_t(v)][size_t(u)];
            if (white) {
                out.c = ch == 'e' || ch == 'n' ? C[5] : WHITE;
                return;
            }
            const double ck = cracks[size_t(v * SW + u)];
            if (ck >= 0 && ck < crack) {
                out.c = crack > .55 && ((u + v) % 2) ? C[6] : K[0];
                return;
            }
            out.c = ch == 'k' || ch == 'g'   ? K[0]
                    : ch == 'd'              ? K[1]
                    : ch == 'm'              ? K[2]
                    : ch == 'l' || ch == 't' ? K[3]
                    : ch == 'h'              ? K[4]
                                             : C[0];
        });
    }
    void eyeFire(Paint& p, double x, double y, double k, int i) const {
        if (k <= 0) return;
        for (double j : {-.6, .7}) {
            const double h  = (1.5 + 3 * (.5 + .5 * noise(t * 8 + j * 3.1 + i * 7, 4))) * k,
                         sw = noise(t * 4 + j * 2 + i * 3, 8) * 1.2;
            for (double d = 1; d <= h; d += .5) {
                const double u = d / h;
                p.dot(x + j + sw * u * u, y - d,
                      u < .3    ? C[7]
                      : u < .6  ? C[6]
                      : u < .85 ? C[5]
                                : C[4],
                      u < .75 ? 1 : (1 - u) * 4);
            }
        }
        p.px(x, y, WHITE);
        p.px(x - 1, y, C[6]);
        p.glow(x, y, 8, C[5], .5 * k);
    }
    void drawBack(Paint& p) const override {
        const shadow::PuffLight light{0, SY, stamped && !broke ? .8 : .3, 50};
        puffs.draw(p, shadow::PuffKind::Soot, &light);
        dust.draw(p, shadow::PuffKind::Soot);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double flood = smooth(STAMP + .04, STAMP + .42, t), ebb = 1 - smooth(BREAK + .15, BREAK + .75, t);
        if (actor.source != PixelFxActorSource::None && flood > 0 && ebb > 0) {
            const double k = ebb, pulse = .85 + .15 * std::sin(t * 9);
            for (int my = 0; my < actor.height; ++my) {
                const double u = double(my) / actor.height;
                if (u > flood + .06) break;
                const bool edge = flood < 1 && std::abs(u - flood) < .05,
                           band = flood >= 1 && std::abs(u - std::fmod((t - STAMP) * 1.3, 1.4) + .2) < .06;
                for (int mx = 0; mx < actor.width; ++mx) {
                    const auto c0 = actor.pixels[size_t(my * actor.width + mx)];
                    if (!c0) continue;
                    const double l  = luma(c0);
                    const auto   cc = l > .72 ? C[6] : l > .5 ? C[5] : l > .33 ? C[4] : l > .18 ? C[3] : C[2];
                    p.blend(actor.x + mx, actor.y + my,
                            edge   ? C[7]
                            : band ? C[6]
                                   : cc,
                            edge   ? .85
                            : band ? .7 * k
                                   : .6 * k * pulse);
                }
            }
            p.outline(
                {actor.width, actor.height, actor.mask}, actor.x, actor.y,
                [&](int mx, int my) { return std::sin(my * .7 - t * 14 + mx * .3) > .2 ? C[6] : C[4]; },
                [&](int, int my) { return double(my) / actor.height <= flood ? k : 0.; });
            p.glow(0, actor.y + actor.height * .45, 30, C[4], .3 * k * pulse);
        }
        if (t < STAMP) {
            const double q = t / STAMP;
            p.glow(0, SY, 14 + 16 * q, C[5], .2 + .5 * q);
            if (q > .15)
                shadow::ringFast(p, 0, SY, 4 + 30 * (1 - ease::inQuad(q)), 1, q > .7 ? C[5] : q > .4 ? C[4] : C[3]);
        }
        if (t >= STAMP && t < BREAK) {
            const double u = (t - STAMP) / .1, s = u < 1 ? 1.7 - .7 * ease::inQuad(u) : 1, land = t - STAMP - .1;
            const double sq    = land > 0 ? 1 - .16 * std::exp(-land * 18) * std::cos(land * 40) : 1;
            const double shake = t > CRACK ? (t - CRACK) * 4 : 0, ox = jsRound(std::sin(t * 61) * shake),
                         oy = land > 0 ? jsRound(std::sin(t * 3.4) * 1.2) : 0;
            skull(p, s, sq, ox, oy, t < STAMP + .067, smooth(CRACK, BREAK, t));
            const double burn = smooth(STAMP + .1, STAMP + .25, t);
            if (u >= 1)
                for (size_t i = 0; i < EYES.size(); ++i)
                    eyeFire(p, ox + EYES[i].x, SY + oy + EYES[i].y, burn * (1 + .4 * smooth(CRACK, BREAK, t)), int(i));
            if (u < 1.2) p.sparkle(0, SY, 14 * (1 - u / 1.2), C[7], C[5], .6);
            p.glow(0, SY, 26, C[4], .25);
        }
        const double ru = (t - STAMP - .08) / .3;
        if (ru > 0 && ru < 1)
            shadow::ringFast(
                p, 0, SY + 2, 10 + 26 * ease::outCubic(ru), std::max(1., 2.5 * (1 - ru)),
                [&](double v, double) { return v > .5 ? WHITE : C[6]; }, 1.2 - ru);
        bits.draw(p);
        puffs.draw(p, shadow::PuffKind::Flame);
        ps.draw(p, Paint::Layer::Front);
        for (const auto& l : lights.list) {
            const double q = l.life / l.max;
            p.glow(l.x, l.y, l.radius * (.6 + .4 * q), l.color, l.strength * q * q);
        }
        if (t >= STAMP + .067 && t < BREAK)
            shadow::unlight(
                p, -10, SY - 10, 10, SY + 10,
                [](double x, double y) {
                    const int u = int(std::floor(x + SW / 2.)), v = int(std::floor(y - SY + SH / 2.));
                    return inSkull(u, v) && SKULL[size_t(v)][size_t(u)] != 'e';
                },
                .2);
    }
};
}  // namespace
const Descriptor& curseDescriptor() {
    static const std::array<Option, 2> options{
        {{"bone", 1, 64, false, 5, &PixelFxShadowSettings::bone}, {"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor d{
        {PixelFxKind::Curse, "shadow/curse", 112, 124, 56, 108, 3.4, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 9;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                              hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
            s.shadow.bone  = {hex(0x170f26), hex(0x4a3a6e), hex(0x7f6ea6), hex(0xb3a4d4), hex(0xe2d8f6)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Curse>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
