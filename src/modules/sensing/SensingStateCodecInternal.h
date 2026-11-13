#pragma once
// Canonical subject payload shared by legacy world snapshots and detached participants.
#include "common/Value.h"
#include "sensing/Sensing.h"
namespace eve::sensing::detail {
inline std::string joinedSensingSet(const std::set<std::string>& values) {
    std::string text;
    for (const auto& value : values) {
        if (!text.empty()) text += ',';
        text += value;
    }
    return text;
}
template <class Range>
Value sensingWorldPayload(const Range& subjects) {
    Value::Array rows;
    for (const auto& subject : subjects)
        rows.push_back(Value::object({{"id", subject.id},
                                      {"x", double(subject.x)},
                                      {"y", double(subject.y)},
                                      {"z", double(subject.z)},
                                      {"faction", subject.faction},
                                      {"tags", joinedSensingSet(subject.tags)},
                                      {"visibleTo", joinedSensingSet(subject.visibleTo)},
                                      {"zones", joinedSensingSet(subject.zones)}}));
    return Value::object({{"schema", "eve.sensing.world"}, {"version", 2}, {"subjects", Value(std::move(rows))}});
}
}  // namespace eve::sensing::detail
