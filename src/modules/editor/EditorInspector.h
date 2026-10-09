#pragma once
#include "common/Export.h"


#include <string>
#include <vector>

namespace eve::editor {

/** @brief Property sheet descriptors + values (host renders via `ui`). */
class EVENGINE_API_ORCHESTRATION EditorInspector {
public:
    /** @brief Clears . */
    void clear();

    /** @brief Adds float. */
    void addFloat(const std::string &id, const std::string &label, float value, float minV,
                  float maxV, float step);
    /** @brief Adds float 3. */
    void addFloat3(const std::string &id, const std::string &label, float x, float y, float z);
    /** @brief Adds bool. */
    void addBool(const std::string &id, const std::string &label, bool value);
    /** @brief Adds string. */
    void addString(const std::string &id, const std::string &label, const std::string &value);
    /** @brief Adds choice. */
    void addChoice(const std::string &id, const std::string &label, const std::string &choicesCsv,
                   const std::string &selected);

    /** @brief Returns the field count. */
    int getFieldCount() const { return static_cast<int>(fields_.size()); }
    /** @brief Returns the field kind. */
    std::string getFieldKind(int index) const;
    /** @brief Returns the field id. */
    std::string getFieldId(int index) const;
    /** @brief Returns the field label. */
    std::string getFieldLabel(int index) const;

    /** @brief Returns the float. */
    float getFloat(const std::string &id) const;
    /** @brief Sets the float. */
    void setFloat(const std::string &id, float value);
    /** @brief Returns the float min. */
    float getFloatMin(const std::string &id) const;
    /** @brief Returns the float max. */
    float getFloatMax(const std::string &id) const;
    /** @brief Returns the float step. */
    float getFloatStep(const std::string &id) const;

    /** @brief Returns the float 3 x. */
    float getFloat3X(const std::string &id) const;
    /** @brief Returns the float 3 y. */
    float getFloat3Y(const std::string &id) const;
    /** @brief Returns the float 3 z. */
    float getFloat3Z(const std::string &id) const;
    /** @brief Sets the float 3. */
    void setFloat3(const std::string &id, float x, float y, float z);

    /** @brief Returns the bool. */
    bool getBool(const std::string &id) const;
    /** @brief Sets the bool. */
    void setBool(const std::string &id, bool value);

    /** @brief Returns the string. */
    std::string getString(const std::string &id) const;
    /** @brief Sets the string. */
    void setString(const std::string &id, const std::string &value);

    /** @brief Returns the choice. */
    std::string getChoice(const std::string &id) const;
    /** @brief Sets the choice. */
    void setChoice(const std::string &id, const std::string &value);
    /** @brief Returns the choices csv. */
    std::string getChoicesCsv(const std::string &id) const;

    /** @brief True when dirty. */
    bool isDirty(const std::string &id) const;
    /** @brief Clears dirty. */
    void clearDirty(const std::string &id);
    /** @brief Clears all dirty. */
    void clearAllDirty();
    /** @brief Returns next dirty field id, or "" if none. */
    std::string pollChangedId();

private:
    struct Field {
        std::string kind;  // float | float3 | bool | string | choice
        std::string id;
        std::string label;
        float f0 = 0.f, f1 = 0.f, f2 = 0.f;
        float minV = 0.f, maxV = 0.f, step = 0.f;
        bool b = false;
        std::string s;
        std::string choices;
        bool dirty = false;
    };

    int findIndex(const std::string &id) const;
    Field *find(const std::string &id);
    const Field *find(const std::string &id) const;

    std::vector<Field> fields_;
    int pollCursor_ = 0;
};

}  // namespace eve::editor
