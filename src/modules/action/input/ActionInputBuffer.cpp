#include "action/input/ActionInputBuffer.h"

#include <algorithm>
#include <utility>

namespace eve::action::input {
namespace {

bool matchesAllow(const std::string& input, const std::vector<std::string>& allows) {
    return std::find(allows.begin(), allows.end(), input) != allows.end();
}

}  // namespace

ActionInputBuffer::ActionInputBuffer(std::uint32_t capacity, std::uint32_t lifetimeTicks)
    : capacity_(capacity), lifetimeTicks_(lifetimeTicks) {}

Result<void> ActionInputBuffer::push(BufferedInput input) {
    if (!input.subject.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("buffered subject is nil"), std::move("subject")));
    if (input.input.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("buffered input is empty"), std::move("input")));
    if (capacity_ == 0 || lifetimeTicks_ == 0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("input buffer capacity/lifetime invalid"), std::move("capacity")));
    if (entries_.size() >= capacity_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "action input buffer is full", "buffer"));
    entries_.push_back(std::move(input));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionInputBuffer::expire(SimulationTick now) {
    while (!entries_.empty()) {
        const auto& front = entries_.front();
        if (now.value() <= front.tick.value() + lifetimeTicks_) break;
        entries_.pop_front();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<BufferedInput> ActionInputBuffer::peek(SubjectRef subject, const std::vector<std::string>& allows) const {
    const BufferedInput* best = nullptr;
    for (const auto& entry : entries_) {
        if (entry.subject != subject) continue;
        if (!matchesAllow(entry.input, allows)) continue;
        if (!best || entry.priority > best->priority ||
            (entry.priority == best->priority && entry.tick.value() < best->tick.value()))
            best = &entry;
    }
    if (!best)
        return Result<BufferedInput>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no buffered input matched", "input"));
    return Result<BufferedInput>::success(*best);
}

Result<BufferedInput> ActionInputBuffer::consume(SubjectRef subject, const std::vector<std::string>& allows) {
    auto peeked = peek(subject, allows);
    if (!peeked) return peeked;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->subject == peeked.value().subject && it->input == peeked.value().input &&
            it->tick == peeked.value().tick && it->priority == peeked.value().priority) {
            BufferedInput value = *it;
            entries_.erase(it);
            return Result<BufferedInput>::success(std::move(value), Status::success(StatusCode::Applied));
        }
    }
    return Result<BufferedInput>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "buffered input disappeared", "input"));
}

void ActionInputBuffer::clear(SubjectRef subject) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const BufferedInput& entry) { return entry.subject == subject; }),
                   entries_.end());
}

}  // namespace eve::action::input
