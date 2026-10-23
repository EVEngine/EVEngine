// Shared native/editor value codec implementation; included inside the private codec namespace.
RoadCodecValue encodeConnection(const procgen::road::RoadConnection& connection) {
    RoadCodecValue::Array vertices, triangles, ports, routes;
    for (const auto& r : connection.facilityRoutes)
        routes.emplace_back(RoadCodecValue::Object{{"firstEdge", std::int64_t{r.firstEdge}},
                                                   {"firstFacility", std::int64_t{r.firstFacility}},
                                                   {"secondEdge", std::int64_t{r.secondEdge}},
                                                   {"secondFacility", std::int64_t{r.secondFacility}}});
    for (const auto& v : connection.vertices)
        vertices.emplace_back(RoadCodecValue::Array{v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v});
    for (const auto& t : connection.triangles)
        triangles.emplace_back(RoadCodecValue::Array{std::int64_t{t.a}, std::int64_t{t.b}, std::int64_t{t.c},
                                                     std::int64_t{static_cast<int>(t.material)}});
    for (const auto& p : connection.ports) {
        RoadCodecValue::Array section;
        for (const auto& point : p.section)
            section.emplace_back(RoadCodecValue::Object{
                {"side", point.side}, {"up", point.up}, {"material", std::int64_t{static_cast<int>(point.material)}}});
        ports.emplace_back(RoadCodecValue::Object{{"id", std::int64_t{p.id}},
                                                  {"edgeId", std::int64_t{p.edgeId}},
                                                  {"x", p.x},
                                                  {"y", p.y},
                                                  {"z", p.z},
                                                  {"forwardX", p.forwardX},
                                                  {"forwardY", p.forwardY},
                                                  {"forwardZ", p.forwardZ},
                                                  {"upX", p.upX},
                                                  {"upY", p.upY},
                                                  {"upZ", p.upZ},
                                                  {"sectionClosed", p.sectionClosed},
                                                  {"section", std::move(section)}});
    }
    return RoadCodecValue::Object{{"facilityRoutes", std::move(routes)}, {"assetId", connection.assetId},
                                  {"yawDegrees", connection.yawDegrees}, {"vertices", std::move(vertices)},
                                  {"triangles", std::move(triangles)},   {"ports", std::move(ports)}};
}
bool decodeConnection(const RoadCodecValue& value, procgen::road::RoadConnection& connection) {
    const auto* identity      = field(value, "assetId");
    const auto* name          = identity ? identity->getIf<std::string>() : nullptr;
    const auto *verticesValue = field(value, "vertices"), *trianglesValue = field(value, "triangles"),
               *portsValue = field(value, "ports");
    const auto* vertices   = verticesValue ? verticesValue->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* triangles  = trianglesValue ? trianglesValue->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* ports      = portsValue ? portsValue->getIf<RoadCodecValue::Array>() : nullptr;
    if (!name || name->size() > 256u || !readNumber(value, "yawDegrees", connection.yawDegrees) || !vertices ||
        !triangles || !ports || vertices->size() > 65536u || triangles->size() > 131072u || ports->size() > 64u)
        return false;
    connection.assetId = *name;
    connection.vertices.clear();
    connection.triangles.clear();
    connection.ports.clear();
    connection.facilityRoutes.clear();
    if (const auto* encodedRoutes = field(value, "facilityRoutes")) {
        const auto* routes = encodedRoutes->getIf<RoadCodecValue::Array>();
        if (!routes || routes->size() > 128u) return false;
        for (const auto& encoded : *routes) {
            procgen::road::RoadFacilityRoute r;
            if (!readId(encoded, "firstEdge", r.firstEdge) || !readId(encoded, "firstFacility", r.firstFacility) ||
                !readId(encoded, "secondEdge", r.secondEdge) || !readId(encoded, "secondFacility", r.secondFacility))
                return false;
            connection.facilityRoutes.push_back(r);
        }
    }
    for (const auto& encoded : *vertices) {
        const auto* data = encoded.getIf<RoadCodecValue::Array>();
        if (!data || data->size() != 8u) return false;
        float numbers[8]{};
        for (std::size_t i = 0; i < 8u; ++i) {
            const auto* number = (*data)[i].getIf<double>();
            if (!number || !std::isfinite(*number) || std::fabs(*number) > std::numeric_limits<float>::max())
                return false;
            numbers[i] = static_cast<float>(*number);
        }
        connection.vertices.push_back(
            {numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5], numbers[6], numbers[7]});
    }
    for (const auto& encoded : *triangles) {
        const auto* data = encoded.getIf<RoadCodecValue::Array>();
        if (!data || data->size() != 4u) return false;
        std::uint32_t numbers[4]{};
        for (std::size_t i = 0; i < 4u; ++i) {
            const auto* number = (*data)[i].getIf<std::int64_t>();
            if (!number || *number < 0 || *number > std::numeric_limits<std::uint32_t>::max()) return false;
            numbers[i] = static_cast<std::uint32_t>(*number);
        }
        if (numbers[3] > static_cast<std::uint32_t>(procgen::road::RoadMaterial::TunnelPortalAsphalt)) return false;
        connection.triangles.push_back(
            {numbers[0], numbers[1], numbers[2], static_cast<procgen::road::RoadMaterial>(numbers[3])});
    }
    for (const auto& encoded : *ports) {
        procgen::road::RoadConnectionPort port;
        const auto*                       edgeValue = field(encoded, "edgeId");
        const auto*                       edge      = edgeValue ? edgeValue->getIf<std::int64_t>() : nullptr;
        if (!readId(encoded, "id", port.id) || !edge || *edge < 0 ||
            *edge > std::numeric_limits<std::uint32_t>::max() || !readNumber(encoded, "x", port.x) ||
            !readNumber(encoded, "y", port.y) || !readNumber(encoded, "z", port.z) ||
            !readNumber(encoded, "forwardX", port.forwardX) || !readNumber(encoded, "forwardY", port.forwardY) ||
            !readNumber(encoded, "forwardZ", port.forwardZ) || !readNumber(encoded, "upX", port.upX) ||
            !readNumber(encoded, "upY", port.upY) || !readNumber(encoded, "upZ", port.upZ))
            return false;
        if (!readOptionalBool(encoded, "sectionClosed", port.sectionClosed)) return false;
        port.edgeId              = static_cast<std::uint32_t>(*edge);
        const auto* sectionValue = field(encoded, "section");
        const auto* section      = sectionValue ? sectionValue->getIf<RoadCodecValue::Array>() : nullptr;
        if (!section || section->size() > 128u) return false;
        for (const auto& point : *section) {
            procgen::road::RoadProfilePoint p;
            int                             material = 0;
            if (!readNumber(point, "side", p.side) || !readNumber(point, "up", p.up) ||
                !readSmallInt(point, "material", 0, static_cast<int>(procgen::road::RoadMaterial::TunnelPortalAsphalt),
                              material))
                return false;
            p.material = static_cast<procgen::road::RoadMaterial>(material);
            port.section.push_back(p);
        }
        connection.ports.push_back(std::move(port));
    }
    return true;
}
