// Shared native/editor value codec implementation; included inside the private codec namespace.
RoadCodecValue encodeEdge(std::uint32_t id, const std::vector<procgen::road::RoadControlPoint>& points) {
    RoadCodecValue::Array encoded;
    encoded.reserve(points.size());
    for (const auto& point : points)
        encoded.emplace_back(
            RoadCodecValue::Array{RoadCodecValue(point.x), RoadCodecValue(point.y), RoadCodecValue(point.z)});
    RoadCodecValue::Array profiles;
    profiles.reserve(points.size());
    for (const auto& point : points)
        profiles.emplace_back(
            RoadCodecValue::Object{{"widthScale", point.widthScale},
                                   {"rollDegrees", point.rollDegrees},
                                   {"rollCenterOffset", point.rollCenterOffset},
                                   {"splineStrength", point.splineStrength},
                                   {"circularFromEnd", point.circularFromEnd},
                                   {"alignStraightStart", point.alignStraightStart},
                                   {"alignStraightEnd", point.alignStraightEnd},
                                   {"pathControl", std::int64_t{static_cast<int>(point.pathControl)}}});
    return RoadCodecValue::Object{
        {"id", std::int64_t{id}}, {"points", std::move(encoded)}, {"pointProfiles", std::move(profiles)}};
}


bool decodeEdge(const RoadCodecValue& value, std::uint32_t& id, std::vector<procgen::road::RoadControlPoint>& points) {
    if (!readId(value, "id", id)) return false;
    const auto* item  = field(value, "points");
    const auto* array = item ? item->getIf<RoadCodecValue::Array>() : nullptr;
    if (!array || array->size() < 2 || array->size() > 4096) return false;
    points.clear();
    points.reserve(array->size());
    for (const auto& encoded : *array) {
        const auto* xyz = encoded.getIf<RoadCodecValue::Array>();
        if (!xyz || xyz->size() != 3) return false;
        float values[3]{};
        for (int i = 0; i < 3; ++i) {
            const auto* number = (*xyz)[static_cast<std::size_t>(i)].getIf<double>();
            if (!number || !std::isfinite(*number) || std::fabs(*number) > std::numeric_limits<float>::max())
                return false;
            values[i] = static_cast<float>(*number);
        }
        points.push_back({values[0], values[1], values[2]});
    }
    if (const auto* encodedProfiles = field(value, "pointProfiles")) {
        const auto* profiles = encodedProfiles->getIf<RoadCodecValue::Array>();
        if (!profiles || profiles->size() != points.size()) return false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            int controller = 0;
            if (field((*profiles)[i], "pathControl") && !readSmallInt((*profiles)[i], "pathControl", 0, 3, controller))
                return false;
            points[i].pathControl = static_cast<procgen::road::RoadPathControl>(controller);
            if (!readNumber((*profiles)[i], "widthScale", points[i].widthScale) ||
                !readNumber((*profiles)[i], "rollDegrees", points[i].rollDegrees) ||
                !readOptionalNumber((*profiles)[i], "rollCenterOffset", points[i].rollCenterOffset) ||
                !readOptionalNumber((*profiles)[i], "splineStrength", points[i].splineStrength) ||
                !readOptionalBool((*profiles)[i], "alignStraightStart", points[i].alignStraightStart) ||
                !readOptionalBool((*profiles)[i], "alignStraightEnd", points[i].alignStraightEnd) ||
                !readOptionalBool((*profiles)[i], "circularFromEnd", points[i].circularFromEnd))
                return false;
        }
    }
    return true;
}

RoadCodecValue encodeNodeRecord(const procgen::road::RoadNode& node) {
    return RoadCodecValue::Object{{"id", std::int64_t{node.id}},
                                  {"x", node.x},
                                  {"y", node.y},
                                  {"z", node.z},
                                  {"junctionRadius", node.junctionRadius},
                                  {"junctionControl", std::int64_t{static_cast<int>(node.junctionControl)}},
                                  {"connection", encodeConnection(node.connection)}};
}

bool decodeNodeRecord(const RoadCodecValue& value, procgen::road::RoadNode& node) {
    if (!readId(value, "id", node.id) || !readNumber(value, "x", node.x) || !readNumber(value, "y", node.y) ||
        !readNumber(value, "z", node.z) || !readNumber(value, "junctionRadius", node.junctionRadius))
        return false;
    int control = 0;
    if (field(value, "junctionControl") && !readSmallInt(value, "junctionControl", 0, 3, control)) return false;
    node.junctionControl   = static_cast<procgen::road::RoadJunctionControl>(control);
    node.connection        = {};
    const auto* connection = field(value, "connection");
    return !connection || decodeConnection(*connection, node.connection);
}

bool readSmallInt(const RoadCodecValue& value, const char* name, int minimum, int maximum, int& output) {
    const auto* item   = field(value, name);
    const auto* number = item ? item->getIf<std::int64_t>() : nullptr;
    if (!number || *number < minimum || *number > maximum) return false;
    output = static_cast<int>(*number);
    return true;
}
