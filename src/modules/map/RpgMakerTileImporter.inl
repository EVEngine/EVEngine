// RPG Maker MV/MZ adapter implementation split from TileConfig.cpp.
// Included inside namespace eve::map so it can reuse the import parser helpers.
namespace {
using Quarter                               = std::array<uint8_t, 8>;
constexpr std::array<Quarter, 48> kRpgFloor = {
    {{{2, 4, 1, 4, 2, 3, 1, 3}}, {{2, 0, 1, 4, 2, 3, 1, 3}}, {{2, 4, 3, 0, 2, 3, 1, 3}}, {{2, 0, 3, 0, 2, 3, 1, 3}},
     {{2, 4, 1, 4, 2, 3, 3, 1}}, {{2, 0, 1, 4, 2, 3, 3, 1}}, {{2, 4, 3, 0, 2, 3, 3, 1}}, {{2, 0, 3, 0, 2, 3, 3, 1}},
     {{2, 4, 1, 4, 2, 1, 1, 3}}, {{2, 0, 1, 4, 2, 1, 1, 3}}, {{2, 4, 3, 0, 2, 1, 1, 3}}, {{2, 0, 3, 0, 2, 1, 1, 3}},
     {{2, 4, 1, 4, 2, 1, 3, 1}}, {{2, 0, 1, 4, 2, 1, 3, 1}}, {{2, 4, 3, 0, 2, 1, 3, 1}}, {{2, 0, 3, 0, 2, 1, 3, 1}},
     {{0, 4, 1, 4, 0, 3, 1, 3}}, {{0, 4, 3, 0, 0, 3, 1, 3}}, {{0, 4, 1, 4, 0, 3, 3, 1}}, {{0, 4, 3, 0, 0, 3, 3, 1}},
     {{2, 2, 1, 2, 2, 3, 1, 3}}, {{2, 2, 1, 2, 2, 3, 3, 1}}, {{2, 2, 1, 2, 2, 1, 1, 3}}, {{2, 2, 1, 2, 2, 1, 3, 1}},
     {{2, 4, 3, 4, 2, 3, 3, 3}}, {{2, 4, 3, 4, 2, 1, 3, 3}}, {{2, 0, 3, 4, 2, 3, 3, 3}}, {{2, 0, 3, 4, 2, 1, 3, 3}},
     {{2, 4, 1, 4, 2, 5, 1, 5}}, {{2, 0, 1, 4, 2, 5, 1, 5}}, {{2, 4, 3, 0, 2, 5, 1, 5}}, {{2, 0, 3, 0, 2, 5, 1, 5}},
     {{0, 4, 3, 4, 0, 3, 3, 3}}, {{2, 2, 1, 2, 2, 5, 1, 5}}, {{0, 2, 1, 2, 0, 3, 1, 3}}, {{0, 2, 1, 2, 0, 3, 3, 1}},
     {{2, 2, 3, 2, 2, 3, 3, 3}}, {{2, 2, 3, 2, 2, 1, 3, 3}}, {{2, 4, 3, 4, 2, 5, 3, 5}}, {{2, 0, 3, 4, 2, 5, 3, 5}},
     {{0, 4, 1, 4, 0, 5, 1, 5}}, {{0, 4, 3, 0, 0, 5, 1, 5}}, {{0, 2, 3, 2, 0, 3, 3, 3}}, {{0, 2, 1, 2, 0, 5, 1, 5}},
     {{0, 4, 3, 4, 0, 5, 3, 5}}, {{2, 2, 3, 2, 2, 5, 3, 5}}, {{0, 2, 3, 2, 0, 5, 3, 5}}, {{0, 0, 1, 0, 0, 1, 1, 1}}}};
constexpr std::array<Quarter, 16> kRpgWall      = {{{{2, 2, 1, 2, 2, 1, 1, 1}},
                                                    {{0, 2, 1, 2, 0, 1, 1, 1}},
                                                    {{2, 0, 1, 0, 2, 1, 1, 1}},
                                                    {{0, 0, 1, 0, 0, 1, 1, 1}},
                                                    {{2, 2, 3, 2, 2, 1, 3, 1}},
                                                    {{0, 2, 3, 2, 0, 1, 3, 1}},
                                                    {{2, 0, 3, 0, 2, 1, 3, 1}},
                                                    {{0, 0, 3, 0, 0, 1, 3, 1}},
                                                    {{2, 2, 1, 2, 2, 3, 1, 3}},
                                                    {{0, 2, 1, 2, 0, 3, 1, 3}},
                                                    {{2, 0, 1, 0, 2, 3, 1, 3}},
                                                    {{0, 0, 1, 0, 0, 3, 1, 3}},
                                                    {{2, 2, 3, 2, 2, 3, 3, 3}},
                                                    {{0, 2, 3, 2, 0, 3, 3, 3}},
                                                    {{2, 0, 3, 0, 2, 3, 3, 3}},
                                                    {{0, 0, 3, 0, 0, 3, 3, 3}}}};
constexpr std::array<Quarter, 4>  kRpgWaterfall = {
    {{{2, 0, 1, 0, 2, 1, 1, 1}}, {{0, 0, 1, 0, 0, 1, 1, 1}}, {{2, 0, 3, 0, 2, 1, 3, 1}}, {{0, 0, 3, 0, 0, 1, 3, 1}}}};

TileLayer::Tileset::Visual rpgMakerVisual(int gid) {
    TileLayer::Tileset::Visual visual;
    visual.gid         = gid;
    constexpr int tile = 48, quarter = 24, a1 = 2048, a2 = 2816, a3 = 4352, a4 = 5888;
    if (gid < a1) {
        visual.x      = ((gid / 128 % 2) * 8 + gid % 8) * tile;
        visual.y      = ((gid % 256 / 8) % 16) * tile;
        visual.width  = tile;
        visual.height = tile;
        return visual;
    }
    const int      kind  = (gid - a1) / 48;
    const int      shape = (gid - a1) % 48;
    const int      tx = kind % 8, ty = kind / 8;
    int            bx = 0, by = 0;
    const Quarter *table        = nullptr;
    int            frameCount   = 1;
    bool           waterSurface = false, waterfall = false;
    if (gid < a2) {
        if (kind == 0) {
            bx           = 0;
            by           = 0;
            waterSurface = true;
        } else if (kind == 1) {
            bx           = 0;
            by           = 3;
            waterSurface = true;
        } else if (kind == 2) {
            bx = 6;
            by = 0;
        } else if (kind == 3) {
            bx = 6;
            by = 3;
        } else {
            bx = (tx / 4) * 8;
            by = ty * 6 + (tx / 2 % 2) * 3;
            if (kind % 2 == 0)
                waterSurface = true;
            else {
                bx += 6;
                waterfall = true;
            }
        }
        if (waterSurface) frameCount = 4;
        if (waterfall) frameCount = 3;
        table = waterfall ? &kRpgWaterfall[size_t(shape % 4)] : &kRpgFloor[size_t(shape)];
    } else if (gid < a3) {
        bx    = tx * 2;
        by    = (ty - 2) * 3;
        table = &kRpgFloor[size_t(shape)];
    } else if (gid < a4) {
        bx    = tx * 2;
        by    = (ty - 6) * 2;
        table = &kRpgWall[size_t(shape % 16)];
    } else {
        bx    = tx * 2;
        by    = int((ty - 10) * 2.5f + (ty % 2 == 1 ? 0.5f : 0.f));
        table = ty % 2 == 1 ? &kRpgWall[size_t(shape % 16)] : &kRpgFloor[size_t(shape)];
    }
    const int waterIndices[4] = {0, 1, 2, 1};
    for (int frame = 0; frame < frameCount; ++frame) {
        TileLayer::Tileset::Visual::SubtileFrame output;
        output.durationMs = 500;
        int frameBx = bx, frameBy = by;
        if (waterSurface) frameBx += waterIndices[frame] * 2;
        if (waterfall) frameBy += frame;
        for (int part = 0; part < 4; ++part) {
            output.parts.push_back({(frameBx * 2 + (*table)[size_t(part * 2)]) * quarter,
                                    (frameBy * 2 + (*table)[size_t(part * 2 + 1)]) * quarter, quarter, quarter,
                                    float((part % 2) * quarter), float((part / 2) * quarter)});
        }
        visual.subtileFrames.push_back(std::move(output));
    }
    return visual;
}
}  // namespace

TileLayer::Tileset::Visual decodeRpgMakerTileVisual(int tileId) { return rpgMakerVisual(tileId); }

eve::Result<RpgMakerImportReceipt> importRpgMakerMap(const std::string &mapPath, const std::string &tilesetsPath,
                                                     const std::string &sourceEngine) {
    auto fail = [](const std::string &code, const std::string &message, const std::string &path) {
        return eve::Result<RpgMakerImportReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, message, path, {{"domain_code", code}}, "map.rpgmaker"));
    };
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    std::string mapText, tilesetsText;
    if (!readImportText(fs, mapPath, mapText))
        return fail("map.import.rpgmaker.map-read-failed", "Could not read RPG Maker map", mapPath);
    if (!readImportText(fs, tilesetsPath, tilesetsText))
        return fail("map.import.rpgmaker.tilesets-read-failed", "Could not read Tilesets.json", tilesetsPath);

    auto                               *dataModule = eve::data::DataModule::create();
    std::string                         decodeError;
    std::unique_ptr<data::JsonDocument> mapDocument(dataModule->decodeJson(mapText, &decodeError));
    if (!mapDocument || !mapDocument->isObject()) return fail("map.import.rpgmaker.map-invalid", decodeError, mapPath);
    std::unique_ptr<data::JsonDocument> tilesetsDocument(dataModule->decodeJson(tilesetsText, &decodeError));
    if (!tilesetsDocument || !tilesetsDocument->isArray())
        return fail("map.import.rpgmaker.tilesets-invalid", decodeError, tilesetsPath);

    auto                   map       = mapDocument->object();
    const int              width     = map && map->has("width") ? asInt(map->get("width"), 0) : 0;
    const int              height    = map && map->has("height") ? asInt(map->get("height"), 0) : 0;
    const int              tilesetId = map && map->has("tilesetId") ? asInt(map->get("tilesetId"), 0) : 0;
    Poco::JSON::Array::Ptr rawData;
    try {
        rawData = map ? map->getArray("data") : nullptr;
    } catch (...) {
    }
    const size_t planeSize = size_t(std::max(0, width) * std::max(0, height));
    if (width <= 0 || height <= 0 || !rawData || rawData->size() < planeSize * 4)
        return fail("map.import.rpgmaker.map-shape-invalid", "Map data must contain four tile planes", mapPath);

    auto                    tilesets = tilesetsDocument->array();
    Poco::JSON::Object::Ptr tileset;
    try {
        if (tilesets && tilesetId > 0 && size_t(tilesetId) < tilesets->size())
            tileset = tilesets->getObject(unsigned(tilesetId));
    } catch (...) {
    }
    if (!tileset) return fail("map.import.rpgmaker.tileset-not-found", "Map tilesetId is not present", tilesetsPath);

    Poco::JSON::Array::Ptr names, flags;
    try {
        names = tileset->getArray("tilesetNames");
        flags = tileset->getArray("flags");
    } catch (...) {
    }
    if (!names || names->size() < 9 || !flags)
        return fail("map.import.rpgmaker.tileset-shape-invalid", "Tileset requires nine sheet names and passage flags",
                    tilesetsPath);

    std::array<std::vector<uint32_t>, 4> planes;
    for (int z = 0; z < 4; ++z) {
        planes[size_t(z)].resize(planeSize);
        for (size_t cell = 0; cell < planeSize; ++cell)
            planes[size_t(z)][cell] = uint32_t(asInt(rawData->get(unsigned(size_t(z) * planeSize + cell)), 0));
    }

    struct SheetSpec {
        int nameIndex;
        int firstGid;
        int columns;
    };
    constexpr std::array<SheetSpec, 9> sheets      = {{{5, 1, 16},
                                                       {6, 256, 16},
                                                       {7, 512, 16},
                                                       {8, 768, 16},
                                                       {4, 1536, 8},
                                                       {0, 2048, 8},
                                                       {1, 2816, 8},
                                                       {2, 4352, 8},
                                                       {3, 5888, 8}}};
    const std::filesystem::path        projectRoot = std::filesystem::path(tilesetsPath).parent_path().parent_path();
    std::vector<TilesetInfo>           infos;
    for (const auto &sheet : sheets) {
        const std::string name = asString(names->get(unsigned(sheet.nameIndex)));
        if (name.empty()) continue;
        TilesetInfo info;
        info.firstGid = sheet.firstGid;
        info.columns  = sheet.columns;
        info.tileW    = 48;
        info.tileH    = 48;
        info.image    = (projectRoot / "img" / "tilesets" / (name + ".png")).lexically_normal().generic_string();
        infos.push_back(std::move(info));
    }
    if (infos.empty())
        return fail("map.import.rpgmaker.tileset-images-empty", "Tileset has no named sheets", tilesetsPath);

    RpgMakerImportReceipt receipt;
    receipt.sourceEngine     = sourceEngine;
    receipt.tilesetId        = tilesetId;
    constexpr uint32_t kStar = 1u << 0, kLadder = 1u << 1, kBush = 1u << 2, kCounter = 1u << 3, kDamage = 1u << 4;
    for (int z = 0; z < 4; ++z) {
        TileLayer *layer     = TileLayer::createLayer(width, height, 48.f, 48.f);
        layer->tiles()->gids = planes[size_t(z)];
        applyTilesets(layer, infos);
        layer->setLayer(z);
        layer->resource()->path            = mapPath;
        layer->resource()->sourceEngine    = sourceEngine;
        layer->resource()->sourceVersion   = "MV/MZ";
        layer->resource()->dependencyPaths = {tilesetsPath};
        std::vector<int> configuredGids;
        for (uint32_t rawGid : planes[size_t(z)]) {
            const int gid = int(tileGid(rawGid));
            if (gid <= 0 || size_t(gid) >= flags->size()) continue;
            if (std::find(configuredGids.begin(), configuredGids.end(), gid) != configuredGids.end()) continue;
            configuredGids.push_back(gid);
            layer->tileset()->visuals.push_back(rpgMakerVisual(gid));
            const int flag       = asInt(flags->get(unsigned(gid)), 0);
            uint8_t   directions = 0x0f;
            if (flag & 0x08) directions &= uint8_t(~0x01);  // up / north
            if (flag & 0x04) directions &= uint8_t(~0x02);  // right / east
            if (flag & 0x01) directions &= uint8_t(~0x04);  // down / south
            if (flag & 0x02) directions &= uint8_t(~0x08);  // left / west
            const bool star      = (flag & 0x10) != 0;
            const bool walkable  = star || directions != 0;
            uint32_t   semantics = 0;
            if (star) semantics |= kStar;
            if (flag & 0x20) semantics |= kLadder;
            if (flag & 0x40) semantics |= kBush;
            if (flag & 0x80) semantics |= kCounter;
            if (flag & 0x100) semantics |= kDamage;
            semantics |= uint32_t((flag >> 12) & 0x0f) << 8;
            layer->setTileNavigationProfile(gid, walkable, 1.f, directions, directions, !star && directions == 0,
                                            int(semantics));
        }
        layer->rebuildSpatialIndex();
        receipt.layers.push_back(layer);
    }

    receipt.navigationLayer = TileLayer::createLayer(width, height, 48.f, 48.f);
    receipt.navigationLayer->setVisible(false);
    receipt.navigationLayer->resource()->path          = mapPath;
    receipt.navigationLayer->resource()->sourceEngine  = sourceEngine;
    receipt.navigationLayer->resource()->sourceVersion = "MV/MZ navigation projection";
    struct ProfileKey {
        uint8_t  directions = 0x0f;
        uint32_t semantics  = 0;
    };
    std::vector<std::pair<ProfileKey, int>> profiles;
    auto                                   &navigationGids = receipt.navigationLayer->tiles()->gids;
    for (size_t cell = 0; cell < planeSize; ++cell) {
        ProfileKey profile;
        profile.directions           = 0;
        constexpr int     rpgBits[4] = {0x08, 0x04, 0x01, 0x02};
        constexpr uint8_t evBits[4]  = {0x01, 0x02, 0x04, 0x08};
        for (int direction = 0; direction < 4; ++direction) {
            bool allowed = true;
            for (int z = 3; z >= 0; --z) {
                const int gid = int(tileGid(planes[size_t(z)][cell]));
                if (gid <= 0 || size_t(gid) >= flags->size()) continue;
                const int flag = asInt(flags->get(unsigned(gid)), 0);
                if (flag & 0x10) continue;
                allowed = (flag & rpgBits[direction]) == 0;
                break;
            }
            if (allowed) profile.directions |= evBits[direction];
        }
        for (int z = 0; z < 4; ++z) {
            const int gid = int(tileGid(planes[size_t(z)][cell]));
            if (gid <= 0 || size_t(gid) >= flags->size()) continue;
            const int flag = asInt(flags->get(unsigned(gid)), 0);
            if (flag & 0x10) profile.semantics |= kStar;
            if (flag & 0x20) profile.semantics |= kLadder;
            if (flag & 0x40) profile.semantics |= kBush;
            if (flag & 0x80) profile.semantics |= kCounter;
            if (flag & 0x100) profile.semantics |= kDamage;
            profile.semantics |= uint32_t((flag >> 12) & 0x0f) << 8;
        }
        auto found      = std::find_if(profiles.begin(), profiles.end(), [&](const auto &entry) {
            return entry.first.directions == profile.directions && entry.first.semantics == profile.semantics;
        });
        int  profileGid = 0;
        if (found == profiles.end()) {
            profileGid = 9000 + int(profiles.size());
            profiles.push_back({profile, profileGid});
            receipt.navigationLayer->setTileNavigationProfile(profileGid, profile.directions != 0, 1.f,
                                                              profile.directions, profile.directions,
                                                              profile.directions == 0, int(profile.semantics));
        } else {
            profileGid = found->second;
        }
        navigationGids[cell] = uint32_t(profileGid);
    }
    receipt.navigationLayer->rebuildSpatialIndex();

    if (rawData->size() >= planeSize * 5) {
        bool hasShadow = false;
        for (size_t cell = 0; cell < planeSize; ++cell)
            hasShadow = hasShadow || (asInt(rawData->get(unsigned(planeSize * 4 + cell)), 0) & 0x0f);
        if (hasShadow) {
            receipt.shadowLayer = TileLayer::createLayer(width * 2, height * 2, 24.f, 24.f);
            receipt.shadowLayer->setLayer(2);
            receipt.shadowLayer->setTint(0.f, 0.f, 0.f, 0.5f);
            for (size_t cell = 0; cell < planeSize; ++cell) {
                const int bits = asInt(rawData->get(unsigned(planeSize * 4 + cell)), 0) & 0x0f;
                const int x = int(cell % size_t(width)), y = int(cell / size_t(width));
                for (int part = 0; part < 4; ++part)
                    if (bits & (1 << part)) receipt.shadowLayer->setTile(x * 2 + part % 2, y * 2 + part / 2, 1);
            }
            receipt.shadowLayer->resource()->path          = mapPath;
            receipt.shadowLayer->resource()->sourceEngine  = sourceEngine;
            receipt.shadowLayer->resource()->sourceVersion = "MV/MZ shadow projection";
        }
    }
    return eve::Result<RpgMakerImportReceipt>::success(std::move(receipt),
                                                       eve::Status::success(eve::StatusCode::Applied));
}
