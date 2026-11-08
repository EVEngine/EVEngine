// eve-hot-path
// Native adaptation of pixelfx weather/storm.js and weather/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                D = 10, SLANT = .42, TLEAD = 2.62, T1 = 3, T2 = 3.3, TSHEET = 6.4, TFAR = 8.6;
constexpr double                BX0 = 62, BY0 = -136, BX1 = 52, BY1 = 3;
constexpr PixelFxInfo           SOURCE{PixelFxKind::Storm, "weather/storm", 320, 180, 160, 170, D, true};
constexpr std::array<double, 6> F1{{1, .62, .95, .4, .7, .22}};
constexpr std::array<double, 5> F2{{.92, .5, .8, .3, .12}};
double                          env(double t, double t0, std::span<const double> e) {
    const double i = std::floor((t - t0) * 30 + 1e-3);
    return i >= 0 && i < double(e.size()) ? e[size_t(i)] : 0;
}
double flashAt(double t) { return std::max(env(t, T1, F1), env(t, T2, F2)); }
double square(double a) { return a * a; }
double sheetAt(double t) {
    const double a = t - TSHEET;
    if (a < 0 || a > .9) return 0;
    const double p1 = std::exp(-square((a - .08) / .05)), p2 = std::exp(-square((a - .3) / .09)),
                 tail = a > .3 ? std::exp(-(a - .3) * 6) * .5 : 0;
    return std::min(1., .7 * p1 + p2 + tail) * (std::fmod(std::floor(a * 30), 3) == 1 ? .75 : 1);
}
double blink(double a, double k) {
    return a < 0 || a > .3 ? 0 : k * (std::exp(-square((a - .06) / .04)) + .65 * std::exp(-square((a - .18) / .05)));
}
struct Path {
    std::vector<Point> pts;
    double             w;
    int                depth;
};
std::vector<Path> tree(Rng& R, double x0, double y0, double x1, double y1, double jag = .3, double forks = .4,
                       double w = 1, int depth = 0) {
    const double       L = jsHypot2(x1 - x0, y1 - y0);
    std::vector<Point> pts{{x0, y0}, {x1, y1}};
    double             off = L * jag;
    while (double(pts.size()) < 2 + L / 3) {
        std::vector<Point> next{pts.front()};
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const auto   a = pts[i], b = pts[i + 1];
            const double raw = jsHypot2(b.x - a.x, b.y - a.y), l = raw == 0 ? 1 : raw, o = R.range(-off, off);
            next.push_back({(a.x + b.x) / 2 + (-(b.y - a.y) / l) * o, (a.y + b.y) / 2 + ((b.x - a.x) / l) * o});
            next.push_back(b);
        }
        pts = std::move(next);
        off *= .52;
    }
    std::vector<Path> out{{pts, w, depth}};
    if (depth < 2)
        for (size_t i = 2; i + 3 < pts.size(); ++i)
            if (R.isChance(forks / (1 + depth) / std::max(4., double(pts.size()) / 6))) {
                const auto   a = pts[i], b = pts[std::min(pts.size() - 1, i + 3)];
                const double angle = std::atan2(b.y - a.y, b.x - a.x) + R.signedRange(.35, .95),
                             len   = (L - (double(i) / double(pts.size())) * L) * R.range(.25, .55);
                auto children = tree(R, a.x, a.y, a.x + std::cos(angle) * len, a.y + std::sin(angle) * len, jag * 1.1,
                                     forks, w * .55, depth + 1);
                out.insert(out.end(), std::make_move_iterator(children.begin()),
                           std::make_move_iterator(children.end()));
            }
    return out;
}
std::vector<Path> channel(uint32_t S) {
    Rng        R(S * 31 + 1);
    auto       paths = tree(R, BX0, BY0, BX1, BY1, .26, .18);
    const auto pts   = paths[0].pts;
    const int  side  = R.sign();
    int        i     = 0;
    for (double u : {.2, .38, .55}) {
        const size_t k = size_t(std::floor(u * double(pts.size() - 1)));
        const auto   a = pts[k], b = pts[std::min(pts.size() - 1, k + 3)];
        const double angle    = std::atan2(b.y - a.y, b.x - a.x) + (i % 2 ? -side : side) * R.range(.6, 1),
                     l        = R.range(36, 56) * (1 - u * .45);
        auto         children = tree(R, a.x, a.y, a.x + std::cos(angle) * l, a.y + std::sin(angle) * l, .3, .5, .6, 1);
        paths.insert(paths.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
        ++i;
    }
    return paths;
}
void splash(Paint& q, double x, double z, double age, double size, uint32_t hot, uint32_t cool, double hash) {
    const double u = age / (.3 + .12 * size);
    if (u >= 1) return;
    if (age < .07) {
        q.px(x, z - 1, hot);
        if (size > .3) {
            q.px(x - 1, z - 1 - (age > .035 ? 1 : 0), hot);
            q.px(x + 1, z - 1 - (age > .035 ? 1 : 0), hot);
        }
        if (size > .65 && age > .03) {
            q.px(x - 2, z - 2, cool);
            q.px(x + 2, z - 2, cool);
        }
    }
    const double rx = 1 + (1.2 + 3.6 * size) * std::sqrt(u), ry = std::max(.5, rx * .26), n = std::ceil(rx * 6.5);
    for (int j = 0; j < n; ++j) {
        const double a = (j / n) * TAU, s = std::sin(a);
        if (s < -.25) continue;
        q.dot(x + std::cos(a) * rx, z + s * ry, u < .3 && s > 0 ? hot : cool,
              u < .45 ? (s > .2 ? 1 : .7) : (1 - u) * 1.6 * (s > .2 ? 1 : .6));
    }
    const uint32_t H = jsUint(std::floor(hash * 1e6));
    for (uint32_t d = 0; d < (size > .55 ? 3u : 2u); ++d) {
        const double vx = (rand({H, d, 1}) - .5) * 2 * (25 + 35 * size),
                     vy = -(55 + rand({H, d, 2}) * 70 * (.5 + size)), y = z - 1 + vy * age + 475 * age * age;
        if (y > z - .5 || age > .34) continue;
        const double xx = x + vx * age;
        q.px(xx, y, age < .12 ? hot : cool);
        if (vy + 950 * age < -40) q.dot(xx - vx * .012, y + 1, cool, .7);
    }
}
struct RollRow {
    double             base, wob;
    uint32_t           n;
    double             rx0, rx1, ry, drift, l0, l1;
    std::array<int, 4> tone;
    bool               tex, haze;
};
constexpr std::array<RollRow, 3> ROWS{{{-161, 3, 18, 16, 30, .8, 15, 4, 7, {1, 2, 3, 4}, true, false},
                                       {-150, 3, 22, 10, 20, .85, 10, 3.5, 6.5, {1, 2, 3, 5}, true, false},
                                       {-140, 3, 26, 7, 14, .9, 6, 3, 6, {2, 2, 3, 5}, false, true}}};
struct Storm final : AnalyticKernel {
    weather::Field    veilField, deckField, floorField, mistField;
    std::vector<Path> first, second, crawl;
    explicit Storm(uint32_t S)
        : veilField(S + 11, weather::LOW_OCTAVES),
          deckField(S * 3 + 1, weather::OCTAVES),
          floorField(S + 3, weather::OCTAVES),
          mistField(S + 4, weather::LOW_OCTAVES),
          first(channel(S)) {
        Rng  R(S * 31 + 2);
        auto p = tree(R, BX0, BY0, BX1, BY1, .3, .3);
        second.push_back(first[0]);
        for (size_t i = 1; i < std::min(size_t(3), p.size()); ++i) second.push_back(std::move(p[i]));
        Rng Rc(S * 7 + 3);
        crawl = tree(Rc, -150, -138, -26, -134, .2, .7);
    }
    void rain(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, std::span<const uint32_t> V, double f,
              bool near) const {
        const auto litAt = [&](double x) {
            const double L = f * (.3 + .7 * std::max(0., 1 - std::abs(x - BX1) / 170));
            return L > .45 ? 2 : L > .15 ? 1 : 0;
        };
        const double ux = SLANT / jsHypot2(SLANT, 1), uy = 1 / jsHypot2(SLANT, 1);
        if (!near) {
            const std::array<uint32_t, 3> body{{C[4], C[4], C[5]}}, head{{C[5], C[6], C[6]}};
            weather::trips(t, D, 300, S * 7 + 1, 22, 28, [&](double u, double k, int m, uint32_t i) {
                const uint32_t j    = jsUint(wrap(k, m));
                const double   stop = -14 - rand({S, i, j, 1}) * 90, y = -176 + (stop + 176) * u,
                               x = -170 + rand({S, i, j, 2}) * 360 - (stop - y) * SLANT, L = 3 + rand({S, i, j, 3}) * 2;
                const auto     lit = litAt(x);
                q.line(x - ux * L, y - uy * L, x - ux * L * .4, y - uy * L * .4, body[lit]);
                q.line(x - ux * L * .4, y - uy * L * .4, x, y, head[lit]);
            });
            return;
        }
        const std::array<uint32_t, 3> body{{C[6], C[6], V[4]}}, head{{C[7], WHITE, WHITE}}, tail{{C[4], C[5], V[3]}};
        weather::trips(t, D, 84, S * 7 + 2, 30, 38, [&](double u, double k, int m, uint32_t i) {
            const uint32_t j = jsUint(wrap(k, m));
            const double   z = 1 + rand({S, i, j, 1}) * 4, y = -180 + (z + 180) * u,
                           x = -175 + rand({S, i, j, 2}) * 370 - (z - y) * SLANT, L = 9 + rand({S, i, j, 3}) * 5;
            const auto     lit = litAt(x);
            q.line(x - ux * L, y - uy * L, x - ux * L * .55, y - uy * L * .55, tail[lit]);
            q.line(x - ux * L * .55, y - uy * L * .55, x - ux, y - uy, body[lit]);
            q.px(x, y, head[lit]);
            const double age = u * (D / m);
            if (age < .4) {
                const uint32_t jp = jsUint(wrap(k - 1, m));
                const double   zp = 1 + rand({S, i, jp, 1}) * 4, xp = -175 + rand({S, i, jp, 2}) * 370;
                const auto     l = litAt(xp);
                splash(q, xp, zp, age, .25 + (zp - 1) / 5.3, l ? WHITE : C[7],
                       std::array<uint32_t, 3>{{C[5], C[6], V[4]}}[l], rand({S, i, jp, 4}));
            }
        });
    }
    void stroke(Paint& q, const std::vector<Path>& paths, double k, std::span<const uint32_t> V) const {
        for (int pass = 0; pass < 3; ++pass)
            for (const auto& p : paths) {
                const bool trunk = p.w >= 1;
                if (!trunk && (k < .4 || (pass == 0 && p.w < .5))) continue;
                for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
                    const auto   a = p.pts[i], b = p.pts[i + 1];
                    const double l = std::max(1., jsHypot2(b.x - a.x, b.y - a.y));
                    for (double d = 0; d <= l; d += .5) {
                        const double x = a.x + ((b.x - a.x) * d) / l, y = a.y + ((b.y - a.y) * d) / l;
                        if (pass == 0)
                            q.circle(x, y, trunk ? (k > .6 ? 2.3 : 1.7) : 1.2, k > .6 ? V[2] : V[1]);
                        else if (pass == 1)
                            q.circle(x, y, trunk ? (k > .6 ? 1.4 : .9) : .6, trunk ? V[4] : V[3]);
                        else if (trunk) {
                            q.px(x, y, k > .3 ? WHITE : V[4]);
                            if (k > .85) q.px(x + 1, y, WHITE);
                        } else if (p.w > .4 && k > .6)
                            q.px(x, y, V[4]);
                    }
                    if (pass == 0 && i % 3 == 0) q.glow(a.x, a.y, trunk ? 18 : 8, V[3], (trunk ? .5 : .3) * k * p.w);
                }
            }
    }
    void bolt(Paint& q, double t, std::span<const uint32_t> V) const {
        if (t < T1) {
            const double reach = BY0 + (BY1 - BY0) * std::pow(std::floor(((t - TLEAD) / (T1 - TLEAD)) * 9) / 9, 1.25);
            for (const auto& p : first)
                for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
                    const auto a = p.pts[i], b = p.pts[i + 1];
                    if (b.y > reach) break;
                    q.line(a.x, a.y, b.x, b.y, p.w >= 1 ? V[2] : V[1]);
                    if (i % 4 == 0) q.glow(a.x, a.y, 6, V[3], .22 * p.w);
                }
            return;
        }
        const bool   restrike = t >= T2;
        const double a = t - (restrike ? T2 : T1), k = restrike ? env(t, T2, F2) : env(t, T1, F1);
        const auto&  paths = restrike ? second : first;
        if (k > 0)
            stroke(q, paths, k, V);
        else if (a < .5) {
            const double g = 1 - a / .5;
            for (const auto& p : paths)
                if (p.w >= 1)
                    for (const auto v : p.pts) q.dot(v.x, v.y, V[1], g * .9);
        }
    }
    void crawler(Paint& q, double t, std::span<const uint32_t> V) const {
        const double a = t - TSHEET - .22;
        if (a < 0 || a > .17) return;
        const double reach = -150 + 130 * std::min(1., (std::floor(a * 30) + 1) / 3), k = a < .1 ? 1 : .5;
        for (const auto& p : crawl)
            for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
                const auto aa = p.pts[i], b = p.pts[i + 1];
                if (aa.x > reach) break;
                q.line(aa.x, aa.y, b.x, b.y, p.w >= 1 && k > .6 ? V[4] : V[3]);
                if (p.w >= 1) q.dot(aa.x, aa.y + 1, V[2], k);
                if (i % 3 == 0) q.glow(aa.x, aa.y, 10, V[3], .35 * k * p.w);
            }
    }
    void strike(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, std::span<const uint32_t> V) const {
        const double a = t - T1;
        if (a < 0 || a > 1.8) return;
        const double k = flashAt(t);
        if (k > 0) {
            Rng Rc(S * 977 + jsUint(std::floor(t * 30 + 1e-3)));
            for (int i = 0; i < 4; ++i) {
                const int    side = i % 2 ? 1 : -1;
                const double l = Rc.range(10, 30) * (.6 + .4 * k), ey = BY1 + Rc.range(-2, 3);
                for (const auto& p : tree(Rc, BX1 + side, BY1 - 1, BX1 + side * l, ey, .35, .5, .6, 1)) {
                    q.polyline(p.pts, k > .6 && p.w > .5 ? V[4] : V[3]);
                    const auto mid = p.pts[p.pts.size() >> 1];
                    q.glow(mid.x, mid.y, 8, V[3], .3 * k);
                }
            }
            q.circle(BX1, BY1 - 1, 2 + 3 * k, V[4]);
            q.circle(BX1, BY1 - 1, 1 + 2 * k, WHITE);
            q.glow(BX1, BY1 - 12, 95, V[2], .55 * k);
            q.glow(BX1, BY1 - 4, 40, V[3], .5 * k);
            q.glow(BX1, BY1 - 2, 14, WHITE, .6 * k);
        }
        Particles ps;
        Rng       R(S * 13 + 5);
        ps.sparks            = {WHITE, V[4], V[3], V[2]};
        ps.haze              = V[2];
        const uint32_t steam = mix(C[4], V[3], .3);
        const auto     burst = [&](int n, double pow) {
            for (int i = 0; i < n; ++i) {
                const double an = -PI / 2 + R.range(-1.35, 1.35), sp = R.range(110, 420) * pow, life = R.range(.18, .6);
                ps.spawn(R, ParticleKind::Spark, BX1, BY1 - 1, std::cos(an) * sp, std::sin(an) * sp, life);
            }
            for (int i = 0; i < 6 * pow; ++i) {
                const int    sd = R.sign();
                const double x = BX1 + sd * R.range(2, 8), y = BY1 - R.range(1, 3), vx = sd * R.range(30, 90),
                             vy = -R.range(4, 16), life = R.range(.6, 1.2), size = R.range(2.5, 4.5),
                             delay = R.range(0, .08);
                ps.spawn(R, ParticleKind::Vapour, x, y, vx, vy, life, steam, size, delay);
            }
        };
        burst(48, 1);
        bool             didSecond = false;
        constexpr double dt        = 1. / 120;
        for (double tt = 0; tt + dt <= a; tt += dt) {
            if (!didSecond && tt >= T2 - T1) {
                didSecond = true;
                burst(16, .7);
            }
            ps.update(dt, R, [](double) { return 4.; }, [](double, double) {});
        }
        ps.draw(q);
        if (a < 1.6) q.glow(BX1, BY1, 16, V[2], .4 * square(1 - a / 1.6));
    }
    void floor(Paint& q, double t, std::span<const uint32_t> C, std::span<const uint32_t> V, double f) const {
        const double wind = (256 * 3 * t) / D;
        weather::pass(q, SOURCE, -14, -.5, [&](double x, double y, weather::Output& out) {
            const double m = mistField.sample(x * 1.1 - wind, y * 4 + 9) - .4;
            if (m <= 0) return;
            out.c     = f > .1 ? mix(C[4], V[3], f) : C[4];
            out.a     = std::min(.3, m * .9) * (1 + y / 14);
            out.blend = true;
        });
        weather::pass(q, SOURCE, 0, 9, [&](double x, double y, weather::Output& out) {
            const auto pool = [&](double yy) { return floorField.sample(x * 1.3 + 31, yy * 9) > .6 - yy * .006; };
            const bool wet = pool(y), lip = wet && !pool(y - 1);
            out.blend = true;
            out.c     = C[1];
            out.a     = .5;
            if (y < 1) {
                out.c     = floorField.sample(x * 2, 50) > .5 ? C[4] : C[3];
                out.a     = 1;
                out.blend = false;
            } else if (wet) {
                const double d = std::abs(x - BX1) / 150;
                if (f > .06) {
                    out.c     = (d < .25 && f > .5) || lip ? V[4] : V[3];
                    out.a     = clamp(f * (1.35 - d) * (lip ? 1.4 : 1));
                    out.blend = false;
                } else {
                    out.c = lip ? C[5] : C[3];
                    out.a = lip ? 1 : .7;
                }
                out.l = V[3];
                out.k = f * .25 * std::max(0., 1 - d);
            }
        });
        if (f > .2)
            for (uint32_t y = 1; y < 10; ++y)
                if (rand({y, jsUint(std::floor(t * 30)), 3}) < .8)
                    q.dot(BX1 + std::sin(y * 1.7 + t * 40) * 1.2, y, y < 4 ? WHITE : V[4], f * (1.1 - y / 12.));
    }
    template <class Light>
    void deck(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, std::span<const uint32_t> V, Light litAt,
              bool lit) const {
        struct Roll {
            double x, h, rx;
        };
        std::array<std::vector<Roll>, 3> rolls;
        for (uint32_t j = 0; j < 3; ++j) {
            const auto& r = ROWS[j];
            for (uint32_t i = 0; i < r.n; ++i) {
                const double L = r.l0 + rand({S, j, i, 1}) * (r.l1 - r.l0), age = wrap(t - rand({S, j, i, 2}) * D, D);
                if (age >= L) continue;
                const double u = age / L, g = std::pow(std::sin(PI * u), .6),
                             rx = r.rx0 + rand({S, j, i, 3}) * (r.rx1 - r.rx0), ry = rx * r.ry,
                             x = -200 + rand({S, j, i, 4}) * 400 + r.drift * (age - L / 2);
                rolls[j].push_back({x, ry * g * (.6 + .4 * rand({S, j, i, 5})), rx * (.8 + .2 * g)});
            }
        }
        const double                  drift = (256 * t) / D, mo = square(std::cos((PI * t) / D));
        const std::array<uint32_t, 9> ramp{{C[0], C[1], C[2], C[3], C[4], C[5], C[6], V[4], WHITE}};
        std::array<float, 3>          top{}, bot{}, bulge{};
        const auto                    B = weather::box(SOURCE);
        for (double x = B.x0; x < B.x1; ++x) {
            const double cx  = x + .5;
            double       low = -170;
            for (size_t j = 0; j < 3; ++j) {
                const auto&  r   = ROWS[j];
                const double jj  = double(j),
                             und = (deckField.sample(cx * .55 + jj * 91, 3) * mo +
                                    deckField.sample(cx * .55 + jj * 91, 60) * (1 - mo) - .5) *
                                   2,
                             sag = 9 * std::exp(-square((cx - 40) / 95)) - 3,
                             c = r.base + sag * (.4 + jj * .3) + 7 * und +
                                 r.wob * 2 *
                                     (deckField.sample(cx * 1.8 + jj * 61 - drift * (.5 - jj * .1), 20 + jj * 17) - .5);
                double       bump = 0;
                for (const auto& b : rolls[j]) {
                    const double dx = (cx - b.x) / b.rx;
                    if (dx * dx < 1) bump = std::max(bump, b.h * std::sqrt(1 - dx * dx));
                }
                const double y1 = c + bump;
                top[j]          = -999;
                bot[j]          = float(y1);
                bulge[j]        = float(bump);
                low             = std::max(low, y1);
            }
            int    prev    = -1;
            double prevBot = -999;
            for (double y = B.y0; y < low; ++y) {
                const double cy = y + .5;
                int          j  = 0;
                while (j < 3 && !(cy >= top[size_t(j)] && cy < bot[size_t(j)])) ++j;
                if (j >= 3) {
                    prev = -1;
                    continue;
                }
                if (j != prev) {
                    if (prev >= 0 && prev < j) prevBot = cy;
                    prev = j;
                }
                const auto&  r = ROWS[size_t(j)];
                const double d = double(bot[size_t(j)]) - cy, th = weather::bayer(x, y), b = bulge[size_t(j)];
                int          i;
                if (d < 1)
                    i = b > 3 ? r.tone[3] : r.tone[3] - 1;
                else if (cy - prevBot < 1.5 && j > 0)
                    i = std::max(0, r.tone[0] - 1);
                else if (d < 2 + b * .25 + th * 2)
                    i = r.tone[2];
                else if (d < 6 + b * .4 + th * 3)
                    i = r.tone[1];
                else {
                    const double nn = deckField.sample(cx * .9 + j * 77 - drift * (.6 - j * .12), cy * 1.7 + j * 31);
                    i               = r.tone[0] + (r.tex && nn > .6 ? 1 : 0);
                }
                double lk = 0;
                if (lit) {
                    const double lv = litAt(cx, cy);
                    if (lv > .02) {
                        i  = std::min(8, i + int(jsRound(lv * (d < 1 ? 4.4 : d < 4 ? 3.8 : 3.1))));
                        lk = lv;
                    }
                }
                if (r.haze && lk == 0 && d >= 1)
                    q.blend(x, y, ramp[size_t(i)], .75);
                else
                    q.px(x, y, ramp[size_t(std::max(0, i))]);
                if (lk > .03) q.light(x, y, V[3], lk * (d < 3 ? .4 : .22));
            }
        }
    }
    void drawBack(Paint& p, double time, const PixelFxSettings& o) const override {
        const double t = jsRound(time * 3e4) / 3e4;
        auto         q = weather::clip(p, SOURCE);
        const auto&  C = o.palette;
        const auto&  V = o.weather.bolt;
        const double f = flashAt(t), sh = sheetAt(t), far = blink(t - TFAR, .45),
                     lead = t > TLEAD && t < T1 ? .25 + .2 * std::sin(t * 70) : 0,
                     pre  = t > TLEAD - .5 && t < TLEAD ? .12 * std::max(0., std::sin((t - TLEAD) * 41)) : 0;
        struct Light {
            double x, y, r, k;
        };
        std::array<Light, 4> src{};
        size_t               n = 0;
        if (f + lead + pre > 0) src[n++] = {BX0, BY0 - 6, 150, f + lead * .5 + pre};
        if (sh > 0) src[n++] = {-96, -146, 120, sh};
        if (far > 0) src[n++] = {128, -156, 80, far};
        const double far2 = blink(t - .9, .3);
        if (far2 > 0) src[n++] = {-40, -160, 70, far2};
        const auto litAt = [&](double x, double y) {
            double L = 0;
            for (size_t j = 0; j < n; ++j) {
                const auto&  s = src[j];
                const double d = jsHypot2(x - s.x, (y - s.y) * 1.5) / s.r;
                if (d < 1) L += s.k * (1 - d) * (1 - d);
            }
            return std::min(1., L);
        };
        if (f * .36 > .01) {
            const auto c = mix(V[2], V[3], .6);
            weather::pass(p, SOURCE, -170, 9, [&](double x, double, weather::Output& out) {
                const double d = std::abs(x - (BX0 + BX1) / 2) / 230;
                out.l          = c;
                out.k          = f * .36 * (.2 + .8 * std::pow(std::max(0., 1 - d), 2.2));
            });
        }
        if (sh > .05)
            weather::pass(p, SOURCE, -170, 0, [&](double x, double y, weather::Output& out) {
                const double d = jsHypot2(x + 96, (y + 150) * 1.3) / 170;
                if (d < 1) {
                    out.l = V[2];
                    out.k = sh * .3 * (1 - d) * (1 - d);
                }
            });
        const double shift = (256 * 2 * t) / D;
        const auto   glow  = mix(C[3], V[3], .5);
        weather::pass(p, SOURCE, -170, -.5, [&](double x, double y, weather::Output& out) {
            const double a    = veilField.sample((x - y * SLANT) * 1.9 - shift, y * .16 + 7),
                         dens = clamp(.45 + (a - .5) * 2.6);
            out.c = f > .05 ? mix(C[1], glow, std::min(1., f * dens * .7 * std::max(.25, 1 - std::abs(x - BX1) / 220)))
                            : C[1];
            out.a = .4 + .38 * dens;
            out.blend = true;
        });
        rain(q, t, o.seed, C, V, f, false);
        if (t > TLEAD && t < T2 + .7) bolt(q, t, V);
        deck(q, t, o.seed, C, V, litAt, n > 0);
        crawler(q, t, V);
        floor(q, t, C, V, f);
        strike(q, t, o.seed, C, V);
    }
    void draw(Paint& p, double time, const PixelFxSettings& o) const override {
        const double t = jsRound(time * 3e4) / 3e4;
        auto         q = weather::clip(p, SOURCE);
        rain(q, t, o.seed, o.palette, o.weather.bolt, flashAt(t), true);
    }
};
}  // namespace
const Descriptor& stormDescriptor() {
    static const std::array<Option, 1> options{{{"bolt", 1, 64, false, 6, &PixelFxWeatherSettings::bolt}}};
    static const Descriptor            d{SOURCE,
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 5;
                                  s.palette      = {hex(0x09080f), hex(0x121120), hex(0x1b1a30), hex(0x262541),
                                                    hex(0x363a5c), hex(0x4f5d8c), hex(0x7790c6), hex(0xc2d6ff)};
                                  s.weather.bolt = {hex(0x2a1a6e), hex(0x4835c2), hex(0x6a6cff),
                                                    hex(0x78b8ff), hex(0xc6ebff), WHITE};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Storm>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
