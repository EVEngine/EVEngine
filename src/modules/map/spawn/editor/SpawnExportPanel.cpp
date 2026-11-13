#include "map/spawn/editor/SpawnExportPanel.h"
#include <cstddef>
#include <optional>
#include "filesystem/AtomicTextWriteLease.h"
#include "map/spawn/editor/PreparedSpawnExport.h"
#include "map/spawn/editor/SpawnForm.h"
#include "property_access/PropertyAccess.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
namespace {
struct State {
    std::shared_ptr<SpawnForm>         form;
    filesystem::AtomicTextWriteLease   filesystem;
    std::string                        path, written;
    std::optional<PreparedSpawnExport> prepared;
    std::shared_ptr<const std::byte>   display;
    std::vector<Diagnostic>            errors;
    std::function<void()>              invalidate;
    bool                               closed = false, busy = false;
    void                               show(Result<void> result) {
        auto next = result.diagnostics();
        if (invalidate) invalidate();
        errors.swap(next);
    }
};
// Keep the guard's destructor outside the generic callback specialization.
struct BusyGuard {
    bool& busy;
    explicit BusyGuard(bool& value) : busy(value) { busy = true; }
    ~BusyGuard() { busy = false; }
};
template <class Callback>
auto action(const std::shared_ptr<State>& state, Callback callback) {
    return [state, display = state->display, revision = state->form->view().revision(),
            sourceRevision = state->form->sourceRevision(), callback = std::move(callback)](auto&&... args) {
        if (state->closed || state->busy) return;
        BusyGuard guard(state->busy);
        if (display != state->display || revision != state->form->view().revision() ||
            sourceRevision != state->form->sourceRevision()) {
            state->show(Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Export controls changed; use the current controls")));
            return;
        }
        auto next      = std::make_shared<const std::byte>();
        auto result    = callback(std::forward<decltype(args)>(args)...);
        state->display = std::move(next);
        state->show(std::move(result));
    };
}
class Panel final : public ui::Component {
    std::shared_ptr<State>        state_;
    property_access::Subscription subscription_;

public:
    Panel(std::shared_ptr<SpawnForm> form, filesystem::AtomicTextWriteLease filesystem, std::string path)
        : state_(std::make_shared<State>()) {
        state_->form       = std::move(form);
        state_->filesystem = std::move(filesystem);
        state_->path       = std::move(path);
        state_->invalidate = [this] { markDirty(); };
        subscription_      = state_->form->view().subscribe([weak = std::weak_ptr<State>(state_)](const auto&) {
            if (auto state = weak.lock(); state && state->invalidate) state->invalidate();
        });
    }
    ~Panel() override {
        state_->closed     = true;
        state_->invalidate = {};
    }
    ui::WidgetDesc build() override {
        auto state     = state_;
        state->display = std::make_shared<const std::byte>();
        std::vector<ui::WidgetDesc> rows;
        rows.push_back(
            ui::text("Runtime JSON export (independent from asset Save)", "spawn/export/title").withWrap(280));
        auto path         = ui::inputText("Relative export path", state->path, "spawn/export/path");
        path.onTextChange = action(state, [state](const std::string& text) {
            state->path = text;
            state->prepared.reset();
            state->written.clear();
            return Result<void>::success();
        });
        rows.push_back(std::move(path));
        auto prepare    = ui::button("Prepare current configuration", "spawn/export/prepare", action(state, [state] {
                                      auto candidate = PreparedSpawnExport::prepare(state->form);
                                      if (!candidate) return Result<void>::failure(candidate.status());
                                      state->prepared = std::move(candidate).takeValue();
                                      state->written.clear();
                                      return Result<void>::success();
                                  }));
        prepare.enabled = !state->busy && !state->form->history().pending;
        rows.push_back(std::move(prepare));
        std::string status = "No prepared snapshot";
        if (state->prepared) {
            status = "Prepared source revision " + std::to_string(state->prepared->sourceRevision().value());
            if (state->prepared->sourceRevision() != state->form->sourceRevision())
                status += "; source changed, writing exports this older snapshot";
        }
        rows.push_back(ui::text(status, "spawn/export/status"));
        auto write = ui::button(
            "Write prepared snapshot", "spawn/export/write", action(state, [state] {
                if (!state->prepared)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                   "Prepare a configuration snapshot first"));
                auto message = "Exported source revision " + std::to_string(state->prepared->sourceRevision().value()) +
                               " to " + state->path;
                auto result = state->prepared->write(state->filesystem, state->path);
                if (result) state->written = std::move(message);
                return result;
            }));
        write.enabled = !state->busy && state->prepared.has_value();
        rows.push_back(std::move(write));
        if (!state->written.empty()) rows.push_back(ui::text(state->written, "spawn/export/written"));
        for (std::size_t i = 0; i < state->errors.size(); ++i)
            rows.push_back(ui::text(state->errors[i].message(), "spawn/export/error/" + std::to_string(i)));
        return ui::column(std::move(rows), "spawn/export");
    }
};
}  // namespace
Result<std::unique_ptr<ui::Component>> createSpawnExportPanel(std::shared_ptr<SpawnForm>       form,
                                                              filesystem::AtomicTextWriteLease filesystem,
                                                              std::string                      initialRelativePath) {
    if (!form || !filesystem.available())
        return Result<std::unique_ptr<ui::Component>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing export form or live atomic writer"));
    return Result<std::unique_ptr<ui::Component>>::success(
        std::make_unique<Panel>(std::move(form), std::move(filesystem), std::move(initialRelativePath)));
}
}  // namespace eve::spawn_editor
