// eve-hot-path
// Adapted from pixelfx fire/flame-pillar.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr double ERUPT = 0.55, RISE = 0.12, FALL = 1.95, GONE = 2.45;
const uint32_t   SOOT = hex(0x0d0a12), OUTLINE = hex(0x1a1422), WARM = rgb(255, 176, 96);
struct Stone {
    uint32_t light, body, shade, dust;
};
struct Crack {
    std::vector<Point> points;
    double             t0, grow;
    bool               wide;
};
struct Strand {
    double offset, width, frequency;
};
struct Spawn {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct FlamePillar final : Kernel {
    std::array<uint32_t, 9> H{};
    double                  maximumHeight, t = 0;
    Stone                   st;
    Rng                     rng;
    uint32_t                S, glow;
    Particles               particles;
    Bits                    bits;
    Lights                  lights;
    std::vector<Crack>      cracks;
    std::array<Strand, 7>   strands{};
    bool                    erupted = false, fell = false;
    explicit FlamePillar(const PixelFxSettings& o) : maximumHeight(o.fire.height), rng(o.seed), S(o.seed * 13) {
        std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
        const auto c = o.fire.stone;
        st = {mix(scale(c, 1.35), WHITE, 0.08), c, scale(c, 0.64), mix(scale(c, 1.2), rgb(150, 140, 150), 0.3)};
        particles.sparks = {H[1], H[2], H[4], H[5]};
        glow             = mix(mix(H[3], H[4], 0.5), WHITE, 0.25);
        for (int i = 0; i < 9; ++i) {
            const double a = double(i) / 9 * TAU + rng.range(-0.25, 0.25);
            const double x = rng.range(-1, 1), y = rng.range(-0.3, 0.3), len = rng.range(20, 40),
                         t0     = rng.range(0, 0.16);
            const auto   points = crack(x, y, a, len, t0, true);
            const int    j      = 3 + rng.integer(0, int(points.size()) - 6);
            if (points.size() > 8) {
                const double angle = a + rng.signedRange(0.5, 1), length = rng.range(6, 14),
                             birth = 0.18 + rng.range(0, 0.15);
                crack(points[size_t(j)].x, points[size_t(j)].y, angle, length, birth, false);
            }
        }
        for (size_t i = 0; i < strands.size(); ++i)
            strands[i] = {(rand({uint32_t(i), S, 1}) - 0.5) * 1.4, 0.05 + rand({uint32_t(i), S, 2}) * 0.06,
                          1 + rand({uint32_t(i), S, 3}) * 2};
    }
    std::vector<Point> crack(double x, double y, double a, double len, double t0, bool wide) {
        std::vector<Point> points{{x, y}};
        for (int d = 0; d < len; d += 2) {
            a += rng.range(-0.55, 0.55);
            x += std::cos(a) * 2;
            y += std::sin(a) * 0.76;
            points.push_back({x, y});
        }
        const double grow = rng.range(0.22, 0.36);
        cracks.push_back({points, t0, grow, wide});
        return points;
    }
    void emit(Spawn q) {
        particles.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags);
    }
    double height() const {
        if (t < ERUPT) return 0;
        if (t < ERUPT + RISE) return maximumHeight * ease::outCubic((t - ERUPT) / RISE);
        if (t < FALL) return maximumHeight * (1 + 0.05 * noise(t * 3, S));
        return maximumHeight * (1 - ease::inQuad(clamp((t - FALL) / (GONE - FALL))));
    }
    double width() const {
        if (t < ERUPT) return 0;
        if (t < FALL) return 12 * (1 + 0.45 * std::exp(-(t - ERUPT) * 7)) * (0.94 + 0.06 * noise(t * 9, S + 1));
        return 12 * (1 - smooth(FALL, GONE, t)) + 1.5 * (1 - smooth(GONE - 0.1, GONE, t));
    }
    double heat() const {
        return smooth(0, ERUPT, t) * 0.75 + 0.25 * smooth(ERUPT - 0.05, ERUPT + 0.1, t) -
               smooth(GONE - 0.3, 5 - 0.6, t) * 1.05;
    }
    void update(double dt) override {
        t += dt;
        if (t < ERUPT && rng.isChance(0.6 * dt * 60 * smooth(0.15, ERUPT, t)))
            emit({ParticleKind::Fire, rng.range(-5, 5), -1, rng.range(-6, 6), -rng.range(20, 45), rng.range(0.15, 0.3),
                  0, rng.range(1, 2.2)});
        if (t < ERUPT && rng.isChance(0.5 * dt * 60 * smooth(0, ERUPT, t))) {
            const auto& c = cracks[size_t(rng.integer(0, int(cracks.size()) - 1))];
            const int   i = rng.integer(0, int(std::floor(c.points.size() * clamp((t - c.t0) / c.grow))) - 1);
            if (i >= 0) {
                const auto q = c.points[size_t(i)];
                if (rng.isChance(0.5))
                    emit({ParticleKind::Fire, q.x, q.y - 1, rng.range(-5, 5), -rng.range(15, 35), rng.range(0.15, 0.3),
                          0, rng.range(0.8, 1.6)});
                else
                    emit({ParticleKind::Ember, q.x, q.y - 1, rng.range(-15, 15), -rng.range(30, 70), rng.range(0.5, 1),
                          H[3], 1});
            }
        }
        if (!erupted && t >= ERUPT) {
            erupted = true;
            lights.add(0, -40, 90, WARM, 0.7, 0.6);
            lights.add(0, -8, 44, WHITE, 0.7, 0.1);
            for (int i = 0; i < 70; ++i) {
                const double a = -PI / 2 + rng.range(-1.1, 1.1), sp = rng.range(100, 260);
                emit({ParticleKind::Spark, rng.range(-4, 4), -2, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(0.25, 0.7)});
            }
            for (int i = 0; i < 18; ++i) {
                Debris b;
                b.kind            = DebrisKind::Chunk;
                b.x               = rng.range(-6, 6);
                b.y               = -2;
                b.vx              = rng.range(-110, 110);
                b.vy              = -rng.range(150, 330);
                b.gravity         = 1;
                b.drag            = 0.4;
                b.bounce          = 0.3;
                b.life            = rng.range(1.6, 2.4);
                b.angle           = rng.range(0, TAU);
                b.angularVelocity = rng.range(-16, 16);
                b.size            = rng.integer(1, 3);
                bits.add(std::move(b));
            }
            const std::array<uint32_t, 4> grit{st.light, st.body, st.body, st.shade};
            for (int i = 0; i < 60; ++i) {
                const double a = -PI / 2 + rng.range(-0.9, 0.9), sp = rng.range(80, 260);
                emit({ParticleKind::Grit, rng.range(-5, 5), -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1, 1.8),
                      rng.pick<uint32_t>(grit), rng.isChance(0.4) ? 2.0 : 1.0});
            }
            for (int i = 0; i < 14; ++i) {
                const double side = rng.sign();
                emit({ParticleKind::Smoke, side * rng.range(2, 10), -rng.range(1, 4), side * rng.range(40, 110),
                      -rng.range(2, 10), rng.range(1, 1.8), st.dust, rng.range(4, 7), rng.range(0, 0.06),
                      ParticleFlag::Big});
            }
        }
        const double h = height(), w = width();
        if (t > ERUPT && t < GONE && h > 4) {
            if (rng.isChance(0.9 * dt * 60)) {
                const double side = rng.sign(), y = -rng.range(4, h * 0.9);
                emit({ParticleKind::Fire, side * w * rng.range(0.7, 1.1), y, side * rng.range(15, 40),
                      -rng.range(30, 70), rng.range(0.25, 0.5), 0, rng.range(1.6, 3.2), 0,
                      rng.isChance(0.35) ? ParticleFlag::Smokes : ParticleFlag::None});
            }
            if (rng.isChance(0.7 * dt * 60))
                emit({ParticleKind::Fire, rng.range(-w, w), -h + rng.range(0, 6), rng.range(-20, 20),
                      -rng.range(30, 80), rng.range(0.35, 0.7), 0, rng.range(3, 5.5), 0,
                      rng.isChance(0.6)
                          ? ParticleFlag::Smokes | (t < FALL - 0.3 ? ParticleFlag::Big : ParticleFlag::None)
                          : ParticleFlag::None});
            if (t < FALL && rng.isChance(0.5 * dt * 60))
                emit({ParticleKind::Ember, rng.range(-w, w) * 0.6, -h + 4, rng.range(-60, 60), -rng.range(50, 110),
                      rng.range(0.7, 1.3), H[3], rng.isChance(0.2) ? 2.0 : 1.0});
        }
        if (t >= FALL && !fell) fell = true;
        if (fell && t < GONE - 0.1 && rng.isChance(1.6 * dt * 60))
            emit({ParticleKind::Fire, rng.range(-w - 2, w + 2), -h - rng.range(0, 8), rng.range(-30, 30),
                  -rng.range(10, 50), rng.range(0.4, 0.75), 0, rng.range(3.5, 6.5), 0, ParticleFlag::Smokes});
        bits.update(dt, [](double) { return 0.0; });
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 3; ++i)
                    emit({ParticleKind::Dust, b.x + rng.range(-1.5, 1.5), -0.5, rng.signedRange(8, 35),
                          -rng.range(2, 18), rng.range(0.3, 0.6), st.dust, rng.range(1.3, 2.6)});
            }
        particles.update(dt, rng, [](double) { return 0.0; }, [](double, double) {});
        lights.update(dt);
    }
    void column(Paint& p) const {
        const double h = height(), w0 = width();
        if (h < 1 || w0 < 0.3) return;
        if (t < FALL + 0.25) {
            const double k = clamp(w0 / 12);
            const auto   N = [&](double rate, uint32_t seed, double off) { return noise(t * rate + off, seed); };
            for (int i = 0; i < 5; ++i) {
                const auto   n  = (S + uint32_t(i)) * 17;
                const double dx = (i - 2) * w0 * 0.4, th = (12 + 12 * (0.5 + 0.5 * noise(t * 3.3 + i, n))) * k *
                                                           (i == 2       ? 1.2
                                                            : i % 4 == 0 ? 0.6
                                                                         : 0.9);
                fire::flameTongue(p, dx + noise(t * 2, n + 2) * 1.5, -h + 12, th, 3.4 * k * (i % 4 == 0 ? 0.7 : 1),
                                  -dx * 0.3 + noise(t * 2.2, n + 1) * 3, N, n, H);
            }
        }
        for (int y = 0; y >= -h; --y) {
            const double v = -y / h, cx = noise(-y * 0.04 + t * 1.6, S + 2) * 1.6 * v,
                         foot = 1 + 0.7 * std::pow(std::max(0.0, 1 - double(-y) / 12), 2),
                         top  = 1 - smooth(0.82, 1, v) * 0.7;
            const double wl =
                             w0 * foot * top * (1 + 0.65 * std::pow(std::max(0.0, noise(y * 0.2 + t * 8, S + 3)), 1.5)),
                         wr =
                             w0 * foot * top * (1 + 0.65 * std::pow(std::max(0.0, noise(y * 0.2 + t * 8, S + 4)), 1.5));
            const int    pulse = std::fmod(-y * 0.032 + t * 3.6, 1) < 0.09 ? 1 : 0;
            for (int x = int(std::floor(cx - wl * 1.15)); x <= std::ceil(cx + wr * 1.15); ++x) {
                const double dx = x + 0.5 - cx, q = std::abs(dx) / (dx < 0 ? wl : wr);
                if (q > 1.15) continue;
                int lvl = q < 0.12   ? 0
                          : q < 0.26 ? 1
                          : q < 0.42 ? 2
                          : q < 0.56 ? 3
                          : q < 0.7  ? 4
                          : q < 0.84 ? 5
                          : q < 0.96 ? 6
                                     : 7;
                for (const auto& strand : strands)
                    if (std::abs(dx / w0 - strand.offset -
                                 0.06 * std::sin(t * strand.frequency * 3 + strand.offset * 9)) < strand.width)
                        lvl = std::max(0, lvl - 1);
                lvl = std::max(0, lvl - pulse);
                if (lvl == 7 && rand({jsUint(x), jsUint(y), jsUint(std::floor(t * 20))}) < 0.5) continue;
                p.px(x, y, H[size_t(lvl)]);
            }
            if (y % 12 == 0) p.glow(cx, y, w0 * 2.4 + 8, glow, 0.28 * clamp(w0 / 12));
        }
        p.glow(0, -h * 0.45, 92, glow, 0.3 * clamp(w0 / 12));
    }
    void cracksDraw(Paint& p) const {
        const double k = heat();
        if (k <= -0.05) return;
        for (const auto& c : cracks) {
            const auto n = size_t(std::floor(c.points.size() * clamp((t - c.t0) / c.grow)));
            for (size_t i = 1; i < n; ++i) {
                const double u = double(i) / c.points.size(), hot = k * (1 - 0.45 * u);
                const auto   a = c.points[i - 1], b = c.points[i];
                if (hot < 0) {
                    const double alpha = 1 + hot * 3;
                    if (alpha > 0) p.line(a.x, a.y, b.x, b.y, SOOT, alpha);
                    continue;
                }
                const auto color = hot > 0.85   ? H[1]
                                   : hot > 0.65 ? H[2]
                                   : hot > 0.45 ? H[4]
                                   : hot > 0.25 ? H[5]
                                   : hot > 0.1  ? H[6]
                                                : H[7];
                if (c.wide && i < c.points.size() * 0.5 && hot > 0.2) {
                    p.line(a.x, a.y + 1, b.x, b.y + 1, hot > 0.6 ? H[5] : H[6]);
                    p.line(a.x, a.y - 1, b.x, b.y - 1, H[7]);
                }
                p.line(a.x, a.y, b.x, b.y, color);
                if (i % 2 == 0) p.glow(b.x, b.y, 5 + 7 * hot, glow, 0.3 * hot);
            }
        }
        if (k > 0) fire::floorLight(p, 0, 0.5, 44, 8, glow, 0.35 * k);
    }
    void rock(Paint& p, const Debris& b) const {
        const double r = 0.6 + b.size * 0.75, fade = std::min(1.0, b.life / 0.5), lx = std::cos(b.angle) * r * 0.4,
                     ly = std::sin(b.angle) * r * 0.4;
        p.shader(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double y, ShadeOutput& out) {
            const double dx = x - b.x, dy = y - b.y, d = jsHypot2(dx * 1.15, dy);
            if (d > r + 0.9) return;
            const double lit = dx * lx + dy * ly;
            out.c            = d > r ? OUTLINE : lit < -0.4 ? st.light : lit > 0.6 ? st.shade : st.body;
            out.a            = fade;
        });
    }
    void drawBack(Paint& p) const override {
        cracksDraw(p);
        fire::simDraw(p, particles, H, Paint::Layer::Back);
        const double k = (t - ERUPT) / 0.45;
        if (k > 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{st.body, st.light, H[4], H[1]};
            band(p, 0, 0.5, 84 * ease::outCubic(k), 0.18, std::max(1.0, 4 * (1 - k)), 1.15 - k, ramp,
                 [](double a) { return std::sin(a) < 0; });
        }
    }
    void draw(Paint& p) const override {
        column(p);
        for (const auto& b : bits.list) rock(p, b);
        fire::simDraw(p, particles, H, Paint::Layer::Front);
        const double k = (t - ERUPT) / 0.45;
        if (k > 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{st.body, st.light, H[4], H[1]};
            band(
                p, 0, 0.5, 84 * ease::outCubic(k), 0.18, std::max(1.0, 4 * (1 - k)), 1.15 - k, ramp,
                [](double a) { return std::sin(a) >= 0; }, k < 0.15);
        }
        const double f = (t - ERUPT) / 0.08;
        if (f > 0 && f < 1) {
            p.ellipse(0, -2, 16 * (1 - f * 0.3), 6, H[2]);
            p.ellipse(0, -3, 11 * (1 - f * 0.3), 4, f < 0.5 ? WHITE : H[1]);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& flamePillarDescriptor() {
    static const std::array<Option, 2> options{
        {{"stone", 4278190080.0, 4294967295.0, false, 0, &PixelFxFireSettings::stone, true},
         {"height", 1, 200, false, 0, &PixelFxFireSettings::height}}};
    static const Descriptor descriptor{
        {PixelFxKind::FlamePillar, "fire/flame-pillar", 184, 256, 92, 232, 5, false},
        9,
        [](PixelFxSettings& s) {
            fireDefaults(s, PixelFxKind::FlamePillar);
            s.seed        = 5;
            s.fire.height = 140;
            s.fire.stone  = hex(0x4a3f69);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FlamePillar>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
