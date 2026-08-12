// RPG cross-module simulation: a tiny "village defense" scenario that drives
// tilemap, sprite animation, coordinate conversion, 2D normals/lighting,
// buffs, and building placement together — the same surface an RPG game would use.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Quad.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "rpg/RPG.h"
#include "rpg/Settlement.h"
#include "window/Window.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace eve::rpg;
using namespace eve::map;
using namespace eve::building;
using namespace eve::graphics;
using namespace eve::animation;

namespace {

bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool approxEqD(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

void setupVitals(RPGActor *actor, double maxHp, double maxMp, double maxStamina) {
    actor->setBaseAttribute("health", maxHp);
    actor->setBaseAttribute("mana", maxMp);
    actor->setBaseAttribute("stamina", maxStamina);
    actor->addAttributeModifier("health", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("health", "system", "clampMax", maxHp, 901);
    actor->addAttributeModifier("mana", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("mana", "system", "clampMax", maxMp, 901);
    actor->addAttributeModifier("stamina", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("stamina", "system", "clampMax", maxStamina, 901);
}

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            float nx = (x < w / 2) ? -0.7f : 0.7f;
            float ny = 0.f;
            float nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

}  // namespace

/**
 * Mini RPG: defend a village tilemap.
 *
 * Timeline (headless, deterministic):
 *  1) Build ortho tilemap + collect tile draw items
 *  2) Convert tile ↔ world ↔ screen ↔ placement cell
 *  3) Drive a walk sprite clip while the hero "moves" toward the gate
 *  4) Place a watchtower via ghost snap on the building grid
 *  5) Attach albedo+normal sprites + point light; collectSprites must pick litPath
 *  6) Buff → cast fireball (burn DOT) → settlement damage → tick until enemy dies
 *
 * Optional GPU append: if a Vulkan window is available, render the lit sprite and
 * assert normal-map side brightness (same contract as Lighting2D.normalMapLitSideBrighter).
 */
TEST_CASE("rpg.simulation.villageDefenseSystems") {
    constexpr float kTile = 32.f;
    constexpr int kMapW = 12;
    constexpr int kMapH = 8;
    constexpr float kViewW = 320.f;
    constexpr float kViewH = 240.f;

    // ------------------------------------------------------------------
    // 1) Tilemap: grass fill + path + wall gate
    // ------------------------------------------------------------------
    hideAllTileLayers();
    auto *mapMod = Map::create();
    TileLayer *layer = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    REQUIRE(layer != nullptr);
    layer->config()->orientation = MapOrientation::Orthogonal;
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
    layer->fill(1);  // grass
    for (int x = 0; x < kMapW; ++x) layer->setTile(x, 4, 2);  // road
    layer->setTile(5, 3, 3);                                  // wall
    layer->setTile(6, 3, 3);
    layer->setTile(7, 3, 0);  // gate opening
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(7, 3), 0);
    CHECK_EQ(layer->getTile(5, 4), 2);

    std::vector<DrawItem2D> tileItems;
    TileRenderSystem::collect(tileItems);
    REQUIRE(tileItems.size() >= 2);
    // Non-empty tiles only; gate cell (7,3) is 0 and must not appear.
    size_t tilesBefore = tileItems.size();
    layer->setTile(7, 3, 4);
    tileItems.clear();
    TileRenderSystem::collect(tileItems);
    CHECK_EQ(tileItems.size(), tilesBefore + 1);
    layer->setTile(7, 3, 0);

    // ------------------------------------------------------------------
    // 2) Coordinate conversion: tile ↔ world ↔ screen ↔ placement cell
    // ------------------------------------------------------------------
    float heroWx = 0.f, heroWy = 0.f;
    layer->tileToWorld(2, 4, heroWx, heroWy);
    CHECK(approxEq(heroWx, 2.f * kTile));
    CHECK(approxEq(heroWy, 4.f * kTile));

    int backTx = -1, backTy = -1;
    layer->worldToTile(heroWx + kTile * 0.5f, heroWy + kTile * 0.5f, backTx, backTy);
    CHECK_EQ(backTx, 2);
    CHECK_EQ(backTy, 4);

    // Free function path must agree with layer wrappers.
    float projWx = 0.f, projWy = 0.f;
    tileToWorld(*layer->config(), 2, 4, projWx, projWy);
    CHECK(approxEq(projWx, heroWx));
    CHECK(approxEq(projWy, heroWy));

    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x = heroWx + 64.f;
    cam->data()->y = heroWy;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.12f, 0.12f, 0.14f);

    float screenX = cam->worldToScreenX(heroWx, heroWy, kViewW, kViewH);
    float screenY = cam->worldToScreenY(heroWx, heroWy, kViewW, kViewH);
    float roundWx = cam->screenToWorldX(screenX, screenY, kViewW, kViewH);
    float roundWy = cam->screenToWorldY(screenX, screenY, kViewW, kViewH);
    CHECK(approxEq(roundWx, heroWx));
    CHECK(approxEq(roundWy, heroWy));

    // Cursor over empty ground near the gate → placement cell.
    float pickWx = cam->screenToWorldX(kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH);
    float pickWy = cam->screenToWorldY(kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH);
    int pickTx = -1, pickTy = -1;
    layer->worldToTile(pickWx, pickWy, pickTx, pickTy);
    CHECK(pickTx >= 0);
    CHECK(pickTy >= 0);

    // ------------------------------------------------------------------
    // 3) Sprite animation: walk cycle while advancing along the road
    // ------------------------------------------------------------------
    auto *animMod = Animation::create();
    std::unique_ptr<SpriteSheet> sheet(animMod->newSpriteSheet());
    sheet->setGrid(4, 1, 16, 16);
    std::unique_ptr<SpriteClip> walk(animMod->newSpriteClip("walk"));
    walk->setLoop(true);
    walk->addFrame(0, 0.1f);
    walk->addFrame(1, 0.1f);
    walk->addFrame(2, 0.1f);
    walk->addFrame(3, 0.1f);

    std::unique_ptr<SpriteAnim> walker(animMod->newSpriteAnim());
    walker->setSheet(sheet.get());
    Quad walkQuad;
    walker->bindQuad(&walkQuad);
    walker->play(walk.get());
    CHECK(walker->isPlaying());
    CHECK_EQ(walker->getSheetFrame(), 0);

    // Move hero three tiles east; animation should advance with dt.
    float moveWx = heroWx;
    float moveWy = heroWy;
    for (int step = 0; step < 3; ++step) {
        moveWx += kTile;
        animMod->update(0.12f);
    }
    CHECK_EQ(walker->getClipFrame(), 3);  // 0.36s into 0.4s loop → frame 3
    CHECK_EQ(walkQuad.getX(), 48);        // frame 3 * 16
    int moveTx = -1, moveTy = -1;
    layer->worldToTile(moveWx + 1.f, moveWy + 1.f, moveTx, moveTy);
    CHECK_EQ(moveTx, 5);
    CHECK_EQ(moveTy, 4);

    // ------------------------------------------------------------------
    // 4) Building placement: watchtower snapped to the same grid
    // ------------------------------------------------------------------
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition tower;
    tower.id = "sim.watchtower";
    tower.displayName = "Watchtower";
    tower.footprintW = 1;
    tower.footprintH = 1;
    tower.snapMode = "grid";
    tower.tags = {"defense", "tower"};
    tower.requireTerrain = {1};
    BuildingRegistry::registerBuilding(tower);

    PlacementWorld world(kMapW, kMapH, kTile);
    world.setId("village");
    world.setOrigin(0.f, 0.f);
    world.fillTerrain(1);  // buildable ground
    // Mirror road as non-buildable for a soft constraint check (terrain 0).
    for (int x = 0; x < kMapW; ++x) world.setTerrain(x, 4, 0);

    CHECK(!world.canPlace("sim.watchtower", 5, 4, 0.f));
    CHECK_EQ(world.canPlaceReason("sim.watchtower", 5, 4, 0.f), "terrain_mismatch");

    // Player clicks near tile (7,2) — ghost snaps to cell, then place.
    Ghost ghost;
    ghost.setBuildingId("sim.watchtower");
    float clickWx = 7.f * kTile + 10.f;
    float clickWy = 2.f * kTile + 6.f;
    ghost.setFromWorld(&world, clickWx, clickWy);
    CHECK_EQ(ghost.getCellX(), 7);
    CHECK_EQ(ghost.getCellY(), 2);
    CHECK(approxEq(ghost.getWorldX(), 7.f * kTile));
    CHECK(approxEq(ghost.getWorldY(), 2.f * kTile));
    CHECK(ghost.validate(&world));

    // Placement cell ↔ world must agree with tile projection for this ortho map.
    CHECK_EQ(world.worldToCellX(ghost.getWorldX()), 7);
    CHECK_EQ(world.worldToCellY(ghost.getWorldY()), 2);
    CHECK(approxEq(world.cellToWorldX(7), 7.f * kTile));
    CHECK(approxEq(world.cellToWorldY(2), 2.f * kTile));
    float tileGateWx = 0.f, tileGateWy = 0.f;
    layer->tileToWorld(7, 2, tileGateWx, tileGateWy);
    CHECK(approxEq(tileGateWx, world.cellToWorldX(7)));
    CHECK(approxEq(tileGateWy, world.cellToWorldY(2)));

    int towerId = world.placeGhost(&ghost);
    REQUIRE(towerId > 0);
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK(!world.isCellEmpty(7, 2));
    CHECK_EQ(PlacementSystem::events().size(), size_t(1));
    CHECK_EQ(PlacementSystem::events()[0].action, "place");
    CHECK_EQ(PlacementSystem::events()[0].buildingId, "sim.watchtower");

    // Occupancy conflict: cannot stack another tower on the same cell.
    CHECK(!world.canPlace("sim.watchtower", 7, 2, 0.f));
    CHECK_EQ(world.canPlaceReason("sim.watchtower", 7, 2, 0.f), "occupied");

    // ------------------------------------------------------------------
    // 5) 2D normals + lighting: litPath collection + sort order (headless)
    // ------------------------------------------------------------------
    Texture albedoTex;
    albedoTex.width = 64;
    albedoTex.height = 64;
    Texture normalTex;
    normalTex.width = 64;
    normalTex.height = 64;

    auto *heroSprite = Renderable2D::create();
    heroSprite->transform()->x = moveWx;
    heroSprite->transform()->y = moveWy;
    heroSprite->sprite()->width = 16.f;
    heroSprite->sprite()->height = 16.f;
    heroSprite->sprite()->texture = &albedoTex;
    heroSprite->sprite()->normalTexture = &normalTex;
    heroSprite->sprite()->quad = &walkQuad;
    heroSprite->sprite()->receiveLight = true;
    heroSprite->sprite()->visible = true;
    heroSprite->sprite()->layer = 10;
    heroSprite->sprite()->camera = cam;

    auto *unlitProp = Renderable2D::create();
    unlitProp->transform()->x = world.getBuildingWorldX(towerId);
    unlitProp->transform()->y = world.getBuildingWorldY(towerId);
    unlitProp->sprite()->width = 24.f;
    unlitProp->sprite()->height = 32.f;
    unlitProp->sprite()->texture = &albedoTex;
    unlitProp->sprite()->normalTexture = nullptr;
    unlitProp->sprite()->receiveLight = true;
    unlitProp->sprite()->visible = true;
    unlitProp->sprite()->layer = 10;
    unlitProp->sprite()->camera = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(moveWx + 8.f, moveWy + 8.f);
    torch->setColor(1.f, 0.85f, 0.6f, 2.0f);
    torch->setRadius(96.f);
    torch->setEnabled(true);
    CHECK_EQ(torch->getType(), "point");
    CHECK(approxEq(torch->getRadius(), 96.f));
    CHECK(torch->isEnabled());

    std::vector<DrawItem2D> scene;
    TileRenderSystem::collect(scene);
    RenderSystem::collectSprites(scene);
    REQUIRE(scene.size() >= 2);

    bool foundLit = false;
    bool foundUnlitTextured = false;
    for (const auto &it : scene) {
        if (it.texture == &albedoTex && it.normal == &normalTex) {
            CHECK(it.litPath);
            CHECK(it.receiveLight);
            CHECK(it.hasUV);
            foundLit = true;
        }
        if (it.texture == &albedoTex && it.normal == nullptr && it.w == 24.f) {
            CHECK(!it.litPath);
            foundUnlitTextured = true;
        }
    }
    CHECK(foundLit);
    CHECK(foundUnlitTextured);

    // Sort contract: same canvas/layer → unlit before lit (GPU batches lit path last).
    sortDrawItems2D(scene);
    int firstLit = -1;
    int lastUnlitSameLayer = -1;
    for (int i = 0; i < int(scene.size()); ++i) {
        if (scene[i].layer != 10 || scene[i].canvas != nullptr) continue;
        if (!scene[i].litPath) lastUnlitSameLayer = i;
        if (scene[i].litPath && firstLit < 0) firstLit = i;
    }
    REQUIRE(firstLit >= 0);
    REQUIRE(lastUnlitSameLayer >= 0);
    CHECK(lastUnlitSameLayer < firstLit);

    // Force unlit when receiveLight=false even with a normal map.
    heroSprite->sprite()->receiveLight = false;
    scene.clear();
    RenderSystem::collectSprites(scene);
    for (const auto &it : scene) {
        if (it.texture == &albedoTex && it.normal == &normalTex) {
            CHECK(!it.litPath);
            CHECK(!it.receiveLight);
        }
    }
    heroSprite->sprite()->receiveLight = true;

    // ------------------------------------------------------------------
    // 6) Buff + combat loop (mirrors examples/rpg dungeon survival)
    // ------------------------------------------------------------------
    auto *rpg = RPG::create();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();
    SettlementPipeline::clearPipeline("sim.damage");

    int effectsLoaded = rpg->registerEffectsFromJson(R"([
        {"id":"sim.buff.power","durationPolicy":"duration","duration":6.0,
         "stackPolicy":"refresh",
         "modifiers":[{"attribute":"attack","op":"add","value":8.0}],
         "tags":["buff"]},
        {"id":"sim.dot.burn","durationPolicy":"duration","duration":3.0,"period":1.0,
         "stackPolicy":"stack","maxStacks":3,"tags":["dot","debuff"]},
        {"id":"sim.instant.heal","durationPolicy":"instant",
         "modifiers":[{"attribute":"health","op":"add","value":20.0}]}
    ])");
    CHECK_EQ(effectsLoaded, 3);

    int skillsLoaded = rpg->registerSkillsFromJson(R"([
        {"id":"sim.strike","cooldown":0.2,"castTime":0.0,
         "costs":[],"grantedEffects":[],"tags":["attack"]},
        {"id":"sim.fireball","cooldown":1.0,"castTime":0.0,
         "costs":[{"attribute":"mana","amount":10.0}],
         "grantedEffects":["sim.dot.burn"],"tags":["attack","magic"]},
        {"id":"sim.power","cooldown":2.0,"castTime":0.0,
         "costs":[{"attribute":"stamina","amount":5.0}],
         "grantedEffects":["sim.buff.power"],"tags":["buff"]}
    ])");
    CHECK_EQ(skillsLoaded, 3);

    SettlementPipeline::registerStage(
        "sim.damage", "base", 0, [](SettlementContext &ctx) {
            ctx.set("result", ctx.get("attack") - ctx.get("defense"));
        });
    SettlementPipeline::registerStage("sim.damage", "floor", 10, [](SettlementContext &ctx) {
        if (ctx.get("result") < 1.0) ctx.set("result", 1.0);
    });

    RPGActor *player = rpg->newActor();
    RPGActor *enemy = rpg->newActor();
    setupVitals(player, 100.0, 40.0, 50.0);
    setupVitals(enemy, 60.0, 0.0, 0.0);
    player->setBaseAttribute("attack", 14.0);
    player->setBaseAttribute("defense", 3.0);
    enemy->setBaseAttribute("attack", 8.0);
    enemy->setBaseAttribute("defense", 2.0);
    player->learnSkill("sim.strike");
    player->learnSkill("sim.fireball");
    player->learnSkill("sim.power");

    // Power stance buff raises attack before the opening strike.
    REQUIRE(player->beginCastSkill("sim.power", player));
    rpg->update(0.016f);
    CHECK(player->hasEffect("sim.buff.power"));
    CHECK(approxEqD(player->getFinalAttribute("attack"), 22.0));
    CHECK_EQ(rpg->getCastEventCount(), 1);
    CHECK_EQ(rpg->getCastEventSkillId(0), "sim.power");

    auto applyStrikeDamage = [&](RPGActor *caster, RPGActor *target) {
        SettlementContext ctx;
        ctx.set("attack", caster->getFinalAttribute("attack"));
        ctx.set("defense", target->getFinalAttribute("defense"));
        SettlementPipeline::run("sim.damage", ctx);
        double dmg = ctx.get("result");
        target->modifyBaseAttribute("health", -dmg);
        return dmg;
    };

    // Opening strike with buffed attack: 22 - 2 = 20
    REQUIRE(player->beginCastSkill("sim.strike", enemy));
    rpg->update(0.016f);
    double strikeDmg = applyStrikeDamage(player, enemy);
    CHECK(approxEqD(strikeDmg, 20.0));
    CHECK(approxEqD(enemy->getFinalAttribute("health"), 40.0));

    // Fireball spends mana and applies burn DOT.
    double manaBefore = player->getFinalAttribute("mana");
    REQUIRE(player->beginCastSkill("sim.fireball", enemy));
    rpg->update(0.016f);
    CHECK(enemy->hasEffect("sim.dot.burn"));
    CHECK(player->getFinalAttribute("mana") < manaBefore);

    // Burn ticks: each period deals 5 fixed DOT damage (game rule owned by sim).
    int burnTicks = 0;
    for (int frame = 0; frame < 4; ++frame) {
        rpg->update(1.0f);
        for (int i = 0; i < rpg->getTickEventCount(); ++i) {
            if (rpg->getTickEventEffectId(i) == "sim.dot.burn" &&
                rpg->getTickEventActor(i) == enemy) {
                enemy->modifyBaseAttribute("health", -5.0);
                ++burnTicks;
            }
        }
        // Keep walk animation alive across combat frames.
        animMod->update(0.1f);
    }
    CHECK_GE(burnTicks, 3);
    // 40 - 5*3 = 25 (or lower if an extra tick landed)
    CHECK(enemy->getFinalAttribute("health") <= 25.0 + 1e-9);
    CHECK(enemy->getFinalAttribute("health") > 0.0);

    // Finish with strikes until enemy falls; heal potion mid-fight.
    player->applyEffect("sim.instant.heal");
    CHECK(approxEqD(player->getFinalAttribute("health"), 100.0));

    int safety = 0;
    while (enemy->getFinalAttribute("health") > 0.0 && safety < 20) {
        if (player->canCastSkill("sim.strike")) {
            player->beginCastSkill("sim.strike", enemy);
            rpg->update(0.25f);
            applyStrikeDamage(player, enemy);
        } else {
            rpg->update(0.25f);
        }
        ++safety;
    }
    CHECK(enemy->getFinalAttribute("health") <= 0.0);
    CHECK(player->getFinalAttribute("health") > 0.0);
    CHECK(world.hasBuilding(towerId));
    CHECK_GE(walker->getSheetFrame(), 0);

    // ------------------------------------------------------------------
    // Optional GPU: normal-map lit side brighter (soft-skip without window)
    // ------------------------------------------------------------------
    {
        auto *win = eve::window::Window::create();
        auto *gfx = Graphics::create();
        if (win && gfx) {
            win->setGraphics(gfx);
            eve::window::WindowSettings ws;
            ws.width = 320;
            ws.height = 240;
            ws.centered = true;
            if (win->setWindowSettings(ws)) {
                Canvas *rt = gfx->newCanvas(128, 64);
                REQUIRE(rt != nullptr);

                auto *gpuCam = Camera2D::createCamera();
                gpuCam->data()->canvas = rt;
                gpuCam->data()->active = true;
                gpuCam->data()->x = 64.f;
                gpuCam->data()->y = 32.f;
                gpuCam->data()->zoom = 1.f;
                gpuCam->setAmbient(0.08f, 0.08f, 0.08f);
                gpuCam->data()->r = 0.f;
                gpuCam->data()->g = 0.f;
                gpuCam->data()->b = 0.f;
                gpuCam->data()->a = 1.f;

                Texture *gpuAlbedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
                Texture *gpuNormal = makeBiasedNormal(gfx, 32, 32);
                REQUIRE(gpuAlbedo != nullptr);
                REQUIRE(gpuNormal != nullptr);

                // Hide headless sprites so they do not pollute the offscreen pass.
                heroSprite->sprite()->visible = false;
                unlitProp->sprite()->visible = false;
                torch->setEnabled(false);
                cam->data()->active = false;

                auto *litSp = Renderable2D::create();
                litSp->transform()->x = 32.f;
                litSp->transform()->y = 8.f;
                litSp->sprite()->width = 64.f;
                litSp->sprite()->height = 48.f;
                litSp->sprite()->texture = gpuAlbedo;
                litSp->sprite()->normalTexture = gpuNormal;
                litSp->sprite()->receiveLight = true;
                litSp->sprite()->canvas = rt;
                litSp->sprite()->visible = true;

                auto *sideLight = Light2D::createLight("point");
                sideLight->setCanvas(rt);
                sideLight->setPosition(110.f, 32.f);
                sideLight->setColor(1.f, 1.f, 1.f, 3.0f);
                sideLight->setRadius(90.f);
                sideLight->setEnabled(true);

                RenderSystem::render(*gfx);
                float leftL = luma(rt->getPixel(40, 32));
                float rightL = luma(rt->getPixel(88, 32));
                // Software ICDs (Lavapipe) may not shade lit2d; skip when both samples are black.
                if (leftL + rightL > 1e-4f) {
                    CHECK(rightL > leftL + 0.05f);
                }

                litSp->sprite()->visible = false;
                sideLight->setEnabled(false);
                gpuCam->data()->active = false;
                win->close();
            }
        }
    }

    // Cleanup registries / actors (process-global).
    player->release();
    enemy->release();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();
    SettlementPipeline::clearPipeline("sim.damage");
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    layer->setVisible(false);
    heroSprite->sprite()->visible = false;
    unlitProp->sprite()->visible = false;
    torch->setEnabled(false);
    cam->data()->active = false;
}
