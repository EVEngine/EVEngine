#pragma once
#include "common/Export.h"


#include "editor/EditorResult.h"

#include <string>
#include <vector>

namespace eve::editor {

class EditorContext;
class IEditCommand;

/** @brief ConstraintDisposition public API. */
enum class ConstraintDisposition { Allow, Warning, Reject };

/** @brief Result of evaluating one replaceable edit constraint. */
struct EVENGINE_API_ORCHESTRATION ConstraintResult {
    ConstraintDisposition disposition = ConstraintDisposition::Allow;
    std::string message;
    /** @brief Allow. */
    static ConstraintResult allow() { return {}; }
    /** @brief Warning. */
    static ConstraintResult warning(std::string message);
    /** @brief Reject. */
    static ConstraintResult reject(std::string message);
};

/**
 * @brief Protocol for game and art-style rules applied before command commit.
 * Implementations may inspect or normalize the mutable command without the
 * editor core knowing the rule's concrete meaning.
 */
class IEditConstraint {
public:
    /** @brief Releases IEditConstraint resources. */
    virtual ~IEditConstraint() = default;
    /** @brief Evaluate. */
    virtual ConstraintResult evaluate(EditorContext &context, IEditCommand &command) = 0;
};

/** @brief Ordered, non-owning constraint chain with diagnostics. */
class EVENGINE_API_ORCHESTRATION EditConstraintPipeline {
public:
    /** @brief Adds . */
    bool add(IEditConstraint *constraint);
    /** @brief Removes . */
    bool remove(IEditConstraint *constraint);
    /** @brief Clears . */
    void clear();
    /** @brief Compatibility-only boolean facade over evaluateChecked(). */
    bool evaluate(EditorContext &context, IEditCommand &command);
    /** @brief Evaluate the chain and return structured allow/warning/reject diagnostics. */
    [[nodiscard]] Result<void> evaluateChecked(EditorContext &context, IEditCommand &command);
    int diagnosticCount() const { return static_cast<int>(diagnostics_.size()); }
    /** @brief Diagnostic. */
    const std::string &diagnostic(int index) const;
    /** @brief Rejected. */
    bool rejected() const { return rejected_; }
private:
    std::vector<IEditConstraint *> constraints_;
    std::vector<std::string> diagnostics_;
    std::vector<EditorDiagnostic> structuredDiagnostics_;
    bool rejected_ = false;
};

}  // namespace eve::editor
