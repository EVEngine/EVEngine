#include "ui/PcgLoadingScreen.h"

#include "common/SquirrelBinding.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ui {
namespace {
Result<void> invalidLoadingScreen(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, {}, {},
                                                    "ui.pcgLoadingScreen"));
}

bool validName(const std::string& value) { return !value.empty() && value.find('\0') == std::string::npos; }
}  // namespace

Result<void> PcgLoadingScreen::configure(float fadeOutSpeed) {
    if (!std::isfinite(fadeOutSpeed) || fadeOutSpeed < 0.f)
        return invalidLoadingScreen("fade-out speed must be finite and non-negative");
    fadeOutSpeed_ = fadeOutSpeed;
    return Result<void>::success();
}

void PcgLoadingScreen::onLoadProgressStarted() noexcept {
    canvasVisible_ = true;
    backgroundAlpha_ = 1.f;
    progress_ = 0.f;
}

Result<void> PcgLoadingScreen::onLoadProgressUpdated(float progress) {
    if (!std::isfinite(progress)) return invalidLoadingScreen("load progress must be finite");
    canvasVisible_ = true;
    progress_ = progress;
    if (progress >= 1.f) onLoadProgressEnded();
    return Result<void>::success();
}

void PcgLoadingScreen::onLoadProgressEnded() noexcept {
    fading_ = true;
    progressVisible_ = false;
    textVisible_ = false;
}

void PcgLoadingScreen::beginTimeout() noexcept {
    timeoutOpen_ = true;
    pendingMissingScenes_.clear();
}

Result<int> PcgLoadingScreen::addMissingScene(const std::string& terrainName, const std::string& impostorName) {
    if (!timeoutOpen_)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "beginTimeout must be called before adding a missing scene", {}, {}, "ui.pcgLoadingScreen"));
    if ((!terrainName.empty() && !validName(terrainName)) || (!impostorName.empty() && !validName(impostorName)))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "missing-scene names must not contain null characters", {}, {}, "ui.pcgLoadingScreen"));
    pendingMissingScenes_.push_back({terrainName, impostorName, {}, {}});
    return Result<int>::success(static_cast<int>(pendingMissingScenes_.size() - 1));
}

Result<void> PcgLoadingScreen::addReference(int sceneIndex, const std::string& objectName, bool impostor) {
    if (!timeoutOpen_ || sceneIndex < 0 || static_cast<size_t>(sceneIndex) >= pendingMissingScenes_.size() ||
        !validName(objectName))
        return invalidLoadingScreen("an open timeout, valid scene index and nonempty object name are required");
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
    if (!timeoutOpen_) return invalidLoadingScreen("beginTimeout must be called before endTimeout");
    std::string message = "Pcg Loading Screen Timeout, closing down loading screen.\r\n";
    message += "If you feel that there is no error but the terrain loading simply needs more time, try to increase the Timeout value in the Terrain Loader Manager.\r\n";
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
        return invalidLoadingScreen("frame delta time must be finite and non-negative");
    if (fading_) {
        backgroundAlpha_ -= deltaTime * fadeOutSpeed_;
        if (backgroundAlpha_ <= 0.f) {
            backgroundAlpha_ = 0.f;
            canvasVisible_ = false;
            fading_ = false;
        }
    }
    return Result<void>::success();
}

void exposePcgLoadingScreenBindings(ssq::Table& table) {
    auto c = table.addClass("PcgLoadingScreen", ssq::Class::Ctor<PcgLoadingScreen()>());
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
    c.addFunc("addMissingScene", [vm](PcgLoadingScreen* self, const std::string& terrain,
                                       const std::string& impostor) {
        return eve::script::projectResult(vm, self->addMissingScene(terrain, impostor),
                                          [](int value) { return eve::Value(value); });
    });
    c.addFunc("addRegularReference", [vm](PcgLoadingScreen* self, int index, const std::string& name) {
        return eve::script::projectResult(vm, self->addRegularReference(index, name));
    });
    c.addFunc("addImpostorReference", [vm](PcgLoadingScreen* self, int index, const std::string& name) {
        return eve::script::projectResult(vm, self->addImpostorReference(index, name));
    });
    c.addFunc("endTimeout", [vm](PcgLoadingScreen* self) {
        return eve::script::projectResult(vm, self->endTimeout());
    });
    c.addFunc("tick", [vm](PcgLoadingScreen* self, float dt) {
        return eve::script::projectResult(vm, self->tick(dt));
    });
    c.addFunc("getCanvasVisible", [](const PcgLoadingScreen* self) { return self->getCanvasVisible(); });
    c.addFunc("getProgressVisible", [](const PcgLoadingScreen* self) { return self->getProgressVisible(); });
    c.addFunc("getTextVisible", [](const PcgLoadingScreen* self) { return self->getTextVisible(); });
    c.addFunc("getFading", [](const PcgLoadingScreen* self) { return self->getFading(); });
    c.addFunc("getProgress", [](const PcgLoadingScreen* self) { return self->getProgress(); });
    c.addFunc("getBackgroundAlpha", [](const PcgLoadingScreen* self) { return self->getBackgroundAlpha(); });
    c.addFunc("getTimeoutMessage", [](const PcgLoadingScreen* self) { return self->getTimeoutMessage(); });
}

}  // namespace eve::ui
