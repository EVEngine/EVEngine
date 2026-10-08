#pragma once
#include "common/Export.h"


/** @file SequenceAsset.h @brief Domain-neutral compiled sequence graph for `.dnut` sources. */

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve::dnut {

/** @brief Declared value kind for one sequence invocation parameter. */
enum class SequenceParameterType : std::uint8_t { Any, String, Integer, Number, Boolean };

/**
 * @brief One typed invocation parameter declared by authored content.
 *
 * The default is an owning canonical value. A null default means that no
 * default was declared; required parameters therefore always have null
 * defaults.
 */
struct SequenceParameter {
    std::string           name;
    SequenceParameterType type         = SequenceParameterType::Any;
    bool                  required     = true;
    eve::Value            defaultValue = {};
    int                   sourceLine   = 0;
    int                   sourceColumn = 0;

    SequenceParameter() = default;
    explicit SequenceParameter(std::string parameterName) : name(std::move(parameterName)) {}
};

/**
 * @brief One outgoing edge of a sequence node.
 *
 * `label` is the author-visible route identity used by `choice` nodes; for
 * `branch` nodes it is descriptive only and the route is selected by
 * `condition`. A null `condition` marks an unconditional route.
 */
struct SequenceRoute {
    std::string label;
    eve::Value  condition;
    std::string target;
    eve::Value  payload      = eve::Value::Object{};
    int         sourceLine   = 0;
    int         sourceColumn = 0;
};

/**
 * @brief One immutable step of a compiled sequence.
 *
 * `type` is a plain string so the vocabulary is owned by a `StepKindRegistry`
 * rather than by a closed enum in the compiler. Core control-flow types
 * (`branch` / `choice` / `call` / `wait` / `end`) are interpreted by the
 * runtime; every other type is dispatched to its registered handler and its
 * `payload` schema is validated by that registration.
 */
struct SequenceNode {
    std::string                id;
    std::string                type;
    std::string                next;
    eve::Value                 payload = eve::Value::Object{};
    std::vector<SequenceRoute> routes;
    int                        sourceLine   = 0;
    int                        sourceColumn = 0;
};

/** @brief Immutable, parameterized and versioned compiled sequence. */
struct EVENGINE_API_PLATFORM SequenceAsset {
    std::string              id;
    int                      version    = 1;
    bool                     repeatable = false;
    std::string              entry;
    std::vector<SequenceParameter> parameters;
    std::vector<SequenceNode> nodes;
    int                            sourceLine   = 0;
    int                            sourceColumn = 0;

    /**
     * @brief Find one node by its stable identifier.
     * @return Borrowed pointer into `nodes`; nullptr when the id is absent.
     * @ownership Borrowed; this asset owns the node.
     * @nullable Yes.
     * @lifetime Valid until this asset is destroyed or `nodes` is structurally mutated.
     * @thread Affine to this asset.
     */
    [[nodiscard]] const SequenceNode* findNode(const std::string& nodeId) const noexcept;

    /**
     * @brief Validate stable identifiers, the entry point and every graph reference.
     * @return Success, or a structured diagnostic naming the first violated rule.
     * @remarks This checks only language-level invariants. Step payload schemas and
     *          domain rules are validated by the owning `StepKindRegistry`.
     * @thread Affine to this asset; no synchronization and no callbacks.
     */
    [[nodiscard]] eve::Result<void> validate() const;
};

/**
 * @brief Return whether `type` is a control-flow type interpreted by the runtime.
 * @param type Step type name as written in the compiled asset.
 * @return True for `branch`, `choice`, `call`, `command`, `wait` and `end`.
 * @thread Reentrant and side-effect free.
 */
[[nodiscard]] bool isCoreSequenceNodeType(const std::string& type) noexcept;

/** @brief Return the stable lowercase spelling of a parameter type. */
[[nodiscard]] const char* sequenceParameterTypeName(SequenceParameterType type) noexcept;

}  // namespace eve::dnut
