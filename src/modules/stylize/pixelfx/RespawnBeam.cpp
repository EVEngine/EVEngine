// eve-hot-path
// Native adaptation of pixelfx spawn/respawn-beam.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SpawnBody.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SLAM = .32, WRITE0 = SLAM + .08, WRITE = .95, DONE = WRITE0 + WRITE, LIFT = DONE + .12, TOPY = -136,
                 DROP = .07;
struct BeamStrand {
    double offset, frequency, width;
};
struct BeamEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct RespawnBeam final : Kernel {
    const std::vector<uint32_t>        C;
    Rng                                R;
    const double                       BW;
    const spawn::Body                  body;
    const std::vector<spawn::RimPixel> rim;
    std::array<BeamStrand, 6>          strands;
    std::vector<double>                rowTimes;
    Particles                          ps;
    Lights                             lights;
    double                             t       = 0;
    bool                               slammed = false, finished = false;
    explicit RespawnBeam(const PixelFxSettings& o)
        : C(o.palette), R(o.seed), BW(o.spawn.width), body(spawn::bodyOf(o.actor, C)), rim(spawn::rimOf(body)) {
        ps.sparks = {WHITE, C[5], C[4], C[3]};
        for (size_t i = 0; i < strands.size(); ++i)
            strands[i] = {rand({uint32_t(i), 1u}) * 1.6 - .8, 1 + rand({uint32_t(i), 2u}) * 3, rand({uint32_t(i), 3u})};
        for (int my = 0; my < body.height; ++my)
            rowTimes.push_back(WRITE0 +
                               (WRITE * std::acos(1 - 2 * clamp(double(body.height - my) / (body.height + 2)))) / PI);
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < DONE + .04 ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible;
    }
    double scanY() const { return -ease::inOutSine(clamp((t - WRITE0) / WRITE)) * (body.height + 2); }
    double width() const {
        return t < SLAM - DROP ? 0
               : t < SLAM      ? BW * .6
               : t < LIFT      ? BW * (1 + .4 * std::exp(-(t - SLAM) * 10)) * (.92 + .08 * std::sin(t * 40))
                               : BW * (1 - ease::outCubic(clamp((t - LIFT) / .3)));
    }
    double bottom() const { return t < LIFT ? 0 : -150 * ease::inCubic(clamp((t - LIFT) / .4)); }
    void   emit(const BeamEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void   update(double dt) override {
        t += dt;
        if (t < SLAM && R.isChance(.5)) {
            const double side = R.sign(), distance = R.range(18, 40), life = R.range(.15, .25),
                         v = (distance * 3) / (std::exp(3 * life) - 1);
            emit({ParticleKind::Streak, side * distance, -R.range(0, 3), -side * v, 0, life,
                  R.isChance(.5) ? C[5] : C[4]});
        }
        if (!slammed && t >= SLAM) {
            slammed = true;
            lights.add(0, -28, 64, C[4], 1, .5);
            lights.add(0, -52, 80, C[5], 1.1, .4);
            lights.add(0, -9, 24, WHITE, .9, .12);
            for (int i = 0; i < 40; ++i) {
                const double side = R.sign(), sp = R.range(60, 135);
                emit({ParticleKind::Spark, side * R.range(0, 6), -1, side * sp, -R.range(20, 140), R.range(.25, .7)});
            }
            for (int i = 0; i < 12; ++i)
                emit({ParticleKind::Star, R.range(-BW, BW), -R.range(2, 40), R.range(-20, 20), -R.range(20, 70),
                      R.range(.5, 1), R.pick<uint32_t>(std::array{WHITE, C[5]}), R.range(.8, 1.8)});
            for (int i = 0; i < 8; ++i) {
                const double side = R.sign();
                emit({ParticleKind::Dust, side * R.range(4, 10), -1, side * R.range(30, 80), -R.range(3, 10),
                      R.range(.4, .7), C[2], R.range(1.5, 3)});
            }
        }
        if (t > WRITE0 && t < DONE && R.isChance(.45)) {
            const double y = scanY(), side = R.sign();
            emit({ParticleKind::Spark, side * R.range(6, 11), y, side * R.range(40, 140), R.range(-60, 10),
                  R.range(.15, .35)});
        }
        if (t > SLAM && t < LIFT + .2 && R.isChance(.6))
            emit({ParticleKind::Star, R.range(-1.3, 1.3) * BW, -R.range(0, 90), R.range(-6, 6), -R.range(30, 90),
                  R.range(.4, .9), R.pick<uint32_t>(std::array{WHITE, C[5], C[5], C[4]}),
                  R.isChance(.3) ? R.range(1, 1.8) : .6});
        if (!finished && t >= DONE) {
            finished = true;
            lights.add(0, body.y + double(body.height) / 2, 40, C[5], .7, .3);
            for (int i = 0; i < 14; ++i) {
                const auto& c = rim[size_t(std::floor(R.next() * double(rim.size())))];
                emit({ParticleKind::Star, c.x, c.y, R.range(-25, 25), -R.range(10, 50), R.range(.4, .9),
                      R.pick<uint32_t>(std::array{WHITE, C[5]}), R.range(.8, 1.8)});
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double w = width(), b = bottom();
        if (t < SLAM) {
            const double k = smooth(0, SLAM * .7, t) * (1 - smooth(SLAM - DROP, SLAM, t) * .5);
            for (int y = int(TOPY); y < -1; y += 2)
                p.dot(0, y + std::fmod(std::trunc(t * 60), 2), C[5], k * (.35 + .65 * ((y - TOPY) / -TOPY)));
            band(p, 0, 0, 10 - 4 * k, .2, 1, k, C, [](double g) { return std::sin(g) < 0; });
            p.glow(0, -2, 16, C[4], .4 * k);
        }
        if (w <= .2) return;
        const double yEnd    = t < SLAM ? TOPY + (-1 - TOPY) * ease::inCubic(clamp((t - SLAM + DROP) / DROP)) : -1 + b;
        const double writing = smooth(WRITE0 - .05, WRITE0 + .1, t) * (1 - smooth(DONE, LIFT, t)),
                     fill    = .2 - .1 * writing;
        for (int y = int(TOPY); y <= yEnd; ++y)
            for (int x = int(std::floor(-w)); x <= std::ceil(w); ++x) {
                const double q = std::abs((x + .5) / w);
                if (q > 1) continue;
                if (q > .86) {
                    p.dot(x, y, q > .94 && rand({jsUint(x), jsUint(y), jsUint(std::trunc(t * 30))}) < .7 ? WHITE : C[5],
                          1);
                    continue;
                }
                const double core = 1 - smooth(SLAM + .06, WRITE0 + .08, t);
                if (q < .7 * core) {
                    bool hot = q < .35 * core;
                    for (const auto& s : strands)
                        if (std::abs((x + .5) / w - s.offset * .6 - .05 * std::sin(t * s.frequency * 3 + y * .05)) <
                            .04)
                            hot = false;
                    p.px(x, y, hot ? WHITE : q < .5 * core ? C[6] : C[5]);
                    continue;
                }
                double   a = fill * (1.2 - q);
                uint32_t c = C[4];
                for (const auto& s : strands)
                    if (std::abs((x + .5) / w - s.offset - .06 * std::sin(t * s.frequency + s.offset * 7)) <
                        .045 + .03 * s.width) {
                        a = .55 - .25 * writing;
                        c = C[5];
                    }
                p.blend(x, y, c, a);
            }
        if (yEnd > TOPY + 24)
            for (int side : {-1, 1}) p.glowLine(side * w, TOPY + 24, side * w, yEnd, 6, C[4], .3);
        p.glow(0, -5, w * 1.5 + 6, C[5], .45);
    }
    void draw(Paint& p) const override {
        const double w = width(), sk = (t - SLAM) / .45;
        if (sk > 0 && sk < 1) {
            const double rx = 14 + 18 * ease::outCubic(sk), ry = 3 + 1.5 * sk;
            p.ellipse(
                0, .5, rx, ry, [&](double d) { return d < .4   ? WHITE
                                                      : d < .7 ? C[6]
                                                               : C[5]; },
                [&](double d) { return (1 - sk) * 1.4 * (1 - d * d); });
            p.glow(0, -2, rx * 1.3, C[5], .5 * (1 - sk));
        }
        for (int i = 0; i < 2; ++i) {
            const double k = (t - SLAM - .08 * i) / .5;
            if (k > 0 && k < 1)
                band(
                    p, 0, 0, 8 + (i ? 22 : 34) * ease::outCubic(k), .2, std::max(1., 3 * (1 - k)), 1.15 - k, C,
                    [](double) { return true; }, k < .15);
        }
        if (t >= WRITE0 && t < DONE + .05) {
            const double sy = scanY(), fr = std::floor(t * 30);
            for (const auto& b : body.list) {
                const double age = t - rowTimes[size_t(b.my)];
                if (age < 0) continue;
                const double   glitch = age < .12 && rand({jsUint(b.my), jsUint(fr), 9u}) < .3
                                            ? (rand({jsUint(b.my), jsUint(fr), 4u}) < .5 ? -2 : 2)
                                            : 0;
                const uint32_t c      = age < .05   ? WHITE
                                        : age < .14 ? C[5]
                                        : age < .3  ? mix(b.color, C[5], .5 * (1 - (age - .14) / .16))
                                                    : b.color;
                p.px(b.x + glitch, b.y, b.edge && age < .3 ? C[6] : c);
            }
            if (t < DONE) {
                p.lineFn(-w - 2, sy, w + 2, sy, [&](double u) { return std::abs(u - .5) < .35 ? WHITE : C[5]; }, 1.);
                p.line(-w, sy + 1, w, sy + 1, C[4], .6);
                p.glowLine(-w, sy, w, sy, 6, C[5], .6);
            }
        }
        const double fk = (t - DONE) / .25;
        if (fk > 0 && fk < 1) {
            for (const auto& c : rim) p.dot(c.x, c.y, fk < .4 ? WHITE : C[5], 1.2 - fk);
            p.sparkle(6, body.y + 6, jsRound(6 * (1 - fk)), WHITE, C[5], .5);
        }
        if (w > .2 && t >= SLAM && t - SLAM < .1) p.glow(0, -30, 56, WHITE, .6 * (1 - (t - SLAM) / .1));
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& respawnBeamDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"width", 0, 256, false, 0, &PixelFxSpawnSettings::width}}};
    static const Descriptor            d{
        {PixelFxKind::RespawnBeam, "spawn/respawn-beam", 128, 150, 64, 136, 2.5, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 12;
            s.actor.source = PixelFxActorSource::Dummy;
            s.spawn.width  = 15;
            s.palette      = {hex(0x061e1b), hex(0x0c3e36), hex(0x136a5a), hex(0x1f9c80),
                              hex(0x45d1a8), hex(0xa8f5d8), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<RespawnBeam>(s); },
        nullptr,
        nullptr,
        true,
        1};
    return d;
}
}  // namespace eve::stylize::pixelfx
