#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>

using namespace eve::dialogue;
using eve::avatar::Avatar;
using eve::avatar::AvatarInstance;
using namespace eve::graphics;
using namespace eve::ui;

namespace {

[[maybe_unused]] void hideAllSprites() {
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [sp] = *it;
        sp->visible = false;
    }
}

[[maybe_unused]] AvatarInstance *makePortrait(Avatar *avmod, float r, float g, float b) {
    AvatarInstance *av = avmod->newImageAvatar();
    av->addLayer("body", nullptr, 0);
    av->setLayerSize("body", 140.f, 280.f);
    av->setLayerColor("body", r, g, b, 1.f);
    av->addLayer("face", nullptr, 1);
    av->setLayerSize("face", 90.f, 90.f);
    av->setLayerOffset("face", 25.f, 30.f);
    av->setLayerColor("face", r * 0.7f + 0.25f, g * 0.7f + 0.2f, b * 0.7f + 0.2f, 1.f);
    av->addLayer("mouth", nullptr, 2);
    av->setLayerSize("mouth", 36.f, 14.f);
    av->setLayerOffset("mouth", 52.f, 95.f);
    av->setLayerColor("mouth", 0.85f, 0.25f, 0.3f, 0.15f);
    av->defineExpression("talk", "mouth=1");
    av->defineExpression("idle", "mouth=0.15");
    return av;
}

}  // namespace

TEST_CASE("dialogue.charactersAndStage") {
    Dialogue *dlg = Dialogue::create();
    Avatar *avmod = Avatar::create();
    REQUIRE(dlg != nullptr);

    CHECK(dlg->registerCharacter("alice", "Alice"));
    CHECK(dlg->registerCharacter("bob", "Bob"));
    CHECK(dlg->hasCharacter("alice"));
    CHECK_EQ(dlg->getDisplayName("bob"), std::string("Bob"));
    CHECK_EQ(dlg->getCharacterCount(), 2);

    AvatarInstance *aliceAv = avmod->newImageAvatar();
    aliceAv->addLayer("body", nullptr, 0);
    aliceAv->setLayerSize("body", 100.f, 200.f);
    aliceAv->setPosition(0.f, 300.f);
    CHECK(dlg->bindAvatar("alice", aliceAv));

    dlg->setSlotX("left", 0.2f);
    CHECK(dlg->show("alice", "left"));
    CHECK(dlg->isShown("alice"));
    CHECK_EQ(dlg->getSlot("alice"), std::string("left"));
    dlg->syncStage(800.f, 600.f);
    CHECK_EQ(aliceAv->getX(), 160.f);  // 0.2 * 800
    CHECK(aliceAv->isVisible());

    CHECK(dlg->hide("alice"));
    CHECK(!dlg->isShown("alice"));

    dlg->reset();
    aliceAv->release();
    delete aliceAv;
}

TEST_CASE("dialogue.typewriterAndAdvance") {
    Dialogue *dlg = Dialogue::create();
    dlg->registerCharacter("hero", "Hero");
    dlg->setTypeSpeed(10.f);  // 10 codepoints / sec

    dlg->say("hero", "Hello");
    CHECK(dlg->isTyping());
    CHECK_EQ(dlg->getPhase(), std::string("typing"));
    CHECK_EQ(dlg->getSpeakerName(), std::string("Hero"));

    dlg->update(0.2f);  // 2 chars
    CHECK_EQ(dlg->getVisibleText(), std::string("He"));
    CHECK(dlg->isTyping());

    dlg->update(1.0f);  // finish
    CHECK(dlg->isWaitingAdvance());
    CHECK_EQ(dlg->getVisibleText(), std::string("Hello"));

    dlg->advance();
    CHECK(dlg->isIdle());

    dlg->setTypeSpeed(0.f);
    dlg->narrate("Instant");
    CHECK(dlg->isWaitingAdvance());
    CHECK_EQ(dlg->getVisibleText(), std::string("Instant"));
    dlg->advance();
    CHECK(dlg->isIdle());
}

TEST_CASE("dialogue.utf8Typewriter") {
    Dialogue *dlg = Dialogue::create();
    dlg->setTypeSpeed(100.f);
    // UTF-8 bytes for 你好 / 你 (avoid u8 literals → char8_t in C++20).
    const std::string hi = "\xe4\xbd\xa0\xe5\xa5\xbd";
    const std::string first = "\xe4\xbd\xa0";
    dlg->narrate(hi);
    dlg->update(0.01f);  // 1 codepoint
    CHECK_EQ(dlg->getVisibleText(), first);
    dlg->skipTyping();
    CHECK_EQ(dlg->getVisibleText(), hi);
    CHECK(dlg->isWaitingAdvance());
}

TEST_CASE("dialogue.choices") {
    Dialogue *dlg = Dialogue::create();
    dlg->registerCharacter("n", "N");
    dlg->say("n", "Pick one");
    dlg->skipTyping();

    dlg->clearChoices();
    CHECK(dlg->addChoice("a", "Alpha"));
    CHECK(dlg->addChoice("b", "Beta"));
    CHECK_EQ(dlg->getChoiceCount(), 2);
    dlg->presentChoices();
    CHECK(dlg->isWaitingChoice());
    CHECK_EQ(dlg->getChoiceLabel(0), std::string("Alpha"));

    CHECK(!dlg->selectChoice(99));
    CHECK(dlg->selectChoice(1));
    CHECK_EQ(dlg->getSelectedChoiceId(), std::string("b"));
    CHECK(dlg->isIdle());
}

TEST_CASE("dialogue.expressionForward") {
    Dialogue *dlg = Dialogue::create();
    Avatar *avmod = Avatar::create();
    dlg->registerCharacter("c", "C");
    AvatarInstance *av = avmod->newImageAvatar();
    av->addLayer("face", nullptr, 0);
    av->defineExpression("happy", "face=1");
    dlg->bindAvatar("c", av);
    CHECK(dlg->setExpression("c", "happy"));
    CHECK_EQ(av->getExpression(), std::string("happy"));
    CHECK(dlg->setMotion("c", "wave"));
    CHECK_EQ(av->getMotion(), std::string("wave"));
    av->release();
    delete av;
}

TEST_CASE("dialogue.lipSyncWhileTyping") {
    Dialogue *dlg = Dialogue::create();
    Avatar *avmod = Avatar::create();
    dlg->registerCharacter("hero", "Hero");
    AvatarInstance *av = avmod->newImageAvatar();
    av->addLayer("mouthOpen", nullptr, 1);
    av->setLayerSize("mouthOpen", 40.f, 20.f);
    av->setLayerColor("mouthOpen", 0.8f, 0.2f, 0.2f, 0.f);
    dlg->bindAvatar("hero", av);
    dlg->show("hero", "center");

    dlg->setLipSyncEnabled(true);
    dlg->setLipSyncParameter("mouthOpen");
    dlg->setLipSyncAmplitude(0.9f);
    dlg->setTypeSpeed(20.f);
    dlg->say("hero", "Hello there");

    CHECK(dlg->isTyping());
    dlg->update(0.05f);
    CHECK(dlg->getLipSyncValue() > 0.1f);
    CHECK(av->getParameter("mouthOpen") > 0.1f);

    dlg->skipTyping();
    dlg->update(0.5f);  // ease shut
    CHECK(dlg->getLipSyncValue() < 0.2f);

    av->release();
    delete av;
}
