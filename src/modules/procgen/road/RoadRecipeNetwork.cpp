#include <algorithm>
#include "procgen/road/RoadNetworkCodec.h"
namespace eve::procgen::road {
Result<void> setRoadRecipeNetwork(Params& params, const RoadNetwork& network) {
    auto snapshot = captureRoadNetwork(network);
    if (!snapshot.ok()) return Result<void>::failure(snapshot.status());
    auto json = snapshot.value().toJson();
    if (!json.ok()) return Result<void>::failure(json.status());
    if (json.value().size() > 8u * 1024u * 1024u)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "road recipe snapshot exceeds 8 MiB", "networkSnapshot"));
    params.setString("networkSnapshot", json.value());
    return Result<void>::success();
}
Result<RoadNetwork> resolveRoadRecipeNetwork(const Params& params) {
    if (params.has("networkSnapshot")) {
        auto text = params.getString("networkSnapshot", "");
        if (text.empty() || text.size() > 8u * 1024u * 1024u)
            return Result<RoadNetwork>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "networkSnapshot must be nonempty JSON at most 8 MiB",
                                                                  "networkSnapshot"));
        auto value = eve::Value::fromJson(text);
        if (!value.ok()) return Result<RoadNetwork>::failure(value.status());
        return restoreRoadNetwork(value.value());
    }
    return RoadNetwork::makeScene(params.getString("scene", "straight"), params.getFloat("span", 36.f),
                                  params.getFloat("bridgeHeight", 6.f), std::max(1, params.getInt("lanes", 2)),
                                  params.getSeed());
}
}  // namespace eve::procgen::road
