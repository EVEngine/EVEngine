#pragma once

#include "common/Export.h"

#include <squirrel.h>

#include <string>
#include <vector>

namespace eve::script {

/** @brief One call-stack frame captured while the script error is still live. */
struct EVENGINE_API ScriptFrame {
    std::string source;    /**< @brief Source name of the frame ("main.nut", "buffer", ...). */
    std::string function;  /**< @brief Function name, when Squirrel debug info knows it. */
    int line = -1;         /**< @brief 1-based source line; -1 when unknown. */
};

/**
 * @brief Structured snapshot of a script error: message, throw site and stack.
 *
 * Captured from inside the Squirrel error handler while the native call stack
 * is still intact, so the throw-site line and every caller frame are known.
 */
struct EVENGINE_API ScriptErrorContext {
    std::string message;   /**< @brief Raw error value ("kaboom", "42", ...). */
    std::string source;    /**< @brief Source of the throwing frame. */
    std::string function;  /**< @brief Function of the throwing frame. */
    int line = -1;         /**< @brief 1-based throw-site line; -1 when unknown. */
    int column = -1;       /**< @brief 1-based column (compile errors only). */
    std::string hint;      /**< @brief Optional source snippet shown under the message. */
    std::vector<ScriptFrame> stack;  /**< @brief Call stack, innermost frame first. */
    bool reported = false; /**< @brief True when a reporter already handled this error. */

    /** @brief True when no error payload was captured. */
    bool empty() const noexcept { return message.empty() && stack.empty(); }
};

/**
 * @brief Captures the pending runtime error from inside the Squirrel error handler.
 * @param vm VM whose error handler is executing (error value at stack slot 2).
 * @return Context with the message and the still-live call stack.
 */
EVENGINE_API ScriptErrorContext captureScriptError(HSQUIRRELVM vm);

/**
 * @brief Captures the last compilation error recorded by the VM.
 * @param vm VM whose compile just failed (ssq's compiler handler stores it).
 * @return Context with message, source, line and column.
 */
EVENGINE_API ScriptErrorContext captureCompileError(HSQUIRRELVM vm);

/**
 * @brief Parses the ssq compile message "Compile error at source:line:column msg".
 * @param text    Message produced by ssq::CompileException::what().
 * @param source  Receives the source name (may be nullptr).
 * @param line    Receives the 1-based line (may be nullptr).
 * @param column  Receives the 1-based column (may be nullptr).
 * @param message Receives the raw compiler description (may be nullptr).
 * @return True when the message matched the expected layout.
 */
EVENGINE_API bool parseCompileError(const std::string& text, std::string* source,
                                    int* line, int* column, std::string* message);

/**
 * @brief Extracts one 1-based line from script source text.
 * @param sourceText Full script source.
 * @param line       1-based line number.
 * @return The line without trailing newline, or empty when out of range.
 */
EVENGINE_API std::string sourceLineText(const std::string& sourceText, int line);

/**
 * @brief Formats a context into a human-readable multi-line report.
 * @param ctx Context to format.
 * @return "source:line (function): message" plus optional hint and stack.
 */
EVENGINE_API std::string formatScriptError(const ScriptErrorContext& ctx);

/**
 * @brief Formats just the call-stack portion of a context.
 * @param frames Frames to render, innermost first.
 * @return One "source:line in function" line per frame, newline separated.
 */
EVENGINE_API std::string formatStackTrace(const std::vector<ScriptFrame>& frames);

/**
 * @brief Records the last error for a VM (thread-local, synchronous consumers).
 * @param vm VM the error belongs to.
 * @param ctx Context to store.
 */
EVENGINE_API void setLastScriptError(HSQUIRRELVM vm, ScriptErrorContext ctx);

/**
 * @brief Consumes and clears the last recorded error for a VM.
 * @param vm VM the error belongs to.
 * @return The stored context, or an empty context when none is pending.
 */
EVENGINE_API ScriptErrorContext takeLastScriptError(HSQUIRRELVM vm);

/**
 * @brief Peeks at the last recorded error for a VM without clearing it.
 * @param vm VM the error belongs to.
 * @return Pointer to the stored context, or nullptr when none is pending.
 */
EVENGINE_API const ScriptErrorContext* peekLastScriptError(HSQUIRRELVM vm);

/**
 * @brief Drops any recorded error for a VM.
 * @param vm VM whose pending error should be forgotten.
 */
EVENGINE_API void clearLastScriptError(HSQUIRRELVM vm);

}  // namespace eve::script
