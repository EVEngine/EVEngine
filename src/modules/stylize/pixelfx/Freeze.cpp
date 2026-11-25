// eve-hot-path
// Native adaptation of pixelfx ice/freeze.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/IceFractures.h"
#include "stylize/pixelfx/IcePanes.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SNAP = .16, SEAL = .6, CRACK = 2.2, CRACK2 = 2.34, FLASH = 2.46, BURST = 2.52, THAW = .36, D = 4,
                 REACH = 38;
constexpr int    X0 = -18, X1 = 18, Y0 = -53, Y1 = 0, BW = X1 - X0;
constexpr Point  CORE{0, -21};
constexpr std::array<Point, 19> V{{{-15, 0},
                                   {-17, -10},
                                   {-17, -31},
                                   {-12, -45},
                                   {-6, -42},
                                   {0, -52},
                                   {6, -43},
                                   {12, -47},
                                   {17, -30},
                                   {16, -9},
                                   {14, 0},
                                   {-6, -36},
                                   {6, -35},
                                   {-9, -21},
                                   {3, -25},
                                   {9, -13},
                                   {-5, -8},
                                   {2, 0},
                                   {0, -40}}};
struct FacetSpec {
    std::array<int, 5> ids;
    int                n;
    double             lit, delay;
};
constexpr std::array<FacetSpec, 13> SPECS{{{{{0, 1, 16, 17, 0}}, 4, .62, 0},
                                           {{{16, 15, 17, 0, 0}}, 3, .42, .03},
                                           {{{15, 8, 9, 10, 17}}, 5, .2, .05},
                                           {{{1, 2, 13, 16, 0}}, 4, .74, .08},
                                           {{{13, 14, 15, 16, 0}}, 4, .5, .1},
                                           {{{12, 8, 15, 14, 0}}, 4, .28, .13},
                                           {{{2, 11, 13, 0, 0}}, 3, .7, .15},
                                           {{{11, 12, 14, 13, 0}}, 4, .58, .18},
                                           {{{4, 18, 6, 12, 11}}, 5, .52, .2},
                                           {{{12, 6, 7, 8, 0}}, 4, .4, .23},
                                           {{{2, 3, 4, 11, 0}}, 4, .88, .25},
                                           {{{5, 6, 18, 0, 0}}, 3, .66, .28},
                                           {{{4, 5, 18, 0, 0}}, 3, 1, .31}}};
constexpr std::array<Point, 11>     OUTLINE{{V[0], V[1], V[2], V[3], V[4], V[5], V[6], V[7], V[8], V[9], V[10]}};
struct Facet {
    std::vector<Point> pts;
    double             lit, at;
    ice::Centroid      c;
    uint32_t           tint;
    double             alpha, d, len;
};
struct Edge {
    int                 a, b;
    std::vector<size_t> faces;
    bool                outer = false, floor = false;
    double              lit = 0, at = 0;
};
struct Feather {
    double x, y;
    int    dx, dy, L;
    double at;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct Chip {
    DebrisKind kind;
    double     x, y, vx, vy, g, drag, bounce, life, a, va, s;
    uint32_t   c;
};
struct Freeze final : Kernel {
    std::vector<uint32_t>            C;
    PixelFxActor                     A;
    uint32_t                         seed;
    Rng                              R;
    Particles                        ps;
    Bits                             bits;
    Lights                           lights;
    ice::Panes                       panes;
    std::vector<float>               reach;
    std::vector<uint32_t>            iceColor;
    std::vector<size_t>              order;
    std::vector<Feather>             feathers;
    std::array<Facet, 13>            facets;
    std::array<int8_t, BW*(Y1 - Y0)> fid{};
    std::vector<Edge>                edges;
    std::vector<ice::Crack>          cracks, cracks2;
    double                           t      = 0;
    size_t                           formed = 0;
    int                              struck = 0;
    bool                             sealed = false, burst = false, white = false;
    explicit Freeze(const PixelFxSettings& o) : C(o.palette), A(o.actor), seed(o.seed), R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        prepareActor();
        prepareBlock();
        const auto           inside = [](double x, double y) { return ice::isInsidePoly(OUTLINE, x, y); };
        ice::FractureOptions f;
        f.length = 9;
        f.angle  = -2.2;
        f.spread = 3.6;
        cracks   = ice::fractures(R, inside, CORE.x, CORE.y, 3, f);
        f        = ice::FractureOptions{};
        f.length = 16;
        f.fork   = .22;
        cracks2  = ice::fractures(R, inside, CORE.x + 1, CORE.y - 2, 6, f);
    }
    bool isActorCell(int x, int y) const {
        return x >= 0 && y >= 0 && x < A.width && y < A.height && A.mask[size_t(y) * size_t(A.width) + size_t(x)] > 0;
    }
    uint32_t frozenColor(uint32_t c) const {
        const double l = luma(c);
        return C[size_t(
            clamp(jsRound(((l < .12 ? 2 : 2 + l * 5.2) * double(C.size() - 1)) / 7), 1, double(C.size() - 1)))];
    }
    /** @brief Build owning source Float32 actor fields once for this private state.
     * @cost Scans up to 65536 cells and a source-ordered linear open list at construction/reset; cached across frames.
     */
    void prepareActor() {
        const int          W = A.width, H = A.height;
        const size_t       N = size_t(W) * size_t(H);
        std::vector<float> dist(N, INFINITY);
        iceColor.resize(N);
        std::vector<uint8_t> rim(N), done(N);
        std::vector<size_t>  open;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                if (!isActorCell(x, y)) continue;
                const size_t i  = size_t(y) * size_t(W) + size_t(x);
                rim[i]          = uint8_t(!isActorCell(x - 1, y) || !isActorCell(x + 1, y) || !isActorCell(x, y - 1));
                const bool up   = !isActorCell(x - 1, y) || !isActorCell(x, y - 1);
                const auto cold = frozenColor(A.pixels[i]);
                const auto idx  = size_t(std::find(C.begin(), C.end(), cold) - C.begin());
                iceColor[i]     = mix(up ? C[std::min(size_t(7), idx + 1)] : cold, C[6], .2);
                if ((y == H - 1 || !isActorCell(x, y + 1)) && y >= H - 4) {
                    dist[i] = float(R.range(0, 1.5));
                    open.push_back(i);
                }
            }
        constexpr std::array<std::array<int, 2>, 4> neighbours{{{{1, 0}}, {{-1, 0}}, {{0, -1}}, {{0, 1}}}};
        while (!open.empty()) {
            size_t bi = 0;
            for (size_t j = 1; j < open.size(); ++j)
                if (dist[open[j]] < dist[open[bi]]) bi = j;
            const size_t i = open[bi];
            open[bi]       = open.back();
            open.pop_back();
            if (done[i]) continue;
            done[i]     = 1;
            const int x = int(i % size_t(W)), y = int(i / size_t(W));
            for (const auto& d : neighbours) {
                const int nx = x + d[0], ny = y + d[1];
                if (!isActorCell(nx, ny)) continue;
                const size_t j         = size_t(ny) * size_t(W) + size_t(nx);
                const double w         = (rim[j] ? .55 : 1) * (d[1] < 0 ? 1 : 1.3) + R.range(0, 1.5),
                             candidate = double(dist[i]) + w;
                if (candidate < dist[j]) {
                    dist[j] = float(candidate);
                    open.push_back(j);
                }
            }
        }
        double maxD = 0;
        for (float d : dist)
            if (d < INFINITY) maxD = std::max(maxD, double(d));
        reach.resize(N);
        for (size_t i = 0; i < N; ++i)
            if (dist[i] < INFINITY) {
                reach[i] = float(.14 + (.62 - .14) * std::pow(dist[i] / maxD, .85));
                order.push_back(i);
            }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return reach[a] < reach[b]; });
        for (size_t i : order) {
            const int x = int(i % size_t(W)), y = int(i / size_t(W));
            if (!rim[i] || rand({uint32_t(i), seed, 3}) > .42 || y > H - 3) continue;
            const int dx = !isActorCell(x - 1, y)        ? -1
                           : !isActorCell(x + 1, y)      ? 1
                           : rand({uint32_t(i), 9}) < .5 ? -1
                                                         : 1,
                      dy = !isActorCell(x, y - 1)        ? -1
                           : rand({uint32_t(i), 4}) < .5 ? -1
                                                         : 0;
            feathers.push_back({A.x + x + dx, A.y + y + dy, dx, dy, 1 + int(std::floor(rand({uint32_t(i), 7}) * 2.6)),
                                double(reach[i]) + .04});
        }
    }
    void prepareBlock() {
        for (size_t i = 0; i < facets.size(); ++i) {
            auto&       f = facets[i];
            const auto& s = SPECS[i];
            for (int j = 0; j < s.n; ++j) f.pts.push_back(V[size_t(s.ids[size_t(j)])]);
            f.lit   = s.lit;
            f.at    = SEAL + s.delay;
            f.c     = ice::centroid(f.pts);
            f.tint  = C[size_t(3 + jsRound(f.lit * 3))];
            f.alpha = .2 + .24 * f.lit;
            f.d     = clamp((f.c.x + .55 * f.c.y + 36) / 46);
            f.len   = 0;
            for (size_t j = 0; j < f.pts.size(); ++j) {
                const auto a = f.pts[j], b = f.pts[(j + 1) % f.pts.size()];
                f.len += jsHypot2(b.x - a.x, b.y - a.y);
            }
        }
        fid.fill(-1);
        for (int y = Y0; y < Y1; ++y)
            for (int x = X0; x < X1; ++x) {
                if (!ice::isInsidePoly(OUTLINE, x + .5, y + .5)) continue;
                int found = -1;
                for (size_t i = 0; i < facets.size(); ++i)
                    if (ice::isInsidePoly(facets[i].pts, x + .5, y + .5)) {
                        found = int(i);
                        break;
                    }
                if (found < 0) {
                    found = 0;
                    for (size_t i = 0; i < facets.size(); ++i)
                        if (jsHypot2(facets[i].c.x - x, facets[i].c.y - y) <
                            jsHypot2(facets[size_t(found)].c.x - x, facets[size_t(found)].c.y - y))
                            found = int(i);
                }
                fid[size_t(y - Y0) * BW + size_t(x - X0)] = int8_t(found);
            }
        for (size_t i = 0; i < facets.size(); ++i) {
            const auto& s = SPECS[i];
            for (int j = 0; j < s.n; ++j) {
                const int a = s.ids[size_t(j)], b = s.ids[size_t((j + 1) % s.n)];
                auto e = std::find_if(edges.begin(), edges.end(),
                                      [&](const Edge& q) { return (q.a == a && q.b == b) || (q.a == b && q.b == a); });
                if (e == edges.end()) {
                    edges.push_back({a, b, {}});
                    e = edges.end() - 1;
                }
                e->faces.push_back(i);
            }
        }
        for (auto& e : edges) {
            const auto a = V[size_t(e.a)], b = V[size_t(e.b)];
            const auto c  = facets[e.faces[0]].c;
            double     nx = b.y - a.y, ny = a.x - b.x;
            if (nx * ((a.x + b.x) / 2 - c.x) + ny * ((a.y + b.y) / 2 - c.y) < 0) {
                nx = -nx;
                ny = -ny;
            }
            e.outer = e.faces.size() == 1;
            e.lit   = (-nx * .55 - ny * .84) / jsHypot2(nx, ny);
            e.floor = a.y == 0 && b.y == 0;
            e.at    = INFINITY;
            for (size_t i : e.faces) e.at = std::min(e.at, facets[i].at);
        }
    }
    int at(int x, int y) const {
        return x < X0 || x >= X1 || y < Y0 || y >= Y1 ? -1 : fid[size_t(y - Y0) * BW + size_t(x - X0)];
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void chip(const Chip& v) {
        Debris q;
        q.kind            = v.kind;
        q.x               = v.x;
        q.y               = v.y;
        q.vx              = v.vx;
        q.vy              = v.vy;
        q.gravity         = v.g;
        q.drag            = v.drag;
        q.bounce          = v.bounce;
        q.life            = v.life;
        q.angle           = v.a;
        q.angularVelocity = v.va;
        q.size            = v.s;
        q.color           = v.c;
        bits.add(std::move(q));
    }
    /** @brief Build owning source pane cuts. @cost At most three binary cut levels per facet at burst. */
    std::vector<std::vector<Point>> chop(std::span<const Point> pts, int depth) {
        const auto c = ice::centroid(pts);
        if (c.area < 20 || depth > 2) return {std::vector<Point>(pts.begin(), pts.end())};
        auto                            parts = ice::splitPoly(pts, c.x, c.y, R.range(0, PI));
        std::vector<std::vector<Point>> out;
        for (const auto& p : parts) {
            auto sub = chop(p, depth + 1);
            for (auto& q : sub) out.push_back(std::move(q));
        }
        return out;
    }
    void explode() {
        burst = true;
        lights.add(CORE.x, CORE.y, 64, WHITE, .85, .14);
        lights.add(CORE.x, CORE.y - 4, 72, C[4], .55, .6);
        for (const auto& F : facets)
            for (const auto& part : chop(F.pts, 0)) {
                const auto   m  = ice::centroid(part);
                const double dx = m.x - CORE.x, dy = m.y - CORE.y, h = jsHypot2(dx, dy), d = h == 0 ? 1 : h,
                             sp = R.range(40, 85) * (1.2 - std::min(.5, m.area / 120));
                ice::Pane    q;
                for (const auto& p : part) q.pts.push_back({p.x - m.x, p.y - m.y});
                q.x       = m.x;
                q.y       = m.y;
                q.vx      = (dx / d) * sp + R.range(-15, 15);
                q.vy      = (dy / d) * sp * .6 - R.range(90, 190);
                q.a       = 0;
                q.va      = R.range(-9, 9);
                q.f       = R.range(-.8, .8);
                q.vf      = R.signedRange(5, 13);
                q.lit     = F.lit;
                q.g       = .85;
                q.drag    = .7;
                q.brittle = 1;
                q.life    = R.range(1, 1.45);
                panes.add(std::move(q));
            }
        for (int i = 0; i < 50; ++i) {
            int x = 0, y = 0;
            for (int k = 0; k < 12 && at(x, y) < 0; ++k) {
                x = R.integer(X0, X1 - 1);
                y = R.integer(Y0, Y1 - 1);
            }
            const double dx = x - CORE.x, dy = y - CORE.y, h = jsHypot2(dx, dy), d = h == 0 ? 1 : h,
                         sp = R.range(50, 130);
            chip({DebrisKind::Shard, double(x), double(y), (dx / d) * sp, (dy / d) * sp * .7 - R.range(60, 170), .85,
                  .9, .3, R.range(.8, 1.4), R.range(0, TAU), R.range(-22, 22), R.range(1.5, 3.2),
                  C[size_t(R.integer(4, 7))]});
        }
        for (const auto& f : feathers)
            if (R.isChance(.5))
                chip({DebrisKind::Shard, f.x, f.y, f.dx * R.range(20, 70), -R.range(20, 90), .8, .8, .3,
                      R.range(.4, .8), R.range(0, TAU), R.range(-20, 20), R.range(1, 1.8), WHITE});
        for (int i = 0; i < 12; ++i) {
            const int s = R.sign();
            emit({ParticleKind::Vapour, s * R.range(2, 16), -R.range(1, 6), s * R.range(30, 90), -R.range(4, 16),
                  R.range(.6, 1.1), C[6], R.range(2.2, 4)});
        }
        for (int i = 0; i < 8; ++i)
            emit({ParticleKind::Vapour, R.range(-14, 14), -R.range(8, 40), R.range(-30, 30), -R.range(10, 30),
                  R.range(.6, 1), C[6], R.range(3, 5)});
        for (int i = 0; i < 18; ++i) {
            const double a = R.range(-PI, 0), sp = R.range(40, 130);
            emit({ParticleKind::Flake, R.range(-10, 10), CORE.y + R.range(-12, 12), std::cos(a) * sp, std::sin(a) * sp,
                  R.range(1, 1.5), R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.6)});
        }
        for (int i = 0; i < 7; ++i)
            emit({ParticleKind::Star, R.range(-26, 26), -R.range(6, 52), R.range(-12, 12), -R.range(4, 16),
                  R.range(.4, .8), R.isChance(.5) ? WHITE : C[6], R.range(1, 2.4)});
    }

    void update(double dt) override {
        t += dt;
        if (t < SNAP)
            for (int s : {-1, 1}) {
                const double x = s * REACH * (1 - t / SNAP);
                if (R.isChance(.12))
                    emit({ParticleKind::Vapour, x, -R.range(0, 2), -s * R.range(40, 90), -R.range(0, 5),
                          R.range(.3, .5), C[6], R.range(1.4, 2.4)});
                if (R.isChance(.45))
                    emit({ParticleKind::Streak, x + s * R.range(0, 6), -R.range(1, 4), -s * R.range(50, 90), 0,
                          R.range(.1, .16), R.isChance(.5) ? C[6] : C[5]});
            }
        if (t > .14 && t < .62 + .03) {
            size_t lo = 0;
            while (lo < order.size() && reach[order[lo]] < t - .06) ++lo;
            size_t hi = lo;
            while (hi < order.size() && reach[order[hi]] <= t) ++hi;
            if (hi > lo && R.isChance(.45)) {
                const size_t i = order[size_t(R.integer(int(lo), int(hi) - 1))];
                emit({ParticleKind::Star, A.x + double(i % size_t(A.width)) + .5, A.y + double(i / size_t(A.width)),
                      R.range(-8, 8), -R.range(0, 18), R.range(.2, .42), R.isChance(.5) ? C[6] : WHITE,
                      R.range(.6, 1.9)});
            }
            if (hi > lo && R.isChance(.12)) {
                const size_t i = order[size_t(R.integer(int(lo), int(hi) - 1))];
                emit({ParticleKind::Flake, A.x + double(i % size_t(A.width)), A.y + double(i / size_t(A.width)),
                      R.range(-20, 20), -R.range(10, 30), R.range(.5, .9), C[6], R.range(.5, 1.2)});
            }
        }
        while (formed < facets.size() && t >= facets[formed].at) {
            const auto& F = facets[formed++];
            const auto  c = F.c;
            for (int i = 0; i < 3; ++i) {
                const double a = std::atan2(c.y - CORE.y, c.x - CORE.x) + R.range(-.7, .7), sp = R.range(50, 120);
                chip({DebrisKind::Shard, c.x, c.y, std::cos(a) * sp, std::sin(a) * sp - 40, .8, .8, .3,
                      R.range(.35, .7), R.range(0, TAU), R.range(-18, 18), R.range(1, 2.2),
                      C[size_t(R.integer(5, 7))]});
            }
            if (c.y > -14)
                emit({ParticleKind::Vapour, c.x, -1, (c.x < 0 ? -1 : 1) * R.range(10, 30), -R.range(2, 8),
                      R.range(.5, .8), C[6], R.range(2.2, 3.4)});
            emit({ParticleKind::Star, c.x + R.range(-2, 2), c.y + R.range(-2, 2), 0, -4, R.range(.25, .45), WHITE,
                  R.range(1.2, 2.2)});
            lights.add(c.x, c.y, 16, C[6], .4, .12);
        }
        if (!sealed && formed == facets.size()) {
            sealed = true;
            lights.add(0, -26, 70, C[5], .6, .4);
            for (int i = 0; i < 5; ++i)
                emit({ParticleKind::Star, R.range(-15, 15), -R.range(6, 44), R.range(-6, 6), -R.range(0, 8),
                      R.range(.3, .6), R.isChance(.5) ? WHITE : C[6], R.range(1, 2.2)});
        }
        if (sealed && t < CRACK2) {
            if (R.isChance(.035)) {
                const auto p = V[size_t(R.integer(0, int(V.size()) - 1))];
                emit({ParticleKind::Star, p.x + R.range(-1, 1), p.y + R.range(-1, 1), 0, -2, R.range(.3, .6),
                      R.isChance(.6) ? C[6] : WHITE, R.range(.8, 2.2)});
            }
            if (R.isChance(.025)) {
                const int s = R.sign();
                emit({ParticleKind::Vapour, s * R.range(12, 18), -R.range(1, 3), s * R.range(8, 24), -R.range(1, 5),
                      R.range(.7, 1.1), C[6], R.range(1.8, 3)});
            }
            if (R.isChance(.012))
                emit({ParticleKind::Flake, R.range(-30, 30), -R.range(56, 76), R.range(-4, 4), R.range(4, 10),
                      R.range(1.2, 1.8), R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.4)});
        }
        if (struck == 0 && t >= CRACK) {
            struck = 1;
            lights.add(CORE.x, CORE.y, 26, WHITE, .4, .1);
            for (int i = 0; i < 4; ++i)
                chip({DebrisKind::Shard, R.range(-14, 14), -R.range(10, 40), R.range(-40, 40), -R.range(10, 60), .9, .6,
                      .3, R.range(.5, .9), R.range(0, TAU), R.range(-20, 20), R.range(1, 2),
                      C[size_t(R.integer(5, 7))]});
        }
        if (struck == 1 && t >= CRACK2) {
            struck = 2;
            lights.add(CORE.x, CORE.y, 40, C[6], .35, .16);
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Star, R.range(-12, 12), -R.range(8, 40), 0, -3, R.range(.15, .3), WHITE,
                      R.range(1, 2)});
        }
        if (!white && t >= FLASH) {
            white = true;
            lights.add(CORE.x, CORE.y, 60, WHITE, .9, .1);
        }
        if (!burst && t >= BURST) explode();
        if (burst && t < BURST + .9 && R.isChance(.07)) {
            // Source explicitly picks a one-element point list when debris is empty,
            // consuming the same selection RNG value as an ordinary pick.
            Point        p{0, -10};
            const size_t count = bits.list.empty() ? 1 : bits.list.size();
            const size_t i     = size_t(R.integer(0, int(count) - 1));
            if (!bits.list.empty()) p = {bits.list[i].x, bits.list[i].y};
            emit({ParticleKind::Star, p.x, p.y - 1, 0, -3, R.range(.2, .4), R.isChance(.5) ? WHITE : C[6],
                  R.range(.6, 1.6)});
        }
        if (burst && t < BURST + THAW && R.isChance(.12)) {
            Debris q;
            q.kind    = DebrisKind::Drip;
            q.x       = R.range(-9, 9);
            q.y       = -A.height + ease::inQuad(seg(t, BURST, BURST + THAW)) * A.height;
            q.vy      = 10;
            q.gravity = .5;
            q.life    = .8;
            q.color   = C[5];
            bits.add(std::move(q));
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        bits.update(dt, [](double) { return 0.; });
        panes.update(
            dt, [](double) { return 0.; },
            [&](const ice::Pane& q, double x, double y, double) {
                for (int i = 0; i < 3; ++i)
                    chip({DebrisKind::Shard, x + R.range(-2, 2), y - 1, q.vx * .6 + R.range(-50, 50), -R.range(30, 100),
                          .8, .6, .35, R.range(.4, .8), R.range(0, TAU), R.range(-18, 18), R.range(1.2, 2.4),
                          C[size_t(R.integer(5, 7))]});
                emit({ParticleKind::Vapour, x, y - 1, R.range(-25, 25), -R.range(2, 10), R.range(.4, .7), C[6],
                      R.range(1.4, 2.4)});
                if (R.isChance(.4))
                    emit({ParticleKind::Star, x, y - 2, 0, -6, R.range(.2, .4), WHITE, R.range(.8, 1.6)});
            },
            [](const ice::Pane&) {}, [](const ice::Pane&, double, double) {});
        lights.update(dt);
        const double left = std::max(.001, D - .05 - t);
        for (auto& q : ps.list)
            if (q.life > left) q.life = left;
        for (auto& q : bits.list)
            if (q.life > left) q.life = left;
        for (auto& q : panes.list)
            if (q.life > left) q.life = left;
    }
    double jolt() const {
        return t >= CRACK && t < CRACK + .06 ? 1
               : t >= CRACK2 && t < FLASH    ? (int(std::floor(t * 40)) % 2 ? 1 : -1)
                                             : 0;
    }
    void drawCharacter(Paint& p) const {
        const double thaw = t >= BURST ? ease::inQuad(seg(t, BURST, BURST + THAW)) * (A.height + 2) - 2 : -1;
        for (size_t i : order) {
            const double age = t - reach[i];
            if (age < 0) break;
            const int x = int(i % size_t(A.width)), y = int(i / size_t(A.width));
            if (y < thaw) continue;
            p.px(A.x + x, A.y + y,
                 y < thaw + 1.2 && t >= BURST ? C[7]
                 : age < .016                 ? WHITE
                 : age < .045                 ? C[6]
                                              : iceColor[i]);
            if (age < .03) p.light(A.x + x, A.y + y, C[6], .3);
        }
        if (t >= BURST) return;
        for (const auto& f : feathers) {
            const double g = clamp((t - f.at) / .08);
            if (!std::isfinite(g) || g <= 0) continue;
            const int L = int(jsRound(f.L * g));
            for (int j = 0; j < L; ++j) p.px(f.x + f.dx * j, f.y + f.dy * j, j == L - 1 ? WHITE : C[6]);
        }
    }
    void drawBlock(Paint& p) const {
        if (t >= BURST || t < SEAL - .08) return;
        const double           jx = jolt();
        const bool             wh = t >= FLASH;
        std::array<double, 13> g{};
        for (size_t i = 0; i < facets.size(); ++i) {
            const auto& f = facets[i];
            for (double s : {1.12, 1.82}) g[i] = std::max(g[i], std::sin(PI * clamp((t - s - f.d * .32) / .13)));
            g[i] *= .35 + .65 * f.lit;
        }
        for (int y = Y0; y < Y1; ++y)
            for (int x = X0; x < X1; ++x) {
                const int f = at(x, y);
                if (f < 0) continue;
                const auto&  F   = facets[size_t(f)];
                const double age = t - F.at;
                if (age < 0) continue;
                if (wh) {
                    p.px(x + jx, y, WHITE);
                    continue;
                }
                if (age < .03) {
                    p.blend(x + jx, y, C[7], .55);
                    continue;
                }
                const double fresh = 1 - clamp((age - .03) / .1), G = g[size_t(f)];
                p.blend(x + jx, y, fresh > .5 || G > .5 ? C[6] : F.tint, F.alpha + .25 * fresh + .15 * G);
                if (G > .05) p.light(x + jx, y, C[5], .42 * G);
            }
        for (const auto& e : edges) {
            if (e.floor || t < e.at) continue;
            const auto a = V[size_t(e.a)], b = V[size_t(e.b)];
            const bool fresh = std::any_of(e.faces.begin(), e.faces.end(),
                                           [&](size_t i) { return t - facets[i].at < .1 || g[i] > .6; });
            if (e.outer && e.lit < 0 && !wh) p.line(a.x + jx + 1, a.y, b.x + jx + 1, b.y, C[1]);
            if (e.outer) p.glowLine(a.x + jx, a.y, b.x + jx, b.y, 5, C[4], .1);
            p.line(a.x + jx, a.y, b.x + jx, b.y,
                   wh                      ? C[6]
                   : fresh                 ? WHITE
                   : e.outer               ? (e.lit > .3    ? WHITE
                                              : e.lit > -.2 ? C[6]
                                                            : C[4])
                   : (a.y + b.y) / 2 < -30 ? C[6]
                                           : C[5]);
        }
        for (const auto& F : facets) {
            const double age = t - F.at;
            if (age < -.07 || age >= 0) continue;
            double left = ((age + .07) / .07) * F.len;
            for (size_t k = 0; k < F.pts.size() && left > 0; ++k) {
                const auto   a = F.pts[k], b = F.pts[(k + 1) % F.pts.size()];
                const double L = jsHypot2(b.x - a.x, b.y - a.y), v = std::min(1., left / L);
                left -= L;
                p.line(a.x + jx, a.y, a.x + jx + (b.x - a.x) * v, a.y + (b.y - a.y) * v, WHITE);
                if (v < 1) p.glow(a.x + jx + (b.x - a.x) * v, a.y + (b.y - a.y) * v, 6, C[6], .6);
            }
        }
        ice::FracturePaint o;
        o.x = jx;
        if (t >= CRACK && !wh) ice::drawFractures(p, cracks, (t - CRACK) * 150, C, o);
        if (t >= CRACK2 && !wh) ice::drawFractures(p, cracks2, (t - CRACK2) * 190, C, o);
    }
    void drawBack(Paint& p) const override {
        const double out = 1 - smooth(2.95, 3.7, t);
        if (out > 0)
            for (int x = -int(REACH); x <= REACH; ++x) {
                const double d = std::abs(x + .5) / REACH, arrive = SNAP * (1 - d), age = t - arrive;
                if (age < 0) continue;
                const double a = out * clamp((1.12 - d) / .2), g = smooth(0, .4, age), h = rand({jsUint(x), 31});
                const int    s = x < 0 ? -1 : 1;
                if (a <= 0) continue;
                p.dot(x, 0, age < .05 || h < .3 ? WHITE : C[6], a);
                if (h < .55) {
                    const int L = int(jsRound(g * (1 + rand({jsUint(x), 7}) * 3.4) * (1 - d * .55)));
                    for (int j = 1; j <= L; ++j) p.dot(x, -j, j == L ? WHITE : C[5], a);
                    if (L >= 2) p.dot(x + s, -L + 1, C[6], a);
                    if (L >= 4) p.dot(x - s, -L + 2, C[5], a);
                }
                const double depth = g * (2 + rand({jsUint(x), 11}) * 4) * (1 - d * .5);
                for (int j = 1; j <= depth; ++j)
                    p.dot(x, j,
                          rand({jsUint(x * 13 + j), 5}) < .07 ? WHITE
                          : j < 2                             ? C[5]
                          : j < 4                             ? C[4]
                                                              : C[3],
                          a * (1.05 - j / (depth + 1)));
            }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        drawCharacter(p);
        drawBlock(p);
        const double hold = smooth(.14, SEAL, t) * (1 - smooth(BURST, BURST + .6, t)), crk = smooth(CRACK, FLASH, t);
        if (hold > 0) {
            p.glow(0, -2, 34, C[4], .22 * hold * (.85 + .15 * std::sin(t * 3)));
            p.glow(CORE.x, CORE.y - 4, 40, C[3], (.22 + .1 * crk) * hold);
        }
        if (t > .14 && t < .62) p.glow(0, -38 * ease::outQuad(seg(t, .14, .62)), 18, C[5], .28);
        const double g = t - facets.back().at;
        if (g > 0 && g < .3)
            p.sparkle(V[5].x, V[5].y, 8 * ease::outBack(std::min(1., g / .06)) * (1 - g / .3), C[6], C[4], .6);
        if (t >= FLASH && t < BURST + .16) {
            const double a = clamp((t - FLASH) / .22);
            flare(p, CORE.x, CORE.y, 34 * std::pow(1 - a, 1.4), 1 - a, C);
        }
        panes.draw(p, C);
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& freezeDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Freeze, "ice/freeze", 176, 128, 88, 108, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                              hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Freeze>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
