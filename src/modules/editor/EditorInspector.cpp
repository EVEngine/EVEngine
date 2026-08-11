#include "editor/EditorInspector.h"

#include "common/Exception.h"

namespace eve::editor {

void EditorInspector::clear() {
    fields_.clear();
    pollCursor_ = 0;
}

int EditorInspector::findIndex(const std::string &id) const {
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i) {
        if (fields_[i].id == id) return i;
    }
    return -1;
}

EditorInspector::Field *EditorInspector::find(const std::string &id) {
    int i = findIndex(id);
    return i < 0 ? nullptr : &fields_[i];
}

const EditorInspector::Field *EditorInspector::find(const std::string &id) const {
    int i = findIndex(id);
    return i < 0 ? nullptr : &fields_[i];
}

void EditorInspector::addFloat(const std::string &id, const std::string &label, float value,
                               float minV, float maxV, float step) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addFloat: bad id");
    Field f;
    f.kind = "float";
    f.id = id;
    f.label = label;
    f.f0 = value;
    f.minV = minV;
    f.maxV = maxV;
    f.step = step;
    fields_.push_back(f);
}

void EditorInspector::addFloat3(const std::string &id, const std::string &label, float x, float y,
                                float z) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addFloat3: bad id");
    Field f;
    f.kind = "float3";
    f.id = id;
    f.label = label;
    f.f0 = x;
    f.f1 = y;
    f.f2 = z;
    fields_.push_back(f);
}

void EditorInspector::addBool(const std::string &id, const std::string &label, bool value) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addBool: bad id");
    Field f;
    f.kind = "bool";
    f.id = id;
    f.label = label;
    f.b = value;
    fields_.push_back(f);
}

void EditorInspector::addString(const std::string &id, const std::string &label,
                                const std::string &value) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addString: bad id");
    Field f;
    f.kind = "string";
    f.id = id;
    f.label = label;
    f.s = value;
    fields_.push_back(f);
}

void EditorInspector::addChoice(const std::string &id, const std::string &label,
                                const std::string &choicesCsv, const std::string &selected) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addChoice: bad id");
    Field f;
    f.kind = "choice";
    f.id = id;
    f.label = label;
    f.choices = choicesCsv;
    f.s = selected;
    fields_.push_back(f);
}

std::string EditorInspector::getFieldKind(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldKind: bad index");
    return fields_[index].kind;
}
std::string EditorInspector::getFieldId(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldId: bad index");
    return fields_[index].id;
}
std::string EditorInspector::getFieldLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldLabel: bad index");
    return fields_[index].label;
}

float EditorInspector::getFloat(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloat: missing float field");
    return f->f0;
}

void EditorInspector::setFloat(const std::string &id, float value) {
    Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::setFloat: missing float field");
    if (f->f0 != value) {
        f->f0 = value;
        f->dirty = true;
    }
}

float EditorInspector::getFloatMin(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatMin: missing");
    return f->minV;
}
float EditorInspector::getFloatMax(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatMax: missing");
    return f->maxV;
}
float EditorInspector::getFloatStep(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatStep: missing");
    return f->step;
}

float EditorInspector::getFloat3X(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3X: missing");
    return f->f0;
}
float EditorInspector::getFloat3Y(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3Y: missing");
    return f->f1;
}
float EditorInspector::getFloat3Z(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3Z: missing");
    return f->f2;
}

void EditorInspector::setFloat3(const std::string &id, float x, float y, float z) {
    Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::setFloat3: missing");
    if (f->f0 != x || f->f1 != y || f->f2 != z) {
        f->f0 = x;
        f->f1 = y;
        f->f2 = z;
        f->dirty = true;
    }
}

bool EditorInspector::getBool(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "bool") throw Exception("EditorInspector::getBool: missing");
    return f->b;
}

void EditorInspector::setBool(const std::string &id, bool value) {
    Field *f = find(id);
    if (!f || f->kind != "bool") throw Exception("EditorInspector::setBool: missing");
    if (f->b != value) {
        f->b = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getString(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "string") throw Exception("EditorInspector::getString: missing");
    return f->s;
}

void EditorInspector::setString(const std::string &id, const std::string &value) {
    Field *f = find(id);
    if (!f || f->kind != "string") throw Exception("EditorInspector::setString: missing");
    if (f->s != value) {
        f->s = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getChoice(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::getChoice: missing");
    return f->s;
}

void EditorInspector::setChoice(const std::string &id, const std::string &value) {
    Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::setChoice: missing");
    if (f->s != value) {
        f->s = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getChoicesCsv(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::getChoicesCsv: missing");
    return f->choices;
}

bool EditorInspector::isDirty(const std::string &id) const {
    const Field *f = find(id);
    return f && f->dirty;
}

void EditorInspector::clearDirty(const std::string &id) {
    Field *f = find(id);
    if (f) f->dirty = false;
}

void EditorInspector::clearAllDirty() {
    for (auto &f : fields_) f.dirty = false;
    pollCursor_ = 0;
}

std::string EditorInspector::pollChangedId() {
    const int n = static_cast<int>(fields_.size());
    if (n == 0) return "";
    for (int k = 0; k < n; ++k) {
        int i = (pollCursor_ + k) % n;
        if (fields_[i].dirty) {
            fields_[i].dirty = false;
            pollCursor_ = (i + 1) % n;
            return fields_[i].id;
        }
    }
    return "";
}

}  // namespace eve::editor
