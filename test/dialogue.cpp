#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"

#include <string>

using namespace eve::dialogue;
using eve::avatar::Avatar;
using eve::avatar::AvatarInstance;

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

    aliceAv->release();
    delete aliceAv;
    dlg->reset();
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
