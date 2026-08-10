#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
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

void hideAllSprites() {
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [sp] = *it;
        sp->visible = false;
    }
}

AvatarInstance *makePortrait(Avatar *avmod, float r, float g, float b) {
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

TEST_CASE("dialogue.stage.avatarTypewriterPreview") {
    hideAllSprites();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 800;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());

    Dialogue *dlg = Dialogue::create();
    Avatar *avmod = Avatar::create();
    dlg->registerCharacter("alice", "Alice");
    dlg->registerCharacter("bob", "Bob");

    AvatarInstance *alice = makePortrait(avmod, 0.35f, 0.55f, 0.95f);
    AvatarInstance *bob = makePortrait(avmod, 0.9f, 0.45f, 0.35f);
    dlg->bindAvatar("alice", alice);
    dlg->bindAvatar("bob", bob);
    dlg->setSlotX("left", 0.12f);
    dlg->setSlotX("right", 0.72f);
    dlg->show("alice", "left");
    dlg->show("bob", "right");

    dlg->setLipSyncEnabled(true);
    dlg->setLipSyncParameter("mouth");
    dlg->setLipSyncAmplitude(0.95f);
    dlg->setTypeSpeed(18.f);

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.08f;
    cam->data()->g = 0.09f;
    cam->data()->b = 0.12f;

    // Floor / stage backdrop.
    auto *stage = Renderable2D::create();
    stage->transform()->x = 0;
    stage->transform()->y = 360;
    stage->sprite()->width = 800;
    stage->sprite()->height = 120;
    stage->sprite()->r = 0.12f;
    stage->sprite()->g = 0.14f;
    stage->sprite()->b = 0.18f;
    stage->sprite()->visible = true;

    enum Phase { Line1, Line2, Choices, Done };
    Phase phase = Line1;
    dlg->say("alice", "Welcome to the EVEngine stage preview.");
    dlg->setExpression("alice", "talk");
    dlg->setExpression("bob", "idle");

    int frames = 0;
    bool sawChoice = false;
    for (int frame = 0; frame < 150; ++frame) {
        ++frames;
        dlg->update(0.016f);
        dlg->syncStage(float(gfx->getWidth()), float(gfx->getHeight()));
        avmod->sync();

        if (phase == Line1 && dlg->isWaitingAdvance()) {
            dlg->advance();
            dlg->say("bob", "Looks like the portraits and typewriter are alive.");
            dlg->setExpression("alice", "idle");
            dlg->setExpression("bob", "talk");
            phase = Line2;
        } else if (phase == Line2 && dlg->isWaitingAdvance()) {
            dlg->advance();
            dlg->clearChoices();
            dlg->addChoice("cont", "Continue");
            dlg->addChoice("exit", "Exit preview");
            dlg->presentChoices();
            dlg->setExpression("bob", "idle");
            phase = Choices;
        } else if (phase == Choices && dlg->isWaitingChoice()) {
            sawChoice = true;
            if (frame > 110) {
                dlg->selectChoice(0);
                phase = Done;
            }
        }

        const std::string speaker = dlg->getSpeakerName();
        const std::string body = dlg->getVisibleText();
        std::vector<WidgetDesc> kids = {
            text(speaker.empty() ? "Narrator" : speaker, "speaker"),
            separator("sep"),
            text(body.empty() ? "..." : body, "line"),
        };
        if (dlg->isWaitingChoice()) {
            kids.push_back(separator("sep2"));
            for (int i = 0; i < dlg->getChoiceCount(); ++i) {
                kids.push_back(text("• " + dlg->getChoiceLabel(i), "c" + std::to_string(i)));
            }
        } else if (dlg->isWaitingAdvance()) {
            kids.push_back(text("[advance]", "hint"));
        }

        ui->remountAs("dlg", window("Dialogue", kids, "root"));
        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(frames, 100);
    CHECK(sawChoice);
    CHECK(dlg->isIdle());

    stage->sprite()->visible = false;
    alice->setVisible(false);
    bob->setVisible(false);
    alice->sync();
    bob->sync();
    alice->release();
    bob->release();
    delete alice;
    delete bob;
    dlg->reset();
    win->close();
}
