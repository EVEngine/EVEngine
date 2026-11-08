// eve-hot-path
// Native adaptation of pixelfx ui/level-up.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiActor.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double REL = .28, RET = 1.02, GONE = 1.34, TOP = 100, TY = -68, DROP = .5, STAG = .05, LANDED = 1.06,
                 SHINE = 1.24, EXIT = 2.3, BASE = TY + 14, END = 3.2 - .05;
struct Strand {
    double x, speed, period, duty, phase;
    int    level;
    double end;
};
struct LevelLetter {
    Glyph  glyph;
    double width, cx, born, pop, off = -18, velocity = 60, squash = 0, flash = 0;
    bool   on = false, landed = false, rest = false, burst = false;
};
struct LevelSpark {
    double x, y, t0, duration, length;
};
struct Rush {
    double   x, y, vx, vy, life, length;
    uint32_t color;
    bool     back;
};
double widthAt(double t) {
    return t < REL ? 0 : 12 * (1 + .4 * std::exp(-(t - REL) * 10)) * (1 - .82 * smooth(RET - .12, RET + .16, t));
}
struct LevelUp final : Kernel {
    const std::vector<uint32_t> C;
    const PixelFxActor          actor;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    const ui::Word              word;
    std::vector<LevelLetter>    letters;
    mutable ui::WordScratch     scratch;
    std::array<Strand, 9>       strands{};
    std::array<LevelSpark, 8>   sats{};
    std::array<double, 4>       halos{};
    size_t                      haloCount = 0;
    std::vector<Rush>           rush;
    double                      t = 0, nextHalo = REL + .03, gather = 0;
    bool                        released = false;
    explicit LevelUp(const PixelFxSettings& o)
        : C(o.palette),
          actor(o.actor),
          rng(o.seed),
          word(ui::spell(o.ui.text)),
          scratch(size_t((word.width + 16) * 4 + 64), 128) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        letters.reserve(word.letters.size());
        rush.reserve(128);
        for (const auto& L : word.letters)
            if (L.glyph.width) {
                const double i = double(letters.size());
                letters.push_back({L.glyph, double(L.width), -word.width + (L.x + L.width / 2.) * 2, DROP + i * STAG,
                                   EXIT + i * .04});
            }
        for (uint32_t i = 0; i < 9; ++i)
            strands[i] = {(double(i) / 8 - .5) * 1.5 + (rand({i, 1}) - .5) * .16,
                          170 + rand({i, 2}) * 170,
                          10 + rand({i, 3}) * 16,
                          .3 + rand({i, 4}) * .35,
                          rand({i, 5}),
                          rand({i, 6}) < .35 ? 1 : 2,
                          .5 + rand({i, 7}) * .42};
        for (size_t i = 0; i < sats.size(); ++i) {
            const double s = i % 2 ? 1 : -1, x = s * rng.range(20, 60);
            double       offset;
            if (std::abs(x) < 50) {
                const int sign = rng.sign();
                offset         = sign * rng.range(11, 18);
            } else
                offset = rng.range(-14, 14);
            const double y = TY + 7 + offset, t0 = SHINE - .1 + double(i) * .13 + rng.range(0, .05),
                         duration = rng.range(.32, .5), length = rng.range(3, 6);
            sats[i] = {x, y, t0, duration, length};
        }
    }
    void spawn(ParticleKind kind, double x, double y, double vx, double vy, double life, uint32_t color = WHITE,
               double size = 1, ParticleFlag flags = ParticleFlag::None) {
        ps.spawn(rng, kind, x, y, vx, vy, std::min(life, END - t), color, size, 0, flags);
    }
    void update(double dt) override {
        t += dt;
        gather += t < REL ? dt * 420 : 0;
        for (; gather >= 1; --gather) {
            const double kind = rng.next(), speed = rng.range(140, 250) * (1 + t * 2);
            double       x, y, tx, ty;
            if (kind < .42) {
                const double a = rng.range(0, TAU), d = rng.range(22, 70);
                x  = std::cos(a) * d;
                y  = -1 - std::abs(std::sin(a)) * d * .1;
                tx = std::cos(a) * 3;
                ty = -1;
            } else if (kind < .84) {
                const double a = rng.range(0, TAU), d = rng.range(34, 64);
                x  = std::cos(a) * d;
                y  = std::min(-3., -20 + std::sin(a) * d * .85);
                tx = std::cos(a) * 11;
                ty = -20 + std::sin(a) * 14;
            } else {
                if (t < REL * .4) continue;
                x  = rng.range(-9, 9);
                y  = -1;
                tx = x * .7;
                ty = -rng.range(26, 44);
            }
            const double dx = tx - x, dy = ty - y, d = jsHypot2(dx, dy), l = d ? d : 1,
                         life = std::min(l / speed, REL + .06 - t), length = rng.range(5, 9);
            const auto   color = rng.isChance(.45) ? C[6] : C[5];
            const bool   back  = kind > .84 && rng.isChance(.5);
            rush.push_back({x, y, (dx / l) * speed, (dy / l) * speed, life, length, color, back});
        }
        for (size_t i = rush.size(); i > 0; --i) {
            auto& m = rush[i - 1];
            if ((m.life -= dt) <= 0)
                rush.erase(rush.begin() + std::ptrdiff_t(i - 1));
            else {
                m.x += m.vx * dt;
                m.y += m.vy * dt;
            }
        }
        const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
        const std::array<uint32_t, 2> bright{C[7], C[6]};
        if (t < REL && rng.isChance(.12)) {
            const double x = rng.range(-30, 30), y = -rng.range(0, 3), vy = -rng.range(10, 30),
                         life = rng.range(.3, .5);
            spawn(ParticleKind::Star, x, y, 0, vy, life, C[6], .6);
        }
        if (!released && t >= REL) {
            released = true;
            lights.add(0, -24, 90, C[5], .6, .7);
            lights.add(0, -8, 40, WHITE, .6, .12);
            for (int i = 0; i < 22; ++i) {
                const double a = (double(i) / 22) * TAU + rng.range(-.12, .12), speed = rng.range(260, 460),
                             y = -18 + std::sin(a) * 8, vy = std::sin(a) * speed * .8,
                             life  = std::min({rng.range(.1, .2),
                                               vy > 0   ? -y / vy
                                               : vy < 0 ? (y + 104) / -vy
                                                        : 1.,
                                               78 / std::abs(std::cos(a) * speed)});
                const auto   color = rng.isChance(.4) ? C[7] : C[6];
                spawn(ParticleKind::Mote, std::cos(a) * 6, y, std::cos(a) * speed, vy, life, color, 1,
                      a > .3 && a < PI - .3 ? ParticleFlag::Back : ParticleFlag::None);
            }
            for (int i = 0; i < 26; ++i) {
                const double a = -PI / 2 + rng.range(-1.3, 1.3), speed = rng.range(120, 290), x = rng.range(-5, 5),
                             life = rng.range(.35, .8);
                spawn(ParticleKind::Spark, x, -2, std::cos(a) * speed, std::sin(a) * speed, life);
            }
            for (int i = 0; i < 16; ++i) {
                const double x = rng.range(-10, 10), y = rng.range(-36, -4), vx = rng.range(-30, 30),
                             vy = -rng.range(15, 60), life = rng.range(.6, 1.3);
                const auto   color = rng.pick<uint32_t>(colors);
                const double size  = rng.isChance(.35) ? rng.range(1.5, 2.6) : rng.range(.4, .9);
                const auto   flags = rng.isChance(.4) ? ParticleFlag::Back : ParticleFlag::None;
                spawn(ParticleKind::Star, x, y, vx, vy, life, color, size, flags);
            }
        }
        const double W = widthAt(t);
        if (t >= nextHalo && t < RET - .1) {
            EV_ASSERT(haloCount < halos.size());
            halos[haloCount++] = t;
            nextHalo += .16;
        }
        if (t > REL && t < RET + .1 && rng.isChance(.5)) {
            const int    sign = rng.sign();
            const double y = rng.range(-4, -60), vy = -rng.range(140, 260), x = sign * rng.range(.75, 1.6) * W,
                         life  = std::min(rng.range(.16, .32), (y + 96) / -vy);
            const auto   color = rng.isChance(.5) ? C[7] : C[6];
            const double size  = rng.range(.6, 1);
            const auto   flags = rng.isChance(.5) ? ParticleFlag::Back : ParticleFlag::None;
            spawn(ParticleKind::Mote, x, y, 0, vy, life, color, size, flags);
        }
        if (t > REL && t < 2.4 && rng.isChance(.14 * (1 - seg(t, 1.6, 2.4)))) {
            const double x = rng.range(-20, 20), y = -rng.range(1, 30), vx = rng.range(-15, 15),
                         vy = -rng.range(20, 55), life = rng.range(.8, 1.6), size = rng.range(1, 2);
            const auto   flags = rng.isChance(.4) ? ParticleFlag::Back : ParticleFlag::None;
            spawn(ParticleKind::Ember, x, y, vx, vy, life, C[5], size, flags);
        }
        if (actor.source != PixelFxActorSource::None && t > REL + .1 && t < 2.2 && rng.isChance(.09))
            for (int n = 0; n < 12; ++n) {
                const int i = rng.integer(0, actor.width - 1), j = rng.integer(0, actor.height - 1);
                if (!actor.mask[size_t(j) * size_t(actor.width) + size_t(i)]) continue;
                const double vx = rng.range(-8, 8), vy = -rng.range(14, 34), life = rng.range(.6, 1.1);
                const auto   color = rng.pick<uint32_t>(colors);
                const double size  = rng.isChance(.2) ? 1.5 : .6;
                const auto   flags = rng.isChance(.5) ? ParticleFlag::Back : ParticleFlag::None;
                spawn(ParticleKind::Star, actor.x + i + .5, actor.y + j + .5, vx, vy, life, color, size, flags);
                break;
            }
        if (t > 1 && t < 2.5 && rng.isChance(.13)) {
            const bool   up = rng.isChance(.6);
            const double x  = up ? rng.range(-58, 58) : rng.range(-20, 20),
                         y = up ? TY + 7 + rng.range(-16, 16) : rng.range(-44, -6), vx = rng.range(-6, 6),
                         vy = -rng.range(4, 14), life = rng.range(.4, .8);
            const std::array<uint32_t, 4> colors4{C[7], C[6], C[6], C[5]};
            const auto                    color = rng.pick<uint32_t>(colors4);
            const double                  size  = rng.isChance(.3) ? rng.range(1.5, 2.4) : .6;
            const auto                    flags = rng.isChance(.3) ? ParticleFlag::Back : ParticleFlag::None;
            spawn(ParticleKind::Star, x, y, vx, vy, life, color, size, flags);
        }
        for (auto& L : letters) {
            if (t < L.born) continue;
            L.on = true;
            if (!L.rest) {
                L.velocity += 1700 * dt;
                L.off += L.velocity * dt;
                if (L.off >= 0 && L.velocity > 0) {
                    L.off = 0;
                    if (L.velocity > 110) {
                        L.squash = 1;
                        if (!L.landed) {
                            L.landed = true;
                            L.flash  = 1;
                            lights.add(L.cx, BASE - 7, 18, C[5], .55, .25);
                            for (int sign : {-1, 1}) {
                                const double y = BASE - rng.range(0, 3), vx = sign * rng.range(15, 40),
                                             vy = -rng.range(5, 25), life = rng.range(.25, .45),
                                             size = rng.range(.5, 1.2);
                                spawn(ParticleKind::Star, L.cx + sign * L.width, y, vx, vy, life, C[6], size);
                            }
                        }
                        L.velocity = -L.velocity * .3;
                    } else {
                        L.velocity = 0;
                        L.rest     = true;
                    }
                }
            }
            L.squash = std::max(0., L.squash - dt / .1);
            L.flash  = std::max(0., L.flash - dt / .12);
            if (!L.burst && t >= L.pop + .06) {
                L.burst = true;
                for (int i = 0; i < 3; ++i) {
                    const double a = rng.range(0, TAU), speed = rng.range(20, 55), x = L.cx + rng.range(-3, 3),
                                 y = BASE - 7 + rng.range(-4, 4), life = rng.range(.3, .6);
                    const auto   color = rng.pick<uint32_t>(bright);
                    const double size  = i ? .6 : rng.range(1.2, 2);
                    spawn(ParticleKind::Star, x, y, std::cos(a) * speed, std::sin(a) * speed - 15, life, color, size);
                }
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void column(Paint& p) const {
        const double a = t - REL;
        if (a < 0 || t > GONE) return;
        const double top = -TOP * ease::outExpo(clamp(a / .12)), bot = -TOP * ease::inCubic(seg(t, RET, GONE)),
                     W = widthAt(t), k = 1 - smooth(RET, GONE, t);
        p.glowLine(0, top, 0, std::min(bot, -34.), W * 1.6 + 6, C[5], .2 * k + .06);
        p.glowLine(0, top, 0, std::min(bot, -40.), 5, WHITE, .18 * k);
        if (a < .04) {
            for (double y = std::floor(top); y < bot; ++y) {
                const double wr = W * .55 * (1 - .3 * (-y / TOP));
                for (double x = std::floor(-wr); x <= std::ceil(wr); ++x)
                    p.dot(x, y, std::abs(x + .5) < wr - 1.5 ? WHITE : C[6], clamp((y + TOP) / 20) * 1.5);
            }
            return;
        }
        for (double y = std::ceil(top); y < std::ceil(bot); ++y) {
            const double h = -y / TOP, fade = clamp((y + TOP) / 16) * (t < RET ? 1 : clamp((bot - y) / 10)),
                         wr   = W * (1 - .3 * h) + .9 * noise2(y * .2, t * 12, 3);
            const bool   head = y - top < 3 && a < .16;
            for (int sign : {-1, 1}) {
                const bool   run = fract(y * .06 + t * 3.4 + (sign > 0 ? .5 : 0)) < .78;
                const double xw  = sign * wr;
                p.dot(xw, y, head ? WHITE : run ? C[6] : C[5], fade * 1.4);
                p.dot(xw + sign, y, C[4], fade * (run ? .5 : .25));
                if (run) p.dot(xw - sign, y, C[5], fade * 1.2);
            }
            p.dot(0, y, WHITE, fade * 1.6);
            p.dot(-1, y, noise2(y * .3, t * 14, 5) > -.2 ? WHITE : C[6], fade * 1.3);
            if (noise2(y * .25, t * 9, 8) > .25) p.dot(1, y, C[6], fade * 1.2);
            for (const auto& S : strands) {
                if (h > S.end) continue;
                const double phase = fract((y + t * S.speed) / S.period + S.phase);
                if (phase > S.duty) continue;
                const double u = phase / S.duty;
                p.px(S.x * wr, y, head || u < .2 ? WHITE : u < .65 ? C[size_t(7 - S.level)] : C[size_t(6 - S.level)]);
            }
        }
    }
    void plate(Paint& p, double k) const {
        if (k <= .02) return;
        const double rx = 9 + 9 * k + std::sin(t * 9), ry = 2.2;
        p.shader(-rx, -ry, rx, ry, [&](double x, double y, ShadeOutput& out) {
            const double dx = x / rx, dy = y / ry, d = std::sqrt(dx * dx + dy * dy);
            if (d > 1) return;
            const double u = d / std::max(.05, k);
            out.c          = u < .3 ? WHITE : u < .55 ? C[6] : u < .8 ? C[5] : C[4];
            out.a          = d < .6 * k ? 1 : k * (1 - d) * 2.2;
        });
        p.glow(0, -1, rx * 2.2, C[4], .35 * k);
    }
    void rays(Paint& p, double k) const {
        if (k <= .02) return;
        for (uint32_t i = 0; i < 20; ++i) {
            const double a = (double(i) / 20) * TAU + .08, ca = std::cos(a), sa = std::sin(a);
            if (std::abs(sa) < .4) continue;
            const double r0  = 1 / std::sqrt(std::pow(ca / 52, 2) + std::pow(sa / 12, 2)) + 2 * k,
                         len = (i % 2 == 0 ? 15 + 7 * rand({i, 9}) : 7 + 4 * rand({i, 8})) * k;
            if (len < 1) continue;
            const double sh = .75 + .25 * std::sin(t * 7 + i * 2.1), x0 = ca * r0, y0 = TY + 7 + sa * r0;
            p.lineFn(
                x0, y0, x0 + ca * len, y0 + sa * len, [&](double u) { return u < .3   ? C[7]
                                                                             : u < .6 ? C[6]
                                                                                      : C[5]; },
                [&](double u) { return sh * (u < .55 ? 1.3 : (1.3 * (1 - u)) / .45); });
        }
    }
    void drawRush(Paint& p, bool back) const {
        for (const auto& m : rush) {
            if (m.back != back) continue;
            const double speed = jsHypot2(m.vx, m.vy), ux = m.vx / speed, uy = m.vy / speed;
            p.line(m.x - ux * m.length, m.y - uy * m.length, m.x - ux * m.length * .4, m.y - uy * m.length * .4,
                   m.color);
            p.line(m.x - ux * m.length * .35, m.y - uy * m.length * .35, m.x, m.y, WHITE);
            p.glow(m.x, m.y, 4, C[5], .25);
        }
    }
    void rings(Paint& p, bool back) const {
        const auto keep = [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; };
        if (t < REL) {
            const double k = seg(t, .02, REL);
            band(p, 0, 0, 4 + 52 * (1 - ease::inQuad(k)), .2, 1.5, k * 1.1, C, keep);
        }
        for (const auto values : std::array<std::array<double, 3>, 2>{{{{0, 72, 4}}, {{.08, 46, 2.5}}}}) {
            const double k = (t - REL - values[0]) / .55;
            if (k > 0 && k < 1)
                band(p, 0, 0, 5 + values[1] * ease::outCubic(k), .2, std::max(1., values[2] * (1 - k)), 1.15 - k, C,
                     keep, k < .15);
        }
        for (size_t i = 0; i < haloCount; ++i) {
            const double k = (t - halos[i]) / .6;
            if (k > 0 && k < 1)
                band(p, 0, -2 - 72 * ease::outQuad(k), (widthAt(t) * 1.45 + 4) * (1 - .4 * k), .3, 1.5, (1 - k) * 1.4,
                     C, keep, k < .12);
        }
    }
    void drawBack(Paint& p) const override {
        rays(p, ease::outExpo(seg(t, LANDED, LANDED + .2)) * (1 - ease::inQuad(seg(t, EXIT - .15, EXIT + .1))));
        rings(p, true);
        column(p);
        plate(p, t < REL ? .55 * seg(t, .05, REL) : 1 - smooth(RET - .1, GONE + .2, t));
        ps.draw(p, Paint::Layer::Back);
        drawRush(p, true);
    }
    void draw(Paint& p) const override {
        rings(p, false);
        const double                  lit = smooth(REL - .02, REL + .04, t) * (1 - smooth(RET, 2.5, t));
        const std::array<uint32_t, 3> tint{C[4], C[5], C[6]};
        ui::tintActor(p, actor, t < REL ? 0 : t < REL + .06 ? 1 : std::exp(-(t - REL - .06) / .07), tint);
        ui::lightActor(p, actor, C[5], .1 * seg(t, .05, REL) * (1 - lit) + .06 * lit, .6 * lit, t);
        ui::haloActor(p, actor, C[5], .85 * lit * (1 - smooth(RET, GONE + .3, t)) + .3 * lit, t);
        if (t < REL) {
            const double k = seg(t, .05, REL), beat = 1 + .4 * std::exp(-std::pow(t - REL * .6, 2) / .001);
            p.sparkle(0, -2, (1 + 4 * k * k) * beat, C[6], C[5], .3 + .4 * k);
        } else if (t < REL + .22) {
            const double a = (t - REL) / .22;
            flare(p, 0, -4, 28 * std::pow(1 - a, 1.5), 1 - a, C);
            p.glow(0, -16, 48, WHITE, .3 * (1 - a));
        }
        ps.draw(p, Paint::Layer::Front);
        drawRush(p, false);
        ui::WordItems items;
        const double  done = (t - LANDED) / .12;
        for (const auto& L : letters) {
            if (!L.on) continue;
            double sx = 2, sy = 2, flash = L.flash;
            if (!L.landed && L.velocity > 150) {
                sx = 1.8;
                sy = 2.3;
            }
            if (L.squash > 0) {
                sx = 2 * (1 + .22 * L.squash);
                sy = 2 * (1 - .22 * L.squash);
            }
            const double pu = (t - L.pop) / .07, hop = seg(t, L.pop - .1, L.pop);
            if (pu >= 1) continue;
            if (hop > 0 && pu <= 0) {
                sx = 2 * (1 - .1 * hop);
                sy = 2 * (1 + .15 * hop);
            }
            if (pu > 0) {
                sx = sy = 2 * (1 + .3 * pu);
                flash   = 1;
            }
            flash          = std::max(flash, done > 0 && done < 1 ? 1 - done : 0);
            const double h = 7 * sy, w = L.width * sx;
            items.add({L.glyph, 7, L.cx - w / 2, BASE + L.off - h - 3 * ease::outQuad(hop), sx, sy,
                       clamp((t - L.born) / .05), flash});
        }
        const double sh = seg(t, SHINE, SHINE + .34), x0 = -word.width, right = word.width;
        const auto   shine =
            sh > 0 && sh < 1 ? std::optional<double>(x0 - 14 + (right - x0 + 30) * ease::inOutSine(sh)) : std::nullopt;
        const std::array<uint32_t, 4> fill{C[3], C[4], C[5], C[6]};
        ui::WordStyle                 style{fill};
        style.edge    = WHITE;
        style.under   = C[2];
        style.outline = C[0];
        style.shadow  = scale(C[0], .5);
        style.shine   = shine;
        style.shineY  = TY;
        style.glint   = C[6];
        scratch.draw(p, items.view(), style);
        if (shine && *shine > x0 && *shine < right) p.glowLine(*shine, TY, *shine + 8.4, BASE, 9, C[6], .3);
        const double hold = smooth(.9, 1.2, t) * (1 - smooth(EXIT, EXIT + .4, t));
        if (hold > 0) p.glow(0, TY + 7, 50, C[5], .22 * hold + (done > 0 && done < 1 ? .3 * (1 - done) : 0));
        for (const auto& L : letters) {
            const double u = (t - L.pop - .07) / .16;
            if (u > 0 && u < 1) p.sparkle(L.cx, BASE - 7, 6 * (1 - u) * (1 - u), C[6], C[5], .5);
        }
        const double g = (t - SHINE - .3) / .26;
        if (g > 0 && g < 1) p.sparkle(right - 2, TY + 1, 7 * std::sin(g * PI), C[6], C[5], .6);
        for (const auto& s : sats) {
            const double u = (t - s.t0) / s.duration;
            if (u > 0 && u < 1) p.sparkle(s.x, s.y, s.length * std::pow(std::sin(u * PI), .7), C[6], C[5], .5);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& levelUpDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor}, {"text", 0, 64, false, 0, &PixelFxUiSettings::text}}};
    static const Descriptor d{
        {PixelFxKind::LevelUp, "ui/level-up", 176, 138, 88, 114, 3.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.ui.text      = "LEVEL UP!";
            s.palette      = {hex(0x2a1405), hex(0x5c3008), hex(0x9a5a0e), hex(0xd48e18),
                              hex(0xf5b92c), hex(0xffde5e), hex(0xfff4bc), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<LevelUp>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
