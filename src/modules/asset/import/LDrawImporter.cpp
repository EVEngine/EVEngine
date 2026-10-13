#include "asset/import/StructuredModelCommon.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>

namespace eve::asset_import {
namespace {
using Point  = std::array<double, 3>;
using Matrix = std::array<double, 12>;
constexpr Matrix identity{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
std::string pathKey(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.empty() || text.front() == '/' || text.find(':') != std::string::npos) return {};
    std::string        out;
    std::istringstream stream(text);
    std::string        component;
    while (std::getline(stream, component, '/')) {
        if (component == ".." || component.empty()) return {};
        if (component == ".") continue;
        for (char& c : component)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (!out.empty()) out += '/';
        out += component;
    }
    return out;
}
Point transform(const Matrix& m, const Point& p) {
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[9], m[3] * p[0] + m[4] * p[1] + m[5] * p[2] + m[10],
            m[6] * p[0] + m[7] * p[1] + m[8] * p[2] + m[11]};
}
Matrix compose(const Matrix& parent, const Matrix& child) {
    Matrix out{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t k = 0; k < 3; ++k) out[row * 3 + column] += parent[row * 3 + k] * child[k * 3 + column];
    const auto translation = transform(parent, {child[9], child[10], child[11]});
    for (std::size_t i = 0; i < 3; ++i) out[9 + i] = translation[i];
    return out;
}
std::optional<unsigned> integer(std::string_view text) {
    unsigned value = 0;
    int      base  = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return {};
    return value;
}
double linear(unsigned byte) {
    const double value = double(byte) / 255;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
std::array<double, 4> rgba(unsigned rgb, double alpha = 1) {
    return {linear((rgb >> 16) & 255), linear((rgb >> 8) & 255), linear(rgb & 255), alpha};
}
struct Colour {
    std::array<double, 4> value;
    std::string           key;
};
struct ColourScope {
    const ColourScope*         parent = nullptr;
    std::map<unsigned, Colour> definitions;
};

class Decoder {
public:
    explicit Decoder(const LDrawImportRequest& request) : request_(request) {
        constexpr unsigned rgb[] = {0x05131d, 0x0055bf, 0x237841, 0x008f9b, 0xc91a09, 0xc870a0, 0x583927, 0x9ba19d,
                                    0x6d6e5c, 0xb4d2e3, 0x4b9f4a, 0x55a5af, 0xf2705e, 0xfc97ac, 0xf2cd37, 0xffffff};
        for (unsigned i = 0; i < 16; ++i) palette_[i] = {rgba(rgb[i]), "colour-" + std::to_string(i)};
    }
    Result<PreparedAssetImport> run() {
        auto indexed = index();
        if (!indexed) return Result<PreparedAssetImport>::failure(indexed.status());
        std::string root = pathKey(request_.sourceName);
        if (const auto found = firstSections_.find(root); found != firstSections_.end()) root = found->second;
        auto expanded = expand(root, identity, palette_.at(7), false, 0, nullptr);
        if (!expanded) return Result<PreparedAssetImport>::failure(expanded.status());
        if (edgeLines_)
            findings_.push_back(
                {request_.sourceName, "LDraw.edgeLines", ImportDisposition::Unsupported,
                 std::to_string(edgeLines_) + " line/conditional-line records are retained in source, not rendered"});
        findings_.push_back({request_.sourceName, "LDraw.geometry", ImportDisposition::Baked,
                             "part transforms baked; 1 LDU = 0.4 mm; colours mapped to linear PBR base colour; "
                             "connectivity not inferred"});
        return structured_detail::packageModel(request_.package, request_.sourceName, "ldraw", std::move(parts_),
                                               usedSources_, std::move(findings_), request_.limits);
    }

private:
    const LDrawImportRequest&                          request_;
    std::map<std::string, std::vector<std::string>>    documents_;
    std::map<std::string, std::string>                 firstSections_;
    std::map<std::string, std::string>                 owners_;
    std::map<std::string, std::vector<std::uint8_t>>   usedSources_;
    std::map<unsigned, Colour>                         palette_;
    std::map<std::string, structured_detail::MeshPart> parts_;
    std::set<std::string>                              active_;
    std::vector<ImportFinding>                         findings_;
    std::uint64_t                                      expandedLines_ = 0, edgeLines_ = 0, vertices_ = 0;

    Result<void> fail(std::string message, std::string path, DiagnosticCode code = DiagnosticCode::ParseError) const {
        return Result<void>::failure(
            Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.import.ldraw"));
    }
    Result<void> index() {
        std::uint64_t total = 0, lines = 0;
        if (request_.files.size() > request_.limits.maximumAssets || request_.maximumDepth == 0 ||
            request_.maximumDepth > 256 || request_.maximumExpandedLines == 0)
            return fail("invalid LDraw source/depth budget", request_.sourceName, DiagnosticCode::InvalidArgument);
        for (const auto& [name, bytes] : request_.files) {
            if (bytes.size() > request_.limits.maximumSourceBytes - total)
                return fail("source byte budget exceeded", name, DiagnosticCode::InvalidArgument);
            total += bytes.size();
            const auto owner = pathKey(name);
            if (owner.empty() || owner.size() > request_.limits.maximumStringBytes || owners_.contains(owner))
                return fail("invalid or duplicate case-insensitive source name", name);
            if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end()) return fail("NUL in LDraw source", name);
            std::string        current = owner;
            std::istringstream stream(std::string(bytes.begin(), bytes.end()));
            std::string        line;
            bool               section = false, closed = false;
            owners_[owner] = name;
            while (std::getline(stream, line)) {
                if (++lines > request_.maximumExpandedLines || line.size() > request_.limits.maximumStringBytes)
                    return fail("source line budget exceeded", name, DiagnosticCode::InvalidArgument);
                line = trim(std::move(line));
                if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
                if (line.starts_with("0 FILE ")) {
                    current = pathKey(trim(line.substr(7)));
                    if (!section) documents_.erase(owner);  // Header comments are not a second MPD document.
                    if (current.empty() || documents_.contains(current))
                        return fail("duplicate/invalid MPD FILE name", name);
                    if (!section) firstSections_[owner] = current;
                    section             = true;
                    closed              = false;
                    documents_[current] = {};
                    owners_[current]    = name;
                } else if (line == "0 NOFILE") {
                    if (!section) return fail("NOFILE without FILE", name);
                    closed = true;
                } else if (!closed) {
                    documents_[current].push_back(line);
                }
                if (owner == "ldconfig.ldr" && line.starts_with("0 !COLOUR ")) {
                    auto color = readColour(line, name, palette_, {});
                    if (!color) return color;
                    usedSources_.try_emplace(name, bytes);
                }
            }
            if (documents_.size() > request_.limits.maximumAssets) return fail("MPD section budget exceeded", name);
        }
        return Result<void>::success();
    }
    Result<void> readColour(const std::string& line, const std::string& name, std::map<unsigned, Colour>& definitions,
                            const std::string& suffix) {
        std::istringstream stream(line);
        std::string        token, codeText, valueText, edgeText, alphaText = "255";
        while (stream >> token) {
            for (char& c : token)
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            if (token == "CODE")
                stream >> codeText;
            else if (token == "VALUE")
                stream >> valueText;
            else if (token == "EDGE")
                stream >> edgeText;
            else if (token == "ALPHA")
                stream >> alphaText;
        }
        auto code = integer(codeText), alpha = integer(alphaText);
        if (!code || *code == 16 || *code == 24 || !alpha || *alpha > 255 || edgeText.empty())
            return fail("invalid !COLOUR definition", name);
        auto rgb  = integer(valueText.starts_with('#') ? "0x" + valueText.substr(1) : valueText);
        auto edge = integer(edgeText.starts_with('#') ? "0x" + edgeText.substr(1) : edgeText);
        if (!rgb || *rgb > 0xffffffu || !edge ||
            !(valueText.starts_with('#') || valueText.starts_with("0x") || valueText.starts_with("0X")))
            return fail("invalid !COLOUR RGB/edge", name);
        if (definitions.size() >= request_.limits.maximumAssets && !definitions.contains(*code))
            return fail("colour definition budget exceeded", name);
        definitions[*code] = {rgba(*rgb, double(*alpha) / 255), "colour-" + std::to_string(*code) + suffix};
        if (line.find(" MATERIAL ") != std::string::npos || line.find(" CHROME") != std::string::npos ||
            line.find(" METAL") != std::string::npos || line.find(" PEARLESCENT") != std::string::npos ||
            line.find(" RUBBER") != std::string::npos || line.find("LUMINANCE") != std::string::npos)
            findings_.push_back({name, "LDraw.colourFinish", ImportDisposition::Unsupported,
                                 "special finish/luminance retained in source; base colour and alpha translated"});
        return Result<void>::success();
    }
    std::string resolve(const std::string& parent, const std::string& filename) const {
        const auto key = pathKey(filename);
        if (key.empty()) return {};
        const auto slash    = parent.find_last_of('/');
        const auto relative = slash == std::string::npos ? key : parent.substr(0, slash + 1) + key;
        for (const auto& candidate : {relative, key, "parts/" + key, "p/" + key, "models/" + key}) {
            if (const auto first = firstSections_.find(candidate); first != firstSections_.end()) return first->second;
            if (documents_.contains(candidate)) {
                return candidate;
            }
        }
        return {};
    }
    Result<Colour> colour(unsigned code, const Colour& inherited, const ColourScope& scope) const {
        if (code == 16) return Result<Colour>::success(inherited);
        if ((code & 0xff000000u) == 0x02000000u)
            return Result<Colour>::success({rgba(code & 0xffffffu), "colour-" + std::to_string(code)});
        for (auto* current = &scope; current; current = current->parent)
            if (const auto found = current->definitions.find(code); found != current->definitions.end())
                return Result<Colour>::success(found->second);
        if (const auto found = palette_.find(code); found != palette_.end())
            return Result<Colour>::success(found->second);
        return Result<Colour>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "unknown face colour: supply LDConfig.ldr; colour 24 is edge-only", {}, {},
            "asset.import.ldraw"));
    }
    Result<void> expand(const std::string& name, const Matrix& matrix, const Colour& inheritedColor, bool invert,
                        unsigned depth, const ColourScope* parentScope) {
        if (depth >= request_.maximumDepth || active_.contains(name)) return fail("cyclic/deep LDraw reference", name);
        const auto document = documents_.find(name);
        if (document == documents_.end()) return fail("missing LDraw part", name, DiagnosticCode::NotFound);
        active_.insert(name);
        const auto& owner = owners_.at(name);
        usedSources_.try_emplace(owner, request_.files.at(owner));
        ColourScope scope{parentScope, {}};
        bool        clockwise = false, invertNext = false;
        for (std::size_t lineNumber = 0; lineNumber < document->second.size(); ++lineNumber) {
            if (++expandedLines_ > request_.maximumExpandedLines) return fail("expanded line budget exceeded", name);
            const auto& line     = document->second[lineNumber];
            const auto  location = name + ":" + std::to_string(lineNumber + 1);
            if (line.empty()) continue;
            std::istringstream stream(line);
            stream.imbue(std::locale::classic());
            unsigned type = 0;
            if (!(stream >> type) || type > 5) return fail("invalid LDraw line type", location);
            if (type == 0) {
                if (line.starts_with("0 !COLOUR ") && name != "ldconfig.ldr") {
                    auto defined = readColour(line, location, scope.definitions, "@" + location);
                    if (!defined) return defined;
                }
                if (line.starts_with("0 !TEXMAP") || line.starts_with("0 !DATA") || line.starts_with("0 !:"))
                    return fail("textured/embedded geometry extensions are unsupported", location,
                                DiagnosticCode::Unsupported);
                if (line.starts_with("0 BFC ")) {
                    const auto command = trim(line.substr(6));
                    if (command == "INVERTNEXT")
                        invertNext = true;
                    else if (command == "CW" || command == "CERTIFY CW" || command == "CLIP CW")
                        clockwise = true;
                    else if (command == "CCW" || command == "CERTIFY CCW" || command == "CLIP CCW" ||
                             command == "CERTIFY")
                        clockwise = false;
                    else if (command != "NOCERTIFY" && command != "NOCLIP" && command != "CLIP")
                        return fail("unknown BFC command", location, DiagnosticCode::Unsupported);
                }
                continue;
            }
            std::string colorText;
            stream >> colorText;
            const auto decodedColor = integer(colorText);
            if (!decodedColor) return fail("invalid LDraw colour", location);
            if (type == 1) {
                auto resolvedColour = colour(*decodedColor, inheritedColor, scope);
                if (!resolvedColour) return Result<void>::failure(resolvedColour.status());
                Matrix local{};
                for (std::size_t i = 9; i < 12; ++i)
                    if (!(stream >> local[i]) || !std::isfinite(local[i]))
                        return fail("invalid part translation", location);
                for (std::size_t i = 0; i < 9; ++i)
                    if (!(stream >> local[i]) || !std::isfinite(local[i])) return fail("invalid part matrix", location);
                std::string filename;
                std::getline(stream, filename);
                const auto reference = resolve(name, trim(std::move(filename)));
                if (reference.empty())
                    return fail("missing/invalid LDraw reference", location, DiagnosticCode::NotFound);
                const double det = local[0] * (local[4] * local[8] - local[5] * local[7]) -
                                   local[1] * (local[3] * local[8] - local[5] * local[6]) +
                                   local[2] * (local[3] * local[7] - local[4] * local[6]);
                if (!std::isfinite(det) || det == 0) return fail("singular part transform", location);
                auto child = expand(reference, compose(matrix, local), resolvedColour.value(), invert != invertNext,
                                    depth + 1, &scope);
                if (!child) return child;
                invertNext = false;
                continue;
            }
            if (invertNext) return fail("INVERTNEXT must precede a reference", location);
            std::array<Point, 4> points{};
            const unsigned       count = type == 2 ? 2 : type == 3 ? 3 : 4;
            for (unsigned i = 0; i < count; ++i)
                for (double& value : points[i])
                    if (!(stream >> value) || !std::isfinite(value)) return fail("invalid LDraw coordinate", location);
            std::string extra;
            if (stream >> extra) return fail("extra LDraw geometry tokens", location);
            if (type == 2 || type == 5) {
                ++edgeLines_;
                continue;
            }
            auto resolvedColour = colour(*decodedColor, inheritedColor, scope);
            if (!resolvedColour) return Result<void>::failure(resolvedColour.status());
            const auto& key = resolvedColour.value().key;
            if (!parts_.contains(key) && parts_.size() * 2 + 3 > request_.limits.maximumAssets)
                return fail("colour part budget exceeded", location);
            auto& part = parts_[key];
            part.color = resolvedColour.value().value;
            for (unsigned i = 0; i < count; ++i) {
                const auto p = transform(matrix, points[i]);
                points[i]    = {p[0] * 0.0004, -p[1] * 0.0004, -p[2] * 0.0004};
            }
            auto triangle = [&](unsigned a, unsigned b, unsigned c) -> Result<void> {
                if ((vertices_ + 3) * 28 + 24 > request_.limits.maximumDecodedBytes)
                    return fail("total mesh byte budget exceeded", location);
                auto added = structured_detail::appendTriangle(
                    part.mesh, {points[a], points[clockwise != invert ? c : b], points[clockwise != invert ? b : c]},
                    request_.limits);
                if (added) vertices_ += 3;
                return added;
            };
            auto added = triangle(0, 1, 2);
            if (!added) return added;
            if (type == 4) {
                auto second = triangle(0, 2, 3);
                if (!second) return second;
            }
        }
        if (invertNext) return fail("dangling INVERTNEXT", name);
        active_.erase(name);
        return Result<void>::success();
    }
};
}  // namespace

Result<PreparedAssetImport> prepareLDrawImport(const LDrawImportRequest& request) { return Decoder(request).run(); }
}  // namespace eve::asset_import
