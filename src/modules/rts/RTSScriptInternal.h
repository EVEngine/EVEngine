#pragma once
#include "common/Value.h"
#include "rts/RTSSystems.h"
namespace ssq {
class Class;
}
namespace eve::rts::script_internal {
inline Result<SubjectRef> parseScriptSubject(std::string_view text, std::string_view path) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed)
        return Result<SubjectRef>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS script identity must be a canonical UUID", std::string(path)));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*parsed));
}

inline Result<std::vector<SubjectRef>> parseScriptSubjects(const std::vector<std::string>& texts) {
    std::vector<SubjectRef> subjects;
    subjects.reserve(texts.size());
    for (std::size_t index = 0; index < texts.size(); ++index) {
        auto subject = parseScriptSubject(texts[index], "subjects[" + std::to_string(index) + "]");
        if (!subject) return Result<std::vector<SubjectRef>>::failure(subject.status());
        subjects.push_back(std::move(subject).takeValue());
    }
    return Result<std::vector<SubjectRef>>::success(std::move(subjects));
}

inline Value fanOutValue(FanOutReceipt receipt) {
    Value::Array orderIds;
    orderIds.reserve(receipt.orderIds.size());
    for (auto& id : receipt.orderIds) orderIds.emplace_back(std::move(id));
    return Value(Value::Object{{"requested", static_cast<std::int64_t>(receipt.requested)},
                               {"accepted", static_cast<std::int64_t>(receipt.accepted)},
                               {"orderIds", Value(std::move(orderIds))}});
}

// Private binding registration; the owning RTS class controls lifetime and VM.
void exposeFormationBindings(ssq::Class& cls);
}  // namespace eve::rts::script_internal
