// eve-hot-path
// Native adaptation of pixelfx status/burning.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusFire.h"
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.2, TICK = .6, WIND = -14;
using Cell  = std::array<int, 2>;
using Bases = std::array<Cell, 4>;
Diagnostic siteError(std::string message) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), "/options/actor/mask");
}
Result<Bases> siteBases(const status::Shape& shape) {
    std::optional<Cell> hair, shoulder;
    for (const auto& q : shape.up) {
        if (!hair && q[0] < shape.width * .42) hair = q;
        if (!shoulder && q[0] > double(shape.width) / 2 && q[1] > shape.height * .4 && q[1] < shape.height * .62)
            shoulder = q;
    }
    if (!hair) return Result<Bases>::failure(siteError("Burning requires an exposed upper-left hair cell"));
    const auto at = [&](double q, int side) {
        const int  y   = int(jsRound(shape.height * q));
        const auto row = shape.rows[size_t(y)];
        return Cell{side < 0 ? row[0] : row[1], y};
    };
    return Result<Bases>::success(Bases{*hair, shoulder.value_or(at(.5, 1)), at(.62, -1), at(.6, 1)});
}
bool isScorchCandidate(const PixelFxActor& a, const Cell& q, double x, double y) {
    const double level = luma(status::colorAt(a, q[0], q[1]));
    return jsHypot2(a.x + q[0] + .5 - x, a.y + q[1] + .5 - y) < 9 && q[1] > a.height * .42 && level > .3 && level < .6;
}
Result<void> validateSites(const PixelFxActor& a) {
    const status::Shape shape(a);
    auto                bases = siteBases(shape);
    if (!bases) return Result<void>::failure(bases.status());
    const bool lower =
        std::any_of(shape.body.begin(), shape.body.end(), [&](const Cell& q) { return q[1] > shape.height * .45; });
    if (lower) return Result<void>::success();
    for (const auto& base : bases.value()) {
        const double x = a.x + base[0] + .5, y = a.y + base[1] + 1.5;
        if (!std::any_of(shape.body.begin(), shape.body.end(),
                         [&](const Cell& q) { return isScorchCandidate(a, q, x, y); }))
            return Result<void>::failure(
                siteError("Burning requires a clothing-near-spot or lower-body scorch candidate for every hot spot"));
    }
    return Result<void>::success();
}
struct Tongue {
    double   dx, height;
    uint32_t seed;
};
struct Spot {
    double              x, y;
    std::vector<Tongue> tongues;
};
struct Scorch {
    double x, y, radius;
};
struct Burning final : AnalyticKernel {
    const status::Shape                  shape;
    const std::vector<uint32_t>          F;
    std::array<Spot, 4>                  spots;
    std::array<std::array<Scorch, 2>, 2> scorch;
    explicit Burning(const PixelFxSettings& o) : shape(o.actor), F(o.palette.rbegin(), o.palette.rend()) {
        const auto& a = o.actor;
        Rng         r(o.seed * 13u + 5u);
        auto        bases = siteBases(shape);
        EV_ASSERT(bool(bases));
        constexpr std::array<int, 4>    counts{3, 2, 2, 2};
        constexpr std::array<double, 4> heights{4.5, 4, 3.5, 3.5};
        for (size_t i = 0; i < spots.size(); ++i) {
            const auto base = bases.value()[i];
            auto&      sp   = spots[i];
            sp.x            = a.x + base[0] + .5;
            sp.y            = a.y + base[1] + 1.5;
            for (int j = 0; j < counts[i]; ++j) {
                const double jitter = r.range(-.4, .4), height = heights[i] * r.range(.8, 1.2);
                sp.tongues.push_back({(j - double(counts[i] - 1) / 2) * 1.6 + jitter, height,
                                      uint32_t(i) * 17u + uint32_t(j) * 5u + o.seed});
            }
        }
        for (size_t tick = 0; tick < 2; ++tick)
            for (size_t k = 0; k < 2; ++k) {
                const auto&       sp = spots[(tick * 2 + k + 1) % spots.size()];
                std::vector<Cell> candidates;
                for (const auto& q : shape.body)
                    if (isScorchCandidate(a, q, sp.x, sp.y)) candidates.push_back(q);
                if (candidates.empty())
                    for (const auto& q : shape.body)
                        if (q[1] > shape.height * .45) candidates.push_back(q);
                EV_ASSERT(!candidates.empty());
                const auto q    = candidates[size_t(std::floor(r.next() * double(candidates.size())))];
                scorch[tick][k] = {a.x + q[0] + .5, a.y + q[1] + .5, r.range(3.2, 4.4)};
            }
    }
    PixelFxActorVisibility actorVisibility(double t) const override {
        return wrap(t, TICK) < .034 ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible;
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const double fl = std::exp(-wrap(t, TICK) / .12), flick = .8 + .2 * status::tnoise(t, D, 7.5, 3);
        p.shader(-30, -3, 30, 7, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2(x / 26, (y - .5) / 4.5);
            if (d < 1) {
                out.l = mix(F[3], F[4], .5);
                out.k = (.16 + .22 * fl) * flick * (1 - d) * (1 - d);
            }
        });
        const uint32_t smokeColor = mix(o.status.smoke, F[6], .2);
        cycle(t, D, 16, o.seed + 4u, .8, 1.2, [&](double age, double life, Rng& r, int i) {
            const auto&  sp = spots[size_t(i) % spots.size()];
            const double x  = sp.x + status::travel(WIND * .6, WIND * .5, .8, age) + std::sin(age * 4 + i) * 1.2,
                         y  = sp.y - 3 - status::travel(18, 6, 1, age);
            status::smoke(p, x, y, r.range(1.1, 1.9), age / life, smokeColor, .9);
        });
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  CH    = o.status.charRamp;
        const auto&  a     = o.actor;
        const double phase = wrap(t, TICK), fl = std::exp(-phase / .12);
        const size_t tick = size_t(std::floor(t / TICK)) % 2;
        if (phase < .034)
            for (const auto& q : shape.body) p.px(a.x + q[0], a.y + q[1], WHITE);
        else if (phase < .22) {
            const double k = 1 - (phase - .034) / .186;
            for (const auto& q : shape.body) p.blend(a.x + q[0], a.y + q[1], F[4], .55 * k);
        }
        for (const auto& sc : scorch[tick]) {
            const double grow = clamp(phase / .14), fade = 1 - clamp((phase - .25) / .33),
                         radius = sc.radius * (.4 + .6 * grow);
            if (fade <= 0) continue;
            for (const auto& q : shape.body) {
                const double x = a.x + q[0] + .5, y = a.y + q[1] + .5,
                             d = jsHypot2(x - sc.x, (y - sc.y) * 1.2) + rand({uint32_t(q[0]), uint32_t(q[1]), 7u}) * .9;
                if (d > radius) continue;
                if (d > radius - 1)
                    p.dot(x - .5, y - .5,
                          rand({uint32_t(q[0]), uint32_t(q[1]), jsUint(std::floor(t * 20))}) < .5 ? F[3] : F[4],
                          fade * 1.2);
                else
                    p.dot(x - .5, y - .5, d < radius * .5 ? CH[0] : CH[1], fade * 1.1);
            }
            p.glow(sc.x, sc.y, 6, F[4], .3 * fade);
        }
        for (const auto& sp : spots) {
            for (const auto& tg : sp.tongues) {
                const double height = tg.height * (.85 + .25 * status::tnoise(t, D, 5, tg.seed)) * (1 + 1.6 * fl);
                status::tongue(p, sp.x + tg.dx, sp.y, height, 1.1 + .3 * fl,
                               WIND * .08 * height / 4 + status::tnoise(t, D, 3, tg.seed + 9u) * .8, F, t, D, tg.seed);
            }
            p.glow(sp.x, sp.y - 2, 9 + 6 * fl, F[4], .22 + .35 * fl);
        }
        cycle(t, D, 20, o.seed + 6u, .5, .9, [&](double age, double life, Rng& r, int i) {
            const auto&  sp     = spots[size_t(i) % spots.size()];
            const double jitter = r.range(-1.5, 1.5), speed = WIND + r.range(-10, 6),
                         x  = sp.x + jitter + status::travel(speed, WIND * .6, 1.2, age) +
                              std::sin(age * 9 + i) * 1.2 * age,
                         vy = -r.range(25, 55), y = sp.y - 2 + status::travel(vy, -18, 1.4, age);
            status::ember(p, x, y, age / life, {F[1], F[3], F[5]}, age + i);
        });
        if (phase < .3)
            for (size_t k = 0; k < spots.size(); ++k)
                for (int j = 0; j < 4; ++j) {
                    const auto&  sp    = spots[k];
                    const double angle = -PI / 2 + (rand({uint32_t(k), uint32_t(j), uint32_t(tick)}) - .5) * 2.2,
                                 v     = 50 + 50 * rand({uint32_t(k), uint32_t(j), 2u}),
                                 x     = sp.x + std::cos(angle) * status::travel(v, 0, 3, phase),
                                 y = sp.y - 2 + std::sin(angle) * status::travel(v, 0, 3, phase) + 60 * phase * phase;
                    p.px(x, y, phase < .12 ? F[1] : F[3]);
                    p.light(x, y, F[3], .4 * (1 - phase / .3));
                }
    }
};
}  // namespace
const Descriptor& burningDescriptor() {
    static const ActorRule rule{
        "status/burning-hotspots",
        "Upper-left exposed hair cell and a source scorch candidate near every hotspot, or a lower-body candidate",
        validateSites};
    static const std::array<Option, 3> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"char", 1, 64, false, 3, &PixelFxStatusSettings::charRamp},
         {"smoke", 0, 4294967295., false, 0, &PixelFxStatusSettings::smoke, true}}};
    static const Descriptor d{
        {PixelFxKind::Burning, "status/burning", 72, 80, 36, 70, D, true},
        9,
        [](PixelFxSettings& s) {
            s.seed            = 3;
            s.actor.source    = PixelFxActorSource::Dummy;
            s.palette         = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                 hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.status.charRamp = {hex(0x120c0a), hex(0x2a1a14), hex(0x4a2c1c)};
            s.status.smoke    = hex(0x5b5566);
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Burning>(o); },
        true,
        1,
        2,
        std::cref(rule)};
    return d;
}
}  // namespace eve::stylize::pixelfx
