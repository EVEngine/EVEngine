#include "map/spawn/editor/SpawnFormPanel.h"
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include "SpawnFieldProjection.inl"
#include "map/spawn/editor/SpawnForm.h"
#include "property_access/PropertyAccess.h"
#include "ui/Component.h"
#include "ui/PropertyView.h"
namespace eve::spawn_editor {
namespace {
// Presentation-only diagnostics; source/history remain in the actual form/model/backend.
struct PanelState final : property_access::IPropertyAccess {
    std::shared_ptr<SpawnForm> form;
    std::vector<Diagnostic>    diagnostics;
    std::function<void()>      invalidate;
    detail::FieldProjection    fields;
    struct Draft {
        Value         source;
        std::uint64_t base;
    };
    bool committing = false;
    struct CommitGuard {
        bool& active;
        explicit CommitGuard(bool& flag) : active(flag) { active = true; }
        ~CommitGuard() { active = false; }
    };
    std::optional<Draft>               draft;
    std::shared_ptr<const std::byte>   stamp = std::make_shared<const std::byte>();
    std::map<std::string, std::string> arrayInputs;
    std::optional<Value>               currentSource() const {
        return draft ? std::optional<Value>(draft->source) : form->view().read("$source");
    }
    Result<void> replaceCandidate(Value source) {
        if (form->history().pending)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Retry or discard the pending commit before editing"));
        if (!draft) return form->view().write("$source", source);
        auto next     = std::make_shared<const std::byte>();
        draft->source = std::move(source);
        stamp         = std::move(next);
        return Result<void>::success();
    }
    Result<void> beginDraft() {
        if (draft || form->history().pending)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Finish the current edit first"));
        auto source = form->view().read("$source");
        if (!source) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Missing form source"));
        auto next = std::make_shared<const std::byte>();
        draft.emplace(Draft{std::move(*source), revision()});
        stamp = std::move(next);
        return Result<void>::success();
    }
    Result<void> cancelDraft() {
        if (form->history().pending)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Discard or retry the pending commit first"));
        auto next = std::make_shared<const std::byte>();
        draft.reset();
        stamp = std::move(next);
        return Result<void>::success();
    }
    Result<void> applyDraft() {
        if (!draft || revision() != draft->base || form->history().pending)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Draft base changed or a commit is pending"));
        CommitGuard guard(committing);
        auto        next   = std::make_shared<const std::byte>();
        auto        result = form->view().write("$source", draft->source);
        if (result) {
            draft.reset();
            stamp = std::move(next);
        }
        return result;
    }
    Result<void> changeArray(const detail::FieldPath& path, std::optional<std::size_t> remove,
                             const std::string& input) {
        if (!draft)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "Start a structure draft first"));
        auto  candidate = draft->source;
        auto* value     = detail::locateField(candidate, path);
        auto* array     = value ? value->getIf<Value::Array>() : nullptr;
        if (!array || (remove && *remove >= array->size()))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Array location changed"));
        if (remove)
            array->erase(array->begin() + static_cast<std::ptrdiff_t>(*remove));
        else {
            auto parsed = Value::fromJson(input);
            if (!parsed) return Result<void>::failure(parsed.status());
            array->push_back(std::move(parsed).takeValue());
        }
        return replaceCandidate(std::move(candidate));
    }
    void retryPending() {
        CommitGuard guard(committing);
        auto        next   = std::make_shared<const std::byte>();
        auto        result = form->retry();
        if (result) {
            draft.reset();
            stamp = std::move(next);
        }
        record(std::move(result));
    }
    explicit PanelState(std::shared_ptr<SpawnForm> owner) : form(std::move(owner)) {}
    const property_access::PropertySchema& schema() const override { return fields.schema; }
    void                                   refreshFields() {
        auto source = currentSource();
        if (source) fields = detail::projectFields(form->view().schema(), std::move(*source));
    }
    std::optional<Value> read(const std::string& path) const override {
        if (path == "$source") return currentSource();
        const auto binding = fields.paths.find(path);
        if (binding == fields.paths.end()) return form->view().read(path);
        const auto* value = detail::locateField(fields.source, binding->second);
        if (!value) return std::nullopt;
        if (binding->second.size() > 1 && value->isNumeric()) {
            auto encoded = value->toJson();
            if (!encoded) return std::nullopt;
            return Value(encoded.value());
        }
        return *value;
    }
    Result<void> write(const std::string& path, const Value& value) override {
        auto result = [&]() -> Result<void> {
            if (form->history().pending)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Retry or discard the pending commit before editing"));
            const auto binding = fields.paths.find(path);
            if (path == "$source") return replaceCandidate(value);
            if (binding == fields.paths.end()) return form->view().write(path, value);
            if (!draft && binding->second.size() == 1) return form->view().write(path, value);
            auto source = currentSource();
            if (!source)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Missing form source"));
            auto* field = detail::locateField(*source, binding->second);
            if (!field)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Form field no longer exists"));
            if (field->isNumeric() && value.isString()) {
                auto parsed = Value::fromJson(value.asString());
                if (!parsed) return Result<void>::failure(parsed.status());
                if (!parsed.value().isNumeric())
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected a JSON number"));
                *field = std::move(parsed).takeValue();
            } else
                *field = value;
            return replaceCandidate(std::move(*source));
        }();
        diagnostics = result.diagnostics();
        if (invalidate) invalidate();
        return result;
    }
    std::uint64_t                 revision() const override { return form->view().revision(); }
    property_access::Subscription subscribe(ChangeCallback callback) override {
        return form->view().subscribe(std::move(callback));
    }
    template <class T>
    void record(Result<T> result) {
        diagnostics = result.diagnostics();
        if (invalidate) invalidate();
    }
    bool accepts(std::uint64_t builtAt, const std::shared_ptr<const std::byte>& builtStamp) {
        if (!committing && revision() == builtAt && stamp == builtStamp) return true;
        diagnostics = {Diagnostic::error(DiagnosticCode::Conflict, "This form changed. Use the refreshed controls.")};
        if (invalidate) invalidate();
        return false;
    }
};
template <class... Args>
void protect(std::function<void(Args...)>& callback, const std::shared_ptr<PanelState>& state, std::uint64_t revision) {
    if (!callback) return;
    callback = [owner = state, action = std::move(callback), revision, builtStamp = state->stamp](Args... args) {
        if (owner->accepts(revision, builtStamp)) action(std::forward<Args>(args)...);
    };
}
void protectTree(ui::WidgetDesc& node, const std::shared_ptr<PanelState>& state, std::uint64_t revision) {
    protect(node.onClick, state, revision);
    protect(node.onToggle, state, revision);
    protect(node.onValue, state, revision);
    protect(node.onTextChange, state, revision);
    for (auto& child : node.children) protectTree(child, state, revision);
}
class Panel final : public SpawnFormPanel {
public:
    explicit Panel(std::shared_ptr<SpawnForm> form) : state_(std::make_shared<PanelState>(std::move(form))) {
        state_->invalidate = [this] { markDirty(); };
        subscription_      = state_->subscribe([weak = std::weak_ptr<PanelState>(state_)](const auto&) {
            if (auto state = weak.lock(); state && state->invalidate) state->invalidate();
        });
    }
    ~Panel() override { state_->invalidate = {}; }
    bool           hasStructureDraft() const noexcept override { return state_->draft.has_value(); }
    ui::WidgetDesc build() override {
        const auto state = state_;
        state->refreshFields();
        const auto history = state->form->history();
        auto       undo    = ui::button("Undo", "spawn/undo", [state] { state->record(state->form->undo()); });
        undo.enabled       = history.canUndo && !history.pending && !state->draft;
        auto redo          = ui::button("Redo", "spawn/redo", [state] { state->record(state->form->redo()); });
        redo.enabled       = history.canRedo && !history.pending && !state->draft;
        auto retry         = ui::button("Retry", "spawn/retry", [state] { state->retryPending(); });
        retry.enabled      = history.pending;
        auto discard =
            ui::button("Discard pending edit", "spawn/discard", [state] { state->record(state->form->discard()); });
        discard.enabled = history.pending;
        ui::PropertyViewOptions options;
        options.idPrefix       = "spawn/field/";
        options.showEditorOnly = true;
        options.title          = "Spawn configuration";
        std::vector<ui::WidgetDesc> rows;
        auto                        historyBar =
            ui::toolbar({std::move(undo), std::move(redo), std::move(retry), std::move(discard)}, "spawn/history");
        historyBar.alignSelf = static_cast<int>(ui::FlexAlign::Stretch);
        rows.push_back(std::move(historyBar));
        auto begin = ui::button("Edit structure", "spawn/draft/begin", [state] { state->record(state->beginDraft()); });
        begin.enabled = !state->draft && !history.pending;
        auto apply    = ui::button("Apply draft", "spawn/draft/apply", [state] { state->record(state->applyDraft()); });
        apply.enabled = bool(state->draft) && !history.pending;
        auto cancel =
            ui::button("Cancel draft", "spawn/draft/cancel", [state] { state->record(state->cancelDraft()); });
        cancel.enabled = bool(state->draft) && !history.pending;
        auto draftBar  = ui::toolbar({std::move(begin), std::move(apply), std::move(cancel)}, "spawn/draft/actions");
        draftBar.alignSelf = static_cast<int>(ui::FlexAlign::Stretch);
        rows.push_back(std::move(draftBar));
        if (state->draft) {
            rows.push_back(ui::text("Editing an unpublished candidate. Apply validates the complete configuration.",
                                    "spawn/draft/status"));
            for (const auto& array : state->fields.arrays) {
                const auto                  key   = detail::fieldKey(array.path);
                const auto*                 value = detail::locateField(state->fields.source, array.path);
                const auto&                 items = *value->getIf<Value::Array>();
                std::vector<ui::WidgetDesc> controls;
                for (std::size_t index = 0; index < items.size(); ++index) {
                    controls.push_back(ui::button("Duplicate " + array.label + "[" + std::to_string(index) + "]",
                                                  "spawn/array/" + key + "/duplicate/" + std::to_string(index),
                                                  [state, path = array.path, item = items[index]] {
                                                      auto encoded = item.toJson();
                                                      if (!encoded) {
                                                          state->record(std::move(encoded));
                                                          return;
                                                      }
                                                      state->record(state->changeArray(path, {}, encoded.value()));
                                                  }));
                    controls.push_back(ui::button(
                        "Remove " + array.label + "[" + std::to_string(index) + "]",
                        "spawn/array/" + key + "/remove/" + std::to_string(index),
                        [state, path = array.path, index] { state->record(state->changeArray(path, index, {})); }));
                }
                controls.push_back(ui::inputText(
                    "New item JSON for " + array.label, state->arrayInputs[key], "spawn/array/" + key + "/input",
                    [state, key](const std::string& text) { state->arrayInputs[key] = text; }));
                controls.push_back(
                    ui::button("Add to " + array.label, "spawn/array/" + key + "/add", [state, path = array.path, key] {
                        state->record(state->changeArray(path, {}, state->arrayInputs[key]));
                    }));
                rows.push_back(
                    ui::collapsingHeader(array.label + " items", std::move(controls), "spawn/array/" + key, false));
            }
        }
        for (std::size_t i = 0; i < state->diagnostics.size(); ++i)
            rows.push_back(ui::text(state->diagnostics[i].message(), "spawn/diagnostic/" + std::to_string(i)));
        rows.push_back(ui::buildPropertyView(*state, options));
        if (const auto source = state->read("$source")) {
            auto encoded = source->toJson();
            if (encoded) {
                auto json      = ui::inputText("Complete configuration JSON", encoded.value(), "spawn/source-json",
                                               [state](const std::string& text) {
                                              auto parsed = Value::fromJson(text);
                                              if (!parsed) {
                                                  state->record(std::move(parsed));
                                                  return;
                                              }
                                              state->record(state->write("$source", parsed.value()));
                                          });
                json.multiline = true;
                rows.push_back(std::move(json));
            } else
                state->diagnostics = encoded.diagnostics();
        }

        auto tree = ui::column(std::move(rows), "spawn/form");
        protectTree(tree, state, state->revision());
        return tree;
    }

private:
    // Subscription detaches before the last panel owner; widget trees independently retain state/form.
    std::shared_ptr<PanelState>   state_;
    property_access::Subscription subscription_;
};
}  // namespace
SpawnFormPanel::~SpawnFormPanel() = default;
Result<std::unique_ptr<SpawnFormPanel>> createSpawnFormPanel(std::shared_ptr<SpawnForm> form) {
    if (!form)
        return Result<std::unique_ptr<SpawnFormPanel>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing Spawn form owner"));
    return Result<std::unique_ptr<SpawnFormPanel>>::success(std::make_unique<Panel>(std::move(form)));
}
}  // namespace eve::spawn_editor
