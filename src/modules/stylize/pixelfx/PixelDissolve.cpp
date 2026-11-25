// eve-hot-path
// Native adaptation of pixelfx transitions/pixel-dissolve.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include <numeric>
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double PERIOD = .17, IN0 = .04, IN1 = .8, OUT0 = 1.12, OUT1 = 1.85;
constexpr int    CELL = 20;
struct Block {
    int    x, y, size;
    double tin = 0, tout = 0;
    bool   glint = false;
    double gx = 0, gy = 0, spin = 0, length = 0;
};
struct PixelDissolve final : AnalyticKernel {
    const std::vector<Block>  blocks;
    static std::vector<Block> prepare(uint32_t seed) {
        std::vector<Block> out;
        out.reserve(2304);
        for (uint32_t cy = 0; cy < screen::H / CELL; ++cy)
            for (uint32_t cx = 0; cx < screen::W / CELL; ++cx) {
                const double h = rand({cx, cy, seed});
                const int    n = h < .32 ? 1 : h < .74 ? 2 : 4, s = CELL / n;
                for (int j = 0; j < n; ++j)
                    for (int i = 0; i < n; ++i) out.push_back({int(cx) * CELL + i * s, int(cy) * CELL + j * s, s});
            }
        const auto rank = [&](bool entering) {
            std::vector<double> values;
            values.reserve(out.size());
            for (const auto& b : out) {
                const double v =
                    fbm((b.x + b.size / 2.) / 64, (b.y + b.size / 2.) / 64, seed + (entering ? 0u : 7u), 3) +
                    .22 * rand({uint32_t(b.x), uint32_t(b.y), seed + (entering ? 1u : 2u)}) +
                    (entering ? (b.size == 20  ? .12
                                 : b.size == 5 ? -.08
                                               : 0)
                              : (b.size == 20  ? -.14
                                 : b.size == 5 ? .1
                                               : 0));
                values.push_back(v);
            }
            std::vector<size_t> order(out.size());
            std::iota(order.begin(), order.end(), size_t(0));
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return values[a] < values[b]; });
            std::vector<float> result(out.size());
            for (size_t k = 0; k < order.size(); ++k) result[order[k]] = float(double(k) / double(out.size() - 1));
            return result;
        };
        const auto in = rank(true), ex = rank(false);
        for (size_t i = 0; i < out.size(); ++i) {
            auto&      b = out[i];
            const auto x = uint32_t(b.x), y = uint32_t(b.y);
            b.tin   = IN0 + double(in[i]) * (IN1 - IN0);
            b.tout  = OUT0 + double(ex[i]) * (OUT1 - OUT0);
            b.glint = rand({x, y, seed + 3}) < (b.size >= 10 ? .32 : .1);
            b.gx    = b.x + (rand({x, y, seed + 4}) < .5 ? 0 : b.size);
            b.gy    = b.y + (rand({x, y, seed + 5}) < .5 ? 0 : b.size);
            b.spin  = (rand({x, y, seed + 6}) - .5) * 3;
            b.length =
                b.size == 20 && rand({x, y, seed + 8}) < .45 ? 9 + 6 * rand({x, y, seed + 9}) : 2.5 + b.size * .32;
        }
        return out;
    }
    explicit PixelDissolve(const PixelFxSettings& o) : blocks(prepare(o.seed)) {}
    static void mote(Paint& p, double x, double y, uint32_t color, bool cross) {
        p.px(x, y, color);
        if (cross)
            for (const auto& d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(x + d.x, y + d.y, color);
    }
    static void square(Paint& p, double x, double y, double s, uint32_t fill, uint32_t rim = 0) {
        const double X = jsRound(x), Y = jsRound(y), S = std::max(1., jsRound(s));
        if (fill) screen::rect(p, X, Y, S, S, fill);
        if (rim && S > 2) {
            screen::rect(p, X, Y, S, 1, rim);
            screen::rect(p, X, Y + S - 1, S, 1, rim);
            screen::rect(p, X, Y, 1, S, rim);
            screen::rect(p, X + S - 1, Y, 1, S, rim);
        }
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p0, double t, const PixelFxSettings& o) const override {
        const auto& C     = o.palette;
        const auto  ink   = o.transition.color;
        auto        p     = screen::clip(p0);
        const auto  frame = jsUint(std::floor(t * 15));
        for (size_t index = 0; index < blocks.size(); ++index) {
            const auto&  b  = blocks[index];
            const auto   n  = uint32_t(index);
            const double ki = (t - b.tin) / PERIOD, ko = (t - b.tout) / PERIOD, x = b.x, y = b.y, s = b.size,
                         cx = x + s / 2, cy = y + s / 2;
            if (ki < 0 || ko >= 1.6) continue;
            if (ko < 0) {
                if (ki >= 1)
                    square(p0, x, y, s, ink);
                else {
                    const double g = ki < .35 ? .35 + .65 * ease::outBack(ki / .35) : 1, h = s * g;
                    const auto   rim = ki < .45 ? WHITE : ki < .6 ? C[6] : ki < .75 ? C[5] : ki < .9 ? C[4] : C[3];
                    square(p, cx - h / 2, cy - h / 2, h, ki > .2 ? ink : 0, rim);
                    if (ki < .3 && s >= 10)
                        for (const auto& d : std::array<Point, 5>{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                            p.px(cx - .5 + d.x, cy - .5 + d.y, d.x || d.y ? C[6] : WHITE);
                    p.glow(cx, cy, s * .8 + 4, C[5], .45 * (1 - ki));
                }
                if (ki < 1.4 && rand({frame, n, 9}) < .35)
                    mote(p, x + rand({frame, n, 10}) * (s + 6) - 3, y + rand({frame, n, 11}) * (s + 6) - 3,
                         rand({frame, n, 12}) < .5 ? WHITE : C[6], rand({frame, n, 17}) < .25);
            } else {
                if (ko < .35) {
                    const double q = ko / .35;
                    square(p0, x, y, s, ink, q < .4 ? C[4] : q < .75 ? C[5] : C[6]);
                    p.glow(cx, cy, s * .8 + 3, C[4], .35 * q);
                } else if (ko < .75) {
                    const double q = (ko - .35) / .4, h = s * (1 - ease::inQuad(q) * .85);
                    square(p, cx - h / 2, cy - h / 2, h, 0, q < .5 ? WHITE : C[6]);
                    p.glow(cx, cy, s * .8 + 4, C[5], .45 * (1 - q * .5));
                } else if (ko < 1) {
                    const double q = (ko - .75) / .25;
                    for (const auto& d : std::array<Point, 5>{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                        if (!(d.x || d.y) || q < .5) p.px(cx - .5 + d.x, cy - .5 + d.y, d.x || d.y ? C[6] : WHITE);
                    p.glow(cx, cy, s * .5 + 3, C[5], .35 * (1 - q));
                }
                if (ko > .3 && ko < 1.5 && rand({frame, n, 13}) < .3)
                    mote(p, x + rand({frame, n, 14}) * (s + 6) - 3, y + rand({frame, n, 15}) * (s + 6) - 3,
                         rand({frame, n, 16}) < .5 ? WHITE : C[6], rand({frame, n, 18}) < .25);
            }
        }
        for (const auto& b : blocks) {
            const double ki = (t - b.tin) / PERIOD, ko = (t - b.tout) / PERIOD;
            if (ki < 0 || ko >= 1.6 || !b.glint) continue;
            double u, dir;
            if (ko < 0) {
                if (!(ki > .15 && ki < 1.6)) continue;
                u   = (ki - .15) / 1.45;
                dir = 1;
            } else {
                if (ko <= .3) continue;
                u   = (ko - .3) / 1.3;
                dir = -1;
            }
            if (u <= 0 || u >= 1) continue;
            const double length = b.length * std::pow(std::sin(PI * u), .7);
            screen::glint(p, b.gx, b.gy, length, b.spin * u * dir + .4, C[5], C[4]);
            if (b.length > 8) p.glow(b.gx, b.gy, 18 + length, C[5], .25 * std::sin(PI * u));
        }
    }
};
}  // namespace
const Descriptor& pixelDissolveDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d{{PixelFxKind::PixelDissolve, "transitions/pixel-dissolve", 320, 180, 0, 0, 2.2, false},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed             = 1;
                                  s.transition.color = hex(0x0b0a10);
                                  s.palette          = {hex(0x1a0516), hex(0x3d0a33), hex(0x701560), hex(0xb0228f),
                                                        hex(0xf53cd1), hex(0xff86e6), hex(0xffd2f6), WHITE};
                              },
                              nullptr,
                              options,
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<PixelDissolve>(s);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
