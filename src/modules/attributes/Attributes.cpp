#include "attributes/Attributes.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::attributes {

Module_IMPL(Attributes, new Attributes());

AttributeSet* Attributes::newSet(const std::string& subject) { return new AttributeSet(subject); }

void Attributes::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Attributes::create, false);
    expose(cls);

    auto set = table.addClass<AttributeSet>(
        "AttributeSet", std::function<AttributeSet*()>([]() -> AttributeSet* { return nullptr; }), true);
    set.addFunc("getSubject", [](AttributeSet* s) { return s ? s->subject() : std::string{}; });
    set.addFunc("setBase", [](AttributeSet* s, const std::string& key, float value) {
        if (s) s->setBase(key, double(value));
    });
    set.addFunc("modifyBase", [](AttributeSet* s, const std::string& key, float value) {
        if (s) s->modifyBase(key, double(value));
    });
    set.addFunc("has", &AttributeSet::has);
    set.addFunc("getBase", [](AttributeSet* s, const std::string& key, float fallback) {
        return s ? float(s->getBase(key, fallback)) : fallback;
    });
    set.addFunc("getFinal", [](AttributeSet* s, const std::string& key, float fallback) {
        return s ? float(s->getFinal(key, fallback)) : fallback;
    });
    set.addFunc("addModifier", [](AttributeSet* s, const std::string& id, const std::string& attribute,
                                  const std::string& source, const std::string& operation, float value, int priority) {
        return s && s->addModifier(id, attribute, source, operation, value, priority);
    });
    set.addFunc("removeModifier", [](AttributeSet *s, const std::string &id) {
        return s && s->removeModifier(id).ok();
    });
    set.addFunc("removeBySource", [](AttributeSet *s, const std::string &source,
                                      const std::string &attribute) {
        if (!s) return 0;
        auto result = s->removeBySource(source, attribute);
        return result.ok() ? result.value() : 0;
    });
    set.addFunc("clearModifiers", &AttributeSet::clearModifiers);
    set.addFunc("getModifierCount", &AttributeSet::modifierCount);
    set.addFunc("getModifierId", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        return m ? m->id : std::string{};
    });
    set.addFunc("getModifierAttribute", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        return m ? m->attribute : std::string{};
    });
    set.addFunc("getModifierSource", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        return m ? m->source : std::string{};
    });
    set.addFunc("getModifierOperation", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        if (!m) return std::string{};
        if (m->operation == AttributeOperation::Custom) return m->policyId;
        return std::string(attributeOperationName(m->operation));
    });
    set.addFunc("getModifierPriority", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        return m ? m->priority : 0;
    });
    set.addFunc("getModifierSequence", [](AttributeSet* s, int index) {
        const auto* m = s ? s->modifierAt(index) : nullptr;
        return m ? m->sequence : std::uint64_t(0);
    });
}

void Attributes::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Attributes::getName);
    cls.addFunc("newSet", &Attributes::newSet);
}

}  // namespace eve::attributes
