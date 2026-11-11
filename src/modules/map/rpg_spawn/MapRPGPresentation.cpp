// ECS scope: none; reads owning scheduler/domain observations, no writes or structural changes.
// Phase: presentation after lifecycle reconciliation; services are explicitly injected.
#include "map/rpg_spawn/MapRPGPresentation.h"
#include <cmath>
#include <limits>
#include <vector>
#include "graphics/ISolidRectRenderer.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rpg/spawn/RPGSpawnFactory.h"
namespace eve::map_rpg_spawn {
Result<std::size_t> drawActors(const map_spawn::MapSpawnSession& session, const rpg_spawn::RPGSpawnFactory& actors,
                               graphics::ISolidRectRenderer& renderer, const ActorStyle& style) {
    if (!std::isfinite(style.width) || !std::isfinite(style.height) || style.width <= 0 || style.height <= 0 ||
        !std::isfinite(style.red) || !std::isfinite(style.green) || !std::isfinite(style.blue) ||
        !std::isfinite(style.alpha) || style.red < 0 || style.red > 1 || style.green < 0 || style.green > 1 ||
        style.blue < 0 || style.blue > 1 || style.alpha < 0 || style.alpha > 1)
        return Result<std::size_t>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid actor style"));
    struct Rect {
        float x, y;
    };
    std::vector<Rect> batch;
    for (const auto source : session.sourceHandles()) {
        auto snapshot = session.world().inspect(source);
        if (!snapshot) return Result<std::size_t>::failure(snapshot.status());
        for (const auto subject : snapshot.value().subjects) {
            auto actor = actors.inspect(subject);
            if (!actor) return Result<std::size_t>::failure(actor.status());
            if (!std::isfinite(actor.value().hp))
                return Result<std::size_t>::failure(
                    Diagnostic::error(DiagnosticCode::InvariantViolation, "Invalid actor health"));
            if (actor.value().hp <= 0) continue;
            auto pixel = session.toPixel(actor.value().position);
            if (!pixel) return Result<std::size_t>::failure(pixel.status());
            const auto x     = pixel.value().x - style.width * 0.5;
            const auto y     = pixel.value().y - style.height * 0.5;
            const auto limit = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > limit || std::abs(y) > limit ||
                std::abs(x + style.width) > limit || std::abs(y + style.height) > limit)
                return Result<std::size_t>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Actor pixel bounds overflow"));
            batch.push_back({static_cast<float>(x), static_cast<float>(y)});
        }
    }
    for (const auto rect : batch)
        renderer.drawSolidRectRGBA(rect.x, rect.y, style.width, style.height, style.red, style.green, style.blue,
                                   style.alpha);
    return Result<std::size_t>::success(batch.size());
}
}  // namespace eve::map_rpg_spawn
