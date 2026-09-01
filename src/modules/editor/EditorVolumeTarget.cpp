#include "editor/EditorVolumeTarget.h"

#include <utility>

namespace eve::editor {

IntVolumeEditCommand::IntVolumeEditCommand(std::string name, IEditableTarget* target)
    : name_(std::move(name)), target_(target) {}

bool IntVolumeEditCommand::record(int x, int y, int z, int after) {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return false;
    for (auto& change : changes_) {
        if (change.x == x && change.y == y && change.z == z) {
            change.after = after;
            return true;
        }
    }
    const int before = volume->readInt3(x, y, z);
    if (before == after) return false;
    changes_.push_back({x, y, z, before, after});
    dirty_.include(x, y, z);
    return true;
}

bool IntVolumeEditCommand::apply() {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return false;
    bool changed = false;
    for (const auto& change : changes_)
        changed |= volume->writeInt3(change.x, change.y, change.z, change.after) ==
                   editing::FieldWriteStatus::Applied;
    return changed;
}

void IntVolumeEditCommand::revert() {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it)
        (void)volume->writeInt3(it->x, it->y, it->z, it->before);
}

std::unique_ptr<IEditCommand> IntVolumeEditCommand::clone() const {
    return std::make_unique<IntVolumeEditCommand>(*this);
}

bool IntVolumeEditCommand::mergeWith(const IEditCommand& laterBase) {
    const auto* later = dynamic_cast<const IntVolumeEditCommand*>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto& incoming : later->changes_) {
        bool merged = false;
        for (auto& current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y && current.z == incoming.z) {
                current.after = incoming.after;
                merged        = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

}  // namespace eve::editor
