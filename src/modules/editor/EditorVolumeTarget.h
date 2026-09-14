#pragma once
#include "editing/EditingVolume.h"
#include "editor/EditCommand.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

using EditVolume = eve::editing::EditVolume;
using IIntVolumeTarget = eve::editing::IIntVolumeTarget;

/** @brief Reversible integer-volume command shared by voxel and custom volume targets. */
class IntVolumeEditCommand final : public IEditCommand {
public:
    /** @brief Create an unapplied command for one editable volume target. */
    IntVolumeEditCommand(std::string name, IEditableTarget* target);
    /** @brief Record a desired value without applying it. */
    bool record(int x, int y, int z, int after);
    const std::string& name() const override { return name_; }
    EditRegion dirtyRegion() const override { return dirty_.xzRegion(); }
    bool apply() override;
    void revert() override;
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    bool mergeWith(const IEditCommand& later) override;

private:
    struct Change {
        int x = 0;
        int y = 0;
        int z = 0;
        int before = 0;
        int after = 0;
    };
    std::string name_;
    IEditableTarget* target_ = nullptr;
    EditVolume dirty_;
    std::vector<Change> changes_;
};

}  // namespace eve::editor
