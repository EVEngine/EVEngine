#include "spawn/SpawnDefinition.h"
#include "spawn/SpawnDecode.inl"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace eve::spawn {
namespace {
using detail::identifier;
using detail::InvalidInput;
using detail::Reader;
using detail::reject;
DefinitionRef reference(const Reader& reader, const IUnitDefinitionResolver& resolver) {
    reader.fields({"type", "id"});
    auto parsed = DefinitionRef::parse(reader.at("type").text() + ":" + reader.at("id").text());
    if (!parsed) reject(reader.path, "Invalid definition reference");
    auto result   = std::move(parsed).takeValue();
    auto resolved = resolver.validate(result);
    if (!resolved) {
        const auto* diagnostic = resolved.status().primaryDiagnostic();
        reject(reader.path, diagnostic == nullptr ? "Unresolved unit definition" : diagnostic->message(),
               diagnostic == nullptr ? DiagnosticCode::NotFound : diagnostic->code());
    }
    return result;
}
std::vector<Member> members(const Reader& reader, bool weighted, const IUnitDefinitionResolver& resolver) {
    std::vector<Member>   result;
    std::set<std::string> seen;
    std::uint64_t         sum = 0;
    for (const auto& item : reader.array()) {
        Reader entry{item, reader.path + "[" + std::to_string(result.size()) + "]"};
        Member member;
        if (weighted) {
            entry.fields({"unit", "weight"});
            member.weight = entry.at("weight").integer(true);
            member.unit   = reference(entry.at("unit"), resolver);
            member.id     = member.unit.format();
            sum += member.weight;
        } else {
            entry.fields({"id", "unit", "count"});
            member.id    = identifier(entry.at("id"));
            member.unit  = reference(entry.at("unit"), resolver);
            member.count = entry.at("count").integer();
            sum += member.count;
        }
        if (!seen.insert(member.id).second) reject(entry.path, "Duplicate member or weighted unit");
        if (sum > std::numeric_limits<std::uint32_t>::max()) reject(reader.path, "Member sum overflows");
        result.push_back(std::move(member));
    }
    if (sum == 0) reject(reader.path, "All weights are zero");
    return result;
}
std::uint64_t memberCount(const std::vector<Member>& members) {
    std::uint64_t result = 0;
    for (const auto& member : members) result += member.count;
    return result;
}
TemplateDefinition decode(const Reader& root, const IUnitDefinitionResolver& resolver) {
    root.fields({"schema", "version", "id", "content", "schedule", "limits", "placement", "batch_policy", "failure",
                 "extensions"});
    root.at("schema").literal("eve.spawn-template");
    if (root.at("version").integer() != 1)
        reject("$.version", "Unknown schema version", DiagnosticCode::UnknownVersion);
    TemplateDefinition result;
    result.id = identifier(root.at("id"));
    if (const auto* extensions = root.value.find("extensions")) {
        Reader ext{*extensions, "$.extensions"};
        if (!extensions->isObject()) reject(ext.path, "Expected namespaced extension object");
        for (const auto& name : extensions->keys()) {
            auto key = DefinitionRef::parse(name);
            if (!key) reject(ext.path + "." + name, "Extension key must be namespace:name");
        }
        result.extensions = *extensions;
    }
    root.at("batch_policy").literal("atomic");
    const auto limits = root.at("limits");
    limits.fields({"alive", "total", "budget_group"});
    result.limits.alive       = limits.at("alive").integer();
    result.limits.budgetGroup = identifier(limits.at("budget_group"));
    const auto total          = limits.at("total");
    const auto totalKind      = total.at("kind").text();
    if (totalKind == "unlimited") {
        total.fields({"kind"});
    } else if (totalKind == "finite") {
        total.fields({"kind", "count"});
        result.limits.total = total.at("count").integer64();
    } else
        reject(total.path, "Unsupported total limit", DiagnosticCode::Unsupported);

    const auto    schedule      = root.at("schedule");
    const auto    scheduleKind  = schedule.at("kind").text();
    std::uint64_t largestBatch  = 0;
    std::uint64_t requiredTotal = 0;
    if (scheduleKind == "waves") {
        if (root.value.find("content") != nullptr) reject("$.content", "Waves own their members; content is forbidden");
        schedule.fields({"kind", "waves"});
        WaveSchedule          waves;
        std::set<std::string> ids;
        const auto            entries = schedule.at("waves");
        for (const auto& item : entries.array()) {
            Reader wave{item, entries.path + "[" + std::to_string(waves.waves.size()) + "]"};
            wave.fields({"id", "delay_seconds", "members", "complete_when"});
            Wave decoded;
            decoded.id = identifier(wave.at("id"));
            if (!ids.insert(decoded.id).second) reject(wave.path, "Duplicate wave id");
            decoded.delay = wave.at("delay_seconds").duration();
            wave.at("complete_when").literal("all_members_dead");
            decoded.members  = members(wave.at("members"), false, resolver);
            const auto count = memberCount(decoded.members);
            largestBatch     = std::max(largestBatch, count);
            if (requiredTotal > std::numeric_limits<std::uint64_t>::max() - count) reject(wave.path, "Total overflows");
            requiredTotal += count;
            waves.waves.push_back(std::move(decoded));
        }
        result.schedule = std::move(waves);
    } else {
        const auto content     = root.at("content");
        const auto contentKind = content.at("kind").text();
        if (scheduleKind == "once") {
            schedule.fields({"kind"});
            content.fields({"kind", "members"});
            content.at("kind").literal("fixed");
            FixedContent fixed{members(content.at("members"), false, resolver)};
            largestBatch    = memberCount(fixed.members);
            requiredTotal   = largestBatch;
            result.content  = std::move(fixed);
            result.schedule = OnceSchedule{};
        } else if (scheduleKind == "interval" || scheduleKind == "maintain_population") {
            content.fields({"kind", "selection", "entries"});
            if (contentKind != "weighted") reject(content.path, "This schedule requires weighted content");
            content.at("selection").literal("per_unit");
            result.content = WeightedContent{members(content.at("entries"), true, resolver)};
            if (scheduleKind == "interval") {
                schedule.fields({"kind", "period_seconds", "initial_delay_seconds", "count_per_batch", "on_missed"});
                schedule.at("on_missed").literal("skip");
                IntervalSchedule interval{schedule.at("period_seconds").duration(true),
                                          schedule.at("initial_delay_seconds").duration(),
                                          schedule.at("count_per_batch").integer()};
                largestBatch    = interval.count;
                requiredTotal   = largestBatch;
                result.schedule = interval;
            } else {
                schedule.fields({"kind", "target_alive", "initial_population", "replacement"});
                const auto replacement = schedule.at("replacement");
                replacement.fields({"after", "delay_seconds"});
                replacement.at("after").literal("death");
                const auto  delay = replacement.at("delay_seconds");
                const auto& range = delay.array();
                if (range.size() != 2) reject(delay.path, "Expected [minimum, maximum] delay");
                PopulationSchedule population{
                    schedule.at("target_alive").integer(), schedule.at("initial_population").integer(true),
                    Reader{range[0], delay.path + "[0]"}.duration(), Reader{range[1], delay.path + "[1]"}.duration()};
                if (population.minimumDelay > population.maximumDelay) reject(delay.path, "Inverted delay range");
                if (population.initial > population.target || population.target > result.limits.alive)
                    reject(schedule.path, "Require initial <= target <= alive limit");
                largestBatch    = population.initial;
                requiredTotal   = largestBatch;
                result.schedule = population;
            }
        } else
            reject(schedule.path, "Unsupported schedule", DiagnosticCode::Unsupported);
    }
    if (largestBatch > result.limits.alive) reject("$.limits.alive", "Atomic batch cannot fit alive limit");
    if (result.limits.total && requiredTotal > *result.limits.total)
        reject("$.limits.total", "Total quota cannot complete initial content");

    const auto placement = root.at("placement");
    placement.fields({"kind", "constraints", "attempts_per_unit"});
    placement.at("kind").literal("random_in_region");
    result.placement.attempts = placement.at("attempts_per_unit").integer();
    // No arbitrary cap here: runtime work limits reject requests too large for their configured budget.
    std::set<std::string> constraintKinds;
    const auto            constraints = placement.at("constraints");
    for (const auto& item : constraints.array()) {
        Reader     entry{item, constraints.path + "[" + std::to_string(result.placement.constraints.size()) + "]"};
        const auto kind = entry.at("kind").text();
        if (!constraintKinds.insert(kind).second) reject(entry.path, "Duplicate constraint");
        PlacementConstraint constraint;
        if (kind == "walkable") {
            entry.fields({"kind"});
            constraint.kind = ConstraintKind::Walkable;
        } else if (kind == "clearance") {
            entry.fields({"kind", "radius"});
            constraint.kind     = ConstraintKind::Clearance;
            constraint.distance = entry.at("radius").number();
            if (constraint.distance == 0) reject(entry.path, "Clearance radius must be positive");
        } else if (kind == "distance_from_players") {
            entry.fields({"kind", "min"});
            constraint.kind     = ConstraintKind::DistanceFromPlayers;
            constraint.distance = entry.at("min").number();
        } else
            reject(entry.path, "Unsupported placement constraint", DiagnosticCode::Unsupported);
        result.placement.constraints.push_back(constraint);
    }
    if (scheduleKind == "interval") {
        if (root.value.find("failure") != nullptr)
            reject("$.failure", "Interval skip and retry are mutually exclusive");
    } else {
        const auto failure = root.at("failure");
        failure.fields({"placement_blocked"});
        const auto blocked = failure.at("placement_blocked");
        blocked.fields({"kind", "delay_seconds"});
        blocked.at("kind").literal("retry");
        result.retryDelay = blocked.at("delay_seconds").duration(true);
    }
    return result;
}
}  // namespace

Result<CompiledTemplate> compileTemplate(const Value& source, const IUnitDefinitionResolver& resolver) {
    try {
        auto definition = decode({source, "$"}, resolver);
        auto canonical  = source.toJson();
        if (!canonical) return Result<CompiledTemplate>::failure(canonical.status());
        return Result<CompiledTemplate>::success(
            CompiledTemplate(std::move(definition), std::move(canonical).takeValue()));
    } catch (InvalidInput& failure) {
        return Result<CompiledTemplate>::failure(std::move(failure.diagnostic));
    }
}
}  // namespace eve::spawn
