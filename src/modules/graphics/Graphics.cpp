#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderSystem.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/Quad.h"
#include "graphics/Font.h"
#include "font/FontData.h"
#include "image/ImageData.h"
#include "common/Exception.h"
#include "common/RenderTrace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>

namespace eve::graphics {

Module_IMPL(Graphics, new vulkan::Graphics());

void Graphics::render3D() {
    eve::debug::rtFrameBegin();
    RenderSystem3D::render(*this);
    eve::debug::rtFrameEnd();
}

void Graphics::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    RenderSystem3D::setDirectionalLight(dx, dy, dz, r, g, b);
}

void Graphics::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Graphics::create, false);
    expose(cls);

    auto meshCls =
        table.addClass<Mesh>("Mesh", std::function<Mesh *()>([]() -> Mesh * { return nullptr; }), true);
    meshCls.addFunc("getVertexCount", &Mesh::getVertexCount);
    meshCls.addFunc("getMorphCount", &Mesh::getMorphCount);
    meshCls.addFunc("getMorphName", &Mesh::getMorphName);
    meshCls.addFunc("hasMorph", &Mesh::hasMorph);
    meshCls.addFunc("setMorphWeight", &Mesh::setMorphWeight);
    meshCls.addFunc("getMorphWeight", &Mesh::getMorphWeight);
    meshCls.addFunc("clearMorphWeights", &Mesh::clearMorphWeights);
    meshCls.addFunc("hasMorphData", &Mesh::hasMorphData);
    meshCls.addFunc("isMorphDirty", &Mesh::isMorphDirty);
    table.addClass<Texture>("Texture", std::function<Texture *()>([]() -> Texture * { return nullptr; }),
                            true);

    auto quad = table.addClass<Quad>(
        "Quad", std::function<Quad *()>([]() -> Quad * { return nullptr; }), true);
    quad.addFunc("setViewport", &Quad::setViewport);
    quad.addFunc("getX", &Quad::getX);
    quad.addFunc("getY", &Quad::getY);
    quad.addFunc("getWidth", &Quad::getWidth);
    quad.addFunc("getHeight", &Quad::getHeight);

    auto shader = table.addClass<Shader>(
        "Shader", std::function<Shader *()>([]() -> Shader * { return nullptr; }), true);
    shader.addFunc("declareFloat", &Shader::declareFloat);
    shader.addFunc("declareVec2", &Shader::declareVec2);
    shader.addFunc("declareVec3", &Shader::declareVec3);
    shader.addFunc("declareVec4", &Shader::declareVec4);
    shader.addFunc("declareMatrix", &Shader::declareMatrix);
    shader.addFunc("sendFloat", &Shader::sendFloat);
    shader.addFunc("sendVec2", &Shader::sendVec2);
    shader.addFunc("sendVec3", &Shader::sendVec3);
    shader.addFunc("sendVec4", &Shader::sendVec4);
    shader.addFunc("hasUniform", &Shader::hasUniform);
    shader.addFunc("getUniformIndex", &Shader::getUniformIndex);

    auto cam2d = table.addClass<Camera2D>(
        "Camera2D", std::function<Camera2D *()>([]() { return Camera2D::createCamera(); }), true);
    cam2d.addFunc("setAmbient", &Camera2D::setAmbient);
    cam2d.addFunc("setPosition", &Camera2D::setPosition);
    cam2d.addFunc("getX", &Camera2D::getX);
    cam2d.addFunc("getY", &Camera2D::getY);
    cam2d.addFunc("setZoom", &Camera2D::setZoom);
    cam2d.addFunc("getZoom", &Camera2D::getZoom);
    cam2d.addFunc("screenToWorldX", &Camera2D::screenToWorldX);
    cam2d.addFunc("screenToWorldY", &Camera2D::screenToWorldY);
    cam2d.addFunc("worldToScreenX", &Camera2D::worldToScreenX);
    cam2d.addFunc("worldToScreenY", &Camera2D::worldToScreenY);

    auto light = table.addClass<Light2D>(
        "Light2D", std::function<Light2D *()>([]() { return Light2D::createLight("point"); }), true);
    light.addFunc("setType", &Light2D::setType);
    light.addFunc("getType", &Light2D::getType);
    light.addFunc("setPosition", &Light2D::setPosition);
    light.addFunc("getX", &Light2D::getX);
    light.addFunc("getY", &Light2D::getY);
    light.addFunc("setDirection", &Light2D::setDirection);
    light.addFunc("getDirX", &Light2D::getDirX);
    light.addFunc("getDirY", &Light2D::getDirY);
    light.addFunc("setColor", &Light2D::setColor);
    light.addFunc("setRadius", &Light2D::setRadius);
    light.addFunc("getRadius", &Light2D::getRadius);
    light.addFunc("setEnabled", &Light2D::setEnabled);
    light.addFunc("isEnabled", &Light2D::isEnabled);
    light.addFunc("setCanvas", &Light2D::setCanvas);

    auto cam = table.addClass<Camera3D>(
        "Camera3D", std::function<Camera3D *()>([]() { return Camera3D::createCamera(); }), true);
    cam.addFunc("setEye", &Camera3D::setEye);
    cam.addFunc("setTarget", &Camera3D::setTarget);
    cam.addFunc("setUp", &Camera3D::setUp);
    cam.addFunc("setFov", &Camera3D::setFov);
    cam.addFunc("setActive", &Camera3D::setActive);
    cam.addFunc("setAmbient", &Camera3D::setAmbient);
    cam.addFunc("setEnvMap", &Camera3D::setEnvMap);
    cam.addFunc("setEnvIntensity", &Camera3D::setEnvIntensity);
    cam.addFunc("screenToRay", &Camera3D::screenToRay);
    cam.addFunc("getScreenRayOriginX", &Camera3D::getScreenRayOriginX);
    cam.addFunc("getScreenRayOriginY", &Camera3D::getScreenRayOriginY);
    cam.addFunc("getScreenRayOriginZ", &Camera3D::getScreenRayOriginZ);
    cam.addFunc("getScreenRayDirX", &Camera3D::getScreenRayDirX);
    cam.addFunc("getScreenRayDirY", &Camera3D::getScreenRayDirY);
    cam.addFunc("getScreenRayDirZ", &Camera3D::getScreenRayDirZ);

    auto light3d = table.addClass<Light3D>(
        "Light3D", std::function<Light3D *()>([]() { return Light3D::createLight("point"); }), true);
    light3d.addFunc("setType", &Light3D::setType);
    light3d.addFunc("getType", &Light3D::getType);
    light3d.addFunc("setPosition", &Light3D::setPosition);
    light3d.addFunc("getX", &Light3D::getX);
    light3d.addFunc("getY", &Light3D::getY);
    light3d.addFunc("getZ", &Light3D::getZ);
    light3d.addFunc("setDirection", &Light3D::setDirection);
    light3d.addFunc("getDirX", &Light3D::getDirX);
    light3d.addFunc("getDirY", &Light3D::getDirY);
    light3d.addFunc("getDirZ", &Light3D::getDirZ);
    light3d.addFunc("setColor", &Light3D::setColor);
    light3d.addFunc("setRadius", &Light3D::setRadius);
    light3d.addFunc("getRadius", &Light3D::getRadius);
    light3d.addFunc("setEnabled", &Light3D::setEnabled);
    light3d.addFunc("isEnabled", &Light3D::isEnabled);
    light3d.addFunc("setCastShadow", &Light3D::setCastShadow);
    light3d.addFunc("getCastShadow", &Light3D::getCastShadow);
    light3d.addFunc("setShadowBias", &Light3D::setShadowBias);
    light3d.addFunc("getShadowBias", &Light3D::getShadowBias);
    light3d.addFunc("setShadowStrength", &Light3D::setShadowStrength);
    light3d.addFunc("getShadowStrength", &Light3D::getShadowStrength);

    auto ent = table.addClass<Renderable3D>(
        "Renderable3D", std::function<Renderable3D *()>([]() { return Renderable3D::create(); }), true);
    ent.addFunc("setPosition", &Renderable3D::setPosition);
    ent.addFunc("setRotation", &Renderable3D::setRotation);
    ent.addFunc("setYaw", &Renderable3D::setYaw);
    ent.addFunc("getYaw", &Renderable3D::getYaw);
    ent.addFunc("setScale", &Renderable3D::setScale);
    ent.addFunc("setMesh", &Renderable3D::setMesh);
    ent.addFunc("setTexture", &Renderable3D::setTexture);
    ent.addFunc("setNormalTexture", &Renderable3D::setNormalTexture);
    ent.addFunc("setShader", &Renderable3D::setShader);
    ent.addFunc("setTint", &Renderable3D::setTint);
    ent.addFunc("setMetallic", &Renderable3D::setMetallic);
    ent.addFunc("setRoughness", &Renderable3D::setRoughness);
    ent.addFunc("setVisible", &Renderable3D::setVisible);
    ent.addFunc("setReceiveLight", &Renderable3D::setReceiveLight);
    ent.addFunc("setCastShadow", &Renderable3D::setCastShadow);
    ent.addFunc("setReceiveShadow", &Renderable3D::setReceiveShadow);
    ent.addFunc("setCamera", &Renderable3D::setCamera);
}

void Graphics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Graphics::getName);
    cls.addFunc("reset", &Graphics::reset);
    cls.addFunc("present", &Graphics::present);
    cls.addFunc("clear", &Graphics::clearScreen);
    cls.addFunc("setBackgroundColor", &Graphics::setBackgroundColorRGBA);
    cls.addFunc("drawSolidRect", &Graphics::drawSolidRectRGBA);
    cls.addFunc("drawTexturedRect", &Graphics::drawTexturedRectRGBA);
    cls.addFunc("newTexture", &Graphics::newTextureFromImageData);
    cls.addFunc("newMeshSphere", &Graphics::newMeshSphere);
    cls.addFunc("newMeshCylinder", &Graphics::newMeshCylinder);
    cls.addFunc("bakeMeshMorph", &Graphics::bakeMeshMorph);
    cls.addFunc("newShader",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newShader));
    cls.addFunc("newMeshShader",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newMeshShader));
    cls.addFunc("newShaderFromSpvFile",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newShaderFromSpvFile));
    cls.addFunc("setShader", static_cast<void (Graphics::*)(Shader *)>(&Graphics::setShader));
    cls.addFunc("getShader", &Graphics::getShader);
    cls.addFunc("render3D", &Graphics::render3D);
    cls.addFunc("setDirectionalLight", &Graphics::setDirectionalLight);
    cls.addFunc("newQuad", &Graphics::newQuad);
}

void Graphics::reset() {
    currentShader = nullptr;
    currentFont   = nullptr;
}

void Graphics::setShader(Shader *shader) { currentShader = shader; }

void Graphics::setShader() { currentShader = nullptr; }

void Graphics::clearScreen() {
    clear(std::nullopt, std::nullopt, std::nullopt);
}

void Graphics::setBackgroundColorRGBA(float r, float g, float b, float a) {
    setBackgroundColor(Color(r, g, b, a));
}

void Graphics::drawSolidRectRGBA(float x, float y, float w, float h, float r, float g, float b,
                                 float a) {
    drawSolidRect(x, y, w, h, Color(r, g, b, a));
}

void Graphics::drawTexturedRectRGBA(Texture *texture, float x, float y, float w, float h, float r,
                                    float g, float b, float a) {
    drawTexturedRect(texture, x, y, w, h, Color(r, g, b, a));
}

Texture *Graphics::newTextureFromImageData(image::ImageData *data, bool repeatU, bool repeatV) {
    if (!data) throw eve::Exception("newTextureFromImageData: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw eve::Exception("newTextureFromImageData: only RGBA8 supported");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), repeatU, repeatV);
}

Quad *Graphics::newQuad(int x, int y, int w, int h) { return new Quad(x, y, w, h); }

Font *Graphics::newFont(font::FontData *data, std::string charset) {
    return new Font(this, data, std::move(charset));
}

void Graphics::print(const std::string &text, float x, float y, const Color &color, float scale) {
    if (currentFont == nullptr) {
        eve::debug::rtDraw("print", "no-font");
        throw eve::Exception("Graphics::print: no font set (call setFont first)");
    }
    eve::debug::rtBind("font", "current");
    eve::debug::rtDraw("print", text.empty() ? "" : "text");

    font::FontData *data     = currentFont->getData();
    float            penX     = x;
    float            baseline = y + currentFont->getBaseline() * scale;
    int              prevCodepoint = -1;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = nextCodepointUtf8(text, i);
        if (cp == 0) continue;
        int code = static_cast<int>(cp);

        if (prevCodepoint >= 0) penX += data->getKerning(prevCodepoint, code) * scale;

        if (const Font::Glyph *g = currentFont->findGlyph(code)) {
            if (g->width > 0 && g->height > 0) {
                float gx = penX + static_cast<float>(g->bearingX) * scale;
                float gy = baseline - static_cast<float>(g->bearingY) * scale;
                drawTexturedRectUV(currentFont->getTexture(), gx, gy, static_cast<float>(g->width) * scale,
                                   static_cast<float>(g->height) * scale, g->u0, g->v0, g->u1, g->v1, color);
            }
            penX += static_cast<float>(g->advance) * scale;
        } else {
            // Not pre-rasterized into this Font's atlas — still advance the pen.
            penX += static_cast<float>(data->getGlyphAdvance(code)) * scale;
        }

        prevCodepoint = code;
    }
}

}  // namespace eve::graphics
