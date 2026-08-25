#pragma once

#include "procgen/Params.h"

#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Semantic type used by schema-driven recipe parameter editors. */
enum class ParamKind { Integer, Float, Boolean, String, Choice };

/** @brief UI-independent metadata for one procedural recipe parameter. */
struct ParamDescriptor {
    std::string              key;
    std::string              displayName;
    std::string              description;
    std::string              category = "General";
    ParamKind                kind     = ParamKind::String;
    std::string              defaultValue;
    bool                     hasMinimum = false;
    bool                     hasMaximum = false;
    double                   minimum    = 0.0;
    double                   maximum    = 0.0;
    double                   step       = 0.0;
    std::vector<std::string> choices;
    bool                     advanced = false;

    /** @brief Construct a bounded integer descriptor. @param key Stable parameter key. @param label Display label.
     * @param defaultValue Default value. @param minimum Inclusive minimum. @param maximum Inclusive maximum. @param
     * step Suggested editor step. @return Parameter metadata. */
    static ParamDescriptor integer(std::string key, std::string label, int defaultValue, int minimum, int maximum,
                                   int step = 1);
    /** @brief Construct a bounded floating-point descriptor. @param key Stable parameter key. @param label Display
     * label. @param defaultValue Default value. @param minimum Inclusive minimum. @param maximum Inclusive maximum.
     * @param step Suggested editor step. @return Parameter metadata. */
    static ParamDescriptor floating(std::string key, std::string label, float defaultValue, float minimum,
                                    float maximum, float step);
    /** @brief Construct a Boolean descriptor stored as zero or one in Params. @param key Stable parameter key. @param
     * label Display label. @param defaultValue Default value. @return Parameter metadata. */
    static ParamDescriptor boolean(std::string key, std::string label, bool defaultValue);
    /** @brief Construct a free-form string descriptor. @param key Stable parameter key. @param label Display label.
     * @param defaultValue Default value. @return Parameter metadata. */
    static ParamDescriptor text(std::string key, std::string label, std::string defaultValue);
    /** @brief Construct a finite-choice string descriptor. @param key Stable parameter key. @param label Display label.
     * @param defaultValue Default value. @param choices Allowed values. @return Parameter metadata. */
    static ParamDescriptor choice(std::string key, std::string label, std::string defaultValue,
                                  std::vector<std::string> choices);
};

/** @brief Complete metadata and parameter schema shared by every procgen recipe family. */
struct RecipeDescriptor {
    std::string                  id;
    std::string                  displayName;
    std::string                  category;
    std::vector<ParamDescriptor> params;

    /** @brief Create a schema with common seed, width and height parameters. @param id Stable recipe id. @param
     * displayName Display name. @param category Browser category. @param minimumWidth Minimum width. @param
     * minimumHeight Minimum height. @param maximumWidth Maximum width. @param maximumHeight Maximum height. @return
     * Recipe metadata. */
    static RecipeDescriptor grid(std::string id, std::string displayName, std::string category, int minimumWidth,
                                 int minimumHeight, int maximumWidth = 4096, int maximumHeight = 4096);
    /** @brief Find one parameter descriptor by stable key. @param key Parameter key. @return Schema-owned descriptor or
     * nullptr. */
    const ParamDescriptor* find(const std::string& key) const;
    /** @brief Fill missing Params values from this schema. @param params Values to update. */
    void applyDefaults(Params& params) const;

    /** @brief Return the number of reflected parameters. @return Parameter count. */
    int getParamCount() const { return int(params.size()); }
    /** @brief Return the stable recipe id. @return Recipe id. */
    std::string getId() const { return id; }
    /** @brief Return the human-readable recipe name. @return Display name. */
    std::string getDisplayName() const { return displayName; }
    /** @brief Return the recipe browser category. @return Category name. */
    std::string getCategory() const { return category; }
    /** @brief Return a parameter key by index. @param index Parameter index. @return Key or empty text. */
    std::string getParamKey(int index) const;
    /** @brief Return a parameter label by index. @param index Parameter index. @return Label or empty text. */
    std::string getParamLabel(int index) const;
    /** @brief Return parameter help text by index. @param index Parameter index. @return Help text or empty text. */
    std::string getParamDescription(int index) const;
    /** @brief Return a parameter category by index. @param index Parameter index. @return Category or empty text. */
    std::string getParamCategory(int index) const;
    /** @brief Return the portable parameter kind. @param index Parameter index. @return int, float, bool, string,
     * choice, or empty text. */
    std::string getParamKind(int index) const;
    /** @brief Return the schema default encoded as text. @param index Parameter index. @return Encoded default or empty
     * text. */
    std::string getParamDefault(int index) const;
    /** @brief Test whether a numeric minimum exists. @param index Parameter index. @return True when bounded below. */
    bool paramHasMinimum(int index) const;
    /** @brief Test whether a numeric maximum exists. @param index Parameter index. @return True when bounded above. */
    bool paramHasMaximum(int index) const;
    /** @brief Return a numeric minimum. @param index Parameter index. @return Minimum or zero. */
    float getParamMinimum(int index) const;
    /** @brief Return a numeric maximum. @param index Parameter index. @return Maximum or zero. */
    float getParamMaximum(int index) const;
    /** @brief Return the suggested numeric editing step. @param index Parameter index. @return Step or zero. */
    float getParamStep(int index) const;
    /** @brief Test whether compact inspectors should hide the field. @param index Parameter index. @return Advanced
     * flag. */
    bool isParamAdvanced(int index) const;
    /** @brief Return the number of choices for one parameter. @param index Parameter index. @return Choice count. */
    int getParamChoiceCount(int index) const;
    /** @brief Return one choice. @param paramIndex Parameter index. @param choiceIndex Choice index. @return Choice or
     * empty text. */
    std::string getParamChoice(int paramIndex, int choiceIndex) const;

private:
    const ParamDescriptor* at(int index) const;
};

}  // namespace eve::procgen
