#include "physics/World.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"

#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>

namespace eve::physics {
namespace {

using eve::graphics::Color;

/**
 * @brief Presentation-only Box2D debug draw sink.
 *
 * It is created for one draw call and never becomes part of World state.  The
 * solver therefore remains usable by server/headless builds without graphics.
 */
class DebugDraw final : public b2Draw {
public:
    DebugDraw() { SetFlags(e_shapeBit | e_jointBit | e_aabbBit); }

    void begin(graphics::Graphics *gfx, float meter) {
        gfx_   = gfx;
        meter_ = meter;
    }

    void end() { gfx_ = nullptr; }

    void DrawPolygon(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color) override {
        drawPoly(vertices, vertexCount, color, false);
    }

    void DrawSolidPolygon(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color) override {
        drawPoly(vertices, vertexCount, color, true);
    }

    void DrawCircle(const b2Vec2 &center, float32 radius, const b2Color &color) override {
        drawCircle(center, radius, color, false);
    }

    void DrawSolidCircle(const b2Vec2 &center, float32 radius, const b2Vec2 & /*axis*/, const b2Color &color) override {
        drawCircle(center, radius, color, true);
    }

    void DrawSegment(const b2Vec2 &p1, const b2Vec2 &p2, const b2Color &color) override {
        if (!gfx_) return;
        float x1   = p1.x * meter_;
        float y1   = p1.y * meter_;
        float x2   = p2.x * meter_;
        float y2   = p2.y * meter_;
        float minx = std::min(x1, x2);
        float miny = std::min(y1, y2);
        float w    = std::max(1.f, std::fabs(x2 - x1));
        float h    = std::max(1.f, std::fabs(y2 - y1));
        gfx_->drawSolidRect(minx, miny, w, h, Color(color.r, color.g, color.b, color.a * 0.8f));
    }

    void DrawTransform(const b2Transform &xf) override {
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetXAxis(), b2Color(1, 0, 0));
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetYAxis(), b2Color(0, 1, 0));
    }

private:
    void drawPoly(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color, bool solid) {
        if (!gfx_ || vertexCount < 2) return;
        for (int32 i = 0; i < vertexCount; ++i) {
            const b2Vec2 &a = vertices[i];
            const b2Vec2 &b = vertices[(i + 1) % vertexCount];
            DrawSegment(a, b, color);
        }
        if (solid && vertexCount >= 3) {
            float minx = vertices[0].x;
            float maxx = vertices[0].x;
            float miny = vertices[0].y;
            float maxy = vertices[0].y;
            for (int32 i = 1; i < vertexCount; ++i) {
                minx = std::min(minx, vertices[i].x);
                maxx = std::max(maxx, vertices[i].x);
                miny = std::min(miny, vertices[i].y);
                maxy = std::max(maxy, vertices[i].y);
            }
            gfx_->drawSolidRect(minx * meter_, miny * meter_, (maxx - minx) * meter_, (maxy - miny) * meter_,
                                Color(color.r, color.g, color.b, color.a * 0.25f));
        }
    }

    void drawCircle(const b2Vec2 &center, float32 radius, const b2Color &color, bool solid) {
        if (!gfx_) return;
        float px = (center.x - radius) * meter_;
        float py = (center.y - radius) * meter_;
        float d  = radius * 2.f * meter_;
        float a  = solid ? color.a * 0.35f : color.a * 0.7f;
        gfx_->drawSolidRect(px, py, d, d, Color(color.r, color.g, color.b, a));
    }

    graphics::Graphics *gfx_   = nullptr;
    float               meter_ = 30.f;
};

class DebugDrawBinding final {
public:
    DebugDrawBinding(b2World *world, b2Draw *draw) : world_(world) { world_->SetDebugDraw(draw); }
    ~DebugDrawBinding() { world_->SetDebugDraw(nullptr); }

    DebugDrawBinding(const DebugDrawBinding &)            = delete;
    DebugDrawBinding &operator=(const DebugDrawBinding &) = delete;

private:
    b2World *world_;
};

}  // namespace

void World::drawDebug(graphics::Graphics *gfx) {
    b2World *world = raw();
    if (!world || !isValid() || !gfx) return;

    DebugDraw draw;
    draw.begin(gfx, getMeter());
    DebugDrawBinding binding(world, &draw);
    world->DrawDebugData();
    draw.end();
}

}  // namespace eve::physics
