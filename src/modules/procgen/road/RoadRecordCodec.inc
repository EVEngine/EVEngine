// Shared private graph record encoding.
RoadCodecValue encodeLink(const procgen::road::RoadLaneConnection& link) {
    return RoadCodecValue::Object{{"inEdge", std::int64_t{link.inEdge}},
                                  {"inLane", std::int64_t{link.inLane}},
                                  {"outEdge", std::int64_t{link.outEdge}},
                                  {"outLane", std::int64_t{link.outLane}},
                                  {"inDirection", std::int64_t{static_cast<int>(link.inDirection)}},
                                  {"outDirection", std::int64_t{static_cast<int>(link.outDirection)}}};
}

bool decodeLink(const RoadCodecValue& value, procgen::road::RoadLaneConnection& link) {
    int inDirection = 0, outDirection = 0;
    if (!readId(value, "inEdge", link.inEdge) || !readSmallInt(value, "inLane", 0, 7, link.inLane) ||
        !readId(value, "outEdge", link.outEdge) || !readSmallInt(value, "outLane", 0, 7, link.outLane) ||
        !readSmallInt(value, "inDirection", 0, 1, inDirection) ||
        !readSmallInt(value, "outDirection", 0, 1, outDirection))
        return false;
    link.inDirection  = static_cast<procgen::road::RoadLaneDirection>(inDirection);
    link.outDirection = static_cast<procgen::road::RoadLaneDirection>(outDirection);
    return true;
}

RoadCodecValue encodeEdgeRecord(const procgen::road::RoadEdge&                        edge,
                                const std::vector<procgen::road::RoadLaneConnection>& links,
                                const std::vector<procgen::road::RoadLaneConnection>& blockedLinks) {
    auto object             = *encodeEdge(edge.id, edge.controlPoints).getIf<RoadCodecValue::Object>();
    object["from"]          = std::int64_t{edge.from};
    object["to"]            = std::int64_t{edge.to};
    object["lanesForward"]  = std::int64_t{edge.lanesForward};
    object["lanesBackward"] = std::int64_t{edge.lanesBackward};
    object["style"]         = encodeStyle(edge.id, edge.style);
    RoadCodecValue::Array encodedLinks;
    encodedLinks.reserve(links.size());
    for (const auto& link : links) encodedLinks.push_back(encodeLink(link));
    object["laneLinks"] = std::move(encodedLinks);
    RoadCodecValue::Array encodedBlocked;
    encodedBlocked.reserve(blockedLinks.size());
    for (const auto& link : blockedLinks) encodedBlocked.push_back(encodeLink(link));
    object["blockedLaneLinks"] = std::move(encodedBlocked);
    return object;
}

bool decodeEdgeRecord(const RoadCodecValue& value, procgen::road::RoadEdge& edge,
                      std::vector<procgen::road::RoadLaneConnection>& links,
                      std::vector<procgen::road::RoadLaneConnection>* blockedLinks) {
    if (!decodeEdge(value, edge.id, edge.controlPoints) || !readId(value, "from", edge.from) ||
        !readId(value, "to", edge.to) || !readSmallInt(value, "lanesForward", 0, 8, edge.lanesForward) ||
        !readSmallInt(value, "lanesBackward", 0, 8, edge.lanesBackward))
        return false;
    const auto*   encodedStyle = field(value, "style");
    std::uint32_t styleEdgeId  = 0;
    if (!encodedStyle || !decodeStyle(*encodedStyle, styleEdgeId, edge.style) || styleEdgeId != edge.id) return false;
    const auto* encodedLinks   = field(value, "laneLinks");
    const auto* array          = encodedLinks ? encodedLinks->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* encodedBlocked = field(value, "blockedLaneLinks");
    const auto* blockedArray   = encodedBlocked ? encodedBlocked->getIf<RoadCodecValue::Array>() : nullptr;
    if (!array || array->size() > 65536 || (blockedArray && blockedArray->size() > 65536) ||
        (!blockedLinks && blockedArray && !blockedArray->empty()))
        return false;
    links.clear();
    links.reserve(array->size());
    for (const auto& encoded : *array) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        links.push_back(link);
    }
    if (blockedLinks) {
        blockedLinks->clear();
        if (blockedArray) {
            blockedLinks->reserve(blockedArray->size());
            for (const auto& encoded : *blockedArray) {
                procgen::road::RoadLaneConnection link;
                if (!decodeLink(encoded, link)) return false;
                blockedLinks->push_back(link);
            }
        }
    }
    return true;
}
