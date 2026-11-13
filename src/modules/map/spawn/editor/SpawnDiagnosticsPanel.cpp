#include "map/spawn/editor/SpawnDiagnosticsPanel.h"
#include <type_traits>
#include "spawn/SpawnDiagnostics.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
namespace {
std::string phaseName(spawn::Phase phase) {
    switch (phase) {
        case spawn::Phase::Dormant: return "Dormant";
        case spawn::Phase::Running: return "Running";
        case spawn::Phase::Completed: return "Completed";
        case spawn::Phase::Faulted: return "Faulted";
    }
    return "Unknown phase";
}
std::string blockedName(spawn::BlockReason reason) {
    switch (reason) {
        case spawn::BlockReason::None: return "None";
        case spawn::BlockReason::BudgetFull: return "Budget full";
        case spawn::BlockReason::PlacementBlocked: return "Placement blocked";
        case spawn::BlockReason::ProviderUnavailable: return "Provider unavailable";
        case spawn::BlockReason::FactoryFailed: return "Factory failed";
        case spawn::BlockReason::UnexpectedRemoval: return "Unexpected removal";
        case spawn::BlockReason::ArithmeticOverflow: return "Arithmetic overflow";
    }
    return "Unknown blocking reason";
}
bool validBinding(const spawn::WorldDiagnostics& capture) {
    return !capture.owner.isNil() && capture.epoch != 0 && !capture.incarnation.isZero();
}
struct Presentation {
    std::shared_ptr<const spawn::WorldDiagnostics> capture;
    std::vector<Diagnostic>                        failures;
};
class View final : public ui::Component {
public:
    std::shared_ptr<const Presentation> state;
    explicit View(std::shared_ptr<const Presentation> initial) : state(std::move(initial)) {}
    ui::WidgetDesc build() override {
        const auto                  current = state;
        const auto&                 data    = *current->capture;
        std::vector<ui::WidgetDesc> rows;
        rows.push_back(ui::text("Captured scheduler diagnostics", "spawn/runtime/title"));
        rows.push_back(ui::text(
            "Tick: " + std::to_string(data.tick.value()) + "  Event cursor: " + std::to_string(data.sequence.value()),
            "spawn/runtime/boundary"));
        if (!current->failures.empty())
            rows.push_back(ui::text("Refresh failed; showing the last successful capture.", "spawn/runtime/stale"));
        for (std::size_t i = 0; i < current->failures.size(); ++i)
            rows.push_back(ui::text(current->failures[i].message(), "spawn/runtime/error/" + std::to_string(i)));
        for (std::size_t i = 0; i < data.budgets.size(); ++i) {
            const auto& budget = data.budgets[i];
            rows.push_back(ui::text(
                "Budget " + budget.id + ": " + std::to_string(budget.alive) + " / " + std::to_string(budget.limit),
                "spawn/runtime/budget/" + std::to_string(i)));
        }
        for (std::size_t i = 0; i < data.sources.size(); ++i) {
            const auto&                 row    = data.sources[i];
            const auto&                 source = row.state;
            std::vector<ui::WidgetDesc> fields;
            const auto                  id = "spawn/runtime/source/" + std::to_string(row.source.slot.index()) + "-" +
                            std::to_string(row.source.slot.generation());
            std::string activation = std::visit(
                [](const auto& predicate) -> std::string {
                    using T = std::decay_t<decltype(predicate)>;
                    if constexpr (std::is_same_v<T, spawn::MapLoadedActivation>)
                        return "Map loaded";
                    else if constexpr (std::is_same_v<T, spawn::EventActivation>)
                        return "Event: " + predicate.event;
                    else
                        return "Player proximity: enter " + std::to_string(predicate.enterDistance) + ", exit " +
                               std::to_string(predicate.exitDistance);
                },
                row.activation);
            fields.push_back(ui::text("Activation: " + activation, id + "/activation"));
            fields.push_back(ui::text("Phase: " + phaseName(source.phase), id + "/phase"));
            fields.push_back(ui::text("Blocked: " + blockedName(source.blocked), id + "/blocked"));
            fields.push_back(ui::text("Scheduler alive: " + std::to_string(source.alive) + " / " +
                                          std::to_string(row.aliveLimit) + "  Total: " + std::to_string(source.total),
                                      id + "/counts"));
            fields.push_back(ui::text("Wave index: " + std::to_string(source.wave) +
                                          "  Remaining: " + std::to_string(source.remaining.seconds()) + " s",
                                      id + "/schedule"));
            fields.push_back(ui::text("Budget: " + row.budget, id + "/budget"));
            if (row.templateRevision) {
                fields.push_back(
                    ui::text("Pinned template: " + row.templateRevision->definition().id, id + "/template"));
                auto source      = ui::inputText("Pinned template source", row.templateRevision->canonicalJson(),
                                                 id + "/template-source");
                source.multiline = true;
                source.readOnly  = true;
                fields.push_back(
                    ui::collapsingHeader("Pinned template content", {std::move(source)}, id + "/content", false));
            }
            rows.push_back(ui::collapsingHeader(source.id, std::move(fields), id, true));
        }
        return ui::column(std::move(rows), "spawn/runtime");
    }
};
}  // namespace
struct SpawnDiagnosticsPanel::Impl {
    View view;
    explicit Impl(std::shared_ptr<const Presentation> initial) : view(std::move(initial)) {}
};
SpawnDiagnosticsPanel::SpawnDiagnosticsPanel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnDiagnosticsPanel::~SpawnDiagnosticsPanel() = default;
Result<std::unique_ptr<SpawnDiagnosticsPanel>> SpawnDiagnosticsPanel::create(spawn::WorldDiagnostics initial) {
    using R = Result<std::unique_ptr<SpawnDiagnosticsPanel>>;
    if (!validBinding(initial))
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing captured world identity"));
    auto capture = std::make_shared<const spawn::WorldDiagnostics>(std::move(initial));
    auto state   = std::make_shared<const Presentation>(Presentation{std::move(capture), {}});
    auto impl    = std::make_unique<Impl>(std::move(state));
    return R::success(std::unique_ptr<SpawnDiagnosticsPanel>(new SpawnDiagnosticsPanel(std::move(impl))));
}
Result<void> SpawnDiagnosticsPanel::refresh(Result<spawn::WorldDiagnostics> capture) {
    auto                                           old    = impl_->view.state->capture;
    std::shared_ptr<const spawn::WorldDiagnostics> next   = old;
    auto                                           result = [&]() -> Result<void> {
        if (!capture) return Result<void>::failure(capture.status());
        const auto& candidate = capture.value();
        if (candidate.owner != old->owner || candidate.epoch != old->epoch ||
            candidate.incarnation != old->incarnation || candidate.tick.value() < old->tick.value() ||
            candidate.sequence.value() < old->sequence.value())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Stale or foreign diagnostic capture; rebind a new world"));
        next = std::make_shared<const spawn::WorldDiagnostics>(std::move(capture).takeValue());
        return Result<void>::success();
    }();
    auto state = std::make_shared<const Presentation>(Presentation{std::move(next), result.diagnostics()});
    impl_->view.markDirty();
    impl_->view.state = std::move(state);
    return result;
}
ui::Component&                                 SpawnDiagnosticsPanel::component() noexcept { return impl_->view; }
std::shared_ptr<const spawn::WorldDiagnostics> SpawnDiagnosticsPanel::snapshot() const noexcept {
    return impl_->view.state->capture;
}
}  // namespace eve::spawn_editor
