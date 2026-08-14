#include "animation/SpineAtlas.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

#include <cctype>
#include <memory>
#include <sstream>

namespace eve::animation {
namespace {

std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool startsWithIndent(const std::string &line) {
    return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

bool parsePair(const std::string &value, int &a, int &b) {
    auto comma = value.find(',');
    if (comma == std::string::npos) return false;
    try {
        a = std::stoi(trim(value.substr(0, comma)));
        b = std::stoi(trim(value.substr(comma + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool(const std::string &value, bool &out) {
    std::string v = trim(value);
    for (char &c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "true" || v == "1" || v == "yes") {
        out = true;
        return true;
    }
    if (v == "false" || v == "0" || v == "no") {
        out = false;
        return true;
    }
    // Spine sometimes uses degrees for rotate (90)
    try {
        int n = std::stoi(v);
        out   = n != 0;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

void SpineAtlas::checkPage(int index) const {
    if (index < 0 || index >= getPageCount())
        throw Exception("SpineAtlas: page index %d out of range (count=%d)", index, getPageCount());
}

void SpineAtlas::checkRegion(int index) const {
    if (index < 0 || index >= getRegionCount())
        throw Exception("SpineAtlas: region index %d out of range (count=%d)", index,
                        getRegionCount());
}

void SpineAtlas::clear() {
    pages_.clear();
    regions_.clear();
    regionByName_.clear();
}

bool SpineAtlas::loadFromFile(const std::string &path, std::string *error) {
    if (path.empty()) {
        if (error) *error = "empty path";
        return false;
    }
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return false;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return false;
    }
    std::string text(static_cast<const char *>(data->getData()),
                     static_cast<size_t>(data->getSize()));
    return loadFromText(text, error);
}

bool SpineAtlas::loadFromText(const std::string &text, std::string *error) {
    clear();
    std::istringstream in(text);
    std::string line;
    int pageIndex = -1;
    Region *cur   = nullptr;

    auto applyKeyValue = [&](const std::string &keyIn, const std::string &val) {
        std::string key = keyIn;
        for (char &c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (cur) {
            if (key == "rotate") {
                bool b = false;
                parseBool(val, b);
                cur->rotate = b;
            } else if (key == "xy") {
                parsePair(val, cur->x, cur->y);
            } else if (key == "size") {
                parsePair(val, cur->w, cur->h);
            } else if (key == "orig" || key == "originalsize") {
                parsePair(val, cur->origW, cur->origH);
            } else if (key == "offset") {
                parsePair(val, cur->offsetX, cur->offsetY);
            }
        } else if (pageIndex >= 0) {
            Page &p = pages_[static_cast<size_t>(pageIndex)];
            if (key == "size") parsePair(val, p.width, p.height);
        }
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string raw = line;
        std::string t   = trim(line);
        if (t.empty()) {
            cur = nullptr;
            continue;
        }

        const bool indented = startsWithIndent(raw);
        auto colon          = t.find(':');

        // Spine atlas: page keys may be unindented ("size: W,H"); region keys are indented.
        if (colon != std::string::npos && (indented || cur == nullptr)) {
            // Unindented key:value while no region → page property (ends region context).
            if (!indented) cur = nullptr;
            applyKeyValue(trim(t.substr(0, colon)), trim(t.substr(colon + 1)));
            continue;
        }

        if (!indented && colon == std::string::npos) {
            std::string lower = t;
            for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool looksLikeImage =
                lower.find(".png") != std::string::npos || lower.find(".jpg") != std::string::npos ||
                lower.find(".jpeg") != std::string::npos || lower.find(".webp") != std::string::npos ||
                lower.find(".ktx") != std::string::npos || lower.find(".pvr") != std::string::npos;

            if (pageIndex < 0 || looksLikeImage) {
                Page p;
                p.name = t;
                pages_.push_back(p);
                pageIndex = static_cast<int>(pages_.size()) - 1;
                cur       = nullptr;
                continue;
            }
            if (regionByName_.count(t)) {
                if (error) *error = "duplicate region: " + t;
                clear();
                return false;
            }
            Region r;
            r.name      = t;
            r.pageIndex = pageIndex;
            regions_.push_back(r);
            regionByName_[t] = static_cast<int>(regions_.size()) - 1;
            cur               = &regions_.back();
            continue;
        }

        if (colon != std::string::npos)
            applyKeyValue(trim(t.substr(0, colon)), trim(t.substr(colon + 1)));
    }

    for (Region &r : regions_) {
        if (r.origW <= 0) r.origW = r.w;
        if (r.origH <= 0) r.origH = r.h;
    }

    if (pages_.empty()) {
        if (error) *error = "no atlas pages";
        clear();
        return false;
    }
    return true;
}

std::string SpineAtlas::getPageName(int pageIndex) const {
    checkPage(pageIndex);
    return pages_[static_cast<size_t>(pageIndex)].name;
}

int SpineAtlas::getPageWidth(int pageIndex) const {
    checkPage(pageIndex);
    return pages_[static_cast<size_t>(pageIndex)].width;
}

int SpineAtlas::getPageHeight(int pageIndex) const {
    checkPage(pageIndex);
    return pages_[static_cast<size_t>(pageIndex)].height;
}

int SpineAtlas::findRegion(const std::string &name) const {
    auto it = regionByName_.find(name);
    return it == regionByName_.end() ? -1 : it->second;
}

std::string SpineAtlas::getRegionName(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].name;
}

int SpineAtlas::getRegionPage(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].pageIndex;
}

int SpineAtlas::getRegionX(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].x;
}

int SpineAtlas::getRegionY(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].y;
}

int SpineAtlas::getRegionWidth(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].w;
}

int SpineAtlas::getRegionHeight(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].h;
}

int SpineAtlas::getRegionOriginalWidth(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].origW;
}

int SpineAtlas::getRegionOriginalHeight(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].origH;
}

int SpineAtlas::getRegionOffsetX(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].offsetX;
}

int SpineAtlas::getRegionOffsetY(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].offsetY;
}

bool SpineAtlas::getRegionRotate(int index) const {
    checkRegion(index);
    return regions_[static_cast<size_t>(index)].rotate;
}

void SpineAtlas::getRegionUV(int index, int texW, int texH, float &u0, float &v0, float &u1,
                             float &v1) const {
    checkRegion(index);
    if (texW <= 0 || texH <= 0) throw Exception("SpineAtlas.getRegionUV: invalid texture size");
    const Region &r = regions_[static_cast<size_t>(index)];
    // Packed size: if rotate, the region is stored 90°-rotated so the packed
    // rectangle spans r.h across and r.w down (matches spine-runtimes spAtlas).
    float x = static_cast<float>(r.x);
    float y = static_cast<float>(r.y);
    u0      = x / static_cast<float>(texW);
    v0      = y / static_cast<float>(texH);
    if (r.rotate) {
        u1 = (x + static_cast<float>(r.h)) / static_cast<float>(texW);
        v1 = (y + static_cast<float>(r.w)) / static_cast<float>(texH);
    } else {
        u1 = (x + static_cast<float>(r.w)) / static_cast<float>(texW);
        v1 = (y + static_cast<float>(r.h)) / static_cast<float>(texH);
    }
}

}  // namespace eve::animation
