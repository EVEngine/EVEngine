#pragma once

#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Context supplied to project- and document-level validation rules. */
struct ValidationRequest {
    std::string subject;
    EditorValue value;
};

/** @brief Registry of composable validation rules owned by extensions. */
class EditorValidationService {
public:
    using Rule = std::function<std::vector<EditorDiagnostic>(const ValidationRequest&)>;

    /** @brief Register or replace a stable rule owned by an extension. */
    EditorResult<void> registerRule(std::string owner, RuleId id, Rule rule);
    /** @brief Remove every rule owned by an unloading extension. */
    std::size_t unregisterOwner(const std::string& owner);
    /** @brief Execute all registered rules in deterministic rule-id order. */
    std::vector<EditorDiagnostic> validate(const ValidationRequest& request) const;

private:
    struct RegisteredRule {
        std::string owner;
        Rule        rule;
    };
    std::map<RuleId, RegisteredRule> rules_;
};

/** @brief Thread-safe diagnostic channels consumed uniformly by UI, scripts and MCP. */
class EditorDiagnosticService {
public:
    /** @brief Replace one producer's current diagnostics atomically. */
    void publish(std::string source, std::vector<EditorDiagnostic> diagnostics);
    /** @brief Clear one producer channel. */
    void clear(const std::string& source);
    /** @brief Return all channels in deterministic source order. */
    std::vector<EditorDiagnostic> snapshot() const;

private:
    mutable std::mutex                                   mutex_;
    std::map<std::string, std::vector<EditorDiagnostic>> channels_;
};

}  // namespace eve::editor
