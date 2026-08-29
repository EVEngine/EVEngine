#pragma once

#include <string>
#include <vector>

namespace eve::editor {

class EditorContext;
class IEditCommand;

enum class ConstraintDisposition { Allow, Warning, Reject };

/** @brief Result of evaluating one replaceable edit constraint. */
struct ConstraintResult {
    ConstraintDisposition disposition = ConstraintDisposition::Allow;
    std::string message;
    static ConstraintResult allow() { return {}; }
    static ConstraintResult warning(std::string message);
    static ConstraintResult reject(std::string message);
};

/**
 * @brief Protocol for game and art-style rules applied before command commit.
 * Implementations may inspect or normalize the mutable command without the
 * editor core knowing the rule's concrete meaning.
 */
class IEditConstraint {
public:
    virtual ~IEditConstraint() = default;
    virtual ConstraintResult evaluate(EditorContext &context, IEditCommand &command) = 0;
};

/** @brief Ordered, non-owning constraint chain with diagnostics. */
class EditConstraintPipeline {
public:
    bool add(IEditConstraint *constraint);
    bool remove(IEditConstraint *constraint);
    void clear();
    bool evaluate(EditorContext &context, IEditCommand &command);
    int diagnosticCount() const { return static_cast<int>(diagnostics_.size()); }
    const std::string &diagnostic(int index) const;
    bool rejected() const { return rejected_; }
private:
    std::vector<IEditConstraint *> constraints_;
    std::vector<std::string> diagnostics_;
    bool rejected_ = false;
};

}  // namespace eve::editor
