#pragma once

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Property sheet descriptors + values (host renders via `ui`). */
class EditorInspector {
public:
    void clear();

    void addFloat(const std::string &id, const std::string &label, float value, float minV,
                  float maxV, float step);
    void addFloat3(const std::string &id, const std::string &label, float x, float y, float z);
    void addBool(const std::string &id, const std::string &label, bool value);
    void addString(const std::string &id, const std::string &label, const std::string &value);
    void addChoice(const std::string &id, const std::string &label, const std::string &choicesCsv,
                   const std::string &selected);

    int getFieldCount() const { return static_cast<int>(fields_.size()); }
    std::string getFieldKind(int index) const;
    std::string getFieldId(int index) const;
    std::string getFieldLabel(int index) const;

    float getFloat(const std::string &id) const;
    void setFloat(const std::string &id, float value);
    float getFloatMin(const std::string &id) const;
    float getFloatMax(const std::string &id) const;
    float getFloatStep(const std::string &id) const;

    float getFloat3X(const std::string &id) const;
    float getFloat3Y(const std::string &id) const;
    float getFloat3Z(const std::string &id) const;
    void setFloat3(const std::string &id, float x, float y, float z);

    bool getBool(const std::string &id) const;
    void setBool(const std::string &id, bool value);

    std::string getString(const std::string &id) const;
    void setString(const std::string &id, const std::string &value);

    std::string getChoice(const std::string &id) const;
    void setChoice(const std::string &id, const std::string &value);
    std::string getChoicesCsv(const std::string &id) const;

    bool isDirty(const std::string &id) const;
    void clearDirty(const std::string &id);
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
