#pragma once
// Private canonical source-operation implementation shared by map and template authoring.
#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <new>
#include <optional>
namespace eve::spawn_editing::detail {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "spawn.editing"));
}
template <class Compiled, class Compiler>
Result<Compiled> compileSafely(Compiler&& compiler) {
    try {
        return compiler();
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        return failure<Compiled>(DiagnosticCode::CallbackFailure, error.what());
    } catch (...) {
        return failure<Compiled>(DiagnosticCode::CallbackFailure, "Spawn content resolver threw");
    }
}
template <class Compiled>
struct DocumentData {
    struct State {
        Value    source;
        Compiled compiled;
        State(Value tree, Compiled content) : source(std::move(tree)), compiled(std::move(content)) {}
    };
    editing::TargetId                             id;
    std::function<Result<Compiled>(const Value&)> compile;
    std::shared_ptr<const State>                  state;
    std::shared_ptr<const std::byte>              identity = std::make_shared<const std::byte>();
    std::optional<editing::Revision>              stagedAt;
    mutable bool                                  busy = false;
};
struct Busy {
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
inline editing::Value payload(std::string before, std::string after) {
    return editing::Value(editing::Value::Object{{"before", editing::Value(std::move(before))},
                                                 {"after", editing::Value(std::move(after))}});
}
struct SourcePair {
    std::string before, after;
};
inline Result<SourcePair> readPair(const editing::Value& value) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object || object->size() != 2 || !object->contains("before") || !object->contains("after"))
        return failure<SourcePair>(DiagnosticCode::ParseError, "Expected exact before/after source payload");
    const auto* before = object->at("before").getIf<std::string>();
    const auto* after  = object->at("after").getIf<std::string>();
    if (!before || !after)
        return failure<SourcePair>(DiagnosticCode::ParseError, "Expected owning JSON source strings");
    return Result<SourcePair>::success({*before, *after});
}
template <class Data>
Result<editing::DomainOperation> planReplace(Data& data, const Value& input, std::string_view operationType) {
    using R = Result<editing::DomainOperation>;
    if (data.busy)
        return failure<editing::DomainOperation>(DiagnosticCode::PreconditionViolation, "Document operation reentry");
    Busy guard(data.busy);
    auto source  = input;
    auto content = data.compile(source);
    if (!content) return R::failure(content.status());
    const auto               before = data.state->compiled.canonicalJson();
    const auto               after  = content.value().canonicalJson();
    editing::DomainOperation operation;
    operation.type        = operationType;
    operation.inverseType = operationType;
    operation.target      = data.id;
    operation.payload     = payload(before, after);
    operation.inverse     = payload(after, before);
    operation.hasInverse  = true;
    operation.affectedProperties.push_back("spawn.source");
    return R::success(std::move(operation));
}
template <class Data, class PublishRevision>
Result<void> applyReplace(Data& data, const editing::DomainOperation& input, std::string_view operationType,
                          editing::Revision revision, PublishRevision publishRevision) {
    if (data.busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Document operation reentry");
    Busy guard(data.busy);
    auto operation = input;
    if (operation.target != data.id || operation.type != operationType || operation.inverseType != operationType ||
        !operation.hasInverse)
        return failure<void>(DiagnosticCode::InvalidArgument, "Unknown or irreversible spawn document operation");
    if (std::any_of(operation.affectedObjects.begin(), operation.affectedObjects.end(),
                    [&](const auto& object) { return object.target != data.id; }))
        return failure<void>(DiagnosticCode::Conflict, "Affected object belongs to another target");
    auto pair = readPair(operation.payload);
    if (!pair) return Result<void>::failure(pair.status());
    auto inverse = readPair(operation.inverse);
    if (!inverse) return Result<void>::failure(inverse.status());
    if (inverse.value().before != pair.value().after || inverse.value().after != pair.value().before)
        return failure<void>(DiagnosticCode::Conflict, "Operation inverse does not restore the source preimage");
    if (pair.value().before != data.state->compiled.canonicalJson())
        return failure<void>(DiagnosticCode::Conflict, "Document source changed since planning");
    if (pair.value().after == pair.value().before) return editing::noOp();
    if (revision == std::numeric_limits<editing::Revision>::max())
        return failure<void>(DiagnosticCode::Conflict, "Document revision exhausted");
    auto source = Value::fromJson(pair.value().after);
    if (!source) return Result<void>::failure(source.status());
    auto content = data.compile(source.value());
    if (!content) return Result<void>::failure(content.status());
    if (content.value().canonicalJson() != pair.value().after)
        return failure<void>(DiagnosticCode::ParseError, "Operation source must use canonical encoding");
    auto state =
        std::make_shared<const typename Data::State>(std::move(source).takeValue(), std::move(content).takeValue());
    data.state = std::move(state);
    publishRevision();
    return editing::applied<void>();
}
}  // namespace eve::spawn_editing::detail
