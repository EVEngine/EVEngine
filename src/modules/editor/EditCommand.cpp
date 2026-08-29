#include "editor/EditCommand.h"

#include <utility>

namespace eve::editor {

IntFieldEditCommand::IntFieldEditCommand(std::string name, IEditableTarget *target)
    : name_(std::move(name)), target_(target) {}
bool IntFieldEditCommand::record(int x, int y, int after) {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field || !field->inBounds(x, y)) return false;
    for (auto &change : changes_) {
        if (change.x == x && change.y == y) { change.after = after; return true; }
    }
    const int before = field->readInt(x, y);
    if (before == after) return false;
    changes_.push_back({x, y, before, after});
    dirty_.include(x, y);
    return true;
}
bool IntFieldEditCommand::apply() {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field) return false;
    for (const auto &change : changes_) field->writeInt(change.x, change.y, change.after);
    return !changes_.empty();
}
void IntFieldEditCommand::revert() {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) field->writeInt(it->x, it->y, it->before);
}
std::unique_ptr<IEditCommand> IntFieldEditCommand::clone() const {
    return std::make_unique<IntFieldEditCommand>(*this);
}
bool IntFieldEditCommand::mergeWith(const IEditCommand &laterBase) {
    auto *later = dynamic_cast<const IntFieldEditCommand *>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto &incoming : later->changes_) {
        bool merged = false;
        for (auto &current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y) {
                current.after = incoming.after;
                merged = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

ScalarFieldEditCommand::ScalarFieldEditCommand(std::string name, IEditableTarget *target)
    : name_(std::move(name)), target_(target) {}
bool ScalarFieldEditCommand::record(int x, int y, float after) {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field || !field->inBounds(x, y)) return false;
    for (auto &change : changes_) {
        if (change.x == x && change.y == y) { change.after = after; return true; }
    }
    const float before = field->readScalar(x, y);
    if (before == after) return false;
    changes_.push_back({x, y, before, after});
    dirty_.include(x, y);
    return true;
}
bool ScalarFieldEditCommand::apply() {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field) return false;
    for (const auto &change : changes_) field->writeScalar(change.x, change.y, change.after);
    return !changes_.empty();
}
void ScalarFieldEditCommand::revert() {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) field->writeScalar(it->x, it->y, it->before);
}
std::unique_ptr<IEditCommand> ScalarFieldEditCommand::clone() const {
    return std::make_unique<ScalarFieldEditCommand>(*this);
}
bool ScalarFieldEditCommand::mergeWith(const IEditCommand &laterBase) {
    auto *later = dynamic_cast<const ScalarFieldEditCommand *>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto &incoming : later->changes_) {
        bool merged = false;
        for (auto &current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y) {
                current.after = incoming.after;
                merged = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

}  // namespace eve::editor
