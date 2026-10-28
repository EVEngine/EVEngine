#pragma once
#include "common/Export.h"


#include "editor/EditorTarget.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Reversible unit accepted by the editor transaction manager. */
class IEditCommand {
public:
    /** @brief Releases IEditCommand resources. */
    virtual ~IEditCommand() = default;
    /** @brief Name. */
    virtual const std::string &name() const = 0;
    /** @brief Applies . */
    virtual bool apply() = 0;
    /** @brief Revert. */
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
    /** @brief Dirty region. */
    virtual EditRegion dirtyRegion() const = 0;
};

/** @brief IntFieldChange public API. */
struct IntFieldChange { int x = 0, y = 0, before = 0, after = 0; };

/** @brief Reversible edits to any target exposing IIntFieldTarget. */
class EVENGINE_API_ORCHESTRATION IntFieldEditCommand final : public IEditCommand {
public:
    /** @brief Int field edit command. */
    IntFieldEditCommand(std::string name, IEditableTarget *target);
    /** @brief Name. */
    const std::string &name() const override { return name_; }
    /** @brief Record. */
    bool record(int x, int y, int after);
    /** @brief Change count. */
    int changeCount() const { return static_cast<int>(changes_.size()); }
    /** @brief Applies . */
    bool apply() override;
    /** @brief Revert. */
    void revert() override;
    /** @brief Deep copy. @ownership Caller deletes. */
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    /** @brief Merge with. */
    bool mergeWith(const IEditCommand &later) override;
    /** @brief Dirty region. */
    EditRegion dirtyRegion() const override { return dirty_; }
private:
    std::string name_;
    IEditableTarget *target_ = nullptr;
    std::vector<IntFieldChange> changes_;
    EditRegion dirty_;
};

/** @brief ScalarFieldChange public API. */
struct ScalarFieldChange { int x = 0, y = 0; float before = 0.f, after = 0.f; };

/** @brief Reversible edits to any target exposing IScalarFieldTarget. */
class EVENGINE_API_ORCHESTRATION ScalarFieldEditCommand final : public IEditCommand {
public:
    /** @brief Scalar field edit command. */
    ScalarFieldEditCommand(std::string name, IEditableTarget *target);
    /** @brief Name. */
    const std::string &name() const override { return name_; }
    /** @brief Record. */
    bool record(int x, int y, float after);
    /** @brief Change count. */
    int changeCount() const { return static_cast<int>(changes_.size()); }
    /** @brief Applies . */
    bool apply() override;
    /** @brief Revert. */
    void revert() override;
    /** @brief Deep copy. @ownership Caller deletes. */
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    /** @brief Merge with. */
    bool mergeWith(const IEditCommand &later) override;
    /** @brief Dirty region. */
    EditRegion dirtyRegion() const override { return dirty_; }
private:
    std::string name_;
    IEditableTarget *target_ = nullptr;
    std::vector<ScalarFieldChange> changes_;
    EditRegion dirty_;
};

}  // namespace eve::editor
