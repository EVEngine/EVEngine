#include "graphics_editing/PrimitiveGizmoRenderer.h"

#include "graphics/IGraphics3D.h"
#include "graphics/PrimitiveDrawList.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string_view>

namespace eve::graphics_editing {
namespace {

eve::Result<graphics::PrimitiveDrawStatistics> failure(std::string message, std::string path) {
    return eve::Result<graphics::PrimitiveDrawStatistics>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
                               "graphics_editing.primitive_gizmo"));
}

bool finite(const editing::GizmoPrimitive& primitive) {
    const auto finiteArray = [](const auto& values) {
        for (double value : values)
            if (!std::isfinite(value)) return false;
        return true;
    };
    return finiteArray(primitive.position) && finiteArray(primitive.size) && finiteArray(primitive.direction) &&
           finiteArray(primitive.color) && std::isfinite(primitive.radius) && std::isfinite(primitive.length);
}

bool supported(std::string_view kind) {
    return kind == "line" || kind == "point" || kind == "box" || kind == "sphere" || kind == "capsule" ||
           kind == "arrow" || kind == "camera" || kind == "area" || kind == "frustum";
}

glm::vec3 vector(const std::array<double, 3>& value) {
    return {static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2])};
}

graphics::ScenePrimitivePaint paint(const editing::GizmoPrimitive& primitive) {
    graphics::ScenePrimitivePaint result;
    result.mode  = graphics::PaintMode::Stroke;
    result.color = graphics::Color(static_cast<float>(primitive.color[0]), static_cast<float>(primitive.color[1]),
                                   static_cast<float>(primitive.color[2]), static_cast<float>(primitive.color[3]));
    result.stroke.width = 2.f;
    result.stroke.cap   = graphics::LineCap::Round;
    result.stroke.join  = graphics::LineJoin::Round;
    if (primitive.dashed)
        result.stroke.dash = graphics::DashPattern{{8.f, 5.f}, 0.f, graphics::DashSpace::ScreenPixels};
    return result;
}

}  // namespace

eve::Result<graphics::PrimitiveDrawStatistics> PrimitiveGizmoRenderer::render(
    const editing::GizmoSnapshot& snapshot, const graphics::SceneDrawContext& context,
    graphics::IGraphics3D& graphicsBackend) const {
    for (std::size_t index = 0; index < snapshot.primitives.size(); ++index) {
        const auto&       primitive = snapshot.primitives[index];
        const std::string path      = "primitives[" + std::to_string(index) + "]";
        if (!supported(primitive.kind))
            return failure("unsupported gizmo primitive kind: " + primitive.kind, path + ".kind");
        if (!finite(primitive)) return failure("gizmo primitive must be finite", path);
        if ((primitive.kind == "sphere" || primitive.kind == "capsule") && primitive.radius <= 0.0)
            return failure("gizmo radius must be positive", path + ".radius");
        if ((primitive.kind == "line" || primitive.kind == "capsule" || primitive.kind == "arrow" ||
             primitive.kind == "camera" || primitive.kind == "frustum") &&
            primitive.length <= 0.0)
            return failure("gizmo length must be positive", path + ".length");
    }

    try {
        graphics::PrimitiveSceneCanvas3D canvas(context);
        for (const auto& primitive : snapshot.primitives) {
            const glm::vec3 origin    = vector(primitive.position);
            glm::vec3       direction = vector(primitive.direction);
            if (glm::length(direction) <= 1e-6f) direction = {0.f, 0.f, -1.f};
            direction  = glm::normalize(direction);
            auto style = paint(primitive);
            if (primitive.kind == "point") {
                style.stroke.width = primitive.radius > 0.0 ? static_cast<float>(primitive.radius) : 5.f;
                canvas.drawPoint(origin, style);
            } else if (primitive.kind == "line") {
                canvas.drawLine(origin, origin + direction * static_cast<float>(primitive.length), style);
            } else if (primitive.kind == "box") {
                const glm::vec3 half = vector(primitive.size) * 0.5f;
                canvas.drawAabb(origin - half, origin + half, style);
            } else if (primitive.kind == "sphere") {
                canvas.drawSphere(origin, static_cast<float>(primitive.radius), style);
            } else if (primitive.kind == "capsule") {
                canvas.drawCapsule(origin, origin + direction * static_cast<float>(primitive.length),
                                   static_cast<float>(primitive.radius), style);
            } else if (primitive.kind == "arrow" || primitive.kind == "camera") {
                const float length = static_cast<float>(primitive.length);
                canvas.drawArrow(origin, origin + direction * length, length * 0.2f, std::max(0.04f, length * 0.08f),
                                 style);
            } else if (primitive.kind == "area") {
                const glm::vec3 half = vector(primitive.size) * 0.5f;
                canvas.drawQuad(origin + glm::vec3(-half.x, 0.f, -half.z), origin + glm::vec3(half.x, 0.f, -half.z),
                                origin + glm::vec3(half.x, 0.f, half.z), origin + glm::vec3(-half.x, 0.f, half.z),
                                style);
            } else {
                const float length = static_cast<float>(primitive.length);
                const float radius = std::max(0.01f, static_cast<float>(primitive.radius));
                glm::vec3   up = std::abs(direction.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
                const glm::vec3 right                    = glm::normalize(glm::cross(direction, up)) * radius;
                up                                       = glm::normalize(glm::cross(right, direction)) * radius;
                const glm::vec3                farCenter = origin + direction * length;
                const std::array<glm::vec3, 8> corners{origin,
                                                       origin,
                                                       origin,
                                                       origin,
                                                       farCenter - right - up,
                                                       farCenter + right - up,
                                                       farCenter - right + up,
                                                       farCenter + right + up};
                canvas.drawFrustum(corners, style);
            }
        }
        graphicsBackend.drawPrimitiveScene(canvas);
        return eve::Result<graphics::PrimitiveDrawStatistics>::success(canvas.statistics());
    } catch (const std::exception& error) {
        return failure(error.what(), "snapshot");
    }
}

}  // namespace eve::graphics_editing
