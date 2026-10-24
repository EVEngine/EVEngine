// Shared native/editor value codec implementation; included inside the private codec namespace.
namespace {
RoadCodecValue encodePrefab(const procgen::road::RoadFacilityPrefab& prefab) {
    procgen::road::RoadConnection mesh;
    mesh.assetId = prefab.assetId;
    for (const auto& v : prefab.vertices) mesh.vertices.push_back({v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v});
    for (const auto& t : prefab.triangles) mesh.triangles.push_back({t.a, t.b, t.c, t.material});
    RoadCodecValue::Object encoded{{"mesh", encodeConnection(mesh)},
                                   {"length", prefab.length},
                                   {"middleCopies", std::int64_t{prefab.middleCopies}},
                                   {"weldCompatibleVertices", prefab.weldCompatibleVertices},
                                   {"sourceStart", prefab.sourceStart},
                                   {"sourceEnd", prefab.sourceEnd}};
    encoded.emplace("automaticSpacing", prefab.automaticSpacing);
    encoded.emplace("averageSpacing", prefab.averageSpacing);
    encoded.emplace("reverseRepeatSequence", prefab.reverseRepeatSequence);
    encoded.emplace("repeatSourceLength", prefab.repeatSourceLength);
    RoadCodecValue::Array groups;
    for (const auto& triangle : prefab.triangles) groups.emplace_back(std::int64_t{triangle.smoothingGroup});
    encoded.emplace("smoothingGroups", std::move(groups));
    auto segment = [](const procgen::road::RoadPrefabSegment& part) {
        procgen::road::RoadFacilityPrefab p;
        p.assetId   = part.assetId;
        p.length    = part.length;
        p.vertices  = part.vertices;
        p.triangles = part.triangles;
        return encodePrefab(p);
    };
    if (!prefab.startMesh.vertices.empty()) encoded.emplace("startMesh", segment(prefab.startMesh));
    if (!prefab.endMesh.vertices.empty()) encoded.emplace("endMesh", segment(prefab.endMesh));
    return encoded;
}
bool decodePrefab(const RoadCodecValue& value, procgen::road::RoadFacilityPrefab& prefab) {
    procgen::road::RoadConnection mesh;
    const auto*                   encoded = field(value, "mesh");
    if (!encoded || !decodeConnection(*encoded, mesh) || !mesh.ports.empty() || !mesh.facilityRoutes.empty() ||
        mesh.yawDegrees != 0.f || !readNumber(value, "length", prefab.length) ||
        !readNumber(value, "sourceStart", prefab.sourceStart) || !readNumber(value, "sourceEnd", prefab.sourceEnd))
        return false;
    prefab.assetId = std::move(mesh.assetId);
    for (const auto& v : mesh.vertices) prefab.vertices.push_back({v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v});
    for (const auto& t : mesh.triangles) prefab.triangles.push_back({t.a, t.b, t.c, t.material});
    if (field(value, "middleCopies") && !readSmallInt(value, "middleCopies", 1, 4096, prefab.middleCopies))
        return false;
    if (!readOptionalBool(value, "weldCompatibleVertices", prefab.weldCompatibleVertices)) return false;
    if (!readOptionalBool(value, "automaticSpacing", prefab.automaticSpacing) ||
        !readOptionalBool(value, "averageSpacing", prefab.averageSpacing) ||
        !readOptionalBool(value, "reverseRepeatSequence", prefab.reverseRepeatSequence) ||
        !readOptionalNumber(value, "repeatSourceLength", prefab.repeatSourceLength))
        return false;
    if (const auto* encodedGroups = field(value, "smoothingGroups")) {
        const auto* groups = encodedGroups->getIf<RoadCodecValue::Array>();
        if (!groups || groups->size() != prefab.triangles.size()) return false;
        for (std::size_t i = 0; i < groups->size(); ++i) {
            const auto* group = (*groups)[i].getIf<std::int64_t>();
            if (!group || *group < 0 || *group > 65535) return false;
            prefab.triangles[i].smoothingGroup = static_cast<std::uint32_t>(*group);
        }
    }
    auto segment = [](const RoadCodecValue& encoded, procgen::road::RoadPrefabSegment& part) {
        // Source segments carry mesh only; nested segments would introduce an unbounded recursive asset.
        if (field(encoded, "startMesh") || field(encoded, "endMesh")) return false;
        procgen::road::RoadFacilityPrefab p;
        if (!decodePrefab(encoded, p) || p.middleCopies != 1 || p.sourceStart != 0.f || p.sourceEnd != 1.f ||
            p.weldCompatibleVertices || p.automaticSpacing || !p.averageSpacing || p.reverseRepeatSequence ||
            p.repeatSourceLength != 0.f)
            return false;
        part = {std::move(p.assetId), p.length, std::move(p.vertices), std::move(p.triangles)};
        return true;
    };
    if (const auto* start = field(value, "startMesh"))
        if (!segment(*start, prefab.startMesh)) return false;
    if (const auto* end = field(value, "endMesh"))
        if (!segment(*end, prefab.endMesh)) return false;
    return true;
}
RoadCodecValue encodeSection(const std::vector<procgen::road::RoadProfilePoint>& section) {
    RoadCodecValue::Array points;
    for (const auto& p : section)
        points.emplace_back(RoadCodecValue::Object{
            {"side", p.side}, {"up", p.up}, {"material", std::int64_t{static_cast<int>(p.material)}}});
    return points;
}
bool decodeSection(const RoadCodecValue& value, std::vector<procgen::road::RoadProfilePoint>& section) {
    const auto* points = value.getIf<RoadCodecValue::Array>();
    if (!points || points->size() > 128u) return false;
    section.clear();
    for (const auto& item : *points) {
        procgen::road::RoadProfilePoint p;
        int                             material = 0;
        if (!readNumber(item, "side", p.side) || !readNumber(item, "up", p.up) ||
            !readSmallInt(item, "material", 0, static_cast<int>(procgen::road::RoadMaterial::TunnelPortalAsphalt),
                          material))
            return false;
        p.material = static_cast<procgen::road::RoadMaterial>(material);
        section.push_back(p);
    }
    return true;
}
bool decodeAuthoring(const RoadCodecValue& value, procgen::road::RoadStyle& style) {
    style.crossSection.clear();
    style.structures.clear();
    style.facilities.clear();
    style.crossSectionClosed  = false;
    style.automaticStructures = true;
    if (const auto* section = field(value, "crossSection"))
        if (!decodeSection(*section, style.crossSection)) return false;
    if (!readOptionalBool(value, "crossSectionClosed", style.crossSectionClosed) ||
        !readOptionalBool(value, "automaticStructures", style.automaticStructures))
        return false;
    if (const auto* encoded = field(value, "structures")) {
        const auto* items = encoded->getIf<RoadCodecValue::Array>();
        if (!items || items->size() > 256u) return false;
        for (const auto& item : *items) {
            procgen::road::RoadStructureInterval interval;
            int                                  kind = 0;
            if (!readNumber(item, "start", interval.start) || !readNumber(item, "end", interval.end) ||
                !readSmallInt(item, "kind", 0, 1, kind) || !readNumber(item, "tunnelHeight", interval.tunnelHeight) ||
                !readNumber(item, "wallThickness", interval.wallThickness) ||
                !readOptionalNumber(item, "portalStartLength", interval.portalStartLength) ||
                !readOptionalNumber(item, "portalEndLength", interval.portalEndLength))
                return false;
            interval.kind = static_cast<procgen::road::RoadStructureKind>(kind);
            style.structures.push_back(interval);
        }
    }
    if (const auto* encoded = field(value, "facilities")) {
        const auto* items = encoded->getIf<RoadCodecValue::Array>();
        if (!items || items->size() > 64u) return false;
        for (const auto& item : *items) {
            procgen::road::RoadContinuousFacility facility;
            const auto*                           profile = field(item, "profile");
            if (!readId(item, "id", facility.id) || !readNumber(item, "start", facility.start) ||
                !readNumber(item, "end", facility.end) || !readNumber(item, "lateralOffset", facility.lateralOffset) ||
                !readNumber(item, "verticalOffset", facility.verticalOffset) ||
                !readNumber(item, "uvMeters", facility.uvMeters) ||
                !readOptionalBool(item, "capStart", facility.capStart) ||
                !readOptionalBool(item, "capEnd", facility.capEnd) || !profile ||
                !decodeSection(*profile, facility.profile))
                return false;
            if (const auto* prefab = field(item, "prefab"))
                if (!decodePrefab(*prefab, facility.prefab)) return false;
            style.facilities.push_back(std::move(facility));
        }
    }
    return true;
}
}  // namespace
RoadCodecValue encodeStyle(std::uint32_t id, const procgen::road::RoadStyle& style) {
    RoadCodecValue::Array structures, facilities;
    for (const auto& item : style.structures)
        structures.emplace_back(RoadCodecValue::Object{{"start", item.start},
                                                       {"end", item.end},
                                                       {"kind", std::int64_t{static_cast<int>(item.kind)}},
                                                       {"tunnelHeight", item.tunnelHeight},
                                                       {"wallThickness", item.wallThickness},
                                                       {"portalStartLength", item.portalStartLength},
                                                       {"portalEndLength", item.portalEndLength}});
    for (const auto& item : style.facilities)
        facilities.emplace_back(RoadCodecValue::Object{{"prefab", encodePrefab(item.prefab)},
                                                       {"id", std::int64_t{item.id}},
                                                       {"start", item.start},
                                                       {"end", item.end},
                                                       {"lateralOffset", item.lateralOffset},
                                                       {"verticalOffset", item.verticalOffset},
                                                       {"capStart", item.capStart},
                                                       {"capEnd", item.capEnd},
                                                       {"uvMeters", item.uvMeters},
                                                       {"profile", encodeSection(item.profile)}});
    return RoadCodecValue::Object{
        {"terrainPolicy",
         RoadCodecValue::Object{{"enabled", style.terrainPolicy.enabled},
                                {"blendDistance", style.terrainPolicy.blendDistance},
                                {"maxVerticalDelta", style.terrainPolicy.maxVerticalDelta},
                                {"splatLayer", std::int64_t{style.terrainPolicy.splatLayer}},
                                {"splatOpacity", style.terrainPolicy.splatOpacity},
                                {"clearDetails", style.terrainPolicy.clearDetails},
                                {"clearTrees", style.terrainPolicy.clearTrees},
                                {"cutTunnelHoles", style.terrainPolicy.cutTunnelHoles},
                                {"tunnelHolesAtPortalsOnly", style.terrainPolicy.tunnelHolesAtPortalsOnly}}},
        {"sideTerrain",
         RoadCodecValue::Object{
             {"mode", std::int64_t{static_cast<int>(style.sideTerrain.mode)}},
             {"minSide", style.sideTerrain.minSide},
             {"maxSide", style.sideTerrain.maxSide},
             {"minForward", style.sideTerrain.minForward},
             {"maxForward", style.sideTerrain.maxForward},
             {"samplesPerAxis", std::int64_t{style.sideTerrain.samplesPerAxis}},
             {"alignment", std::int64_t{static_cast<int>(style.sideTerrain.alignment)}},
             {"assetRef", style.sideTerrain.asset ? style.sideTerrain.asset->asset.format() : std::string{}},
             {"assetBuildId", style.sideTerrain.asset ? style.sideTerrain.asset->buildId.format() : std::string{}},
             {"assetRootPose",
              std::int64_t{style.sideTerrain.asset ? static_cast<int>(style.sideTerrain.asset->rootPose) : 0}}}},
        {"crossSection", encodeSection(style.crossSection)},
        {"samplingSpacingMeters", style.samplingSpacingMeters},
        {"samplingPolicy", std::int64_t{static_cast<int>(style.samplingPolicy)}},
        {"samplingAngleDegrees", style.samplingAngleDegrees},
        {"crossSectionSubdivisions", std::int64_t{style.crossSectionSubdivisions}},
        {"widthInterpolation", std::int64_t{static_cast<int>(style.widthInterpolation)}},
        {"frameOrientation", std::int64_t{static_cast<int>(style.frameOrientation)}},
        {"rollInterpolation", std::int64_t{static_cast<int>(style.rollInterpolation)}},
        {"crossSectionClosed", style.crossSectionClosed},
        {"automaticStructures", style.automaticStructures},
        {"structures", std::move(structures)},
        {"facilities", std::move(facilities)},
        {"id", std::int64_t{id}},
        {"laneWidth", style.laneWidth},
        {"curbWidth", style.curbWidth},
        {"curbHeight", style.curbHeight},
        {"sidewalkWidth", style.sidewalkWidth},
        {"sidewalkHeight", style.sidewalkHeight},
        {"deckThickness", style.deckThickness},
        {"pierWidth", style.pierWidth},
        {"pierDepth", style.pierDepth},
        {"pierSpacing", style.pierSpacing},
        {"pierClearance", style.pierClearance},
        {"markingWidth", style.markingWidth},
        {"dashLength", style.dashLength},
        {"dashGap", style.dashGap},
        {"uvMeters", style.uvMeters},
        {"speedLimitMps", style.speedLimitMps},
        {"trafficPriority", std::int64_t{style.trafficPriority}},
        {"sideObjectStartOffset", style.sideObjectStartOffset},
        {"sideObjectDistribution", std::int64_t{static_cast<int>(style.sideObjectDistribution)}},
        {"sideObjectEndOffset", style.sideObjectEndOffset},
        {"sidePlacement",
         RoadCodecValue::Object{{"reference", std::int64_t{static_cast<int>(style.sidePlacement.reference)}},
                                {"lateralOffset", style.sidePlacement.lateralOffset},
                                {"verticalOffset", style.sidePlacement.verticalOffset},
                                {"yawDegrees", style.sidePlacement.yawDegrees},
                                {"pathSampling", std::int64_t{static_cast<int>(style.sidePlacement.pathSampling)}}}},
        {"sideObjectsLeft", style.sideObjectsLeft},
        {"sideObjectsRight", style.sideObjectsRight}};
}

bool readPolicyBool(const RoadCodecValue& value, const char* name, bool& output) {
    return field(value, name) && readOptionalBool(value, name, output);
}
bool decodeStyle(const RoadCodecValue& value, std::uint32_t& id, procgen::road::RoadStyle& style) {
    style.terrainPolicy = {};
    if (const auto* encoded = field(value, "terrainPolicy")) {
        auto& policy = style.terrainPolicy;
        if (!encoded->template getIf<RoadCodecValue::Object>() ||
            !readPolicyBool(*encoded, "enabled", policy.enabled) ||
            !readNumber(*encoded, "blendDistance", policy.blendDistance) ||
            !readNumber(*encoded, "maxVerticalDelta", policy.maxVerticalDelta) ||
            !readSmallInt(*encoded, "splatLayer", -1, std::numeric_limits<int>::max(), policy.splatLayer) ||
            !readNumber(*encoded, "splatOpacity", policy.splatOpacity) ||
            !readPolicyBool(*encoded, "clearDetails", policy.clearDetails) ||
            !readPolicyBool(*encoded, "clearTrees", policy.clearTrees) ||
            !readPolicyBool(*encoded, "cutTunnelHoles", policy.cutTunnelHoles) ||
            !readPolicyBool(*encoded, "tunnelHolesAtPortalsOnly", policy.tunnelHolesAtPortalsOnly))
            return false;
    }

    style.sidePlacement = {};
    if (const auto* placement = field(value, "sidePlacement")) {
        int reference = 0;
        if (!placement->template getIf<RoadCodecValue::Object>() ||
            !readSmallInt(*placement, "reference", 0, 2, reference) ||
            !readNumber(*placement, "lateralOffset", style.sidePlacement.lateralOffset) ||
            !readNumber(*placement, "verticalOffset", style.sidePlacement.verticalOffset) ||
            !readNumber(*placement, "yawDegrees", style.sidePlacement.yawDegrees))
            return false;
        style.sidePlacement.reference = static_cast<procgen::road::RoadSideReference>(reference);
        int pathSampling              = 0;
        if (field(*placement, "pathSampling") && !readSmallInt(*placement, "pathSampling", 0, 1, pathSampling))
            return false;
        style.sidePlacement.pathSampling = static_cast<procgen::road::RoadSidePathSampling>(pathSampling);
    }
    style.sideTerrain = {};
    if (const auto* terrain = field(value, "sideTerrain")) {
        int mode = 0;
        if (!terrain->template getIf<RoadCodecValue::Object>() || !readSmallInt(*terrain, "mode", 0, 4, mode) ||
            !readNumber(*terrain, "minSide", style.sideTerrain.minSide) ||
            !readNumber(*terrain, "maxSide", style.sideTerrain.maxSide) ||
            !readNumber(*terrain, "minForward", style.sideTerrain.minForward) ||
            !readNumber(*terrain, "maxForward", style.sideTerrain.maxForward) ||
            !readSmallInt(*terrain, "samplesPerAxis", 2, 17, style.sideTerrain.samplesPerAxis))
            return false;
        style.sideTerrain.mode = static_cast<procgen::road::RoadSideTerrainMode>(mode);
        int alignment          = 0;
        if (field(*terrain, "alignment") && !readSmallInt(*terrain, "alignment", 0, 3, alignment)) return false;
        style.sideTerrain.alignment = static_cast<procgen::road::RoadSideAlignment>(alignment);
        std::string assetText, buildText;
        if (const auto* item = field(*terrain, "assetRef")) {
            const auto* text = item->template getIf<std::string>();
            if (!text) return false;
            assetText = *text;
        }
        if (const auto* item = field(*terrain, "assetBuildId")) {
            const auto* text = item->template getIf<std::string>();
            if (!text) return false;
            buildText = *text;
        }
        if (!assetText.empty() || !buildText.empty()) {
            auto asset = eve::AssetRef::parse(assetText);
            auto build = eve::PersistentId::parse(buildText);
            if (!asset.ok()) return false;
            if (!build || build->isNil() || asset.value().format() != assetText || build->format() != buildText)
                return false;
            int rootPose = 0;
            if (field(*terrain, "assetRootPose") && !readSmallInt(*terrain, "assetRootPose", 0, 1, rootPose))
                return false;
            style.sideTerrain.asset =
                procgen::road::RoadSideAssetBinding{asset.value(), *build, static_cast<eve::PrefabRootPose>(rootPose)};
        }
    }
    // Missing historical fields preserve the pre-metric style, independently of new author defaults.
    style.samplingSpacingMeters    = 0.f;
    style.samplingAngleDegrees     = 4.f;
    style.crossSectionSubdivisions = 1;
    int samplingPolicy             = 0;
    if (field(value, "samplingPolicy") && !readSmallInt(value, "samplingPolicy", 0, 1, samplingPolicy)) return false;
    style.samplingPolicy = static_cast<procgen::road::RoadSamplingPolicy>(samplingPolicy);
    if (!readOptionalNumber(value, "samplingSpacingMeters", style.samplingSpacingMeters) ||
        !readOptionalNumber(value, "samplingAngleDegrees", style.samplingAngleDegrees) ||
        (field(value, "crossSectionSubdivisions") &&
         !readSmallInt(value, "crossSectionSubdivisions", 1, 32, style.crossSectionSubdivisions)))
        return false;
    int widthInterpolation = 0;  // Missing schema-v1 field preserves the original linear profile.
    if (field(value, "widthInterpolation") && !readSmallInt(value, "widthInterpolation", 0, 1, widthInterpolation))
        return false;
    style.widthInterpolation = static_cast<procgen::road::RoadWidthInterpolation>(widthInterpolation);
    int rollInterpolation    = 0;
    if (field(value, "rollInterpolation") && !readSmallInt(value, "rollInterpolation", 0, 1, rollInterpolation))
        return false;
    style.rollInterpolation = static_cast<procgen::road::RoadRollInterpolation>(rollInterpolation);
    int frameOrientation    = 0;
    if (field(value, "frameOrientation") && !readSmallInt(value, "frameOrientation", 0, 1, frameOrientation))
        return false;
    style.frameOrientation = static_cast<procgen::road::RoadFrameOrientation>(frameOrientation);
    int sideDistribution   = 0;
    if (field(value, "sideObjectDistribution") &&
        !readSmallInt(value, "sideObjectDistribution", 0, 2, sideDistribution))
        return false;
    style.sideObjectDistribution = static_cast<procgen::road::RoadSideDistribution>(sideDistribution);
    return readId(value, "id", id) && readNumber(value, "laneWidth", style.laneWidth) &&
           readNumber(value, "curbWidth", style.curbWidth) && readNumber(value, "curbHeight", style.curbHeight) &&
           readNumber(value, "sidewalkWidth", style.sidewalkWidth) &&
           readNumber(value, "sidewalkHeight", style.sidewalkHeight) &&
           readNumber(value, "deckThickness", style.deckThickness) && readNumber(value, "pierWidth", style.pierWidth) &&
           readNumber(value, "pierDepth", style.pierDepth) && readNumber(value, "pierSpacing", style.pierSpacing) &&
           readNumber(value, "pierClearance", style.pierClearance) &&
           readNumber(value, "markingWidth", style.markingWidth) && readNumber(value, "dashLength", style.dashLength) &&
           readNumber(value, "dashGap", style.dashGap) && readNumber(value, "uvMeters", style.uvMeters) &&
           readNumber(value, "speedLimitMps", style.speedLimitMps) &&
           readSmallInt(value, "trafficPriority", 0, 255, style.trafficPriority) &&
           readOptionalNumber(value, "sideObjectStartOffset", style.sideObjectStartOffset) &&
           readOptionalNumber(value, "sideObjectEndOffset", style.sideObjectEndOffset) &&
           readOptionalBool(value, "sideObjectsLeft", style.sideObjectsLeft) &&
           readOptionalBool(value, "sideObjectsRight", style.sideObjectsRight) && decodeAuthoring(value, style);
}
