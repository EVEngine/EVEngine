#include "editor/EditorQueueInspector.h"

#include "orders/CommandQueue.h"
#include "production/Production.h"

#include <cmath>

namespace eve::editor {
namespace {
bool matches(const std::string& actual, const std::string& filter) { return filter.empty() || actual == filter; }
}

QueueInspectionSnapshot RuntimeQueueInspector::capture(const orders::CommandQueue& queue,
                                                        const std::string& state,
                                                        const std::string& kind) const {
    QueueInspectionSnapshot result; result.domain = "orders";
    for (int i = 0; i < queue.orderCount(); ++i) {
        const auto borrowed = queue.orderAt(i); const auto* order = borrowed ? &borrowed->get() : nullptr;
        if (!order) continue;
        const std::string stateName = orders::stateName(order->state);
        if (!matches(stateName, state) || !matches(order->kind, kind)) continue;
        result.items.push_back({order->id, {}, order->kind, {}, stateName, order->priority,
                                order->elapsedSeconds, order->timeoutSeconds, order->payload.toJson()});
        if (!std::isfinite(order->elapsedSeconds) || order->elapsedSeconds < 0.0 ||
            !std::isfinite(order->timeoutSeconds) || order->timeoutSeconds < 0.0)
            result.diagnostics.push_back({RuleId("editor.queue.invalid-order-time"), DiagnosticSeverity::Error,
                                          "Order " + order->id + " has invalid timing metadata"});
        if (order->timeoutSeconds > 0.0 && order->elapsedSeconds > order->timeoutSeconds &&
            order->state != orders::OrderState::Failed)
            result.diagnostics.push_back({RuleId("editor.queue.overdue-order"), DiagnosticSeverity::Warning,
                                          "Order " + order->id + " exceeded its timeout without failing"});
    }
    for (int i = 0; i < queue.eventCount(); ++i) {
        const auto borrowed = queue.eventAt(i);
        const auto* event = borrowed ? &borrowed->get() : nullptr;
        if (!event) continue;
        result.events.push_back({event->sequence, 0, event->orderId, event->kind,
                                 orders::stateName(event->from), orders::stateName(event->to), event->reason});
    }
    return result;
}

QueueInspectionSnapshot RuntimeQueueInspector::capture(const production::WorkQueue& queue,
                                                        const std::string& owner,
                                                        const std::string& state,
                                                        const std::string& kind) const {
    QueueInspectionSnapshot result; result.domain = "production";
    for (int i = 0; i < queue.taskCount(); ++i) {
        const auto borrowed = queue.taskAt(i); const auto* task = borrowed ? &borrowed->get() : nullptr;
        if (!task) continue;
        const std::string stateName(production::taskStateName(task->state));
        if (!matches(task->owner, owner) || !matches(stateName, state) || !matches(task->kind, kind)) continue;
        const double progress = task->progress.seconds(); const double duration = task->duration.seconds();
        result.items.push_back({task->id, task->owner, task->kind, task->product, stateName,
                                task->priority, progress, duration, {}});
        if (!std::isfinite(progress) || !std::isfinite(duration) || progress < 0.0 || duration <= 0.0 ||
            progress > duration)
            result.diagnostics.push_back({RuleId("editor.queue.invalid-production-progress"),
                DiagnosticSeverity::Error, "Production task " + task->id + " has invalid progress"});
    }
    for (int i = 0; i < queue.eventCount(); ++i) {
        const auto borrowed = queue.eventAt(i); const auto* event = borrowed ? &borrowed->get() : nullptr;
        if (!event) continue;
        result.events.push_back({event->sequence, event->tick.value(), event->taskId,
                                 std::string(production::eventKindName(event->kind)), {}, {}, event->reason});
    }
    return result;
}

}  // namespace eve::editor
