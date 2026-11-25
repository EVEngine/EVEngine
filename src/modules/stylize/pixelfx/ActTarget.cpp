// eve-hot-path
// Native adaptation of procedural Target drawing in core/act.js, MIT (c) 2026 Patch.
#include "stylize/pixelfx/ActTarget.h"
#include "stylize/pixelfx/ActRuntime.h"
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx {
namespace {
const uint32_t ink = hex(0x0b0a1a), wood = hex(0x724c2e), woodL = hex(0xa0703a), woodD = hex(0x4e331f),
               straw = hex(0xd2aa55), strawL = hex(0xf0d98a), burlapD = hex(0x86643a), burlap = hex(0xb38b57),
               burlapL = hex(0xc9a26c), targetRed = hex(0x9a2f3c), redL = hex(0xc4434f);
struct Mark {
    int      x, y;
    uint32_t color;
};
// Source direct typed-array writes accept only integral flattened indices, even with fractional placement.
void pixel(PixelFxCanvas canvas, double x, double y, uint32_t c) {
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return;
    const double index = y * canvas.width + x;
    if (index != std::floor(index) || index < 0 || index >= double(canvas.color.size())) return;
    canvas.color[size_t(index)] = c;
}
}  // namespace
const TargetArt& targetArt() {
    static const TargetArt art = [] {
        TargetArt out;
        auto      px = [&](int x, int y, uint32_t c) {
            if (x >= 0 && y >= 0 && x < 38 && y < 52) out.pixels[size_t(y * 38 + x)] = c;
        };
        auto R = [&](int x, int y, int w, int h, uint32_t c) {
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) px(x + i, y + j, c);
        };
        R(15, 38, 8, 14, ink);
        R(16, 34, 6, 18, wood);
        R(16, 34, 2, 18, woodL);
        R(20, 34, 2, 18, woodD);
        R(1, 18, 36, 1, ink);
        R(1, 23, 36, 1, ink);
        R(1, 19, 36, 4, wood);
        R(1, 19, 36, 1, woodL);
        R(1, 22, 36, 1, woodD);
        for (int bx : {0, 33}) {
            R(bx, 17, 5, 8, straw);
            for (const auto& m : std::array<Mark, 10>{{{2, 16, straw},
                                                       {3, 15, strawL},
                                                       {1, 17, strawL},
                                                       {2, 25, straw},
                                                       {1, 26, strawL},
                                                       {3, 25, burlapD},
                                                       {2, 19, strawL},
                                                       {3, 21, strawL},
                                                       {1, 22, burlapD},
                                                       {2, 23, strawL}}})
                px(bx + m.x, m.y, m.color);
            px(bx == 0 ? bx - 1 : bx + 5, 19, straw);
            px(bx == 0 ? bx - 1 : bx + 5, 22, strawL);
        }
        auto sack = [&](int x, int y, int w, int h) {
            R(x + 1, y, w - 2, 1, ink);
            R(x + 1, y + h - 1, w - 2, 1, ink);
            R(x, y + 1, 1, h - 2, ink);
            R(x + w - 1, y + 1, 1, h - 2, ink);
            R(x + 1, y + 1, w - 2, h - 2, burlap);
            R(x + w - 4, y + 1, 3, h - 2, burlapD);
            R(x + 1, y + h - 3, w - 2, 2, burlapD);
            R(x + 1, y + 1, 1, h - 4, burlapL);
            R(x + 1, y + 1, w - 6, 1, burlapL);
            for (int j = y + 2; j < y + h - 2; j += 3)
                for (int i = x + 3 + (j % 2); i < x + w - 4; i += 4) px(i, j, burlapD);
        };
        sack(9, 17, 20, 24);
        for (int y = 19; y < 36; ++y)
            for (int x = 11; x < 27; ++x) {
                const double d = jsHypot2(x - 18.5, y - 27);
                if ((d > 4 && d < 5.4) || d < 1.9) px(x, y, x + y < 45 ? redL : targetRed);
            }
        for (int x = 10; x < 28; ++x) {
            px(x, 36, (x & 1) ? woodL : straw);
            px(x, 37, (x & 1) ? woodD : wood);
        }
        for (const auto& m : std::array<Mark, 9>{{{11, 41, straw},
                                                  {12, 42, strawL},
                                                  {14, 41, strawL},
                                                  {15, 42, straw},
                                                  {23, 41, straw},
                                                  {24, 42, strawL},
                                                  {26, 41, straw},
                                                  {13, 43, straw},
                                                  {25, 43, straw}}})
            px(m.x, m.y, m.color);
        sack(10, 1, 18, 16);
        for (int ex : {14, 23})
            for (int d = -2; d <= 2; ++d) {
                px(ex + d, 7 + d, ink);
                px(ex - d, 7 + d, ink);
            }
        for (int x = 14; x < 24; ++x) px(x, 12, (x & 1) ? ink : burlapD);
        for (int x : {15, 18, 21}) {
            px(x, 11, ink);
            px(x, 13, ink);
        }
        for (int x = 12; x < 26; ++x) {
            px(x, 16, (x & 1) ? woodL : straw);
            px(x, 17, (x & 1) ? wood : woodD);
        }
        for (const auto& m : std::array<Mark, 9>{{{13, 0, straw},
                                                  {15, 0, strawL},
                                                  {18, 0, straw},
                                                  {21, 0, strawL},
                                                  {24, 0, straw},
                                                  {9, 4, straw},
                                                  {28, 6, straw},
                                                  {8, 7, strawL},
                                                  {29, 3, strawL}}})
            px(m.x, m.y, m.color);
        return out;
    }();
    return art;
}
void drawActTargets(const TargetArt& art, std::span<const ActTargetState> targets, PixelFxCanvas canvas,
                    PixelFxPlacement placement, bool particles) {
    const std::array<uint32_t, 9>   tint{0,
                                         WHITE,
                                         hex(0x1a0c0e),
                                         hex(0xa8e8ff),
                                         hex(0x3a1a70),
                                         hex(0xfff4c0),
                                         hex(0xff2040),
                                         hex(0x8fd6ff),
                                         hex(0x58c43a)};
    constexpr std::array<double, 9> strength{0, 1, .72, .72, .5, .5, .45, .6, .5};
    const std::array<uint32_t, 5>   strawColors{strawL, straw, straw, burlapD, burlapD};
    const double                    X = placement.x + canvas.originX, Y = placement.y + canvas.originY;
    for (const auto& target : targets) {
        if (particles) {
            for (const auto& p : target.straw) {
                const auto   c  = strawColors[size_t(std::min(4.0, std::floor((p.age / p.life) * 5)))];
                const double lx = jsRound(p.x), ly = jsRound(p.y) - 1;
                for (int i = 0; i < (p.big ? 2 : 1); ++i)
                    pixel(canvas, X + (placement.flip ? -(lx + i) - 1 : lx + i), Y + ly, c);
            }
        } else {
            const double tan  = std::tan(target.theta);
            const int    sv   = target.shiver > 0 ? ((uint64_t(std::floor(target.clock * 30)) & 1) ? 1 : -1) : 0;
            const size_t skin = size_t(target.flashTime > 0 ? ActTargetSkin::Flash : target.skin);
            const double base = X + (placement.flip ? -target.x - 1 : target.x);
            for (int r = 0; r < 52; ++r) {
                const double offset = jsRound((51 - r) * tan * .8) + sv;
                for (int x = 0; x < 38; ++x) {
                    const auto c = art.pixels[size_t(r * 38 + x)];
                    if (c)
                        pixel(canvas, base + (placement.flip ? -(x - 19 + offset) - 1 : x - 19 + offset), Y - 52 + r,
                              skin ? mix(c, tint[skin], strength[skin]) : c);
                }
            }
        }
    }
}
}  // namespace eve::stylize::pixelfx
