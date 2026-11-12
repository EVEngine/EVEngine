// eve-hot-path
// Native adaptation of pixelfx lightning/tesla-coil.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                P = 2.4, TY = -57, RR = 12, TR = 3.6, SY = .45, CTOP = -50, CBOT = -12;
constexpr std::array<double, 8> FL{{1, .7, .95, .55, .85, .45, .7, .35}};
const std::array<uint32_t, 5>   IRON{{hex(0x120f18), hex(0x1e1a27), hex(0x2c2637), hex(0x3f374d), hex(0x5a5068)}};
double                          ageOf(uint32_t f, int f0) { return wrap(double(f) - f0, 72); }
Point root(double phi, double up = 0) { return {std::cos(phi) * (RR + TR * .6), TY + std::sin(phi) * RR * SY - up}; }
struct Arc {
    int    f0, len, side;
    double phi, th0, th1, L;
    bool   front;
};
struct Strike {
    int    f0, side;
    double x, phi;
};
struct Vec3 {
    double x, y, z;
};
Vec3 norm(double x, double y, double z) {
    const double l = jsHypot3(x, y, z);
    return {x / l, y / l, z / l};
}
/** @brief Own the source Float32 projection; built once per immutable prepared program.
 * @cost 480 by96 tube samples per configuration, reused across all draws and seeks. */
struct Torus {
    int                x0 = -int(std::ceil(RR + TR)) - 1, y0 = int(std::floor(TY - RR * SY - TR)) - 2, w = 2 * -x0 + 2,
                       h  = int(std::ceil(TY + RR * SY + TR)) + 3 - y0;
    double             sa = SY, ca = std::sqrt(1 - SY * SY);
    std::vector<float> z, n;
    Torus() : z(size_t(w * h), -1e9f), n(size_t(w * h * 3), 0) {
        for (int i = 0; i < 480; ++i) {
            const double phi = (i / 480.) * TAU, cp = std::cos(phi), sp = std::sin(phi);
            for (int j = 0; j < 96; ++j) {
                const double psi = (j / 96.) * TAU, cs = std::cos(psi), ss = std::sin(psi), rr = RR + TR * cs,
                             X = rr * cp, Y = TR * ss, Z = rr * sp;
                const int    px = int(std::floor(X)) - x0, py = int(std::floor(TY - Y * ca + Z * sa)) - y0;
                const double d = Z * ca + Y * sa;
                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                const size_t k = size_t(py * w + px);
                if (d > z[k]) {
                    z[k]         = float(d);
                    n[k * 3]     = float(cs * cp);
                    n[k * 3 + 1] = float(ss);
                    n[k * 3 + 2] = float(cs * sp);
                }
            }
        }
    }
    void draw(Paint& p, std::span<const uint32_t> St, std::span<const uint32_t> C,
              const std::array<double, 2>& lit) const {
        const auto KEY = norm(-.55, .75, .45), HALF = norm(KEY.x, KEY.y + sa, KEY.z + ca);
        const auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && z[size_t(y * w + x)] > -1e8; };
        const double m = double(St.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t k = size_t(y * w + x);
                if (z[k] < -1e8) continue;
                const double nx = n[k * 3], ny = n[k * 3 + 1], nz = n[k * 3 + 2],
                             dif   = std::max(0., nx * KEY.x + ny * KEY.y + nz * KEY.z),
                             spec  = std::pow(std::max(0., nx * HALF.x + ny * HALF.y + nz * HALF.z), 28),
                             inner = nx * (x + x0 + .5) + nz * 0 < 0 && std::abs(x + x0 + .5) < RR ? .75 : 1;
                auto c = spec > .55
                             ? St.back()
                             : St[size_t(clamp(std::floor((.12 + .8 * dif * inner + spec) * (m - 1.01)), 0, m - 2))];
                for (int side : {-1, 1}) {
                    const double l = lit[side > 0 ? 1 : 0],
                                 r = std::max(0., side * nx * .85 + ny * .25 + nz * .45) * std::min(1., l);
                    if (r > .88)
                        c = C[6];
                    else if (r > .62)
                        c = mix(c, C[5], .6);
                    else if (r > .38)
                        c = mix(c, C[4], .3);
                }
                if (!on(x, y + 1) && ny < .3) c = St[0];
                p.px(x + x0, y + y0, c);
            }
    }
};
void drawCoil(Paint& p, std::span<const uint32_t> Cu, const std::array<double, 2>& lit, std::span<const uint32_t> C) {
    const double n = double(Cu.size());
    p.shader(-14, -8, 14, 0, [&](double x, double y, ShadeOutput& o) {
        if (x < -13 || x > 13 || y < -7) return;
        const int edge = x < -12 ? 2 : x > 12 ? 0 : 1;
        o.c            = y < -6   ? IRON[4]
                         : y < -5 ? IRON[3]
                         : y > -1 ? IRON[0]
                                  : IRON[size_t(edge + 1 > 2 ? 2 : edge + (y < -3 ? 1 : 0))];
    });
    for (int rx : {-10, 10}) {
        p.px(rx, -4, IRON[4]);
        p.px(rx, -3, IRON[0]);
    }
    const auto turn = [&](double y, bool front) {
        for (double a = 0; a < TAU; a += .04) {
            if ((std::sin(a) >= 0) != front) continue;
            const double x = std::cos(a) * 10, yy = y + std::sin(a) * 2.2,
                         l = .5 - .4 * std::cos(a) + (front ? .25 : -.2);
            p.px(x, yy, Cu[size_t(clamp(jsRound(l * (n - 1)), 0, n - 1))]);
            p.px(x, yy + 1, Cu[size_t(clamp(jsRound(l * (n - 1)) - 2, 0, n - 1))]);
        }
    };
    turn(-10.5, false);
    turn(-13.5, false);
    p.shader(-6, CTOP, 6, CBOT, [&](double x, double y, ShadeOutput& o) {
        if (x < -5 || x > 5) return;
        const double u = (x + 5.5) / 11, nx = u * 2 - 1;
        const bool   wire = (jsUint(std::floor(y)) & 1u) == 0;
        double       l    = .62 - .55 * nx - .35 * nx * nx + (std::abs(nx + .35) < .12 ? .3 : 0);
        if (!wire) l -= .32;
        o.c = Cu[size_t(clamp(jsRound(l * (n - 1)), 0, n - 1))];
        if (wire && ((nx < -.75 && lit[0] > .25) || (nx > .75 && lit[1] > .25)))
            o.c = nx < 0 ? (lit[0] > .6 ? C[6] : C[4]) : lit[1] > .6 ? C[6] : C[4];
    });
    p.line(-5, CTOP - 1, 5, CTOP - 1, IRON[3]);
    p.line(-4, CTOP - 2, 4, CTOP - 2, IRON[2]);
    turn(-10.5, true);
    turn(-13.5, true);
}
struct TeslaCoil final : AnalyticKernel {
    Torus                 torus;
    std::vector<Arc>      arcs;
    std::array<Strike, 2> strikes{};
    explicit TeslaCoil(uint32_t seed) {
        Rng r(hash({seed, 1}));
        for (auto vn :
             std::array<std::array<int, 2>, 6>{{{{2, 3}}, {{20, 3}}, {{30, 1}}, {{38, 3}}, {{56, 3}}, {{66, 1}}}})
            for (int j = 0; j < vn[1] + r.integer(0, 1); ++j) {
                const int    side  = r.isChance(.5) ? 1 : -1;
                const bool   front = r.isChance(.55);
                const double a     = r.range(.15, 1.1),
                             phi   = side > 0 ? (front ? a : -a)
                                     : front  ? PI - a
                                              : PI + a,
                             th0   = r.range(-1.15, .35);
                const int    f0 = (vn[0] + j * r.integer(2, 4)) % 72, len = r.integer(3, 7);
                const double th1 = th0 + r.range(-.6, .6), L = r.range(24, 52);
                arcs.push_back({f0, len, side, phi, th0, th1, L, front});
            }
        size_t i = 0;
        for (auto fs : std::array<std::array<int, 2>, 2>{{{{14, 1}}, {{47, -1}}}}) {
            const double x = fs[1] * r.range(38, 56);
            strikes[i++]   = {fs[0], fs[1], x, fs[1] > 0 ? .4 : PI - .4};
        }
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&           C  = o.palette;
        const auto&           Cu = o.lightning.copper;
        const auto&           St = o.lightning.steel;
        const auto&           S  = o.lightning.sparks;
        const uint32_t        f  = jsUint(wrap(std::floor(t * 30 + 1e-6), 72));
        std::array<double, 2> lit{};
        for (const auto& a : arcs) {
            const auto age = size_t(ageOf(f, a.f0));
            if (age < size_t(a.len)) lit[a.side > 0 ? 1 : 0] += FL[age] * .6;
        }
        for (const auto& s : strikes)
            if (ageOf(f, s.f0) < 6) lit[s.side > 0 ? 1 : 0] += 1;
        const double act = std::min(1., (lit[0] + lit[1]) * .5);
        p.glow(0, -10, 58, C[3], .14 + .25 * act);
        p.ellipse(0, .5, 15, 1.6, IRON[0]);
        for (const auto& s : strikes) {
            const double age = wrap(t - s.f0 / 30., P);
            lightning::scorch(p, s.x, 0, 7, std::pow(clamp(1 - age / 1.1), 1.3), 1 - clamp((age - 1.4) / .6));
            for (int j = 0; j < 2; ++j) {
                const double u = (age - .12 - j * .18) / 1.2;
                if (u > 0 && u < 1)
                    lightning::puff(p, s.x + (j ? 3 : -2) + u * 5 * s.side, -3 - u * 16 - j * 2, 2.6 + j, u,
                                    mix(hex(0x5b5566), C[2], .3));
            }
        }
        const auto drawArc = [&](const Arc& a) {
            const auto   age = size_t(ageOf(f, a.f0));
            const double u = double(age) / std::max(1, a.len - 1), th = a.th0 + (a.th1 - a.th0) * ease::outQuad(u),
                         ang = a.side > 0 ? th : PI - th, L = a.L * (age == 0 ? .75 : 1);
            const auto   v = root(a.phi);
            Rng          r(hash({o.seed, 21, uint32_t(a.f0), f}));
            const auto   b = lightning::zigzag(r, v.x, v.y, v.x + std::cos(ang) * L, v.y + std::sin(ang) * L, .32, .75);
            lightning::Stroke options;
            options.light = .4;
            lightning::stroke(p, b, FL[age], C, .6, options);
            p.glow(v.x, v.y, 10, C[5], .5 * FL[age]);
        };
        for (const auto& a : arcs)
            if (ageOf(f, a.f0) < a.len && !a.front) drawArc(a);
        drawCoil(p, Cu, lit, C);
        torus.draw(p, St, C, lit);
        Rng rc(hash({o.seed, 9, f}));
        for (int i = 0; i < 6; ++i) {
            const double phi = rc.range(0, TAU), d = rc.range(1, 3), x = std::cos(phi) * (RR + TR + d),
                         y = TY + std::sin(phi) * (RR * SY + TR + d * .7);
            p.px(x, y, rc.isChance(.3) ? C[5] : C[3]);
            if (rc.isChance(.4)) p.px(x + std::cos(phi), y + std::sin(phi) * .6, C[2]);
            p.glow(x, y, 4, C[3], .3);
        }
        for (int i = 0; i < 2; ++i) {
            const double phi = rc.range(0, TAU), x0 = std::cos(phi) * (RR + TR * .8),
                         y0 = TY + std::sin(phi) * (RR * SY + TR * .6),
                         a  = std::atan2(std::sin(phi) * .6 - .5, std::cos(phi)) + rc.range(-.5, .5),
                         l  = rc.range(4, 10);
            const auto   b  = lightning::zigzag(rc, x0, y0, x0 + std::cos(a) * l, y0 + std::sin(a) * l, .4, .4);
            const double k  = rc.range(.35, .65);
            lightning::crackle(p, b, k, C, .3);
        }
        p.glow(0, TY, 34, C[3], .18 + .3 * act);
        for (const auto& a : arcs)
            if (ageOf(f, a.f0) < a.len && a.front) drawArc(a);
        for (const auto& s : strikes) {
            const auto age = size_t(ageOf(f, s.f0));
            if (age < 6) {
                const double      k = std::array<double, 6>{{1, .75, 1, .6, .85, .4}}[age];
                const auto        v = root(s.phi, -2);
                Rng               r(hash({o.seed, 41, uint32_t(s.f0), f}));
                const auto        b = lightning::zigzag(r, v.x, v.y, s.x, 0, .22, .55);
                lightning::Stroke options;
                options.hero = age == 0;
                lightning::stroke(p, b, k, C, age == 0 ? .95 : .8, options);
                p.circle(s.x, -1, 3 * k + 1.5, C[2]);
                p.circle(s.x, -1, 3 * k + .5, C[5]);
                p.circle(s.x, -1, 2 * k, WHITE);
                p.glow(s.x, -10, 40, C[4], .55 * k);
                p.glow(s.x, -3, 12, C[6], .5 * k);
                if (age < 2) p.glow(0, -50, 120, C[3], .25 * k);
            }
            const double st = wrap(t - s.f0 / 30., P);
            for (uint32_t j = 0; j < 18; ++j) {
                Rng          r(hash({o.seed, 51, uint32_t(s.f0), j}));
                const double life = r.range(.3, .65);
                if (st >= life) continue;
                const double a = -PI / 2 + r.range(-1.3, 1.3), sp = r.range(50, 190);
                lightning::spark(p, lightning::flight(s.x, -1, std::cos(a) * sp, std::sin(a) * sp, st, 0),
                                 1 - st / life, S);
            }
        }
        for (const auto& a : arcs) {
            const double age = wrap(t - a.f0 / 30., P), ang = a.side > 0 ? a.th0 : PI - a.th0;
            const auto   v = root(a.phi);
            for (uint32_t j = 0; j < 3; ++j) {
                Rng          r(hash({o.seed, 61, uint32_t(a.f0), j}));
                const double life = r.range(.15, .35);
                if (age >= life) continue;
                const double tx = v.x + std::cos(ang) * a.L * .75, ty = v.y + std::sin(ang) * a.L * .75,
                             b = ang + r.range(-.9, .9), sp = r.range(50, 150);
                lightning::spark(p, lightning::flight(tx, ty, std::cos(b) * sp, std::sin(b) * sp, age, 0),
                                 1 - age / life, S);
            }
        }
    }
};
}  // namespace
const Descriptor& teslaCoilDescriptor() {
    static const std::array<Option, 3> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"copper", 1, 64, false, 6, &PixelFxLightningSettings::copper},
                                                {"steel", 1, 64, false, 7, &PixelFxLightningSettings::steel}}};
    static const Descriptor d{{PixelFxKind::TeslaCoil, "lightning/tesla-coil", 224, 136, 112, 120, 2.4, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed = 4;
                                  s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
                                  s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
                                  s.lightning.copper = {hex(0x2a120c), hex(0x5a2814), hex(0x8e4a1e),
                                                        hex(0xc4772f), hex(0xeaa65a), hex(0xffd9a0)};
                                  s.lightning.steel  = {hex(0x0f121c), hex(0x1f2433), hex(0x353d52), hex(0x556078),
                                                        hex(0x7f8ba3), hex(0xb8c3d6), hex(0xf2f6ff)};
                              },
                              nullptr,
                              options,
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<TeslaCoil>(s.seed);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
