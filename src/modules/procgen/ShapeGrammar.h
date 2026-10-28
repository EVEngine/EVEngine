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
 * suffix. Module symbols do not contain decimal digits, which are reserved for
 * exact repetition. Repetitions fill available spline length without crossing its end.
 */
class EVENGINE_API_DOMAINS ShapeGrammar {
public:
    /** @brief Remove every registered symbol and diagnostic. */
    void clear();
    /** @brief Register a weighted asset variant for a symbol. */
    bool addModule(const std::string& symbol, const std::string& asset, float length,
                   float weight = 1.f);
    /** @brief Removes module. */
    bool removeModule(const std::string& symbol);
    /** @brief True when module. */
    bool hasModule(const std::string& symbol) const;
    /** @brief Returns the module count. */
    int  getModuleCount() const;
    /** @brief Returns the module symbol. */
    std::string getModuleSymbol(int index) const;
    /** @brief Returns the variant count. */
    int         getVariantCount(const std::string& symbol) const;
    /** @brief Returns the variant asset. */
    std::string getVariantAsset(const std::string& symbol, int index) const;
    /** @brief Returns the variant length. */
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
    /** @brief Returns the error. */
    std::string getError() const;
    /** @brief Debug report. */
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
        /** @brief Sequence. */
        std::vector<Element> sequence(char terminator = '\0');
        /** @brief Skip whitespace. */
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
