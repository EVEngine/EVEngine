#include "ui/UiMotionSinks.h"

#include "common/Diagnostic.h"

#include <algorithm>

namespace eve::ui {
namespace {

}  // namespace

eve::Result<void> UiHostPosSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, std::string("UiHostPosSink") + ".write: host handle is stale"));
    auto m          = host->get().meta();
    m->hasPos       = true;
    m->animDrivePos = true;
    m->posX         = value.x;
    m->posY         = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiHostSizeSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, std::string("UiHostSizeSink") + ".write: host handle is stale"));
    auto m           = host->get().meta();
    m->hasSize       = true;
    m->animDriveSize = true;
    m->sizeX         = value.x;
    m->sizeY         = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiHostOverlayAlphaSink::write(float value) {
    auto host = UIHost::resolve(host_);
    if (!host) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, std::string("UiHostOverlayAlphaSink") + ".write: host handle is stale"));
    host->get().meta()->overlayBgAlpha = std::clamp(value, 0.f, 1.f);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiNodeOpacitySink::write(float value) {
    auto host = UIHost::resolve(host_);
    if (!host) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, std::string("UiNodeOpacitySink") + ".write: host handle is stale"));
    auto node = host->get().findById(nodeId_);
    if (!node) return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, std::string("UiNodeOpacitySink") + ".write: node '" + nodeId_ + "' not found"));
    node->get().opacity = std::clamp(value, 0.f, 1.f);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> UiNodePosSink::write(eve::animation::MotionVec2 value) {
    auto host = UIHost::resolve(host_);
    if (!host) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, std::string("UiNodePosSink") + ".write: host handle is stale"));
    auto node = host->get().findById(nodeId_);
    if (!node) return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, std::string("UiNodePosSink") + ".write: node '" + nodeId_ + "' not found"));
    auto &n    = node->get();
    n.absolute = true;
    n.posX     = value.x;
    n.posY     = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::ui
