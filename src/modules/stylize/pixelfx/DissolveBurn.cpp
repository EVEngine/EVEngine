// eve-hot-path
// Native adaptation of pixelfx spawn/dissolve-burn.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SpawnBody.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double              IGNITE = .15, BURN = 1.9, D = 3.6;
const std::array<uint32_t, 3> CHAR{hex(0x100908), hex(0x22130e), hex(0x3a2016)};
struct BurnCell {
    double key;
    bool   gone = false;
};
struct Tongue {
    double x, h, seed;
};
struct BurnEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
    ParticleFlag flags = ParticleFlag::None;
};
struct DissolveBurn final : Kernel {
    const std::vector<uint32_t> C, A;
    Rng                         R;
    const spawn::Body           body;
    std::vector<BurnCell>       cells;
    std::array<Tongue, 6>       tongues;
    Particles                   ps;
    Lights                      lights;
    double                      t = 0;
    explicit DissolveBurn(const PixelFxSettings& o)
        : C(o.palette), A(o.spawn.ash), R(o.seed), body(spawn::bodyOf(o.actor, C)) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        for (const auto& b : body.list)
            cells.push_back({double(body.height - 1 - b.my) / body.height +
                                 .08 * (rand({jsUint(b.mx), jsUint(b.my), 3u}) - .5) + .06 * noise(b.mx * .35, 4u) -
                                 (b.edge ? .02 : 0),
                             false});
        for (size_t i = 0; i < tongues.size(); ++i)
            tongues[i] = {-9 + double(i) * 3.6 + rand({uint32_t(i), 1u}) * 1.5, 6 + rand({uint32_t(i), 2u}) * 6,
                          double(i) * 13};
    }
    PixelFxActorVisibility actorVisibility(double) const override { return PixelFxActorVisibility::Hidden; }
    double                 front() const { return -.04 + 1.1 * ease::inOutSine(clamp((t - IGNITE) / BURN)); }
    double                 left(double a, double b) { return std::max(.05, std::min(R.range(a, b), D - .1 - t)); }
    void emit(const BurnEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, 0, e.flags); }
    void update(double dt) override {
        t += dt;
        const double f = front();
        for (size_t i = 0; i < body.list.size(); ++i) {
            if (cells[i].gone || cells[i].key >= f) continue;
            cells[i].gone  = true;
            const auto&  b = body.list[i];
            const double r = R.next();
            if (r < .2) {
                if (b.y > -7)
                    emit({ParticleKind::Grit, b.x, b.y, R.range(-12, 12), R.range(-20, 0), left(.6, 1.2),
                          R.pick<uint32_t>(A), 1});
                else
                    emit({ParticleKind::Flake, b.x, b.y, R.range(-10, 10), R.range(-8, 4),
                          left(.5, std::min(1.5, -b.y / 18)), R.pick<uint32_t>(A), R.isChance(.2) ? 1.4 : 1});
            } else if (r < .32)
                emit({ParticleKind::Ember, b.x, b.y, R.range(-12, 12), -R.range(15, 45), left(.6, 1.4), C[5],
                      R.range(1, 1.6)});
            else if (r < .46)
                emit({ParticleKind::Fire, b.x, b.y, R.range(-6, 6), -R.range(14, 30), R.range(.2, .42), 0,
                      R.range(1.2, 2.2), R.isChance(.5) ? ParticleFlag::Smokes : ParticleFlag::None});
            else if (r < .5)
                emit({ParticleKind::Smoke, b.x, b.y - 2, R.range(-5, 5), -R.range(14, 26), left(1.2, 2),
                      mix(A[1], C[1], .3), R.range(2, 3.4)});
        }
        if (t >= IGNITE && t - dt < IGNITE) {
            lights.add(0, -4, 34, C[4], .8, .4);
            for (int i = 0; i < 10; ++i)
                emit({ParticleKind::Spark, R.range(-8, 8), -1, R.range(-80, 80), -R.range(40, 150), R.range(.2, .5)});
        }
        if (f > 0 && f < 1.02) {
            const double fy = body.y + (1 - clamp(f)) * body.height;
            if (R.isChance(.34))
                emit({ParticleKind::Smoke, R.range(-7, 7), fy - 5, R.range(-6, 6), -R.range(16, 30), left(1.4, 2.4),
                      mix(A[1], C[3], .35), R.range(2.6, 4.2)});
            if (R.isChance(.5))
                emit({ParticleKind::Fire, R.range(-9, 9), fy, R.range(-6, 6), -R.range(18, 34), R.range(.18, .36), 0,
                      R.range(1.2, 2.4), R.isChance(.35) ? ParticleFlag::Smokes : ParticleFlag::None});
            if (R.isChance(.15))
                emit({ParticleKind::Ember, R.range(-9, 9), fy, R.range(-15, 15), -R.range(30, 70), left(.8, 1.6), C[5],
                      1.4});
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void tongue(Paint& p, double x, double y, double h, double n) const {
        struct Layer {
            double   width, height;
            uint32_t color;
        };
        for (const auto& g : std::array<Layer, 4>{{{1, 1, C[3]}, {.7, .8, C[4]}, {.45, .6, C[5]}, {.25, .35, C[6]}}}) {
            const double H = h * g.height;
            for (int r = 0; r <= H; ++r) {
                const double u    = r / std::max(1., H);
                const double half = 1.8 * g.width * std::pow(1 - u, .9) + noise(t * 9 + r * .5, jsUint(n)) * .4;
                const double cx   = x + noise(t * 4 + r * .25, jsUint(n + 7)) * u * 1.6;
                if (half < .3) continue;
                for (double px = jsRound(cx - half); px <= jsRound(cx + half); ++px) p.px(px, jsRound(y - r), g.color);
            }
        }
    }
    void drawBack(Paint& p) const override {
        const double k = smooth(0, IGNITE + .2, t) * (1 - smooth(IGNITE + BURN * .6, IGNITE + BURN + .4, t));
        if (k > 0) p.glow(0, -1, 30, C[4], .3 * k);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double f = front(), fr = std::floor(t * 30);
        for (size_t i = 0; i < body.list.size(); ++i) {
            if (cells[i].gone) continue;
            const auto&  b = body.list[i];
            const double d = cells[i].key - f;
            uint32_t     c;
            if (d < .025)
                c = rand({jsUint(b.mx), jsUint(b.my), jsUint(fr)}) < .35 ? WHITE : C[6];
            else if (d < .05)
                c = rand({jsUint(b.mx), jsUint(b.my), jsUint(fr + 7)}) < .5 ? C[5] : C[4];
            else if (d < .11)
                c = rand({jsUint(b.mx), jsUint(b.my), 13u}) < .15 && rand({jsUint(b.mx), jsUint(fr), 2u}) < .6
                        ? C[3]
                        : CHAR[d < .075 ? 0 : 1];
            else if (d < .16)
                c = mix(b.color, CHAR[2], .6);
            else if (d < .26)
                c = mix(b.color, C[2], .18);
            else
                c = b.color;
            p.px(b.x, b.y, c);
        }
        if (f > -.02 && f < 1.05) {
            const double fy = body.y + (1 - clamp(f)) * body.height, k = 1 - smooth(.95, 1.05, f);
            for (const auto& g : tongues) {
                const double row = jsRound(fy - body.y), mx = jsRound(g.x - body.x);
                if (!body.isOccupied(int(mx), int(std::min(double(body.height - 1), std::max(0., row))))) continue;
                const double h =
                    g.h * k * (.6 + .6 * (noise(t * 2.6 + g.seed, jsUint(g.seed)) * .5 + .5)) * smooth(-.02, .06, f);
                if (h >= 1) tongue(p, g.x, fy + 1, h, g.seed);
            }
            const double fl = .85 + .15 * noise(t * 11, 9u);
            p.glowLine(-11, fy, 11, fy, 9, C[4], .45 * k);
            p.glow(0, fy - 6, 56, C[4], .38 * k * fl);
            p.glow(0, fy - 2, 18, C[5], .28 * k * fl);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& dissolveBurnDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor}, {"ash", 1, 64, false, 3, &PixelFxSpawnSettings::ash}}};
    static const Descriptor d{
        {PixelFxKind::DissolveBurn, "spawn/dissolve-burn", 96, 112, 48, 100, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 16;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a0e08), hex(0x5a1a0c), hex(0xa8320f), hex(0xe8601a),
                              hex(0xff9a2e), hex(0xffd25a), hex(0xfff3c0), WHITE};
            s.spawn.ash    = {hex(0x2a2428), hex(0x4a4248), hex(0x6e6670)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DissolveBurn>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
