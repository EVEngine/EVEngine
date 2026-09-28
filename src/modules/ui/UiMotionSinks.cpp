#include "ui/UiMotionSinks.h"

#include "common/Diagnostic.h"

#include <algorithm>

namespace eve::ui {
namespace {

eve::Result<void> staleHost(const char *sink) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::StaleHandle, std::string(sink) + ".write: host handle is stale"));
}

eve::Result<void> missingNode(const char *sink, const std::string &id) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, std::string(sink) + ".write: node '" + id + "' not found"));
}

}  // namespace

eve::Result<void> UiHostPosSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return staleHost("UiHostPosSink");
    auto m = host->get().meta();
    m->hasPos = true;
    m->posX = value.x;
    m->posY = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiHostSizeSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return staleHost("UiHostSizeSink");
    auto m = host->get().meta();
    m->hasSize = true;
    m->sizeX = value.x;
    m->sizeY = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiHostOverlayAlphaSink::write(float value) {
    auto host = UIHost::resolve(host_);
    if (!host) return staleHost("UiHostOverlayAlphaSink");
    host->get().meta()->overlayBgAlpha = std::clamp(value, 0.f, 1.f);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiNodeOpacitySink::write(float value) {
    auto host = UIHost::resolve(host_);
    if (!host) return staleHost("UiNodeOpacitySink");
    auto node = host->get().findById(nodeId_);
    if (!node) return missingNode("UiNodeOpacitySink", nodeId_);
    node->get().opacity = std::clamp(value, 0.f, 1.f);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiNodePosSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return staleHost("UiNodePosSink");
    auto node = host->get().findById(nodeId_);
    if (!node) return missingNode("UiNodePosSink", nodeId_);
    auto &n = node->get();
    n.absolute = true;
    n.posX = value.x;
    n.posY = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::ui
