#include "ui/PcgPhotoModeSession.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "common/SquirrelBinding.h"

namespace eve::ui {
namespace {
Result<void> fail(const char* message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, {}, {}, "ui.pcgPhotoModeSession"));
}
}  // namespace

Result<void> PcgPhotoModeSession::begin(const std::string& capturedJson, const std::string& savedJson,
                                        bool loadSaved, bool savedEver, int currentPipeline, int savedPipeline,
                                        const std::string& sceneName, int lightingProfile) {
    if (active_) return fail("photo-mode session is already active");
    PcgPhotoModeValues captured;
    auto capturedResult = captured.restoreJson(capturedJson);
    if (!capturedResult.ok()) return Result<void>::failure(capturedResult.status());

    PcgPhotoModeValues working = captured;
    auto decision              = PcgPhotoModeLoadDecision::Current;
    if (loadSaved && savedEver && currentPipeline == savedPipeline && !savedJson.empty()) {
        PcgPhotoModeValues saved;
        auto savedResult = saved.restoreJson(savedJson);
        if (!savedResult.ok()) return Result<void>::failure(savedResult.status());
        auto savedScene    = saved.getString("m_lastSceneName");
        auto savedLighting = saved.getInt("m_selectedPcgLightingProfile");
        auto savedPcg      = saved.getBool("m_isUsingPcgLighting");
        if (!savedScene.ok()) return Result<void>::failure(savedScene.status());
        if (!savedLighting.ok()) return Result<void>::failure(savedLighting.status());
        if (!savedPcg.ok()) return Result<void>::failure(savedPcg.status());
        const bool lightingMatches = savedLighting.value() == lightingProfile &&
                                     savedPcg.value() == (lightingProfile >= 0);
        if (savedScene.value() == sceneName && lightingMatches) {
            working  = std::move(saved);
            decision = PcgPhotoModeLoadDecision::Saved;
        } else {
            decision = PcgPhotoModeLoadDecision::SaveCurrent;
        }
    }

    captured_                   = std::move(captured);
    working_                    = working;
    output_                     = working_;
    loadDecision_               = decision;
    active_                     = true;
    restoreRequested_           = false;
    removePhotoCameraRequested_ = false;
    unfreezePlayerRequested_    = false;
    ++revision_;
    return Result<void>::success();
}

Result<void> PcgPhotoModeSession::replaceWorkingJson(const std::string& json) {
    if (!active_) return fail("photo-mode session is not active");
    PcgPhotoModeValues candidate;
    auto result = candidate.restoreJson(json);
    if (!result.ok()) return Result<void>::failure(result.status());
    working_ = std::move(candidate);
    output_  = working_;
    ++revision_;
    return Result<void>::success();
}

Result<void> PcgPhotoModeSession::end(bool resetOnDisable, bool applicationPlaying) {
    if (!active_) return fail("photo-mode session is not active");
    removePhotoCameraRequested_ = true;
    unfreezePlayerRequested_    = true;
    restoreRequested_           = resetOnDisable && applicationPlaying;
    output_                     = restoreRequested_ ? captured_ : working_;
    active_                     = false;
    ++revision_;
    return Result<void>::success();
}

Result<std::string> PcgPhotoModeSession::workingJson() const { return working_.snapshotJson(); }
Result<std::string> PcgPhotoModeSession::outputJson() const { return output_.snapshotJson(); }

void exposePcgPhotoModeSessionBindings(ssq::Table& table) {
    auto vm  = table.getHandle();
    auto cls = table.addClass("PcgPhotoModeSession", ssq::Class::Ctor<PcgPhotoModeSession()>());
    cls.addFunc("begin", [vm](PcgPhotoModeSession* self, const std::string& captured, const std::string& saved,
                              bool loadSaved, bool savedEver, int currentPipeline, int savedPipeline,
                              const std::string& sceneName, int lightingProfile) {
        return eve::script::projectResult(vm, self->begin(captured, saved, loadSaved, savedEver, currentPipeline,
                                                          savedPipeline, sceneName, lightingProfile));
    });
    cls.addFunc("replaceWorkingJson", [vm](PcgPhotoModeSession* self, const std::string& value) {
        return eve::script::projectResult(vm, self->replaceWorkingJson(value));
    });
    cls.addFunc("end", [vm](PcgPhotoModeSession* self, bool reset, bool playing) {
        return eve::script::projectResult(vm, self->end(reset, playing));
    });
    cls.addFunc("workingJson", [vm](const PcgPhotoModeSession* self) {
        return eve::script::projectResult(vm, self->workingJson(), [](const std::string& value) { return value; });
    });
    cls.addFunc("outputJson", [vm](const PcgPhotoModeSession* self) {
        return eve::script::projectResult(vm, self->outputJson(), [](const std::string& value) { return value; });
    });
    cls.addFunc("getActive", [](const PcgPhotoModeSession* self) { return self->getActive(); });
    cls.addFunc("getRestoreRequested", [](const PcgPhotoModeSession* self) { return self->getRestoreRequested(); });
    cls.addFunc("getRemovePhotoCameraRequested",
                [](const PcgPhotoModeSession* self) { return self->getRemovePhotoCameraRequested(); });
    cls.addFunc("getUnfreezePlayerRequested",
                [](const PcgPhotoModeSession* self) { return self->getUnfreezePlayerRequested(); });
    cls.addFunc("getLoadDecision", [](const PcgPhotoModeSession* self) { return self->getLoadDecision(); });
    cls.addFunc("getRevision", [](const PcgPhotoModeSession* self) { return self->getRevision(); });
}
}  // namespace eve::ui
