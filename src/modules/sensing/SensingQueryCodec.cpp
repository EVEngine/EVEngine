#include "sensing/SensingQueryCodec.h"
#include <cmath>
#include <stdexcept>
#include "common/Json.h"

namespace eve::sensing {
namespace {
struct DecodeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
using Node = json::Value;
void require(bool ok, const std::string& field) {
    if (!ok) throw DecodeError(field);
}
std::string text(Node n, const char* key, std::string fallback = {}) {
    if (!n.has(key)) return fallback;
    require(n.get(key).isString(), key);
    return n.getString(key);
}
double number(Node n, const char* key, double fallback = 0) {
    if (!n.has(key)) return fallback;
    require(n.get(key).isNumber() && std::isfinite(n.getDouble(key)), key);
    return n.getDouble(key);
}
float scalar(Node n, const char* key) {
    require(n.has(key), key);
    const auto value = number(n, key);
    require(std::abs(value) <= std::numeric_limits<float>::max(), key);
    return static_cast<float>(value);
}
std::uint32_t count(Node n, const char* key, std::uint32_t fallback) {
    if (!n.has(key)) return fallback;
    require(n.get(key).isInt64() && n.get(key).asInt64() >= 0 &&
                n.get(key).asInt64() <= std::numeric_limits<std::uint32_t>::max(),
            key);
    return static_cast<std::uint32_t>(n.get(key).asInt64());
}
bool flag(Node n, const char* key, bool fallback) {
    if (!n.has(key)) return fallback;
    require(n.get(key).isBool(), key);
    return n.getBool(key);
}
template <class E>
E choice(Node n, const char* key, E fallback, std::initializer_list<std::pair<std::string_view, E>> names) {
    if (!n.has(key)) return fallback;
    const auto name = text(n, key);
    for (const auto& [label, value] : names)
        if (label == name) return value;
    throw DecodeError(key);
}
std::vector<std::string> strings(Node n, const char* key) {
    if (!n.has(key)) return {};
    auto a = n.get(key);
    require(a.isArray(), key);
    std::vector<std::string> result;
    for (std::size_t i = 0; i < a.size(); ++i) {
        require(a.at(i).isString(), key);
        result.push_back(a.at(i).asString());
    }
    return result;
}
CoordinateSpace space(Node n) {
    require(n.has("space"), "space");
    return choice<CoordinateSpace>(n, "space", CoordinateSpace::World2D,
                                   {{"world2d", CoordinateSpace::World2D},
                                    {"world3d", CoordinateSpace::World3D},
                                    {"grid2d", CoordinateSpace::Grid2D},
                                    {"grid3d", CoordinateSpace::Grid3D}});
}
TargetLocation location(Node n) {
    require(n.isObject(), "location");
    const auto tag = space(n);
    if (tag == CoordinateSpace::World2D || tag == CoordinateSpace::World3D) {
        auto p = tag == CoordinateSpace::World2D ? WorldPoint::world2D(scalar(n, "x"), scalar(n, "y"))
                                                 : WorldPoint::world3D(scalar(n, "x"), scalar(n, "y"), scalar(n, "z"));
        require(p.ok(), "location");
        return p.value();
    }
    const auto integer = [&](const char* key) {
        require(n.get(key).isInt64() && n.get(key).asInt64() >= std::numeric_limits<int>::min() &&
                    n.get(key).asInt64() <= std::numeric_limits<int>::max(),
                key);
        return static_cast<int>(n.get(key).asInt64());
    };
    if (tag == CoordinateSpace::Grid2D) return GridPoint::grid2D(integer("x"), integer("y"));
    return GridPoint::grid3D(integer("x"), integer("y"), integer("z"));
}
template <class T, class F>
Result<T> decode(std::string_view bytes, std::string_view schema, F parse) {
    std::string error;
    auto        document = json::Document::parse(std::string(bytes), &error);
    if (!document.valid())
        return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, error, {}, {}, "sensing.codec"));
    try {
        auto root = document.root();
        require(root.isObject() && text(root, "schema") == schema && root.get("version").isInt64() &&
                    root.get("version").asInt64() == 1,
                "schema/version");
        return Result<T>::success(parse(root));
    } catch (const DecodeError& error) {
        return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid sensing request field",
                                                    error.what(), {}, "sensing.codec"));
    }
}
}  // namespace
Result<SensingQueryRequest> decodeSensingQuery(std::string_view bytes) {
    return decode<SensingQueryRequest>(bytes, "eve.sensing.query", [](Node n) {
        SensingQueryRequest r;
        auto                p     = location(n.get("origin"));
        const auto*         world = std::get_if<WorldPoint>(&p);
        require(world != nullptr, "origin.space");
        r.origin = {world->x(), world->y(), std::nullopt, world->z(), world->space()};
        if (n.has("subjectId")) r.origin.subjectId = text(n, "subjectId");
        r.spec.axes = choice<QueryAxes>(
            n, "axes", world->space() == CoordinateSpace::World3D ? QueryAxes::XYZ : QueryAxes::XY,
            {{"xy", QueryAxes::XY}, {"xz", QueryAxes::XZ}, {"yz", QueryAxes::YZ}, {"xyz", QueryAxes::XYZ}});
        r.spec.minRange = static_cast<float>(number(n, "minRange", 0));
        r.spec.maxRange = static_cast<float>(number(n, "maxRange", std::numeric_limits<float>::infinity()));
        require(!n.has("maxRange") || std::isfinite(r.spec.maxRange), "maxRange");
        r.spec.minCount        = count(n, "minCount", 0);
        r.spec.maxCount        = count(n, "maxCount", std::numeric_limits<std::uint32_t>::max());
        r.spec.requiredTags    = strings(n, "requiredTags");
        r.spec.excludedTags    = strings(n, "excludedTags");
        r.spec.includeFactions = strings(n, "includeFactions");
        r.spec.excludeFactions = strings(n, "excludeFactions");
        if (n.has("visibleTo")) r.spec.visibleTo = text(n, "visibleTo");
        r.spec.requireLineOfSight = flag(n, "lineOfSight", false);
        r.spec.countPolicy =
            choice<CountPolicy>(n, "countPolicy", CountPolicy::TruncateToMax,
                                {{"truncate", CountPolicy::TruncateToMax}, {"fail", CountPolicy::FailIfOutOfRange}});
        r.spec.sortKey = choice<SortKey>(n, "sort", SortKey::DistanceAscending,
                                         {{"distance", SortKey::DistanceAscending}, {"none", SortKey::None}});
        if (n.has("shape")) {
            auto shape = n.get("shape");
            require(shape.isObject(), "shape");
            const auto kind = text(shape, "kind");
            if (kind == "sphere")
                r.spec.shape =
                    QuerySphere{scalar(shape, "x"), scalar(shape, "y"), scalar(shape, "z"), scalar(shape, "radius")};
            else if (kind == "circle")
                r.spec.shape = QueryCircle{scalar(shape, "x"), scalar(shape, "y"), scalar(shape, "radius")};
            else if (kind == "box")
                r.spec.shape = QueryBox{scalar(shape, "minX"), scalar(shape, "minY"), scalar(shape, "maxX"),
                                        scalar(shape, "maxY")};
            else if (kind == "cone")
                r.spec.shape = QueryCone{scalar(shape, "x"),    scalar(shape, "y"),         scalar(shape, "dirX"),
                                         scalar(shape, "dirY"), scalar(shape, "halfAngle"), scalar(shape, "range")};
            else
                throw DecodeError("shape.kind");
        }
        auto valid = r.spec.validate();
        require(valid.ok(), "query");
        return r;
    });
}
Result<EnvironmentRequest> decodeEnvironmentRequest(std::string_view bytes) {
    return decode<EnvironmentRequest>(bytes, "eve.sensing.environmentRequest", [](Node n) {
        EnvironmentRequest r;
        auto               contexts = n.get("contexts");
        require(contexts.isArray(), "contexts");
        for (std::size_t i = 0; i < contexts.size(); ++i) {
            auto               context = contexts.at(i);
            EnvironmentContext c;
            c.name      = text(context, "name");
            auto points = context.get("locations");
            require(points.isArray(), "contexts.locations");
            for (std::size_t j = 0; j < points.size(); ++j) c.locations.push_back(location(points.at(j)));
            r.contexts.push_back(std::move(c));
        }
        if (n.has("input")) {
            auto items = n.get("input");
            require(items.isArray(), "input");
            for (std::size_t i = 0; i < items.size(); ++i) {
                auto item = items.at(i);
                require(item.get("id").isInt64() && item.get("id").asInt64() >= 0, "input.id");
                EnvironmentItem value{static_cast<std::uint64_t>(item.get("id").asInt64()),
                                      location(item.get("location")), std::nullopt};
                if (item.has("subjectKey")) value.subjectKey = text(item, "subjectKey");
                r.input.push_back(std::move(value));
            }
        }
        return r;
    });
}
Result<EnvironmentQueryDefinition> decodeEnvironmentDefinition(std::string_view bytes) {
    return decode<EnvironmentQueryDefinition>(bytes, "eve.sensing.environmentDefinition", [](Node n) {
        EnvironmentQueryDefinition d;
        d.id            = text(n, "id");
        d.space         = space(n);
        d.contexts      = strings(n, "contexts");
        d.revision      = count(n, "revision", 1);
        d.maxResults    = count(n, "maxResults", 8);
        d.optimize      = flag(n, "optimize", true);
        d.maxTraceItems = count(n, "maxTraceItems", 256);
        d.mode          = choice<EnvironmentRunMode>(n, "mode", EnvironmentRunMode::All,
                                                     {{"all", EnvironmentRunMode::All}, {"best", EnvironmentRunMode::Best}});
        d.trace         = choice<EnvironmentTraceLevel>(n, "trace", EnvironmentTraceLevel::Summary,
                                                        {{"none", EnvironmentTraceLevel::None},
                                                         {"summary", EnvironmentTraceLevel::Summary},
                                                         {"detailed", EnvironmentTraceLevel::Detailed}});
        auto options    = n.get("options");
        require(options.isArray(), "options");
        for (std::size_t i = 0; i < options.size(); ++i) {
            auto o = options.at(i);
            require(o.isObject(), "options");
            EnvironmentOption option;
            option.id = text(o, "id");
            if (o.has("generator")) {
                auto g = o.get("generator");
                require(g.isObject(), "generator");
                option.generator.kind     = choice<EnvironmentGeneratorKind>(g, "kind", EnvironmentGeneratorKind::Input,
                                                                             {{"input", EnvironmentGeneratorKind::Input},
                                                                              {"ring", EnvironmentGeneratorKind::Ring},
                                                                              {"grid", EnvironmentGeneratorKind::Grid}});
                option.generator.context  = text(g, "context");
                option.generator.radius   = number(g, "radius", 10);
                option.generator.spacing  = number(g, "spacing", 2);
                option.generator.samples  = count(g, "samples", 16);
                option.generator.maxItems = count(g, "maxItems", 4096);
            }
            auto tests = o.get("tests");
            require(tests.isArray(), "tests");
            for (std::size_t j = 0; j < tests.size(); ++j) {
                auto t = tests.at(j);
                require(t.isObject(), "tests");
                EnvironmentTest test;
                test.id      = text(t, "id");
                test.context = text(t, "context");
                test.metric  = choice<EnvironmentMetricKind>(
                    t, "metric", EnvironmentMetricKind::Distance,
                    {{"distance", EnvironmentMetricKind::Distance}, {"los", EnvironmentMetricKind::LineOfSight}});
                test.purpose = choice<EnvironmentPurpose>(t, "purpose", EnvironmentPurpose::FilterAndScore,
                                                          {{"filter", EnvironmentPurpose::Filter},
                                                           {"score", EnvironmentPurpose::Score},
                                                           {"filterAndScore", EnvironmentPurpose::FilterAndScore}});
                test.filterOp =
                    choice<EnvironmentFilterOp>(t, "filterOp", EnvironmentFilterOp::All,
                                                {{"all", EnvironmentFilterOp::All}, {"any", EnvironmentFilterOp::Any}});
                test.aggregate = choice<EnvironmentAggregate>(t, "aggregate", EnvironmentAggregate::Average,
                                                              {{"average", EnvironmentAggregate::Average},
                                                               {"minimum", EnvironmentAggregate::Minimum},
                                                               {"maximum", EnvironmentAggregate::Maximum}});
                test.normalization =
                    choice<EnvironmentNormalization>(t, "normalization", EnvironmentNormalization::FixedRange,
                                                     {{"fixed", EnvironmentNormalization::FixedRange},
                                                      {"population", EnvironmentNormalization::ActivePopulation}});
                test.curve          = choice<EnvironmentCurve>(t, "curve", EnvironmentCurve::Linear,
                                                               {{"linear", EnvironmentCurve::Linear},
                                                                {"square", EnvironmentCurve::Square},
                                                                {"sqrt", EnvironmentCurve::SquareRoot}});
                test.preference     = choice<EnvironmentPreference>(t, "preference", EnvironmentPreference::Lower,
                                                                    {{"lower", EnvironmentPreference::Lower},
                                                                     {"higher", EnvironmentPreference::Higher},
                                                                     {"reference", EnvironmentPreference::Reference}});
                test.minimum        = number(t, "minimum", 0);
                test.maximum        = number(t, "maximum", std::numeric_limits<double>::max());
                test.scoreMinimum   = number(t, "scoreMinimum", 0);
                test.scoreMaximum   = number(t, "scoreMaximum", 100);
                test.referenceValue = number(t, "referenceValue", 0);
                test.weight         = number(t, "weight", 1);
                option.tests.push_back(std::move(test));
            }
            d.options.push_back(std::move(option));
        }
        return d;
    });
}
}  // namespace eve::sensing
