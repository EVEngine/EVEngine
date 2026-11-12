// eve-hot-path
// Native adaptation of pixelfx status/poisoned.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/StatusEyes.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.6, TICK = .8, G = 760;
using Cell = std::array<int, 2>;
bool isBlisterSite(const status::Shape& shape, const Cell& q) {
    return q[1] > 4 && shape.isOccupied(q[0] - 1, q[1]) && shape.isOccupied(q[0] + 1, q[1]) &&
           shape.isOccupied(q[0], q[1] - 1);
}
Result<void> validateBlisterSites(const PixelFxActor& a) {
    const status::Shape shape(a);
    if (!std::any_of(shape.body.begin(), shape.body.end(), [&](const Cell& q) { return isBlisterSite(shape, q); }))
        return Result<void>::failure(
            Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                       "Poisoned requires a blister cell below row4 with occupied left, right and upper neighbors",
                       "/options/actor/mask"));
    return Result<void>::success();
}
struct Blister {
    int    x, y;
    double radius;
};
struct Poisoned final : AnalyticKernel {
    const status::Shape                             shape;
    std::vector<std::vector<std::array<double, 3>>> veins;
    Cell                                            wound;
    std::array<std::array<Blister, 3>, 2>           blisters;
    std::vector<Cell>                               sweat;
    explicit Poisoned(const PixelFxSettings& o) : shape(o.actor) {
        const auto& a = o.actor;
        const int   H = shape.height, W = shape.width;
        Rng         r(o.seed * 7u + 3u);
        const int   wy  = int(jsRound(H * .6));
        const auto  row = shape.rows[size_t(wy)];
        const int   wx  = row[1] - 1;
        wound           = {wx, wy};
        const auto walk = [&](auto&& self, double x, double y, double angle, double length, int depth) -> void {
            std::vector<std::array<double, 3>> points;
            for (int step = 0; step < length; ++step) {
                angle += r.range(-.5, .5);
                x += std::cos(angle);
                y += std::sin(angle);
                if (!shape.isOccupied(int(jsRound(x)), int(jsRound(y)))) break;
                points.push_back({jsRound(x), jsRound(y), step / length});
                if (depth < 2 && step > 2 && r.isChance(.16)) {
                    const double turn = r.signedRange(.6, 1.1);
                    self(self, x, y, angle + turn, length * .55, depth + 1);
                }
            }
            veins.push_back(std::move(points));
        };
        for (int i = 0; i < 4; ++i) walk(walk, wx, wy, PI + (double(i) / 4 - .45) * 2.2, 11, 0);
        std::vector<Cell> skin, candidates;
        for (const auto& q : shape.body) {
            if (luma(status::colorAt(a, q[0], q[1])) > .55 && shape.isOccupied(q[0] - 1, q[1]) &&
                shape.isOccupied(q[0] + 1, q[1]))
                skin.push_back(q);
            if (isBlisterSite(shape, q)) candidates.push_back(q);
        }
        EV_ASSERT(!candidates.empty());
        for (auto& tick : blisters)
            for (auto& b : tick) {
                const auto q = candidates[size_t(std::floor(r.next() * double(candidates.size())))];
                b            = {q[0], q[1], r.range(1.1, 1.8)};
            }
        const auto eyes = status::eyes(a, shape);
        for (const auto& e : eyes) sweat.push_back({e.x0 < double(W) / 2 ? e.x0 - 1 : e.x1 + 1, e.y0 - 1});
        for (int side : {-1, 1})
            for (const auto& q : shape.body)
                if (q[1] > H * .5 && q[1] < H * .7 && (side < 0 ? q[0] <= row[0] + 1 : q[0] >= row[1] - 1) &&
                    !shape.isOccupied(q[0], q[1] + 1)) {
                    sweat.push_back(q);
                    break;
                }
        if (sweat.empty()) {
            const auto& list = skin.empty() ? shape.body : skin;
            sweat.push_back(list[size_t(std::floor(r.next() * double(list.size())))]);
        }
    }
    PixelFxActorVisibility actorVisibility(double) const override { return PixelFxActorVisibility::Hidden; }
    void                   drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&                   P     = o.palette;
        const double                  phase = wrap(t, TICK), k = phase / .55;
        const std::array<uint32_t, 4> ramp{P[3], P[4], P[5], P[6]};
        if (k < 1)
            band(p, 0, 0, 5 + 22 * std::sqrt(k), .18, std::max(1., 2.5 * (1 - k)), 1 - k, ramp,
                 [](double g) { return std::sin(g) < 0; });
        p.shader(-28, -3, 28, 6, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2(x / 24, (y - .5) / 4);
            if (d < 1) {
                out.l = P[4];
                out.k = (.14 + .2 * std::exp(-phase / .2)) * (1 - d) * (1 - d);
            }
        });
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  P     = o.palette;
        const auto&  a     = o.actor;
        const int    H     = shape.height;
        const double phase = wrap(t, TICK), lean = 1.3 * std::sin((t / D) * TAU),
                     w = phase < .12 ? std::sin((phase / .12) * PI) : 0;
        const auto off = [&](double my) { return jsRound(lean * (1 - my / H) + w * std::sin(my * .42 + phase * 40)); };
        const size_t tick = size_t(std::floor(t / TICK)) % 2;
        for (const auto& q : shape.body) {
            const int      mx = q[0], my = q[1];
            const uint32_t c = status::colorAt(a, mx, my);
            const double   l = luma(c), wave = .5 + .5 * std::sin(TAU * ((double(my) / H) * 1.6 + (t / D) * 2)),
                           k    = .22 + .4 * wave * wave;
            const uint32_t sick = P[size_t(clamp(1 + jsRound(l * 6.2), 1, 7))];
            p.px(a.x + mx + off(my), a.y + my, mix(c, wave > .85 && l > .4 ? mix(sick, P[6], .3) : sick, k));
        }
        const double grow = smooth(0, D * .55, t) * (1 - smooth(D * .62, D, t)) * 1.15;
        for (const auto& points : veins)
            for (const auto& q : points)
                if (q[2] < grow) p.px(a.x + q[0] + off(q[1]), a.y + q[1], q[2] > grow - .12 ? P[2] : o.status.veins);
        const int wx = wound[0], wy = wound[1];
        p.px(a.x + wx + off(wy), a.y + wy, P[6]);
        p.px(a.x + wx + off(wy) - 1, a.y + wy, P[3]);
        p.glow(a.x + wx, a.y + wy, 6, P[5], .3);
        for (const auto& b : blisters[tick]) {
            const double x = a.x + b.x + off(b.y), y = a.y + b.y, grow2 = clamp(phase / .16);
            const bool   pop = phase > .38;
            if (!pop) {
                const double radius = b.radius * grow2;
                p.circle(x, y, radius + .6, P[3]);
                p.circle(x - .2, y - .2, radius, P[5]);
                if (radius > .8) p.px(x - .5, y - .8, WHITE);
                p.glow(x, y, 4, P[5], .3 * grow2);
            } else if (phase < .56) {
                const double k = (phase - .38) / .18;
                for (int j = 0; j < 6; ++j) {
                    const double angle = (double(j) / 6) * TAU + b.x, d = b.radius + .5 + k * 3.5;
                    p.dot(x + std::cos(angle) * d, y + std::sin(angle) * d - k * 2, j % 2 ? P[6] : P[5], 1.2 - k);
                }
                p.glow(x, y, 6, P[6], .4 * (1 - k));
            }
        }
        for (size_t i = 0; i < sweat.size(); ++i) {
            const auto   q   = sweat[i];
            const double age = wrap(t - double(i) * .37, D / 2), x = a.x + q[0] + .5 + off(q[1]), y0 = a.y + q[1];
            if (age < .35) {
                const double k = age / .35, run = jsRound(k * 2);
                p.px(x, y0 + run, k > .5 ? P[6] : P[5]);
                if (k > .5) p.px(x, y0 + run - 1, P[4]);
                continue;
            }
            const double f = age - .35, fallH = -1 - (y0 + 2), tf = std::sqrt((2 * std::max(1., fallH)) / G);
            if (f < tf) {
                const double y = y0 + 2 + .5 * G * f * f;
                p.line(x, y - std::min(3., G * f * .012), x, y, P[4]);
                p.px(x, y, P[6]);
                continue;
            }
            const double h = f - tf;
            if (h > .4) continue;
            const double k = h / .4;
            p.line(x - 1 - k * 2, -1, x + 1 + k * 2, -1, P[k < .4 ? 6 : 4], 1.2 - k);
            for (int j = 0; j < 3; ++j) p.dot(x + (j - 1) * (2 + 3 * k), -2 - j % 2 - 5 * k * (1 - k), P[6], 1 - k);
            p.disc(x + std::sin(k * 6) * 1.5, -3 - k * 6, 1 + 1.5 * k, P[5], .35 * (1 - k));
            p.glow(x, -1.5, 5, P[5], .35 * (1 - k));
        }
        const double                  k = phase / .55;
        const std::array<uint32_t, 4> ramp{P[3], P[4], P[5], P[6]};
        if (k < 1)
            band(p, 0, 0, 5 + 22 * std::sqrt(k), .18, std::max(1., 2.5 * (1 - k)), 1 - k, ramp,
                 [](double g) { return std::sin(g) >= 0; });
        p.glow(0, a.y + H * .45, 26, P[4], .14 + .2 * std::exp(-phase / .18));
    }
};
}  // namespace
const Descriptor& poisonedDescriptor() {
    static const ActorRule             rule{"status/poisoned-blisters",
                                            "An occupied cell below row4 with occupied left, right and upper neighbors",
                                            validateBlisterSites};
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"veins", 0, 4294967295., false, 0, &PixelFxStatusSettings::veins, true}}};
    static const Descriptor d{
        {PixelFxKind::Poisoned, "status/poisoned", 72, 64, 36, 54, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x0e1d14), hex(0x173320), hex(0x23552e), hex(0x318232),
                              hex(0x55b92f), hex(0x8eea3f), hex(0xcfff63), hex(0xf6ffc2)};
            s.status.veins = hex(0x2c1a3a);
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Poisoned>(o); },
        true,
        1,
        2,
        std::cref(rule)};
    return d;
}
}  // namespace eve::stylize::pixelfx
