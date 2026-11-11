#include "spawn/SpawnMap.h"
#include "spawn/SpawnPointDecode.inl"

#include <map>
#include <set>

namespace eve::spawn {
namespace {
using detail::identifier;
using detail::Reader;
using detail::reject;

using detail::point;
long double orientation(Point a, Point b, Point c) {
    return (static_cast<long double>(b.x) - a.x) * (static_cast<long double>(c.y) - a.y) -
           (static_cast<long double>(b.y) - a.y) * (static_cast<long double>(c.x) - a.x);
}
bool onSegment(Point a, Point b, Point c) {
    return orientation(a, b, c) == 0 && c.x >= std::min(a.x, b.x) && c.x <= std::max(a.x, b.x) &&
           c.y >= std::min(a.y, b.y) && c.y <= std::max(a.y, b.y);
}
bool intersects(Point a, Point b, Point c, Point d) {
    const auto p = orientation(a, b, c), q = orientation(a, b, d), r = orientation(c, d, a), s = orientation(c, d, b);
    return (((p > 0 && q < 0) || (p < 0 && q > 0)) && ((r > 0 && s < 0) || (r < 0 && s > 0))) || onSegment(a, b, c) ||
           onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b);
}
Region region(const Reader& reader) {
    reader.at("space").literal("world2d");
    const auto kind = reader.at("kind").text();
    if (kind == "circle") {
        reader.fields({"kind", "space", "center", "radius"});
        CircleRegion result{point(reader.at("center")), reader.at("radius").number()};
        if (result.radius == 0) reject(reader.path, "Circle radius must be positive");
        return result;
    }
    if (kind != "points" && kind != "polygon") reject(reader.path, "Unsupported region", DiagnosticCode::Unsupported);
    reader.fields(kind == "points" ? std::initializer_list<std::string_view>{"kind", "space", "points"}
                                   : std::initializer_list<std::string_view>{"kind", "space", "vertices"});
    const auto                          entries = reader.at(kind == "points" ? "points" : "vertices");
    std::vector<Point>                  points;
    std::set<std::pair<double, double>> seen;
    for (const auto& entry : entries.array()) {
        const auto decoded = point({entry, entries.path + "[" + std::to_string(points.size()) + "]"});
        if (!seen.emplace(decoded.x, decoded.y).second) reject(entries.path, "Repeated region vertex");
        points.push_back(decoded);
    }
    if (kind == "points") return PointRegion{std::move(points)};
    if (points.size() < 3) reject(entries.path, "Polygon needs at least three vertices");
    long double area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto next = (i + 1) % points.size();
        area += orientation(points[0], points[i], points[next]);
        // Consecutive collinear vertices obscure overlaps; require a minimal simple polygon.
        if (orientation(points[i], points[next], points[(i + 2) % points.size()]) == 0)
            reject(entries.path, "Collinear adjacent polygon edges");
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            if (j == next || (j + 1) % points.size() == i) continue;
            if (intersects(points[i], points[next], points[j], points[(j + 1) % points.size()]))
                reject(entries.path, "Self-intersecting polygon");
        }
    }
    if (!std::isfinite(area) || area == 0) reject(entries.path, "Degenerate or out-of-range polygon");
    return PolygonRegion{std::move(points)};
}
Activation activation(const Reader& reader) {
    const auto kind = reader.at("kind").text();
    if (kind == "map_loaded") {
        reader.fields({"kind"});
        return MapLoadedActivation{};
    }
    if (kind == "event") {
        reader.fields({"kind", "event"});
        return EventActivation{identifier(reader.at("event"))};
    }
    if (kind == "player_proximity") {
        reader.fields({"kind", "enter_distance", "exit_distance"});
        ProximityActivation result{reader.at("enter_distance").number(), reader.at("exit_distance").number()};
        if (result.exitDistance <= result.enterDistance)
            reject(reader.path, "Exit distance must exceed enter distance");
        return result;
    }
    reject(reader.path, "Unsupported activation", DiagnosticCode::Unsupported);
}
std::uint64_t largestBatch(const TemplateDefinition& definition) {
    if (const auto* fixed = std::get_if<FixedContent>(&definition.content);
        fixed != nullptr && std::holds_alternative<OnceSchedule>(definition.schedule)) {
        std::uint64_t count = 0;
        for (const auto& member : fixed->members) count += member.count;
        return count;
    }
    if (const auto* interval = std::get_if<IntervalSchedule>(&definition.schedule)) return interval->count;
    if (const auto* population = std::get_if<PopulationSchedule>(&definition.schedule)) return population->target;
    std::uint64_t result = 0;
    for (const auto& wave : std::get<WaveSchedule>(definition.schedule).waves) {
        std::uint64_t count = 0;
        for (const auto& member : wave.members) count += member.count;
        result = std::max(result, count);
    }
    return result;
}
}  // namespace

Result<CompiledMap> compileMap(const Value& source, const ITemplateResolver& templates,
                               const IUnitDefinitionResolver& units) {
    try {
        Reader root{source, "$"};
        root.fields({"schema", "version", "map", "budgets", "instances"});
        root.at("schema").literal("eve.map-spawns");
        if (root.at("version").integer() != 1)
            reject("$.version", "Unknown map schema version", DiagnosticCode::UnknownVersion);
        auto                               id = identifier(root.at("map"));
        std::vector<BudgetDefinition>      budgets;
        std::map<std::string, std::size_t> budgetIndices;
        const auto                         budgetEntries = root.at("budgets");
        for (const auto& item : budgetEntries.array()) {
            Reader reader{item, budgetEntries.path + "[" + std::to_string(budgets.size()) + "]"};
            reader.fields({"id", "alive_limit"});
            BudgetDefinition budget{identifier(reader.at("id")), reader.at("alive_limit").integer()};
            if (!budgetIndices.emplace(budget.id, budgets.size()).second) reject(reader.path, "Duplicate budget id");
            budgets.push_back(std::move(budget));
        }
        std::vector<InstanceDefinition> instances;
        std::set<std::string>           ids;
        const auto                      instanceEntries = root.at("instances");
        for (const auto& item : instanceEntries.arrayValues()) {
            Reader reader{item, instanceEntries.path + "[" + std::to_string(instances.size()) + "]"};
            reader.fields({"id", "template", "region", "activation", "inactive_policy", "unload_policy", "overrides"});
            InstanceDefinition instance;
            instance.id = identifier(reader.at("id"));
            if (!ids.insert(instance.id).second) reject(reader.path, "Duplicate instance id");
            reader.at("inactive_policy").literal("pause");
            reader.at("unload_policy").literal("persist");
            auto ref = reader.at("template");
            ref.fields({"type", "id"});
            ref.at("type").literal("spawn.template");
            auto parsedRef = DefinitionRef::parse("spawn.template:" + identifier(ref.at("id")));
            if (!parsedRef) reject(ref.path, "Invalid template reference");
            auto resolved = templates.resolve(parsedRef.value());
            if (!resolved) return Result<CompiledMap>::failure(resolved.status());
            auto revision = std::move(resolved).takeValue();
            if (revision.definition().id != ref.at("id").text())
                reject(ref.path, "Resolved template identity mismatch");
            if (const auto* overrideValue = item.find("overrides")) {
                Reader overrides{*overrideValue, reader.path + ".overrides"};
                overrides.fields({"population"});
                auto population = overrides.at("population");
                population.fields({"target_alive", "initial_population", "alive_limit"});
                if (!std::holds_alternative<PopulationSchedule>(revision.definition().schedule))
                    reject(overrides.path, "Population override requires maintain_population");
                auto decoded = Value::fromJson(revision.canonicalJson());
                if (!decoded) return Result<CompiledMap>::failure(decoded.status());
                auto candidate = std::move(decoded).takeValue();
                candidate.find("schedule")
                    ->set("target_alive", Value(static_cast<std::int64_t>(population.at("target_alive").integer())));
                candidate.find("schedule")
                    ->set("initial_population",
                          Value(static_cast<std::int64_t>(population.at("initial_population").integer(true))));
                candidate.find("limits")->set("alive",
                                              Value(static_cast<std::int64_t>(population.at("alive_limit").integer())));
                auto compiled = compileTemplate(candidate, units);
                if (!compiled) return Result<CompiledMap>::failure(compiled.status());
                revision = std::move(compiled).takeValue();
            }
            const auto budget = budgetIndices.find(revision.definition().limits.budgetGroup);
            if (budget == budgetIndices.end()) reject(reader.path, "Unknown shared budget", DiagnosticCode::NotFound);
            instance.budgetIndex = budget->second;
            if (largestBatch(revision.definition()) > budgets[instance.budgetIndex].alive)
                reject(reader.path, "Shared budget cannot fit requested population or atomic batch");
            instance.region     = region(reader.at("region"));
            instance.activation = activation(reader.at("activation"));
            instance.revision   = std::make_shared<const CompiledTemplate>(std::move(revision));
            instances.push_back(std::move(instance));
        }
        auto canonical = source.toJson();
        if (!canonical) return Result<CompiledMap>::failure(canonical.status());
        return Result<CompiledMap>::success(
            CompiledMap(std::move(id), std::move(budgets), std::move(instances), std::move(canonical).takeValue()));
    } catch (detail::InvalidInput& failure) {
        return Result<CompiledMap>::failure(std::move(failure.diagnostic));
    }
}
}  // namespace eve::spawn
