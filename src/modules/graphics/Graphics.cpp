#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::graphics {

Module_IMPL(Graphics, new vulkan::Graphics());

void Graphics::render3D() { RenderSystem3D::render(*this); }

void Graphics::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    RenderSystem3D::setDirectionalLight(dx, dy, dz, r, g, b);
}

void Graphics::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Graphics::create, false);
    expose(cls);

    table.addClass<Mesh>("Mesh", std::function<Mesh *()>([]() -> Mesh * { return nullptr; }), true);
    table.addClass<Texture>("Texture", std::function<Texture *()>([]() -> Texture * { return nullptr; }),
                            true);

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

    auto cam = table.addClass<Camera3D>(
        "Camera3D", std::function<Camera3D *()>([]() { return Camera3D::createCamera(); }), true);
    cam.addFunc("setEye", &Camera3D::setEye);
    cam.addFunc("setTarget", &Camera3D::setTarget);
    cam.addFunc("setUp", &Camera3D::setUp);
    cam.addFunc("setFov", &Camera3D::setFov);
    cam.addFunc("setActive", &Camera3D::setActive);

    auto ent = table.addClass<Renderable3D>(
        "Renderable3D", std::function<Renderable3D *()>([]() { return Renderable3D::create(); }), true);
    ent.addFunc("setPosition", &Renderable3D::setPosition);
    ent.addFunc("setRotation", &Renderable3D::setRotation);
    ent.addFunc("setYaw", &Renderable3D::setYaw);
    ent.addFunc("getYaw", &Renderable3D::getYaw);
    ent.addFunc("setScale", &Renderable3D::setScale);
    ent.addFunc("setMesh", &Renderable3D::setMesh);
    ent.addFunc("setTexture", &Renderable3D::setTexture);
    ent.addFunc("setShader", &Renderable3D::setShader);
    ent.addFunc("setTint", &Renderable3D::setTint);
    ent.addFunc("setVisible", &Renderable3D::setVisible);
    ent.addFunc("setCamera", &Renderable3D::setCamera);
}

void Graphics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Graphics::getName);
    cls.addFunc("reset", &Graphics::reset);
    cls.addFunc("present", &Graphics::present);
    cls.addFunc("clear", &Graphics::clearScreen);
    cls.addFunc("setBackgroundColor", &Graphics::setBackgroundColorRGBA);
    cls.addFunc("drawSolidRect", &Graphics::drawSolidRectRGBA);
    cls.addFunc("newMeshSphere", &Graphics::newMeshSphere);
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
}

void Graphics::reset() { currentShader = nullptr; }

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

}  // namespace eve::graphics
