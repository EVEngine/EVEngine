std::string base64Encode(const uint8_t *data, size_t size) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string       out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < size) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < size) ? tbl[v & 63] : '=';
    }
    return out;
}

void appendF32LE(std::vector<uint8_t> &bin, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    bin.push_back(uint8_t(bits));
    bin.push_back(uint8_t(bits >> 8));
    bin.push_back(uint8_t(bits >> 16));
    bin.push_back(uint8_t(bits >> 24));
}

void appendU16LE(std::vector<uint8_t> &bin, uint16_t v) {
    bin.push_back(uint8_t(v));
    bin.push_back(uint8_t(v >> 8));
}

/**
 * Minimal glTF 2.0 quad (XY plane facing +Z) with an embedded PNG base color
 * texture: top half red, bottom half blue. Y=+1 maps to v=0 so a correctly
 * oriented render shows red on top / blue on bottom.
 */
std::string makeUvOrientationGltf(const std::string &pngBase64) {
    std::vector<uint8_t> bin;
    const float          pos[4][3] = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
    const float          nrm[4][3] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
    const float          uv[4][2]  = {{0.f, 1.f}, {1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f}};
    for (const auto &p : pos)
        for (float c : p) appendF32LE(bin, c);
    for (const auto &n : nrm)
        for (float c : n) appendF32LE(bin, c);
    for (const auto &t : uv)
        for (float c : t) appendF32LE(bin, c);
    const uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    for (uint16_t i : idx) appendU16LE(bin, i);
    if (bin.size() != 140) return {};

    const std::string binBase64 = base64Encode(bin.data(), bin.size());
    return std::string(
               "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
               "\"nodes\":[{\"mesh\":0}],"
               "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
               "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
               "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
               "\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}],"
               "\"textures\":[{\"source\":0}],"
               "\"images\":[{\"uri\":\"data:image/png;base64,") +
           pngBase64 + "\"}],\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," + binBase64 +
           "\",\"byteLength\":140}],\"bufferViews\":[" +
           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12,\"target\":34963}],"
           "\"accessors\":[" +
           "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"}," +
           "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";
}


void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

