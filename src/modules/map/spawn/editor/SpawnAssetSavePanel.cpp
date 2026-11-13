#include "map/spawn/editor/SpawnAssetSavePanel.h"
#include <cstddef>
#include "map/spawn/editor/SpawnAssetDocument.h"
#include "map/spawn/editor/SpawnForm.h"
#include "property_access/PropertyAccess.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
namespace {
struct State {
    std::shared_ptr<SpawnAssetDocument> document;
    std::shared_ptr<const std::byte>    display;
    std::vector<Diagnostic>             errors;
    std::function<void()>               invalidate;
    bool                                closed = false, busy = false;
    void                                show(Result<void> result) {
        auto next = result.diagnostics();
        if (invalidate) invalidate();
        errors.swap(next);
    }
};
class Panel final : public ui::Component {
    std::shared_ptr<State>        state_;
    property_access::Subscription subscription_;

public:
    explicit Panel(std::shared_ptr<SpawnAssetDocument> document) : state_(std::make_shared<State>()) {
        state_->document   = std::move(document);
        state_->invalidate = [this] { markDirty(); };
        subscription_ = state_->document->form()->view().subscribe([weak = std::weak_ptr<State>(state_)](const auto&) {
            if (auto state = weak.lock(); state && state->invalidate) state->invalidate();
        });
    }
    ~Panel() override {
        state_->closed     = true;
        state_->invalidate = {};
    }
    ui::WidgetDesc build() override {
        auto                        state    = state_;
        auto                        display  = std::make_shared<const std::byte>();
        const auto                  revision = state->document->form()->view().revision();
        std::vector<ui::WidgetDesc> rows;
        rows.push_back(
            ui::text(state->document->dirty() ? "Unsaved configuration" : "Configuration saved", "spawn/asset/status"));
        rows.push_back(ui::text("Save writes committed configuration. Apply pending drafts first.", "spawn/asset/help")
                           .withWrap(280));
        auto button =
            ui::button("Save configuration", "spawn/asset/save",
                       [state, display, revision, sourceRevision = state->document->form()->sourceRevision()] {
                           if (state->closed || state->busy) return;
                           struct Guard {
                               bool& busy;
                               explicit Guard(bool& value) : busy(value) { busy = true; }
                               ~Guard() { busy = false; }
                           } guard(state->busy);
                           if (state->display != display || state->document->form()->view().revision() != revision ||
                               state->document->form()->sourceRevision() != sourceRevision) {
                               state->show(Result<void>::failure(Diagnostic::error(
                                   DiagnosticCode::Conflict,
                                   "Configuration or save controls changed; use the current Save button")));
                               return;
                           }
                           auto next      = std::make_shared<const std::byte>();
                           auto result    = state->document->save();
                           state->display = std::move(next);
                           state->show(std::move(result));
                       });
        button.enabled = !state->busy && !state->document->form()->history().pending;
        rows.push_back(std::move(button));
        for (std::size_t i = 0; i < state->errors.size(); ++i)
            rows.push_back(ui::text(state->errors[i].message(), "spawn/asset/error/" + std::to_string(i)));
        auto result    = ui::column(std::move(rows), "spawn/asset");
        state->display = std::move(display);
        return result;
    }
};
}  // namespace
Result<std::unique_ptr<ui::Component>> createSpawnAssetSavePanel(std::shared_ptr<SpawnAssetDocument> document) {
    if (!document)
        return Result<std::unique_ptr<ui::Component>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing spawn asset document"));
    return Result<std::unique_ptr<ui::Component>>::success(std::make_unique<Panel>(std::move(document)));
}
}  // namespace eve::spawn_editor
