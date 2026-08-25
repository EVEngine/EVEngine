#include "procgen/ShapeGrammar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace eve::procgen {
namespace {

uint32_t mixGrammar(uint32_t value) {
    value += 0x9e3779b9u;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}
float grammarUnit(uint32_t seed) { return float(mixGrammar(seed) >> 8u) * (1.f / 16777216.f); }

float polylineLength(const PointSet& points) {
    float result = 0.f;
    for (size_t i = 1; i < points.points().size(); ++i) {
        const auto& a = points.points()[i - 1];
        const auto& b = points.points()[i];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float dz = b.z - a.z;
        result += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return result;
}

bool samplePolyline(const PointSet& points, float distance, ProcgenPoint& output) {
    float cursor = 0.f;
    for (size_t i = 1; i < points.points().size(); ++i) {
        const auto& a = points.points()[i - 1];
        const auto& b = points.points()[i];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float dz = b.z - a.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= 0.f) continue;
        if (distance <= cursor + length || i + 1 == points.points().size()) {
            const float t = std::clamp((distance - cursor) / length, 0.f, 1.f);
            output.x      = a.x + dx * t;
            output.y      = a.y + dy * t;
            output.z      = a.z + dz * t;
            output.yaw    = std::atan2(dz, dx) * 57.29577951308232f;
            return true;
        }
        cursor += length;
    }
    return false;
}

}  // namespace

void ShapeGrammar::clear() {
    modules_.clear();
    moduleOrder_.clear();
    error_.clear();
    lastSymbolCount_  = 0;
    lastUsedLength_   = 0.f;
    lastSplineLength_ = 0.f;
}

bool ShapeGrammar::addModule(const std::string& symbol, const std::string& asset, float length,
                             float weight) {
    if (symbol.empty() || asset.empty() || length <= 0.f || weight <= 0.f) return false;
    const auto existing = modules_.find(symbol);
    if (existing != modules_.end() && !existing->second.empty() &&
        std::abs(existing->second.front().length - length) > 0.0001f)
        return false;
    if (modules_.find(symbol) == modules_.end()) moduleOrder_.push_back(symbol);
    modules_[symbol].push_back({asset, length, weight});
    return true;
}

bool ShapeGrammar::removeModule(const std::string& symbol) {
    if (modules_.erase(symbol) == 0) return false;
    moduleOrder_.erase(std::remove(moduleOrder_.begin(), moduleOrder_.end(), symbol),
                       moduleOrder_.end());
    return true;
}
bool ShapeGrammar::hasModule(const std::string& symbol) const {
    return modules_.find(symbol) != modules_.end();
}
int ShapeGrammar::getModuleCount() const { return int(moduleOrder_.size()); }
std::string ShapeGrammar::getModuleSymbol(int index) const {
    return index >= 0 && index < int(moduleOrder_.size()) ? moduleOrder_[size_t(index)]
                                                          : std::string();
}
int ShapeGrammar::getVariantCount(const std::string& symbol) const {
    const auto found = modules_.find(symbol);
    return found == modules_.end() ? 0 : int(found->second.size());
}
std::string ShapeGrammar::getVariantAsset(const std::string& symbol, int index) const {
    const auto found = modules_.find(symbol);
    return found != modules_.end() && index >= 0 && index < int(found->second.size())
               ? found->second[size_t(index)].asset
               : std::string();
}
float ShapeGrammar::getVariantLength(const std::string& symbol, int index) const {
    const auto found = modules_.find(symbol);
    return found != modules_.end() && index >= 0 && index < int(found->second.size())
               ? found->second[size_t(index)].length
               : 0.f;
}

bool ShapeGrammar::validate(const std::string& grammar) {
    error_.clear();
    Parser parser{grammar};
    const auto parsed = parser.sequence();
    if (!parser.error.empty()) {
        error_ = parser.error;
        return false;
    }
    if (parsed.empty()) {
        error_ = "grammar is empty";
        return false;
    }
    std::vector<const Element*> stack;
    for (const auto& element : parsed) stack.push_back(&element);
    while (!stack.empty()) {
        const Element* element = stack.back();
        stack.pop_back();
        if (!element->symbol.empty() && !hasModule(element->symbol)) {
            error_ = "unknown module: " + element->symbol;
            return false;
        }
        for (const auto& child : element->children) stack.push_back(&child);
    }
    return true;
}

PointSet* ShapeGrammar::generate(const std::string& grammar, PointSet* controlPoints,
                                 uint32_t seed, bool acceptIncomplete) {
    error_.clear();
    lastSymbolCount_  = 0;
    lastUsedLength_   = 0.f;
    lastSplineLength_ = 0.f;
    if (!controlPoints || controlPoints->getCount() < 2) {
        error_ = "generate: requires at least two control points";
        return nullptr;
    }
    if (!validate(grammar)) return nullptr;
    Parser parser{grammar};
    const auto parsed = parser.sequence();
    lastSplineLength_ = polylineLength(*controlPoints);
    if (!acceptIncomplete && sequenceMinLength(parsed) > lastSplineLength_ + 0.0001f) {
        error_ = "mandatory grammar does not fit spline";
        return nullptr;
    }
    std::vector<std::string> symbols;
    float used = 0.f;
    if (!expandSequence(parsed, lastSplineLength_, symbols, used)) {
        error_ = "grammar expansion failed";
        return nullptr;
    }

    PointSet output;
    float cursor = 0.f;
    for (size_t i = 0; i < symbols.size(); ++i) {
        const auto* variant = chooseVariant(symbols[i], mixGrammar(seed ^ uint32_t(i)));
        if (!variant || cursor + variant->length > lastSplineLength_ + 0.0001f) {
            if (!acceptIncomplete) {
                error_ = "expanded module does not fit spline: " + symbols[i];
                return nullptr;
            }
            break;
        }
        ProcgenPoint point;
        if (!samplePolyline(*controlPoints, cursor + variant->length * 0.5f, point)) break;
        point.seed = mixGrammar(seed ^ uint32_t(i));
        point.stringAttributes["module"] = symbols[i];
        point.stringAttributes["asset"]  = variant->asset;
        point.floatAttributes["length"]  = variant->length;
        output.points().push_back(std::move(point));
        cursor += variant->length;
    }
    lastSymbolCount_ = output.getCount();
    lastUsedLength_  = cursor;
    return new PointSet(std::move(output));
}

std::string ShapeGrammar::getError() const { return error_; }
std::string ShapeGrammar::debugReport() const {
    std::ostringstream out;
    out << "modules=" << modules_.size() << " symbols=" << lastSymbolCount_
        << " used=" << lastUsedLength_ << " spline=" << lastSplineLength_;
    if (!error_.empty()) out << " error=" << error_;
    return out.str();
}

void ShapeGrammar::Parser::skipWhitespace() {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        ++position;
}

std::vector<ShapeGrammar::Element> ShapeGrammar::Parser::sequence(char terminator) {
    std::vector<Element> result;
    while (position < text.size()) {
        skipWhitespace();
        if (position >= text.size()) break;
        if (terminator != '\0' && text[position] == terminator) {
            ++position;
            return result;
        }
        Element element;
        if (text[position] == '[') {
            ++position;
            element.children = sequence(']');
            if (!error.empty()) return {};
            if (element.children.empty()) {
                error = "empty grammar group";
                return {};
            }
        } else {
            const size_t start = position;
            while (position < text.size()) {
                const unsigned char ch = static_cast<unsigned char>(text[position]);
                if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.') break;
                ++position;
            }
            if (start == position) {
                error = "expected module at character " + std::to_string(position);
                return {};
            }
            element.symbol = text.substr(start, position - start);
        }
        skipWhitespace();
        if (position < text.size() && text[position] == '*') {
            element.repeatMin = 0;
            element.repeatMax = -1;
            ++position;
        } else if (position < text.size() && text[position] == '+') {
            element.repeatMin = 1;
            element.repeatMax = -1;
            ++position;
        } else if (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
            const size_t start = position;
            while (position < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[position])))
                ++position;
            element.repeatMin = element.repeatMax =
                std::max(0, std::atoi(text.substr(start, position - start).c_str()));
        }
        result.push_back(std::move(element));
        skipWhitespace();
        if (position < text.size() && text[position] == ',') {
            ++position;
            continue;
        }
        if (terminator != '\0' && position < text.size() && text[position] == terminator) continue;
        if (position < text.size()) {
            error = "expected comma at character " + std::to_string(position);
            return {};
        }
    }
    if (terminator != '\0') error = "unterminated grammar group";
    return result;
}

float ShapeGrammar::elementMinLength(const Element& element) const {
    float unit = 0.f;
    if (!element.symbol.empty()) {
        const auto found = modules_.find(element.symbol);
        if (found != modules_.end() && !found->second.empty()) {
            unit = found->second.front().length;
            for (const auto& variant : found->second) unit = std::min(unit, variant.length);
        }
    } else {
        unit = sequenceMinLength(element.children);
    }
    return unit * float(element.repeatMin);
}

float ShapeGrammar::sequenceMinLength(const std::vector<Element>& sequence, size_t from) const {
    float result = 0.f;
    for (size_t i = from; i < sequence.size(); ++i) result += elementMinLength(sequence[i]);
    return result;
}

bool ShapeGrammar::expandSequence(const std::vector<Element>& sequence, float available,
                                  std::vector<std::string>& symbols, float& used) const {
    for (size_t i = 0; i < sequence.size(); ++i) {
        const auto& element = sequence[i];
        float unitLength = !element.symbol.empty() ? elementMinLength(Element{element.symbol, {}, 1, 1})
                                                   : sequenceMinLength(element.children);
        if (unitLength <= 0.f) return false;
        const float reservedAfter = sequenceMinLength(sequence, i + 1);
        int repeats = element.repeatMin;
        if (element.repeatMax < 0) {
            const float room = std::max(0.f, available - used - reservedAfter);
            repeats = std::max(element.repeatMin, int(std::floor(room / unitLength)));
        } else {
            repeats = element.repeatMax;
        }
        for (int repeat = 0; repeat < repeats; ++repeat) {
            if (used + unitLength + reservedAfter > available + 0.0001f) return true;
            if (!element.symbol.empty()) {
                symbols.push_back(element.symbol);
                used += unitLength;
            } else {
                float groupUsed = 0.f;
                if (!expandSequence(element.children, available - used - reservedAfter, symbols,
                                    groupUsed))
                    return false;
                used += groupUsed;
            }
        }
    }
    return true;
}

const ShapeModuleVariant* ShapeGrammar::chooseVariant(const std::string& symbol,
                                                       uint32_t seed) const {
    const auto found = modules_.find(symbol);
    if (found == modules_.end() || found->second.empty()) return nullptr;
    float total = 0.f;
    for (const auto& variant : found->second) total += variant.weight;
    float choice = grammarUnit(seed) * total;
    for (const auto& variant : found->second) {
        choice -= variant.weight;
        if (choice <= 0.f) return &variant;
    }
    return &found->second.back();
}

}  // namespace eve::procgen
