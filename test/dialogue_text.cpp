#include "dialogue/ConversationText.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::dialogue;

TEST_CASE("dialogueText.parametersFallbackAndTransforms") {
    Value bindings =
        Value::Object{{"speaker", Value::Object{{"name", Value("mara")}}},
                      {"listener", Value::Object{{"name", Value("Iris")}}},
                      {"location", Value::Object{{"name", Value("harbor")}}}};
    Value locals = Value::Object{{"speaker", Value::Object{{"name", Value("local mara")}}}};
    ConversationTextRenderer renderer;
    CHECK(renderer.render("{speaker.name|capitalize} greets {listener.name} at {location.name|upper}.", bindings,
                          locals) == "Local mara greets Iris at HARBOR.");
    CHECK(renderer.render("Welcome, {listener.title??traveler}.", bindings, locals) == "Welcome, traveler.");
    CHECK(renderer.render("Keep {unknown.value} visible.", bindings, locals) == "Keep {unknown.value} visible.");
}

TEST_CASE("dialogueText.stateDrivenCharacterTone") {
    ConversationTextRenderer renderer;
    renderer.addToneRule("speaker.tired", "... ", " ...", "Good", "Fine");
    renderer.addToneRule("speaker.formal", "Captain, ", {}, {}, {});
    Value values = Value::Object{};
    CHECK(renderer.render("Good morning.", values, values, [](const std::string& expression) {
        return expression == "speaker.tired";
    }) == "... Fine morning. ...");
    CHECK(renderer.render("Good morning.", values, values, [](const std::string& expression) {
        return expression == "speaker.formal";
    }) == "Captain, Good morning.");
}
