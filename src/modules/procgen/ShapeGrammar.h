#pragma once

#include "procgen/PointSet.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief One weighted asset variant represented by a shape-grammar symbol. */
struct ShapeModuleVariant {
    std::string asset;
    float       length = 1.f;
    float       weight = 1.f;
};

/**
 * @brief Module-based shape grammar expanded continuously along a 3D polyline.
 *
 * Grammar syntax follows UE PCG's useful subset: comma-separated symbols,
 * bracket groups, `*` (zero or more), `+` (one or more), and an integer repeat
 * suffix. Repetitions fill the available spline length without crossing its end.
 */
class ShapeGrammar {
public:
    /** @brief Remove every registered symbol and diagnostic. */
    void clear();
    /** @brief Register a weighted asset variant for a symbol. */
    bool addModule(const std::string& symbol, const std::string& asset, float length,
                   float weight = 1.f);
    bool removeModule(const std::string& symbol);
    bool hasModule(const std::string& symbol) const;
    int  getModuleCount() const;
    std::string getModuleSymbol(int index) const;
    int         getVariantCount(const std::string& symbol) const;
    std::string getVariantAsset(const std::string& symbol, int index) const;
    float       getVariantLength(const std::string& symbol, int index) const;

    /** @brief Parse and validate grammar without generating output. */
    bool validate(const std::string& grammar);
    /**
     * @brief Expand grammar along control points.
     * @param acceptIncomplete Keep a final module only when it fully fits; when false,
     * generation fails if mandatory grammar cannot fit the spline.
     */
    PointSet* generate(const std::string& grammar, PointSet* controlPoints, uint32_t seed,
                       bool acceptIncomplete);
    std::string getError() const;
    std::string debugReport() const;

private:
    struct Element {
        std::string          symbol;
        std::vector<Element> children;
        int                  repeatMin = 1;
        int                  repeatMax = 1;  // -1 = fill available length
    };
    struct Parser {
        const std::string& text;
        size_t             position = 0;
        std::string        error;
        std::vector<Element> sequence(char terminator = '\0');
        void skipWhitespace();
    };

    float elementMinLength(const Element& element) const;
    float sequenceMinLength(const std::vector<Element>& sequence, size_t from = 0) const;
    bool  expandSequence(const std::vector<Element>& sequence, float available,
                         std::vector<std::string>& symbols, float& used) const;
    const ShapeModuleVariant* chooseVariant(const std::string& symbol, uint32_t seed) const;

    std::unordered_map<std::string, std::vector<ShapeModuleVariant>> modules_;
    std::vector<std::string> moduleOrder_;
    std::string              error_;
    int                      lastSymbolCount_ = 0;
    float                    lastUsedLength_  = 0.f;
    float                    lastSplineLength_ = 0.f;
};

}  // namespace eve::procgen
