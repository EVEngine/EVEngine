#include "physics/World.h"

#include "graphics/Graphics.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitivePath.h"

#include <Box2D/Box2D.h>

#include <vector>

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

    void begin(graphics::PrimitiveCanvas2D *canvas, float meter) {
        canvas_ = canvas;
        meter_ = meter;
    }

    void end() { canvas_ = nullptr; }

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
        if (!canvas_) return;
        canvas_->drawLine({p1.x * meter_, p1.y * meter_}, {p2.x * meter_, p2.y * meter_}, strokePaint(color));
    }

    void DrawTransform(const b2Transform &xf) override {
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetXAxis(), b2Color(1, 0, 0));
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetYAxis(), b2Color(0, 1, 0));
    }

private:
    static graphics::PrimitivePaint strokePaint(const b2Color &color) {
        graphics::PrimitivePaint paint;
        paint.color        = Color(color.r, color.g, color.b, color.a * 0.8f);
        paint.mode         = graphics::PaintMode::Stroke;
        paint.stroke.width = 1.5f;
        paint.stroke.cap   = graphics::LineCap::Round;
        paint.stroke.join  = graphics::LineJoin::Round;
        return paint;
    }

    void drawPoly(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color, bool solid) {
        if (!canvas_ || vertexCount < 2) return;
        std::vector<glm::vec2> points;
        points.reserve(static_cast<std::size_t>(vertexCount));
        for (int32 i = 0; i < vertexCount; ++i) points.emplace_back(vertices[i].x * meter_, vertices[i].y * meter_);
        graphics::PrimitivePaint paint = strokePaint(color);
        if (solid && vertexCount >= 3) {
            paint.mode    = graphics::PaintMode::FillAndStroke;
            paint.color.a = color.a * 0.35f;
            graphics::Path2D path;
            path.moveTo(points.front());
            for (std::size_t i = 1; i < points.size(); ++i) path.lineTo(points[i]);
            path.close();
            canvas_->drawPath(path, paint);
        } else {
            canvas_->drawPolyline(points, true, paint);
        }
    }

    void drawCircle(const b2Vec2 &center, float32 radius, const b2Color &color, bool solid) {
        if (!canvas_) return;
        graphics::PrimitivePaint paint = strokePaint(color);
        if (solid) {
            paint.mode    = graphics::PaintMode::FillAndStroke;
            paint.color.a = color.a * 0.35f;
        }
        canvas_->drawCircle({center.x * meter_, center.y * meter_}, radius * meter_, paint);
    }

    graphics::PrimitiveCanvas2D *canvas_ = nullptr;
    float                        meter_  = 30.f;
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
    graphics::PrimitiveCanvas2D canvas;
    draw.begin(&canvas, getMeter());
    DebugDrawBinding binding(world, &draw);
    world->DrawDebugData();
    draw.end();
    gfx->drawPrimitiveCanvas(canvas);
}

}  // namespace eve::physics
