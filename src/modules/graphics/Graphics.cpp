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
    cls.addFunc("render3D", &Graphics::render3D);
    cls.addFunc("setDirectionalLight", &Graphics::setDirectionalLight);
}

void Graphics::reset() {}

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
