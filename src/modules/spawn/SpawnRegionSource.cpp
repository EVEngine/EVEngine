#include "spawn/SpawnPointDecode.inl"
#include "spawn/SpawnRegion.h"
namespace eve::spawn {
Result<std::vector<Point>> decodePlayerFrame(const Value& input, std::size_t maxPlayers) {
    using R = Result<std::vector<Point>>;
    try {
        detail::Reader root{input, "players"};
        const auto&    rows = root.arrayValues();
        if (rows.size() > maxPlayers) detail::reject(root.path, "Player frame exceeds input cap");
        std::vector<Point> result;
        result.reserve(rows.size());
        for (const auto& row : rows)
            result.push_back(detail::point({row, root.path + "[" + std::to_string(result.size()) + "]"}));
        return R::success(std::move(result));
    } catch (const detail::InvalidInput& error) {
        return R::failure(error.diagnostic);
    }
}
Value buildRegionSource(const Region& region) {
    Value result = Value::object({{"space", "world2d"}});
    if (const auto* circle = std::get_if<CircleRegion>(&region)) {
        result.set("kind", "circle");
        result.set("center", Value::array({circle->center.x, circle->center.y}));
        result.set("radius", circle->radius);
    } else {
        Value::Array points;
        const auto*  polygon  = std::get_if<PolygonRegion>(&region);
        const auto&  vertices = polygon ? polygon->vertices : std::get<PointRegion>(region).points;
        for (auto point : vertices) points.push_back(Value::array({point.x, point.y}));
        result.set("kind", polygon ? "polygon" : "points");
        result.set(polygon ? "vertices" : "points", Value(std::move(points)));
    }
    return result;
}
}  // namespace eve::spawn
