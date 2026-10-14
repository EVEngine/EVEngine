#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "common/Export.h"

namespace eve::script {
/** @brief Owning syntax expression; dynamic means outside the supported expression subset. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptExpression {
    std::string                   kind, value;
    std::vector<ScriptExpression> operands;
    unsigned                      line = 1, column = 1;
    size_t                        begin = 0, end = 0;
};
/** @brief Function parameter identity and optional erased source type. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptParameterSyntax {
    std::string name, type;
};
/** @brief Named function body and source parameters; offsets address the original source. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptFunctionSyntax {
    std::string                        name;
    std::vector<ScriptParameterSyntax> parameters;
    unsigned                           line  = 1;
    size_t                             begin = 0, end = 0;
};
/** @brief Source binding; local bindings remain in their owning function scope. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptBindingSyntax {
    std::string      name, scope, declaredType;
    bool             local = false;
    ScriptExpression expression;
};
/** @brief Call expression and an immediately adjacent literal comparison, if present. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptCallSyntax {
    std::string      scope, comparison;
    ScriptExpression expression, comparedValue;
};
/** @brief Named class body retained for field and method reflection. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptClassSyntax {
    std::string name;
    unsigned    line  = 1;
    size_t      begin = 0, end = 0;
};
/** @brief Bounded structural analysis, independent of VM execution and binding resolution. */
struct EVENGINE_API_FOUNDATION_INLINE ScriptStructure {
    std::vector<ScriptClassSyntax>    classes;
    std::vector<ScriptFunctionSyntax> functions;
    std::vector<ScriptBindingSyntax>  bindings;
    std::vector<ScriptCallSyntax>     calls;
    std::vector<std::string>          diagnostics;
};
/**
 * @brief Parse named functions, lexical bindings and call/member/literal expressions.
 * @param source Original EveScript source; comments and quoted text are lexed, never regex-matched as code.
 * @return Owning syntax records. Unsupported expressions remain dynamic; malformed delimiters report diagnostics.
 * @ownership Returned records own text and contain no VM or world pointers.
 * @lifetime Independent of the source view and compiler.
 * @thread Pure analysis; safe for independent callers. Token, depth and record budgets bound work.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION ScriptStructure analyzeScriptStructure(std::string_view source);
/**
 * @brief Build-generated source fingerprint of the structural analyzer and project projection.
 * @return Immutable source evidence hash; not a runtime frame or whole-binary identity.
 * @ownership Borrowed process-owned constant text.
 * @lifetime Valid until process exit.
 * @thread Immutable reads; no filesystem access or callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view scriptStructureProviderHash();
}  // namespace eve::script
