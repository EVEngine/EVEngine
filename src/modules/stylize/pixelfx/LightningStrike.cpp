// eve-hot-path
// Native adaptation of pixelfx lightning/lightning-strike.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double W = 120, TOP = -224, RX = 28, SY = .22, REACH = 52, LEAD0 = .48, MEET = -30, S1 = .76;
struct StrokeTime {
    double t, k;
};
constexpr std::array<StrokeTime, 3> STROKES{{{.76, 1}, {1.04, .8}, {1.26, .55}}};
constexpr std::array<double, 7>     FLICK{{1, .72, .3, .95, .5, .8, .22}};
struct CrackPoint {
    double x, y, d;
};
struct Crack {
    std::vector<CrackPoint> pts;
    double                  w;
};
void grow(std::vector<Crack>& out, Rng& R, double x, double y, double a, double len, double w, double d, int depth) {
    std::vector<CrackPoint> pts{{x, y, d}};
    for (double s = 0; s < len; s += 1.5) {
        a += R.range(-.32, .32);
        x += std::cos(a) * 1.5;
        y = clamp(y + std::sin(a) * .45, 0, 5);
        d += 1.5;
        pts.push_back({x, y, d});
        if (depth < 2 && s > 3 && R.isChance(.09)) {
            const double angle = a + R.signedRange(.5, 1.2), l = (len - s) * R.range(.3, .6);
            grow(out, R, x, y, angle, l, w * .6, d, depth + 1);
        }
    }
    out.push_back({std::move(pts), w});
}
void sigil(Paint& p, double t, std::span<const uint32_t> C, double k, bool near) {
    const double reveal = clamp(t / .22), spin = t * .8, burnt = t > S1 ? t - S1 : -1;
    if (burnt > .45) return;
    const double a0   = burnt < 0 ? 1 : burnt < .07 ? 1 : 1 - (burnt - .07) / .38;
    const bool   hot  = burnt >= 0 && burnt < .07;
    const auto   on   = [&](double a) { return (std::sin(a) >= 0) == near; };
    const auto   from = [](double a) {
        return std::abs(std::fmod(std::fmod(a - PI / 2, TAU) + TAU + PI, TAU) - PI) / PI;
    };
    for (int i = 0; i < 22; ++i) {
        const double a = (i / 22.) * TAU + spin;
        if (!on(a) || from(a) > reveal) continue;
        for (double da : {0., .06, .12}) {
            const double x = std::cos(a + da) * RX, y = std::sin(a + da) * RX * SY;
            p.dot(x, y, hot ? WHITE : near ? (k > .7 ? C[6] : C[5]) : k > .7 ? C[4] : C[3], a0);
            if (near) p.dot(x, y + 1, hot ? C[6] : C[2], a0);
        }
        if (i % 3 == 0) p.glow(std::cos(a) * RX, std::sin(a) * RX * SY, 5, C[4], .3 * k * a0);
    }
    for (int j = 0; j < 4; ++j) {
        const double a = spin * -.5 + (j * TAU) / 4 + PI / 4;
        if (!on(a) || reveal < 1) continue;
        p.line(std::cos(a) * (RX - 5), std::sin(a) * (RX - 5) * SY, std::cos(a) * (RX + 5), std::sin(a) * (RX + 5) * SY,
               hot    ? WHITE
               : near ? C[6]
                      : C[4],
               a0);
    }
    if (t > .3)
        for (double a = 0; a < TAU; a += .09) {
            if (!on(a)) continue;
            p.dot(std::cos(a) * 14, std::sin(a) * 14 * SY,
                  hot    ? WHITE
                  : near ? C[4]
                         : C[2],
                  a0 * clamp((t - .3) / .12));
        }
    if (near && burnt < 0) {
        const double pulse = .6 + .4 * std::sin(t * (10 + 30 * k));
        p.px(0, 0, k > .5 ? WHITE : C[6]);
        for (auto d : std::array<Point, 3>{{{1, 0}, {-1, 0}, {0, -1}}}) p.px(d.x, d.y, C[5]);
        p.glow(0, -1, 10 + 10 * k, C[5], .3 + .4 * k * pulse);
    }
}
std::vector<lightning::Path> keep(std::vector<lightning::Path> paths) {
    std::vector<lightning::Path> out;
    for (auto& q : paths) {
        std::vector<Point> pts;
        for (auto pt : q.pts) {
            if (std::abs(pt.x) > W - 5 || pt.y > -3 || pt.y < TOP + 6) break;
            pts.push_back(pt);
        }
        if (q.w >= 1 || pts.size() > 3)
            out.push_back(q.w >= 1 ? std::move(q) : lightning::Path{std::move(pts), q.w, q.depth});
    }
    return out;
}
void trace(Paint& p, std::span<const Point> pts, uint32_t c, int from, int to) {
    for (int i = std::max(0, from); i < std::min(to, int(pts.size()) - 1); ++i)
        p.line(pts[size_t(i)].x, pts[size_t(i)].y, pts[size_t(i + 1)].x, pts[size_t(i + 1)].y, c);
}
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct Now {
    int    i   = -1;
    double age = 0, k = 0;
};
struct LightningStrike final : Kernel {
    std::vector<uint32_t>                       C;
    uint32_t                                    seed;
    Rng                                         R;
    Particles                                   ps, hot;
    Lights                                      lights;
    double                                      X0, t = 0;
    int                                         struck = -1;
    std::array<std::vector<lightning::Path>, 3> bolts;
    std::vector<Crack>                          tree;
    std::vector<double>                         gone;
    explicit LightningStrike(const PixelFxSettings& o) : C(o.palette), seed(o.seed), R(o.seed) {
        ps.sparks        = o.lightning.sparks;
        const double x   = R.range(8, 18);
        X0               = x * (R.isChance(.5) ? 1 : -1);
        bolts[0]         = keep(lightning::zigzag(R, X0, TOP, 0, -1, .17, .5));
        const auto trunk = bolts[0][0].pts;
        for (int j = 1; j < 3; ++j) {
            auto         pts = trunk;
            const double dx  = j * 1.5;
            for (auto& v : pts) v.x += dx * std::sin((PI * v.y) / TOP);
            bolts[size_t(j)].push_back({std::move(pts), 1, 0});
        }
        for (int i = 0; i < 6; ++i) {
            const double xx = R.range(-1, 1), y = R.range(0, 1), a = i % 2 ? R.range(-.5, .7) : PI + R.range(-.7, .5),
                         len = REACH * R.range(.55, 1);
            grow(tree, R, xx, y, a, len, 1, 0, 0);
        }
        const auto& end = bolts[2][0].pts;
        for (size_t i = 0; i < end.size(); ++i)
            gone.push_back(STROKES.back().t + .25 +
                           .75 * (.55 * rand({seed, uint32_t(i), 5}) + .45 * (1 - end[i].y / TOP)));
    }
    void emit(Particles& target, const Emission& e) {
        target.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay);
    }
    Now now() const {
        for (int i = 2; i >= 0; --i)
            if (t >= STROKES[size_t(i)].t) {
                const auto&  s = STROKES[size_t(i)];
                const double a = t - s.t, frame = std::floor(a * 30 + 1e-4);
                return {i, a, frame >= 0 && frame < double(FLICK.size()) ? FLICK[size_t(frame)] * s.k : 0};
            }
        return {};
    }
    double charge() const { return std::pow(clamp(t / S1), 1.5); }
    double heatAt() const {
        double h = 0;
        for (int i = 0; i <= struck; ++i) {
            const auto& s = STROKES[size_t(i)];
            h             = std::max(h, s.k * std::pow(clamp(1 - (t - s.t) / 1.6), 1.4));
        }
        return h;
    }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 3> STONE{{hex(0x655b7a), hex(0x4a4263), hex(0x342d45)}};
        for (int i = 0; i < 3; ++i) {
            const auto& s = STROKES[size_t(i)];
            if (struck >= i || t < s.t) continue;
            struck         = i;
            const double k = s.k;
            lights.add(0, -50, 120, C[4], .55 * k, .4);
            lights.add(0, -3, 24, WHITE, .5 * k, .1);
            for (int j = 0; j < jsRound(44 * k); ++j) {
                const double a = -PI / 2 + R.range(-1.3, 1.3), sp = R.range(110, 320) * (.65 + .35 * k);
                emit(ps, {ParticleKind::Spark, R.range(-1.5, 1.5), -2, std::cos(a) * sp, std::sin(a) * sp,
                          R.range(.2, .55)});
            }
            for (int j = 0; j < jsRound(12 * k); ++j) {
                const double a = -PI / 2 + R.range(-1.2, 1.2), sp = R.range(70, 220);
                emit(hot,
                     {ParticleKind::Spark, R.range(-2, 2), -1, std::cos(a) * sp, std::sin(a) * sp, R.range(.5, 1)});
            }
            for (int j = 0; j < jsRound(5 * k); ++j)
                emit(hot,
                     {ParticleKind::Ember, R.range(-4, 4), -2, R.range(-25, 25), -R.range(30, 80), R.range(.8, 1.5)});
            for (int j = 0; j < jsRound(18 * k); ++j) {
                const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(50, 190);
                emit(ps, {ParticleKind::Grit, R.range(-3, 3), -1, std::cos(a) * sp, std::sin(a) * sp, R.range(1, 1.5),
                          STONE[size_t(std::floor(R.next() * 3))], R.isChance(.35) ? 2. : 1.});
            }
            for (int j = 0; j < 8; ++j)
                emit(ps, {ParticleKind::Dust, R.range(-3, 3), -1, R.signedRange(30, 110), -R.range(4, 24),
                          R.range(.4, .8), hex(0x6e6382), R.range(1.5, 3)});
            for (int j = 0; j < jsRound(4 * k); ++j)
                emit(ps, {ParticleKind::Smoke, R.range(-8, 8), -4, R.range(-10, 10), -R.range(10, 25),
                          R.range(1.1, 1.6), mix(hex(0x5b5566), C[2], .3), R.range(3, 6), R.range(.05, .25)});
        }
        if (struck >= 0 && t < 1.7 && R.isChance(6 * dt))
            emit(ps, {ParticleKind::Smoke, R.range(-8, 8), -2, R.range(-4, 4), -R.range(9, 17), R.range(1, 1.3), 0,
                      R.range(2, 3.5)});
        const auto& end = bolts[2][0].pts;
        for (size_t i = 0; i + 1 < gone.size(); ++i)
            if (gone[i] <= t && gone[i] > t - dt && rand({seed, uint32_t(i), 6}) < .5) {
                const auto v = end[i];
                emit(ps, {ParticleKind::Mote, v.x, v.y, R.range(-8, 8), R.range(6, 22), R.range(.4, .8),
                          R.isChance(.5) ? C[5] : C[4], R.range(.5, .9)});
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        hot.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double dark = .5 * smooth(.46, .66, t) * (1 - smooth(S1 - .01, S1 + .02, t));
        if (dark > .01)
            for (int y = -228; y < 36; ++y)
                for (int x = -120; x < 120; ++x) {
                    const double d = jsHypot2(x / W, (y + 96) / 132.), a = dark * (1 - smooth(.55, 1, d));
                    if (a > .01) p.blend(x + .5, y + .5, hex(0x04030a), a);
                }
        sigil(p, t, C, charge(), false);
        if (struck >= 0) {
            const auto   s     = now();
            const double front = REACH * ease::outCubic(clamp((t - S1) / .35)), T0 = heatAt(),
                         fade = 1 - smooth(2.3, 2.95, t),
                         elec = s.age < .14 ? STROKES[size_t(s.i)].k * (1 - s.age / .14) : 0;
            for (const auto& br : tree)
                for (const auto v : br.pts) {
                    if (v.d > front) break;
                    const double q = v.d / REACH, n = rand({jsUint(jsRound(v.x * 2)), jsUint(jsRound(v.y * 2)), 9});
                    const bool   fresh = v.d > front - 6 && front < REACH;
                    const double T     = T0 * (1.15 - q) * (br.w < 1 ? .8 : 1) - n * .25;
                    const auto   h = lightning::heat(T), c = fresh || (elec > .3 && n < .85) ? (q < .5 ? WHITE : C[6])
                                                             : h                             ? h
                                                                                             : hex(0x1c1522);
                    p.dot(v.x, v.y + 1, hex(0x110c16), fade);
                    p.dot(v.x, v.y, c, fade);
                    if (br.w >= 1 && v.d < 9) p.dot(v.x, v.y + 2, hex(0x110c16), fade);
                    if ((fresh || elec > .3) && (jsUint(jsRound(v.d)) & 3u) == 0)
                        p.glow(v.x, v.y, 7, C[5], .4);
                    else if (T > .2 && (jsUint(jsRound(v.d)) & 3u) == 0)
                        p.glow(v.x, v.y, 5, c, .2 * T);
                }
            lightning::scorch(p, 0, 0, 12, T0, fade);
            if (T0 > 0) p.glow(0, -2, 34, lightning::heat(.55), .3 * T0);
        }
        ps.draw(p, Paint::Layer::Back);
        hot.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const auto     s = now();
        const uint32_t f = jsUint(std::floor(t * 30 + 1e-4));
        const double   k = charge();
        sigil(p, t, C, k, true);
        if (t > .15 && t < S1) {
            Rng r(hash({seed, 61, f}));
            for (int i = 0; i < jsRound(1 + 4 * k); ++i) {
                const double a = r.range(0, TAU), x = std::cos(a) * RX, y = std::sin(a) * RX * SY,
                             x1 = x + r.range(-2.5, 2.5), y1 = y - r.range(4, 9) * (.6 + k);
                const auto   b = lightning::zigzag(r, x, y, x1, y1, .45, .3);
                lightning::crackle(p, b, .45 + .4 * k, C, .3);
            }
            p.glow(0, -2, 36, C[4], .12 + .3 * k);
        }
        if (t >= LEAD0 && t < S1) {
            const double u = clamp((t - LEAD0) / (S1 - LEAD0)), st = std::min(9., std::floor(u * 9) + 1),
                         fresh = 1 - std::fmod(u * 9, 1), reach = TOP + (MEET - TOP) * std::pow(st / 9, 1.2),
                         prev = TOP + (MEET - TOP) * std::pow((st - 1) / 9, 1.2);
            for (const auto& q : bolts[0]) {
                const bool tk = q.w >= 1;
                for (size_t i = 0; i + 1 < q.pts.size(); ++i) {
                    const auto a = q.pts[i], b = q.pts[i + 1];
                    if (b.y > reach) {
                        if (a.y <= reach && i > 0) {
                            p.px(a.x, a.y, WHITE);
                            p.glow(a.x, a.y, 7, C[5], .5 * fresh + .15);
                        }
                        break;
                    }
                    const bool young = b.y > prev;
                    p.line(a.x, a.y, b.x, b.y, young ? (fresh > .4 ? (tk ? C[6] : C[5]) : C[4]) : tk ? C[3] : C[2]);
                    if (i % 4 == 0 && a.y > TOP + 14)
                        p.glow(a.x, a.y, young ? 8 : 5, C[4], young ? .3 * fresh + .1 : .12);
                }
            }
            if (t > .62) {
                const double h = -MEET * ease::outQuad((t - .62) / (S1 - .62));
                Rng          r(hash({seed, 77, f}));
                const double x1  = r.range(-2, 2);
                const auto   b   = lightning::zigzag(r, 0, -1, x1, -std::max(3., h), .3, .3);
                const auto   tip = b[0].pts.back();
                lightning::crackle(p, b, .85, C, .45);
                p.px(tip.x, tip.y, WHITE);
                p.glow(tip.x, tip.y, 9, C[5], .5);
            }
        }
        int next = -1;
        for (int i = 0; i < 3; ++i)
            if (STROKES[size_t(i)].t > t) {
                next = i;
                break;
            }
        if (next > 0 && t > STROKES[size_t(next)].t - .06) {
            const auto&  tr = bolts[size_t(next)][0].pts;
            const double u  = (t - (STROKES[size_t(next)].t - .06)) / .06;
            const int    i1 = int(std::floor(u * double(tr.size())));
            trace(p, tr, C[3], 0, int(tr.size()) - 1);
            trace(p, tr, C[6], i1 - 18, i1);
            for (int i = std::max(0, i1 - 18); i < std::min(int(tr.size()), i1); i += 4)
                if (tr[size_t(i)].y > TOP + 14) p.glow(tr[size_t(i)].x, tr[size_t(i)].y, 9, C[4], .35);
        } else if (s.i >= 0 && s.k > 0) {
            const bool        hero = s.i == 0 && s.age < 1. / 30;
            lightning::Stroke o;
            o.hero = hero;
            o.core = s.k > .6 ? (hero ? 1.2 : .8) : 0;
            o.minY = TOP + 14;
            lightning::stroke(p, bolts[size_t(s.i)], s.k, C, s.i == 0 ? 1.5 : 1.2, o);
            const double r = 4.5 * s.k;
            p.circle(0, -1, r + 1.5, C[2]);
            p.circle(0, -1, r + .5, C[5]);
            p.circle(0, -1, r * .65, WHITE);
            if (s.age < .07) p.sparkle(0, -2, jsRound(12 * s.k * (1 - s.age / .07)), C[6], C[4], .25);
            p.glow(0, -16, 96, C[3], .42 * s.k);
            p.glow(0, -3, 22, C[5], .3 * s.k);
            if (hero) p.glow(X0 * .5, -110, 170, C[3], .3);
        } else if (s.i >= 0) {
            const bool   crumble = s.i == 2;
            const double cool    = clamp((s.age - .23) / .45);
            const auto&  tr      = bolts[size_t(s.i)][0].pts;
            for (size_t i = 0; i + 1 < tr.size(); ++i) {
                if (crumble && t >= gone[i]) continue;
                const double h =
                    crumble ? 1 - clamp((t - STROKES.back().t - .23) / (gone[i] - STROKES.back().t - .23)) : 1 - cool;
                p.line(tr[i].x, tr[i].y, tr[i + 1].x, tr[i + 1].y,
                       h > .7    ? C[5]
                       : h > .4  ? C[4]
                       : h > .15 ? C[3]
                                 : C[2]);
                if (i % 6 == 0 && h > .3 && tr[i].y > TOP + 14) p.glow(tr[i].x, tr[i].y, 6, C[4], .15 * h);
            }
        }
        if (s.i >= 0 && s.age < .4) {
            Rng          r(hash({seed, 91, f}));
            const double run = s.i == 0 ? REACH * ease::outCubic(clamp(s.age / .35)) : REACH * clamp(s.age / .22);
            for (const auto& br : tree) {
                if (br.w < 1 || r.next() < .3) continue;
                size_t j1 = 0;
                while (j1 < br.pts.size() && br.pts[j1].d <= run) ++j1;
                const int          j0 = std::max(0, int(j1) - r.integer(7, 13));
                std::vector<Point> pts;
                for (size_t j = size_t(j0); j < j1; ++j) {
                    const double x = br.pts[j].x + r.range(-.6, .6), y = br.pts[j].y - 1.5 - r.range(0, 2.5);
                    pts.push_back({x, y});
                }
                if (pts.size() > 1) lightning::crackle(p, {{std::move(pts), 1, 0}}, .9 * (1 - s.age / .4) + .1, C, .35);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        hot.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& lightningStrikeDescriptor() {
    static const std::array<Option, 1> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks}}};
    static const Descriptor            d{
        {PixelFxKind::LightningStrike, "lightning/lightning-strike", 240, 264, 120, 228, 3, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 5;
            s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
            s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<LightningStrike>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
