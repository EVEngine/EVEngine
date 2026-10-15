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


TEST_CASE("dialogue.stage.avatarTypewriterPreview") {
    hideAllSprites();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);
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

        REQUIRE(UIHost::resolve(ui->remountAs("dlg", window("Dialogue", kids, "root"))).has_value());
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
    dlg->reset();
    alice->release();
    bob->release();
    delete alice;
    delete bob;
    win->close();
}
