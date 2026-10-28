#include "ui/PcgColorPreviewSync.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgPhotoModeColorPicker.h"

// PcgColorPreviewSync.cpp
namespace eve::ui {
Result<void> PcgColorPreviewSync::sync(float r, float g, float b, float a) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "source color must be finite",
                                                       "ui.pcgColorPreview.sync"));
    if (r == 0.f && g == 0.f && b == 0.f) {
        red_ = green_ = blue_ = 0.5f;
        alpha_                = 1.f;
    } else {
        red_   = r * 2.5f;
        green_ = g * 2.5f;
        blue_  = b * 2.5f;
        alpha_ = a * 2.5f;
    }
    return Result<void>::success();
}
void exposePcgColorPreviewSyncBindings(ssq::Table& table) {
    auto cls = table.addClass("PcgColorPreviewSync", ssq::Class::Ctor<PcgColorPreviewSync()>());
    auto vm  = table.getHandle();
    cls.addFunc("sync", [vm](PcgColorPreviewSync* self, float r, float g, float b, float a) {
        return eve::script::projectResult(vm, self->sync(r, g, b, a));
    });
    cls.addFunc("getRed", [](const PcgColorPreviewSync* self) { return self->getRed(); });
    cls.addFunc("getGreen", [](const PcgColorPreviewSync* self) { return self->getGreen(); });
    cls.addFunc("getBlue", [](const PcgColorPreviewSync* self) { return self->getBlue(); });
    cls.addFunc("getAlpha", [](const PcgColorPreviewSync* self) { return self->getAlpha(); });
}
}  // namespace eve::ui

// PcgControllerSelection.cpp
namespace eve::ui {
namespace {}
Result<void> PcgControllerSelection::add(const std::string& name, int type, const std::string& id) {
    if (name.empty() || id.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "controller name and widget id are required",
                                                       "ui.pcgControllerSelection"));
    rows_.push_back({name, id, type, false});
    return Result<void>::success();
}
Result<bool> PcgControllerSelection::refresh(int current) {
    int found = -1;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].controllerType == current) {
            found = int(i);
            break;
        }
    if (found < 0) return Result<bool>::success(false);
    for (size_t i = 0; i < rows_.size(); ++i) rows_[i].visible = int(i) == found;
    selected_ = found;
    return Result<bool>::success(true);
}
Result<bool> PcgControllerSelection::getVisible(int i) const {
    if (i < 0 || i >= int(rows_.size()))
        return Result<bool>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "controller row index is out of range", "ui.pcgControllerSelection"));
    return Result<bool>::success(rows_[size_t(i)].visible);
}
Result<std::string> PcgControllerSelection::getWidgetId(int i) const {
    if (i < 0 || i >= int(rows_.size()))
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "controller row index is out of range", "ui.pcgControllerSelection"));
    return Result<std::string>::success(rows_[size_t(i)].widgetId);
}
void exposePcgControllerSelectionBindings(ssq::Table& t) {
    auto c  = t.addClass("PcgControllerSelection", ssq::Class::Ctor<PcgControllerSelection()>());
    auto vm = t.getHandle();
    c.addFunc("add", [vm](PcgControllerSelection* s, const std::string& n, int type, const std::string& id) {
        return eve::script::projectResult(vm, s->add(n, type, id));
    });
    c.addFunc("refresh", [vm](PcgControllerSelection* s, int type) {
        return eve::script::projectResult(vm, s->refresh(type), [](bool v) { return v; });
    });
    c.addFunc("getCount", [](const PcgControllerSelection* s) { return s->getCount(); });
    c.addFunc("getSelectedIndex", [](const PcgControllerSelection* s) { return s->getSelectedIndex(); });
    c.addFunc("getVisible", [vm](const PcgControllerSelection* s, int i) {
        return eve::script::projectResult(vm, s->getVisible(i), [](bool v) { return v; });
    });
    c.addFunc("getWidgetId", [vm](const PcgControllerSelection* s, int i) {
        return eve::script::projectResult(vm, s->getWidgetId(i), [](std::string v) { return v; });
    });
}
}  // namespace eve::ui

// PcgLoadingScreen.cpp
namespace eve::ui {
namespace {

bool validName(const std::string& value) { return !value.empty() && value.find('\0') == std::string::npos; }
}  // namespace

Result<void> PcgLoadingScreen::configure(float fadeOutSpeed) {
    if (!std::isfinite(fadeOutSpeed) || fadeOutSpeed < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fade-out speed must be finite and non-negative", {}, {},
                                                       "ui.pcgLoadingScreen"));
    fadeOutSpeed_ = fadeOutSpeed;
    return Result<void>::success();
}

void PcgLoadingScreen::onLoadProgressStarted() noexcept {
    canvasVisible_   = true;
    backgroundAlpha_ = 1.f;
    progress_        = 0.f;
}

Result<void> PcgLoadingScreen::onLoadProgressUpdated(float progress) {
    if (!std::isfinite(progress))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "load progress must be finite",
                                                       {}, {}, "ui.pcgLoadingScreen"));
    canvasVisible_ = true;
    progress_      = progress;
    if (progress >= 1.f) onLoadProgressEnded();
    return Result<void>::success();
}

void PcgLoadingScreen::onLoadProgressEnded() noexcept {
    fading_          = true;
    progressVisible_ = false;
    textVisible_     = false;
}

void PcgLoadingScreen::beginTimeout() noexcept {
    timeoutOpen_ = true;
    pendingMissingScenes_.clear();
}

Result<int> PcgLoadingScreen::addMissingScene(const std::string& terrainName, const std::string& impostorName) {
    if (!timeoutOpen_)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "beginTimeout must be called before adding a missing scene", {},
                                                      {}, "ui.pcgLoadingScreen"));
    if ((!terrainName.empty() && !validName(terrainName)) || (!impostorName.empty() && !validName(impostorName)))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "missing-scene names must not contain null characters", {}, {},
                                                      "ui.pcgLoadingScreen"));
    pendingMissingScenes_.push_back({terrainName, impostorName, {}, {}});
    return Result<int>::success(static_cast<int>(pendingMissingScenes_.size() - 1));
}

Result<void> PcgLoadingScreen::addReference(int sceneIndex, const std::string& objectName, bool impostor) {
    if (!timeoutOpen_ || sceneIndex < 0 || static_cast<size_t>(sceneIndex) >= pendingMissingScenes_.size() ||
        !validName(objectName))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "an open timeout, valid scene index and nonempty object name are required",
            {}, {}, "ui.pcgLoadingScreen"));
    auto& scene = pendingMissingScenes_[static_cast<size_t>(sceneIndex)];
    (impostor ? scene.impostorReferences : scene.regularReferences).push_back(objectName);
    return Result<void>::success();
}

Result<void> PcgLoadingScreen::addRegularReference(int sceneIndex, const std::string& objectName) {
    return addReference(sceneIndex, objectName, false);
}

Result<void> PcgLoadingScreen::addImpostorReference(int sceneIndex, const std::string& objectName) {
    return addReference(sceneIndex, objectName, true);
}

Result<void> PcgLoadingScreen::endTimeout() {
    if (!timeoutOpen_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "beginTimeout must be called before endTimeout", {}, {},
                                                       "ui.pcgLoadingScreen"));
    std::string message = "Pcg Loading Screen Timeout, closing down loading screen.\r\n";
    message +=
        "If you feel that there is no error but the terrain loading simply needs more time, try to increase the "
        "Timeout value in the Terrain Loader Manager.\r\n";
    message += "The following scenes are still not loaded:\r\n";
    for (const auto& scene : pendingMissingScenes_) {
        if (!scene.regularReferences.empty()) {
            message += "\r\n " + scene.terrainName + "\r\n Referencing Objects:\r\n";
            for (const auto& name : scene.regularReferences) message += name + ", ";
        }
        if (!scene.impostorReferences.empty()) {
            message += "\r\n " + scene.impostorName + "\r\n Referencing Objects:\r\n";
            for (const auto& name : scene.impostorReferences) message += name + ", ";
        }
        message += "\r\n";
    }
    timeoutMessage_ = std::move(message);
    pendingMissingScenes_.clear();
    timeoutOpen_ = false;
    onLoadProgressEnded();
    return Result<void>::success();
}

Result<void> PcgLoadingScreen::tick(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "frame delta time must be finite and non-negative", {}, {},
                                                       "ui.pcgLoadingScreen"));
    if (fading_) {
        backgroundAlpha_ -= deltaTime * fadeOutSpeed_;
        if (backgroundAlpha_ <= 0.f) {
            backgroundAlpha_ = 0.f;
            canvasVisible_   = false;
            fading_          = false;
        }
    }
    return Result<void>::success();
}

void exposePcgLoadingScreenBindings(ssq::Table& table) {
    auto       c  = table.addClass("PcgLoadingScreen", ssq::Class::Ctor<PcgLoadingScreen()>());
    const auto vm = table.getHandle();
    c.addFunc("configure", [vm](PcgLoadingScreen* self, float speed) {
        return eve::script::projectResult(vm, self->configure(speed));
    });
    c.addFunc("onLoadProgressStarted", [](PcgLoadingScreen* self) { self->onLoadProgressStarted(); });
    c.addFunc("onLoadProgressUpdated", [vm](PcgLoadingScreen* self, float progress) {
        return eve::script::projectResult(vm, self->onLoadProgressUpdated(progress));
    });
    c.addFunc("onLoadProgressEnded", [](PcgLoadingScreen* self) { self->onLoadProgressEnded(); });
    c.addFunc("beginTimeout", [](PcgLoadingScreen* self) { self->beginTimeout(); });
    c.addFunc("addMissingScene", [vm](PcgLoadingScreen* self, const std::string& terrain, const std::string& impostor) {
        return eve::script::projectResult(vm, self->addMissingScene(terrain, impostor),
                                          [](int value) { return eve::Value(value); });
    });
    c.addFunc("addRegularReference", [vm](PcgLoadingScreen* self, int index, const std::string& name) {
        return eve::script::projectResult(vm, self->addRegularReference(index, name));
    });
    c.addFunc("addImpostorReference", [vm](PcgLoadingScreen* self, int index, const std::string& name) {
        return eve::script::projectResult(vm, self->addImpostorReference(index, name));
    });
    c.addFunc("endTimeout",
              [vm](PcgLoadingScreen* self) { return eve::script::projectResult(vm, self->endTimeout()); });
    c.addFunc("tick",
              [vm](PcgLoadingScreen* self, float dt) { return eve::script::projectResult(vm, self->tick(dt)); });
    c.addFunc("getCanvasVisible", [](const PcgLoadingScreen* self) { return self->getCanvasVisible(); });
    c.addFunc("getProgressVisible", [](const PcgLoadingScreen* self) { return self->getProgressVisible(); });
    c.addFunc("getTextVisible", [](const PcgLoadingScreen* self) { return self->getTextVisible(); });
    c.addFunc("getFading", [](const PcgLoadingScreen* self) { return self->getFading(); });
    c.addFunc("getProgress", [](const PcgLoadingScreen* self) { return self->getProgress(); });
    c.addFunc("getBackgroundAlpha", [](const PcgLoadingScreen* self) { return self->getBackgroundAlpha(); });
    c.addFunc("getTimeoutMessage", [](const PcgLoadingScreen* self) { return self->getTimeoutMessage(); });
}

}  // namespace eve::ui

// PcgPhotoModeColorPicker.cpp
namespace eve::ui {
namespace {
bool finite4(float a, float b, float c, float d) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d);
}
}  // namespace
bool         PcgPhotoModeColorPicker::hdrActive() const noexcept { return hdr_ && (a_ > 1.f || a_ < 0.f); }
Result<void> PcgPhotoModeColorPicker::open(float r, float g, float b, float a, bool hdr) {
    if (!finite4(r, g, b, a))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "color must be finite", "ui.pcgPhotoModeColorPicker"));
    float nr = r, ng = g, nb = b;
    if (hdr && (a > 1.f || a < 0.f)) {
        nr *= a;
        ng *= a;
        nb *= a;
    }
    r_     = nr;
    g_     = ng;
    b_     = nb;
    a_     = a;
    hdr_   = hdr;
    open_  = true;
    front_ = true;
    return Result<void>::success();
}
Result<void> PcgPhotoModeColorPicker::setLast(float r, float g, float b, float a) {
    if (!finite4(r, g, b, a))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "color must be finite", "ui.pcgPhotoModeColorPicker"));
    lastR_ = r;
    lastG_ = g;
    lastB_ = b;
    lastA_ = a;
    return Result<void>::success();
}
Result<void> PcgPhotoModeColorPicker::setChannel(float& dst, float v) {
    if (!std::isfinite(v))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "color must be finite", "ui.pcgPhotoModeColorPicker"));
    dst = v;
    ++revision_;
    return Result<void>::success();
}
Result<void> PcgPhotoModeColorPicker::setRed(float v) { return setChannel(r_, v); }
Result<void> PcgPhotoModeColorPicker::setGreen(float v) { return setChannel(g_, v); }
Result<void> PcgPhotoModeColorPicker::setBlue(float v) { return setChannel(b_, v); }
Result<void> PcgPhotoModeColorPicker::setHdr(float v) { return setChannel(a_, v); }
void         PcgPhotoModeColorPicker::reset() noexcept {
    r_ = lastR_;
    g_ = lastG_;
    b_ = lastB_;
    a_ = lastA_;
    revision_ += 5;
}
float PcgPhotoModeColorPicker::getPreviewRed() const noexcept { return r_ * (hdrActive() ? a_ : 1.f); }
float PcgPhotoModeColorPicker::getPreviewGreen() const noexcept { return g_ * (hdrActive() ? a_ : 1.f); }
float PcgPhotoModeColorPicker::getPreviewBlue() const noexcept { return b_ * (hdrActive() ? a_ : 1.f); }
float PcgPhotoModeColorPicker::getHdrSwatch() const noexcept { return std::clamp((a_ + 5.f) / 10.f, 0.f, 1.f); }
PcgUiRequestStatus PcgPhotoModeColorPicker::consumeBringToFront() noexcept {
    auto v = front_ ? PcgUiRequestStatus::Requested : PcgUiRequestStatus::None;
    front_ = false;
    return v;
}
void exposePcgPhotoModeColorPickerBindings(ssq::Table& t) {
    auto c  = t.addClass("PcgPhotoModeColorPicker", ssq::Class::Ctor<PcgPhotoModeColorPicker()>());
    auto vm = t.getHandle();
    c.addFunc("open", [vm](PcgPhotoModeColorPicker* s, float r, float g, float b, float a, bool h) {
        return eve::script::projectResult(vm, s->open(r, g, b, a, h));
    });
    c.addFunc("setLast", [vm](PcgPhotoModeColorPicker* s, float r, float g, float b, float a) {
        return eve::script::projectResult(vm, s->setLast(r, g, b, a));
    });
    c.addFunc("setFocusedName", &PcgPhotoModeColorPicker::setFocusedName);
    c.addFunc("close", [](PcgPhotoModeColorPicker* s) { s->close(); });
    c.addFunc("setRed",
              [vm](PcgPhotoModeColorPicker* s, float v) { return eve::script::projectResult(vm, s->setRed(v)); });
    c.addFunc("setGreen",
              [vm](PcgPhotoModeColorPicker* s, float v) { return eve::script::projectResult(vm, s->setGreen(v)); });
    c.addFunc("setBlue",
              [vm](PcgPhotoModeColorPicker* s, float v) { return eve::script::projectResult(vm, s->setBlue(v)); });
    c.addFunc("setHdr",
              [vm](PcgPhotoModeColorPicker* s, float v) { return eve::script::projectResult(vm, s->setHdr(v)); });
    c.addFunc("reset", [](PcgPhotoModeColorPicker* s) { s->reset(); });
    c.addFunc("getRed", [](const PcgPhotoModeColorPicker* s) { return s->getRed(); });
    c.addFunc("getGreen", [](const PcgPhotoModeColorPicker* s) { return s->getGreen(); });
    c.addFunc("getBlue", [](const PcgPhotoModeColorPicker* s) { return s->getBlue(); });
    c.addFunc("getAlpha", [](const PcgPhotoModeColorPicker* s) { return s->getAlpha(); });
    c.addFunc("getPreviewRed", [](const PcgPhotoModeColorPicker* s) { return s->getPreviewRed(); });
    c.addFunc("getPreviewGreen", [](const PcgPhotoModeColorPicker* s) { return s->getPreviewGreen(); });
    c.addFunc("getPreviewBlue", [](const PcgPhotoModeColorPicker* s) { return s->getPreviewBlue(); });
    c.addFunc("getHdrSwatch", [](const PcgPhotoModeColorPicker* s) { return s->getHdrSwatch(); });
    c.addFunc("getHdrEnabled", [](const PcgPhotoModeColorPicker* s) { return s->getHdrEnabled(); });
    c.addFunc("getOpen", [](const PcgPhotoModeColorPicker* s) { return s->getOpen(); });
    c.addFunc("getFocusedName", [](const PcgPhotoModeColorPicker* s) { return s->getFocusedName(); });
    c.addFunc("getChangeRevision", [](const PcgPhotoModeColorPicker* s) { return s->getChangeRevision(); });
    c.addFunc("consumeBringToFront",
              [](PcgPhotoModeColorPicker* s) { return s->consumeBringToFront() == PcgUiRequestStatus::Requested; });
}
}  // namespace eve::ui
