#include "voxel/CubeTypeRegistry.h"

#include "common/Json.h"

#include <cstring>

namespace eve::voxel {

namespace {

using eve::json::Value;

const CubeTypeRegistry &kEmptyRegistry() {
    static const CubeTypeRegistry reg;
    return reg;
}

// 把某体素在面 dir 上应显示的纹理，从“基础类型 faceTex + orientation”旋转出来。
// faceTex_variant[dir] = faceTex_base[rotateFaceY(dir, -orientation)]。
void rotateFaceTex(const uint8_t src[6], int orientation, uint8_t dst[6]) {
    for (int d = 0; d < 6; ++d) {
        const FaceDir srcDir = rotateFaceY(FaceDir(d), (4 - (orientation & 3)) & 3);
        dst[d] = src[int(srcDir)];
    }
}

void readFaceTex(Value o, uint8_t out[6]) {
    std::memset(out, 0, sizeof(uint8_t) * 6);
    const Value arr = o.get("faceTex");
    const size_t n = arr.size() < 6 ? arr.size() : 6;
    for (size_t i = 0; i < n; ++i) out[i] = uint8_t(arr.at(i).asInt(0) & 0xFF);
}

CubeType parseCubeType(Value o) {
    CubeType t;
    if (!o.isObject()) return t;
    t.name = o.getString("name");
    readFaceTex(o, t.faceTex);
    t.directional = o.getBool("directional", false);
    t.composeGroup = o.getString("composeGroup");
    t.connects = o.getBool("connects", false);
    return t;
}

}  // namespace

const CubeTypeRegistry &CubeTypeRegistry::empty() { return kEmptyRegistry(); }

uint8_t CubeTypeRegistry::add(const CubeType &type) {
    if (type.name.empty()) return 0;
    auto it = byName_.find(type.name);
    if (it != byName_.end()) return it->second;

    const int variants = type.directional ? 4 : 1;
    // 上限：id 为 uint8_t，0 保留给空气，最多 255 个类型槽位。
    if (int(types_.size()) + variants > 256) return 0;

    const uint8_t baseId = uint8_t(types_.size());
    for (int o = 0; o < variants; ++o) {
        CubeType v = type;
        v.id = uint8_t(baseId + o);
        if (type.directional && o > 0) {
            rotateFaceTex(type.faceTex, o, v.faceTex);
        }
        types_.push_back(v);
    }
    byName_[type.name] = baseId;
    return baseId;
}

int CubeTypeRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    const Value root = doc.root();
    int n = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) {
            CubeType t = parseCubeType(root.at(i));
            if (t.name.empty()) continue;
            add(t);
            ++n;
        }
    } else if (root.isObject()) {
        CubeType t = parseCubeType(root);
        if (!t.name.empty()) {
            add(t);
            ++n;
        }
    }
    return n;
}

const CubeType *CubeTypeRegistry::find(const std::string &name) const {
    auto it = byName_.find(name);
    if (it == byName_.end()) return nullptr;
    return &types_[it->second];
}

const CubeType *CubeTypeRegistry::find(uint8_t id) const {
    if (id == 0 || size_t(id) >= types_.size()) return nullptr;
    return &types_[id];
}

uint8_t CubeTypeRegistry::variantId(const std::string &name, int orientation) const {
    auto it = byName_.find(name);
    if (it == byName_.end()) return 0;
    const CubeType &base = types_[it->second];
    if (!base.directional) return base.id;
    return uint8_t(base.id + (orientation & 3));
}

void CubeTypeRegistry::clear() {
    byName_.clear();
    types_.clear();
    types_.push_back(CubeType{});  // index 0 = 空气占位
}

}  // namespace eve::voxel
