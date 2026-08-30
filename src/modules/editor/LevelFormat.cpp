#include "editor/LevelFormat.h"
#include "common/Json.h"
#include "editor/LevelDocument.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace eve::editor {
namespace {
std::string quote(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 32)
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else
                    o << char(c);
        }
    }
    return o.str() + '"';
}
void properties(std::ostringstream& o, const std::unordered_map<std::string, std::string>& p, bool tiled = false) {
    o << (tiled ? '[' : '{');
    bool first = true;
    for (const auto& v : p) {
        if (!first) o << ',';
        first = false;
        if (tiled)
            o << "{\"name\":" << quote(v.first) << ",\"type\":\"string\",\"value\":" << quote(v.second) << '}';
        else
            o << quote(v.first) << ':' << quote(v.second);
    }
    o << (tiled ? ']' : '}');
}
void readProperties(eve::json::Value v, std::unordered_map<std::string, std::string>& out) {
    if (v.isObject())
        for (const auto& k : v.keys()) out[k] = v.get(k.c_str()).asString();
    else if (v.isArray())
        for (size_t i = 0; i < v.size(); ++i) {
            auto p                   = v.at(i);
            out[p.getString("name")] = p.get("value").asString();
        }
}

class JsonLevelFormat final : public LevelFormat {
public:
    std::string              id() const override { return "eve.level"; }
    std::vector<std::string> extensions() const override { return {".level.json", ".evelevel"}; }
    bool                     canRead(const std::string& s) const override {
        auto d = eve::json::Document::parse(s);
        return d.valid() && d.root().getString("format") == "eve.level";
    }
    std::unique_ptr<LevelDocument> read(const std::string& s, std::string& error) const override {
        return readJson(s, error, false);
    }
    bool write(const LevelDocument& d, std::string& text, std::string&) const override {
        text = writeJson(d, false);
        return true;
    }
    static std::unique_ptr<LevelDocument> readJson(const std::string& s, std::string& error, bool tiled) {
        auto json = eve::json::Document::parse(s, &error);
        if (!json.valid()) return {};
        auto  r = json.root();
        int   w = r.getInt("width"), h = r.getInt("height");
        float tw = r.getFloat("tilewidth", 32), th = r.getFloat("tileheight", 32);
        if (w < 1 || h < 1) {
            error = "map dimensions must be positive";
            return {};
        }
        auto out = std::make_unique<LevelDocument>(w, h, tw, th);
        out->setOrientation(r.getString("orientation", "orthogonal"));
        readProperties(r.get("properties"), out->properties());
        auto ls = r.get("layers");
        for (size_t i = 0; i < ls.size(); ++i) {
            auto        v    = ls.at(i);
            std::string kind = v.getString("type");
            bool        obj  = kind == "objectgroup" || kind == "objects";
            int         li   = obj ? out->addObjectLayer(v.getString("name")) : out->addTileLayer(v.getString("name"));
            auto*       l    = out->layer(li);
            l->id            = tiled ? std::to_string(v.getInt("id", int(i + 1))) : v.getString("id", l->id);
            l->visible       = v.getBool("visible", true);
            l->opacity       = v.getFloat("opacity", 1);
            l->offsetX       = v.getFloat("offsetx", 0);
            l->offsetY       = v.getFloat("offsety", 0);
            readProperties(v.get("properties"), l->properties);
            if (obj) {
                auto os = v.get("objects");
                for (size_t j = 0; j < os.size(); ++j) {
                    auto q = os.at(j);
                    int  oi =
                        out->addObject(li, q.getString("type", q.getString("class")), q.getFloat("x"), q.getFloat("y"));
                    auto* ob     = out->object(li, oi);
                    ob->id       = std::to_string(q.getInt("id", int(j + 1)));
                    ob->name     = q.getString("name");
                    ob->width    = q.getFloat("width");
                    ob->height   = q.getFloat("height");
                    ob->rotation = q.getFloat("rotation");
                    ob->visible  = q.getBool("visible", true);
                    readProperties(q.get("properties"), ob->properties);
                }
            } else {
                auto data = v.get("data");
                for (size_t n = 0; n < data.size() && n < size_t(w * h); ++n)
                    l->tiles->setGid(int(n) % w, int(n) / w, data.at(n).asInt());
            }
        }
        return out;
    }
    static std::string writeJson(const LevelDocument& d, bool tiled) {
        std::ostringstream o;
        o << "{\n  ";
        if (!tiled)
            o << "\"format\":\"eve.level\",\n  \"version\":1,\n  ";
        else
            o << "\"type\":\"map\",\n  \"version\":\"1.10\",\n  ";
        o << "\"orientation\":" << quote(d.getOrientation()) << ",\n  \"width\":" << d.getWidth()
          << ",\"height\":" << d.getHeight() << ",\"tilewidth\":" << d.getTileWidth()
          << ",\"tileheight\":" << d.getTileHeight() << ",\n  \"properties\":";
        properties(o, d.properties(), tiled);
        o << ",\n  \"layers\":[";
        for (int i = 0; i < d.getLayerCount(); ++i) {
            if (i) o << ',';
            auto* l = d.layer(i);
            o << "\n    {\"id\":" << (tiled ? std::to_string(i + 1) : quote(l->id)) << ",\"name\":" << quote(l->name)
              << ",\"type\":" << quote(l->kind == LevelLayer::Kind::Tiles ? "tilelayer" : "objectgroup")
              << ",\"visible\":" << (l->visible ? "true" : "false") << ",\"opacity\":" << l->opacity
              << ",\"offsetx\":" << l->offsetX << ",\"offsety\":" << l->offsetY << ",\"properties\":";
            properties(o, l->properties, tiled);
            if (l->tiles) {
                o << ",\"width\":" << d.getWidth() << ",\"height\":" << d.getHeight() << ",\"data\":[";
                for (int y = 0; y < d.getHeight(); ++y)
                    for (int x = 0; x < d.getWidth(); ++x) {
                        if (x || y) o << ',';
                        o << l->tiles->getGid(x, y);
                    }
                o << ']';
            } else {
                o << ",\"objects\":[";
                for (size_t j = 0; j < l->objects.size(); ++j) {
                    if (j) o << ',';
                    auto& q = l->objects[j];
                    o << "{\"id\":" << (tiled ? std::to_string(j + 1) : quote(q.id)) << ",\"name\":" << quote(q.name)
                      << ",\"type\":" << quote(q.type) << ",\"x\":" << q.x << ",\"y\":" << q.y
                      << ",\"width\":" << q.width << ",\"height\":" << q.height << ",\"rotation\":" << q.rotation
                      << ",\"visible\":" << (q.visible ? "true" : "false") << ",\"properties\":";
                    properties(o, q.properties, tiled);
                    o << '}';
                }
                o << ']';
            }
            o << '}';
        }
        o << "\n  ]\n}\n";
        return o.str();
    }
};
class TiledFormat final : public LevelFormat {
public:
    std::string              id() const override { return "tiled.json"; }
    std::vector<std::string> extensions() const override { return {".tmj", ".json"}; }
    bool                     canRead(const std::string& s) const override {
        auto d = eve::json::Document::parse(s);
        return d.valid() && d.root().getString("type") == "map";
    }
    std::unique_ptr<LevelDocument> read(const std::string& s, std::string& e) const override {
        return JsonLevelFormat::readJson(s, e, true);
    }
    bool write(const LevelDocument& d, std::string& t, std::string&) const override {
        t = JsonLevelFormat::writeJson(d, true);
        return true;
    }
};
}  // namespace

LevelFormatRegistry::LevelFormatRegistry() {
    registerFormat(std::make_unique<JsonLevelFormat>());
    registerFormat(std::make_unique<TiledFormat>());
}
void LevelFormatRegistry::registerFormat(std::unique_ptr<LevelFormat> f) {
    if (!f) return;
    for (auto& i : formats_)
        if (i->id() == f->id()) {
            i = std::move(f);
            return;
        }
    formats_.push_back(std::move(f));
}
std::string LevelFormatRegistry::getFormatId(int i) const {
    return i < 0 || i >= int(formats_.size()) ? std::string() : formats_[i]->id();
}
const LevelFormat* LevelFormatRegistry::find(const std::string& id) const {
    for (auto& i : formats_)
        if (i->id() == id) return i.get();
    return nullptr;
}
std::string LevelFormatRegistry::detect(const std::string& p, const std::string& t) const {
    for (auto& i : formats_)
        if (i->canRead(t)) return i->id();
    for (auto& i : formats_)
        for (auto& e : i->extensions())
            if (p.size() >= e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0) return i->id();
    return {};
}
std::unique_ptr<LevelDocument> LevelFormatRegistry::decode(const std::string& id, const std::string& t,
                                                           std::string* err) const {
    auto*       f = find(id);
    std::string e;
    if (!f) e = "unknown level format: " + id;
    auto r = f ? f->read(t, e) : nullptr;
    if (err) *err = e;
    return r;
}
std::string LevelFormatRegistry::encode(const std::string& id, const LevelDocument& d, std::string* err) const {
    auto*       f = find(id);
    std::string e, t;
    if (!f)
        e = "unknown level format: " + id;
    else
        f->write(d, t, e);
    if (err) *err = e;
    return t;
}
std::unique_ptr<LevelDocument> LevelFormatRegistry::load(const std::string& p, const std::string& id,
                                                         std::string* err) const {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + p;
        return {};
    }
    std::ostringstream s;
    s << f.rdbuf();
    auto fmt = id.empty() ? detect(p, s.str()) : id;
    return decode(fmt, s.str(), err);
}
bool LevelFormatRegistry::save(const std::string& p, const LevelDocument& d, const std::string& id,
                               std::string* err) const {
    auto fmt = id.empty() ? detect(p, "") : id;
    if (fmt.empty()) fmt = "eve.level";
    auto t = encode(fmt, d, err);
    if (t.empty()) return false;
    std::ofstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot write " + p;
        return false;
    }
    f << t;
    return bool(f);
}
}  // namespace eve::editor
