#pragma once

#include "editor/EditorTarget.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Reversible unit accepted by the editor transaction manager. */
class IEditCommand {
public:
    virtual ~IEditCommand() = default;
    virtual const std::string &name() const = 0;
    virtual bool apply() = 0;
    virtual void revert() = 0;
    /**
     * @brief Creates an owning staged copy for merge evaluation.
     * @return A detached command copy, or null when this command has no
     *         candidate-merge representation.
     * @remarks The copy may retain the same borrowed target because the
     *          consumer evaluates mergeWith() before publishing the incoming
     *          command to that target. It must not mutate the target itself.
     */
    [[nodiscard]] virtual std::unique_ptr<IEditCommand> clone() const { return nullptr; }
    /** @brief Optionally absorb a later command from the same stroke. */
    virtual bool mergeWith(const IEditCommand &later) {
        (void)later;
        return false;
    }
    virtual EditRegion dirtyRegion() const = 0;
};

struct IntFieldChange { int x = 0, y = 0, before = 0, after = 0; };

/** @brief Reversible edits to any target exposing IIntFieldTarget. */
class IntFieldEditCommand final : public IEditCommand {
public:
    IntFieldEditCommand(std::string name, IEditableTarget *target);
    const std::string &name() const override { return name_; }
    bool record(int x, int y, int after);
    int changeCount() const { return static_cast<int>(changes_.size()); }
    bool apply() override;
    void revert() override;
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    bool mergeWith(const IEditCommand &later) override;
    EditRegion dirtyRegion() const override { return dirty_; }
private:
    std::string name_;
    IEditableTarget *target_ = nullptr;
    std::vector<IntFieldChange> changes_;
    EditRegion dirty_;
};

struct ScalarFieldChange { int x = 0, y = 0; float before = 0.f, after = 0.f; };

/** @brief Reversible edits to any target exposing IScalarFieldTarget. */
class ScalarFieldEditCommand final : public IEditCommand {
public:
    ScalarFieldEditCommand(std::string name, IEditableTarget *target);
    const std::string &name() const override { return name_; }
    bool record(int x, int y, float after);
    int changeCount() const { return static_cast<int>(changes_.size()); }
    bool apply() override;
    void revert() override;
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    bool mergeWith(const IEditCommand &later) override;
    EditRegion dirtyRegion() const override { return dirty_; }
private:
    std::string name_;
    IEditableTarget *target_ = nullptr;
    std::vector<ScalarFieldChange> changes_;
    EditRegion dirty_;
};

}  // namespace eve::editor
