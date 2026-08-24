#include "graphics/Graphics.h"
#include "common/Capability.h"
#include "common/config.h"
#include "graphics/GraphicsCapabilities.h"
#include "graphics/Grass.h"
#include "graphics/HairShader.h"

#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#else
#include "graphics/vulkan/Graphics.h"
#endif
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Font.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"

#ifndef EVENGINE_WEBGPU
#include "font/FontData.h"
#endif
#include "common/Exception.h"
#include "common/RenderTrace.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"


#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

namespace eve::graphics {

namespace {

bool copyArrayFloats(ssq::Array arr, std::vector<float> &out) {
    const size_t n = arr.size();
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = arr.get<float>(i);
    return n > 0;
}

bool copyArrayUints(ssq::Array arr, std::vector<uint32_t> &out) {
    const size_t n = arr.size();
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint32_t>(arr.get<int>(i));
    return n > 0;
}

Mesh *newMeshFromArraysScript(Graphics *gfx, ssq::Array posArr, ssq::Array nrmArr,
                              ssq::Array uvArr, int vertexCount, ssq::Array idxArr,
                              int indexCount) {
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    copyArrayFloats(posArr, pos);
    copyArrayFloats(nrmArr, nrm);
    copyArrayFloats(uvArr, uv);
    copyArrayUints(idxArr, idx);
    return gfx->newMeshFromArrays(pos.data(), nrm.empty() ? nullptr : nrm.data(),
                                  uv.empty() ? nullptr : uv.data(), vertexCount,
                                  idx.empty() ? nullptr : idx.data(), indexCount);
}

bool updateMeshVerticesScript(Graphics *gfx, Mesh *mesh, ssq::Array posArr, ssq::Array nrmArr,
                              ssq::Array uvArr, int vertexCount, ssq::Array idxArr,
                              int indexCount) {
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    copyArrayFloats(posArr, pos);
    copyArrayFloats(nrmArr, nrm);
    copyArrayFloats(uvArr, uv);
    copyArrayUints(idxArr, idx);
    return gfx->updateMeshVertices(mesh, pos.data(), nrm.empty() ? nullptr : nrm.data(),
                                   uv.empty() ? nullptr : uv.data(), vertexCount,
                                   idx.empty() ? nullptr : idx.data(), indexCount);
}

void setMesh3DViewProjScript(Graphics *gfx, ssq::Array a) {
    if (a.size() != 16) throw eve::Exception("setMesh3DViewProj: expected 16 floats");
    glm::mat4 m(1.f);
    for (size_t i = 0; i < 16; ++i) glm::value_ptr(m)[i] = a.get<float>(i);
    gfx->setMesh3DViewProj(m);
}

void setMesh3DViewScript(Graphics *gfx, ssq::Array a) {
    if (a.size() != 16) throw eve::Exception("setMesh3DView: expected 16 floats");
    glm::mat4 m(1.f);
    for (size_t i = 0; i < 16; ++i) glm::value_ptr(m)[i] = a.get<float>(i);
    gfx->setMesh3DView(m);
}

void setMesh3DCameraPosScript(Graphics *gfx, float x, float y, float z) {
    gfx->setMesh3DCameraPos(glm::vec3(x, y, z));
}

void setCanvasScript(Graphics *gfx, ssq::Object obj) {
    if (obj.isNull()) {
        gfx->setCanvas(nullptr);
        return;
    }
    gfx->setCanvas(obj.toPtrUnsafe<Canvas *>());
}

}  // namespace

Graphics::Graphics() {
    // The window module owns the native window; we own the render surface.
    // Register as its surface host so window never has to include graphics.
    // The query happens after native window creation, so this pointer is valid
    // by the time it is used; see common/WindowSurfaceHost.h.
    eve::cap::provide<IWindowSurfaceHost>(this);
    registerGraphicsCapabilities();
}

Graphics::~Graphics() {
    // Out-of-line so unique_ptr members of effect classes (Outline, AO, GI, …)
    // are destroyed where the complete types are visible.
}

#ifdef EVENGINE_WEBGPU
Module_IMPL(Graphics, new eve::graphics::webgpu::Graphics());
#else
Module_IMPL(Graphics, new vulkan::Graphics());
#endif

void Graphics::render3D() {
    pushValidationScope();
    recordingEngine3D_ = true;
    eve::debug::rtFrameBegin();
    RenderSystem3D::render(*this);
    eve::debug::rtFrameEnd();
    recordingEngine3D_ = false;
    popValidationScope();
}

void Graphics::renderScene3DToCanvas(Canvas *canvas, Camera3D *camera) {
    pushValidationScope();
    RenderSystem3D::renderToCanvas(*this, canvas, camera);
    popValidationScope();
}

Shader* Graphics::prepareSceneColorResolveShader(Texture* scene) {
    if (!scene) return nullptr;
    AntiAliasing* aa = pipelineAntiAliasing();
    aa->setMode("fxaa");
    const bool doAA = !renderControl_ || renderControl_->isEnabled("aa");
    if (doAA) {
        aa->setQuality("medium");
    } else {
        aa->setFloat("edgeThreshold", 1.f);
        aa->setFloat("edgeThresholdMin", 1.f);
        aa->setFloat("subpix", 0.f);
    }
    aa->prepareSource(scene);
    return aa->getShader();
}

void Graphics::drawScene3DRGBA(float x, float y, float w, float h, float r, float g, float b, float a) {
    Texture* scene = getSceneColorTexture();
    if (!scene) return;
    // Scene color A is linear view-depth, not opacity. The default textured
    // blit multiplies that into SrcAlpha, so the planet composites against
    // the dark clear color and looks dim. Use the same opaque FXAA resolve
    // as the engine auto-composite path (aa shader writes alpha = 1).
    if (Shader* sh = prepareSceneColorResolveShader(scene))
        drawTexturedRectShader(scene, sh, x, y, w, h, Color(r, g, b, a));
    else
        drawTexturedRect(scene, x, y, w, h, Color(r, g, b, a));
}

void Graphics::drawCanvasRGBA(Canvas* canvas, float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!canvas) return;
    Texture* tex = canvas->getTexture();
    if (!tex) return;
    drawTexturedRect(tex, x, y, w, h, Color(r, g, b, a));
}

void Graphics::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    RenderSystem3D::setDirectionalLight(dx, dy, dz, r, g, b);
}

Material* Graphics::newMaterial() { return new Material(); }

RenderControl* Graphics::getRenderControl() {
    if (!renderControl_) {
        renderControl_ = std::make_unique<RenderControl>();
        renderControl_->attach(this);
        renderControl_->compile();
    }
    return renderControl_.get();
}

void Graphics::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Graphics::create, false);
    expose(cls);

    auto canvasCls =
        table.addClass<Canvas>("Canvas", std::function<Canvas*()>([]() -> Canvas* { return nullptr; }), true);
    canvasCls.addFunc("getWidth", &Canvas::getWidth);
    canvasCls.addFunc("getHeight", &Canvas::getHeight);
    canvasCls.addFunc("getTexture", &Canvas::getTexture);

    auto texCls =
        table.addClass<Texture>("Texture", std::function<Texture*()>([]() -> Texture* { return nullptr; }), true);
    texCls.addFunc("setCastOcclusion", &Texture::setCastOcclusion);
    texCls.addFunc("getCastOcclusion", &Texture::getCastOcclusion);
    texCls.addFunc("getWidth", &Texture::getWidth);
    texCls.addFunc("getHeight", &Texture::getHeight);
    texCls.addFunc("getMipmapCount", &Texture::getMipmapCount);
    texCls.addFunc("setAlphaConvention", &Texture::setAlphaConvention);
    texCls.addFunc("getAlphaConvention", &Texture::getAlphaConvention);

    // Texture / Mesh expose occlusion flags used by volumetric light shafts.
    // (create returns null — instances come from Graphics::newTexture / newMesh*)

    auto meshCls = table.addClass<Mesh>("Mesh", std::function<Mesh*()>([]() -> Mesh* { return nullptr; }), true);
    meshCls.addFunc("getVertexCount", &Mesh::getVertexCount);
    meshCls.addFunc("getIndexCount",
                    std::function<int(Mesh *)>([](Mesh *mesh) { return mesh->indexCount; }));
    meshCls.addFunc("getMorphCount", &Mesh::getMorphCount);
    meshCls.addFunc("getMorphName", &Mesh::getMorphName);
    meshCls.addFunc("hasMorph", &Mesh::hasMorph);
    meshCls.addFunc("setMorphWeight", &Mesh::setMorphWeight);
    meshCls.addFunc("getMorphWeight", &Mesh::getMorphWeight);
    meshCls.addFunc("clearMorphWeights", &Mesh::clearMorphWeights);
    meshCls.addFunc("hasMorphData", &Mesh::hasMorphData);
    meshCls.addFunc("isMorphDirty", &Mesh::isMorphDirty);
    meshCls.addFunc("setCastOcclusion", &Mesh::setCastOcclusion);
    meshCls.addFunc("getCastOcclusion", &Mesh::getCastOcclusion);

    auto quad = table.addClass<Quad>("Quad", std::function<Quad*()>([]() -> Quad* { return nullptr; }), true);
    quad.addFunc("setViewport", &Quad::setViewport);
    quad.addFunc("getX", &Quad::getX);
    quad.addFunc("getY", &Quad::getY);
    quad.addFunc("getWidth", &Quad::getWidth);
    quad.addFunc("getHeight", &Quad::getHeight);

    auto shader = table.addClass<Shader>("Shader", std::function<Shader*()>([]() -> Shader* { return nullptr; }), true);
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

    // ECS entities live in the registry buffer (not standalone new/delete).
    // Squirrel must not delete them on VM shutdown or close asserts
    // _CrtIsValidHeapPointer.
    auto cam2d = table.addClass<Camera2D>("Camera2D",
                                          std::function<Camera2D*()>([]() { return Camera2D::createCamera(); }), false);
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
        "Light2D", std::function<Light2D*()>([]() { return Light2D::createLight("point"); }), false);
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
    light.addFunc("setVolumetric", &Light2D::setVolumetric);
    light.addFunc("getVolumetric", &Light2D::getVolumetric);
    light.addFunc("setVolumetricIntensity", &Light2D::setVolumetricIntensity);
    light.addFunc("getVolumetricIntensity", &Light2D::getVolumetricIntensity);
    light.addFunc("setCanvas", &Light2D::setCanvas);

    auto cam = table.addClass<Camera3D>("Camera3D",
                                        std::function<Camera3D*()>([]() { return Camera3D::createCamera(); }), false);
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
        "Light3D", std::function<Light3D*()>([]() { return Light3D::createLight("point"); }), false);
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
    light3d.addFunc("setVolumetric", &Light3D::setVolumetric);
    light3d.addFunc("getVolumetric", &Light3D::getVolumetric);
    light3d.addFunc("setVolumetricIntensity", &Light3D::setVolumetricIntensity);
    light3d.addFunc("getVolumetricIntensity", &Light3D::getVolumetricIntensity);

    auto ent = table.addClass<Renderable3D>(
        "Renderable3D", std::function<Renderable3D*()>([]() { return Renderable3D::create(); }), false);
    ent.addFunc("setPosition", &Renderable3D::setPosition);
    ent.addFunc("setRotation", &Renderable3D::setRotation);
    ent.addFunc("setYaw", &Renderable3D::setYaw);
    ent.addFunc("getYaw", &Renderable3D::getYaw);
    ent.addFunc("setScale", &Renderable3D::setScale);
    ent.addFunc("setMesh", &Renderable3D::setMesh);
    ent.addFunc("getMesh", &Renderable3D::getMesh);
    ent.addFunc("setTexture", &Renderable3D::setTexture);
    ent.addFunc("setNormalTexture", &Renderable3D::setNormalTexture);
    ent.addFunc("setHeightTexture", &Renderable3D::setHeightTexture);
    ent.addFunc("setShader", &Renderable3D::setShader);
    ent.addFunc("setMaterial", &Renderable3D::setMaterial);
    ent.addFunc("getMaterial", &Renderable3D::getMaterial);
    ent.addFunc("setPart", &Renderable3D::setPart);
    ent.addFunc("clearParts", &Renderable3D::clearParts);
    ent.addFunc("getPartCount", &Renderable3D::getPartCount);
    ent.addFunc("getPartName", &Renderable3D::getPartName);
    ent.addFunc("getPartMesh", &Renderable3D::getPartMesh);
    ent.addFunc("getPartMaterial", &Renderable3D::getPartMaterial);
    ent.addFunc("setHair", &Renderable3D::setHair);
    ent.addFunc("getHair", &Renderable3D::getHair);
    ent.addFunc("setTint", &Renderable3D::setTint);
    ent.addFunc("setMetallic", &Renderable3D::setMetallic);
    ent.addFunc("setRoughness", &Renderable3D::setRoughness);
    ent.addFunc("setTexCellBomb", &Renderable3D::setTexCellBomb);
    ent.addFunc("getTexCellBombScale", &Renderable3D::getTexCellBombScale);
    ent.addFunc("getTexCellBombStrength", &Renderable3D::getTexCellBombStrength);
    ent.addFunc("getTexCellBombRotation", &Renderable3D::getTexCellBombRotation);
    ent.addFunc("setParallax", &Renderable3D::setParallax);
    ent.addFunc("getParallaxScale", &Renderable3D::getParallaxScale);
    ent.addFunc("getParallaxMinLayers", &Renderable3D::getParallaxMinLayers);
    ent.addFunc("getParallaxMaxLayers", &Renderable3D::getParallaxMaxLayers);
    ent.addFunc("setVisible", &Renderable3D::setVisible);
    ent.addFunc("setReceiveLight", &Renderable3D::setReceiveLight);
    ent.addFunc("setCastShadow", &Renderable3D::setCastShadow);
    ent.addFunc("setReceiveShadow", &Renderable3D::setReceiveShadow);
    ent.addFunc("setCastOcclusion", &Renderable3D::setCastOcclusion);
    ent.addFunc("getCastOcclusion", &Renderable3D::getCastOcclusion);
    ent.addFunc("setCamera", &Renderable3D::setCamera);
    ent.addFunc("setMeshLod", &Renderable3D::setMeshLod);
    ent.addFunc("clearMeshLod", &Renderable3D::clearMeshLod);
    ent.addFunc("getMeshLodCount", &Renderable3D::getMeshLodCount);
    ent.addFunc("getMeshLodLevelAtDistance", &Renderable3D::getMeshLodLevelAtDistance);

    auto sprite2d = table.addClass<Renderable2D>(
        "Sprite2D", std::function<Renderable2D *()>([]() { return Renderable2D::create(); }), false);
    sprite2d.addFunc("setPosition", &Renderable2D::setPosition);
    sprite2d.addFunc("getX", &Renderable2D::getX);
    sprite2d.addFunc("getY", &Renderable2D::getY);
    sprite2d.addFunc("setRotation", &Renderable2D::setRotation);
    sprite2d.addFunc("getRotation", &Renderable2D::getRotation);
    sprite2d.addFunc("setScale", &Renderable2D::setScale);
    sprite2d.addFunc("getScaleX", &Renderable2D::getScaleX);
    sprite2d.addFunc("getScaleY", &Renderable2D::getScaleY);
    sprite2d.addFunc("setSize", &Renderable2D::setSize);
    sprite2d.addFunc("getWidth", &Renderable2D::getWidth);
    sprite2d.addFunc("getHeight", &Renderable2D::getHeight);
    sprite2d.addFunc("setTexture", &Renderable2D::setTexture);
    sprite2d.addFunc("getTexture", &Renderable2D::getTexture);
    sprite2d.addFunc("setQuad", &Renderable2D::setQuad);
    sprite2d.addFunc("getQuad", &Renderable2D::getQuad);
    sprite2d.addFunc("setColor", &Renderable2D::setColor);
    sprite2d.addFunc("setLayer", &Renderable2D::setLayer);
    sprite2d.addFunc("getLayer", &Renderable2D::getLayer);
    sprite2d.addFunc("setVisible", &Renderable2D::setVisible);
    sprite2d.addFunc("getVisible", &Renderable2D::getVisible);
    sprite2d.addFunc("setReceiveLight", &Renderable2D::setReceiveLight);
    sprite2d.addFunc("getReceiveLight", &Renderable2D::getReceiveLight);
    sprite2d.addFunc("setBlend", &Renderable2D::setBlend);
    sprite2d.addFunc("getBlend", &Renderable2D::getBlend);
    sprite2d.addFunc("setAnchor", &Renderable2D::setAnchor);
    sprite2d.addFunc("getAnchorX", &Renderable2D::getAnchorX);
    sprite2d.addFunc("getAnchorY", &Renderable2D::getAnchorY);
    sprite2d.addFunc("setFlip", &Renderable2D::setFlip);
    sprite2d.addFunc("getFlipX", &Renderable2D::getFlipX);
    sprite2d.addFunc("getFlipY", &Renderable2D::getFlipY);
    sprite2d.addFunc("setFrameLayout", &Renderable2D::setFrameLayout);
    sprite2d.addFunc("setCastOcclusion", &Renderable2D::setCastOcclusion);
    sprite2d.addFunc("getCastOcclusion", &Renderable2D::getCastOcclusion);
    sprite2d.addFunc("destroy", [](Renderable2D *self) { self->release(); });

    auto material =
        table.addClass<Material>("Material", std::function<Material*()>([]() -> Material* { return nullptr; }), true);
    material.addFunc("setShadingModel", &Material::setShadingModel);
    material.addFunc("getShadingModel", &Material::getShadingModel);
    material.addFunc("setAlbedoTexture", &Material::setAlbedoTexture);
    material.addFunc("getAlbedoTexture", &Material::getAlbedoTexture);
    material.addFunc("setNormalTexture", &Material::setNormalTexture);
    material.addFunc("getNormalTexture", &Material::getNormalTexture);
    material.addFunc("setHeightTexture", &Material::setHeightTexture);
    material.addFunc("getHeightTexture", &Material::getHeightTexture);
    material.addFunc("setShader", &Material::setShader);
    material.addFunc("getShader", &Material::getShader);
    material.addFunc("setTint", &Material::setTint);
    material.addFunc("setMetallic", &Material::setMetallic);
    material.addFunc("getMetallic", &Material::getMetallic);
    material.addFunc("setRoughness", &Material::setRoughness);
    material.addFunc("getRoughness", &Material::getRoughness);
    material.addFunc("setTexCellBomb", &Material::setTexCellBomb);
    material.addFunc("setParallax", &Material::setParallax);
    material.addFunc("setReceiveLight", &Material::setReceiveLight);
    material.addFunc("getReceiveLight", &Material::getReceiveLight);
    material.addFunc("setCastShadow", &Material::setCastShadow);
    material.addFunc("getCastShadow", &Material::getCastShadow);
    material.addFunc("setReceiveShadow", &Material::setReceiveShadow);
    material.addFunc("getReceiveShadow", &Material::getReceiveShadow);
    material.addFunc("setCastOcclusion", &Material::setCastOcclusion);
    material.addFunc("getCastOcclusion", &Material::getCastOcclusion);
    material.addFunc("setHair", &Material::setHair);
    material.addFunc("getHair", &Material::getHair);
    material.addFunc("setSurfaceMode", &Material::setSurfaceMode);
    material.addFunc("getSurfaceMode", &Material::getSurfaceMode);
    material.addFunc("setAlphaCutoff", &Material::setAlphaCutoff);
    material.addFunc("getAlphaCutoff", &Material::getAlphaCutoff);
    material.addFunc("setBlendMode", &Material::setBlendMode);
    material.addFunc("getBlendMode", &Material::getBlendMode);
    material.addFunc("setDepthWrite", &Material::setDepthWrite);
    material.addFunc("getDepthWrite", &Material::getDepthWrite);
    material.addFunc("setDoubleSided", &Material::setDoubleSided);
    material.addFunc("getDoubleSided", &Material::getDoubleSided);
    material.addFunc("setSortPriority", &Material::setSortPriority);
    material.addFunc("getSortPriority", &Material::getSortPriority);
    material.addFunc("setAlphaTechnique", &Material::setAlphaTechnique);
    material.addFunc("getAlphaTechnique", &Material::getAlphaTechnique);
    material.addFunc("hasParam", &Material::hasParam);
    material.addFunc("setFloat", &Material::setFloat);
    material.addFunc("getFloat", &Material::getFloat);

    auto gbuffer =
        table.addClass<GBuffer>("GBuffer", std::function<GBuffer*()>([]() -> GBuffer* { return nullptr; }), true);
    gbuffer.addFunc("isValid", &GBuffer::isValid);
    gbuffer.addFunc("getWidth", &GBuffer::getWidth);
    gbuffer.addFunc("getHeight", &GBuffer::getHeight);
    gbuffer.addFunc("getDepthTexture", &GBuffer::getDepthTexture);
    gbuffer.addFunc("getHwDepthTexture", &GBuffer::getHwDepthTexture);
    gbuffer.addFunc("getNormalTexture", &GBuffer::getNormalTexture);
    gbuffer.addFunc("getAlbedoTexture", &GBuffer::getAlbedoTexture);
    gbuffer.addFunc("hasBuffer", &GBuffer::hasBuffer);
    gbuffer.addFunc("getBuffer", &GBuffer::getBuffer);

    auto rctrl = table.addClass<RenderControl>(
        "RenderControl", std::function<RenderControl*()>([]() -> RenderControl* { return nullptr; }), true);
    rctrl.addFunc("supports", &RenderControl::supports);
    rctrl.addFunc("enable", &RenderControl::enable);
    rctrl.addFunc("disable", &RenderControl::disable);
    rctrl.addFunc("isEnabled", &RenderControl::isEnabled);
    rctrl.addFunc("compile", &RenderControl::compile);
    rctrl.addFunc("isCompiled", &RenderControl::isCompiled);
    rctrl.addFunc("getPassCount", &RenderControl::getPassCount);
    rctrl.addFunc("getPassName", &RenderControl::getPassName);
    rctrl.addFunc("hasPass", &RenderControl::hasPass);
    rctrl.addFunc("getGBuffer", static_cast<GBuffer* (RenderControl::*)()>(&RenderControl::getGBuffer));

    auto vol = table.addClass<Volumetric>("Volumetric",
                                          std::function<Volumetric*()>([]() -> Volumetric* { return nullptr; }), true);
    vol.addFunc("setQuality", &Volumetric::setQuality);
    vol.addFunc("getQuality", &Volumetric::getQuality);
    vol.addFunc("setMode", &Volumetric::setMode);
    vol.addFunc("getMode", &Volumetric::getMode);
    vol.addFunc("setLightScreenUV", &Volumetric::setLightScreenUV);
    vol.addFunc("getLightScreenU", &Volumetric::getLightScreenU);
    vol.addFunc("getLightScreenV", &Volumetric::getLightScreenV);
    vol.addFunc("setLightScreenPos", &Volumetric::setLightScreenPos);
    vol.addFunc("setLightDirection", &Volumetric::setLightDirection);
    vol.addFunc("setCamera", &Volumetric::setCamera);
    vol.addFunc("setShaftColor", &Volumetric::setShaftColor);
    vol.addFunc("setFogColor", &Volumetric::setFogColor);
    vol.addFunc("setIntensity", &Volumetric::setIntensity);
    vol.addFunc("setTime", &Volumetric::setTime);
    vol.addFunc("setDensity", &Volumetric::setDensity);
    vol.addFunc("hasParam", &Volumetric::hasParam);
    vol.addFunc("setFloat", &Volumetric::setFloat);
    vol.addFunc("getFloat", &Volumetric::getFloat);
    vol.addFunc("getSampleCount", &Volumetric::getSampleCount);
    vol.addFunc("getDownscale", &Volumetric::getDownscale);
    vol.addFunc("resolutionFor", &Volumetric::resolutionFor);
    vol.addFunc("beginOcclusionMap", &Volumetric::beginOcclusionMap);
    vol.addFunc("drawOccluder", &Volumetric::drawOccluder);
    vol.addFunc("drawOccluderSolid", &Volumetric::drawOccluderSolid);
    vol.addFunc("drawOccluderTexture", &Volumetric::drawOccluderTexture);
    vol.addFunc("drawOccluders2D", &Volumetric::drawOccluders2D);
    vol.addFunc("scatter", &Volumetric::scatter);
    vol.addFunc("scatterTo", &Volumetric::scatterTo);
    vol.addFunc("applyFromScene", &Volumetric::applyFromScene);
    vol.addFunc("applyFromSceneTo", &Volumetric::applyFromSceneTo);
    vol.addFunc("rayMarch", &Volumetric::rayMarch);
    vol.addFunc("rayMarchTo", &Volumetric::rayMarchTo);
    vol.addFunc("setFogHeight", &Volumetric::setFogHeight);
    vol.addFunc("setFogHeightFalloff", &Volumetric::setFogHeightFalloff);
    vol.addFunc("setFogStart", &Volumetric::setFogStart);
    vol.addFunc("setFogEnd", &Volumetric::setFogEnd);
    vol.addFunc("setFogNoise", &Volumetric::setFogNoise);
    vol.addFunc("setCloudLayer", &Volumetric::setCloudLayer);
    vol.addFunc("setCloudCoverage", &Volumetric::setCloudCoverage);
    vol.addFunc("setCloudDensity", &Volumetric::setCloudDensity);
    vol.addFunc("setCloudScale", &Volumetric::setCloudScale);
    vol.addFunc("setCloudWind", &Volumetric::setCloudWind);
    vol.addFunc("setCloudLightColor", &Volumetric::setCloudLightColor);
    vol.addFunc("applyFog", &Volumetric::applyFog);
    vol.addFunc("applyFogTo", &Volumetric::applyFogTo);
    vol.addFunc("renderClouds", &Volumetric::renderClouds);
    vol.addFunc("renderCloudsTo", &Volumetric::renderCloudsTo);
    vol.addFunc("getShader", &Volumetric::getShader);
    vol.addFunc("getRayMarchShader", &Volumetric::getRayMarchShader);
    vol.addFunc("getFogShader", &Volumetric::getFogShader);
    vol.addFunc("getCloudShader", &Volumetric::getCloudShader);

    auto grassField = table.addClass<GrassField>(
        "GrassField", std::function<GrassField*()>([]() -> GrassField* { return nullptr; }), true);
    grassField.addFunc("bakePlane", static_cast<void (GrassField::*)(float, float, int, int)>(&GrassField::bakePlane));
    grassField.addFunc("update", &GrassField::update);
    grassField.addFunc("setTime", &GrassField::setTime);
    grassField.addFunc("getTime", &GrassField::getTime);
    grassField.addFunc("setFrameDuration", &GrassField::setFrameDuration);
    grassField.addFunc("getFrameDuration", &GrassField::getFrameDuration);
    grassField.addFunc("draw", static_cast<void (GrassField::*)()>(&GrassField::draw));
    grassField.addFunc("getDenseMesh", &GrassField::getDenseMesh);
    grassField.addFunc("getSparseMesh", &GrassField::getSparseMesh);
    grassField.addFunc("getShader", &GrassField::getShader);
    grassField.addFunc("getAtlas", &GrassField::getAtlas);
    grassField.addFunc("getDenseCount", &GrassField::getDenseCount);
    grassField.addFunc("getSparseCount", &GrassField::getSparseCount);

    auto waterfall = table.addClass<Waterfall>(
        "Waterfall", std::function<Waterfall*()>([]() -> Waterfall* { return nullptr; }), true);
    waterfall.addFunc("createSheet", &Waterfall::createSheet);
    waterfall.addFunc("createCurvedSheet", &Waterfall::createCurvedSheet);
    waterfall.addFunc("update", &Waterfall::update);
    waterfall.addFunc("setTime", &Waterfall::setTime);
    waterfall.addFunc("getTime", &Waterfall::getTime);
    waterfall.addFunc("setFlowSpeed", &Waterfall::setFlowSpeed);
    waterfall.addFunc("getFlowSpeed", &Waterfall::getFlowSpeed);
    waterfall.addFunc("setTurbulence", &Waterfall::setTurbulence);
    waterfall.addFunc("getTurbulence", &Waterfall::getTurbulence);
    waterfall.addFunc("setStreakCount", &Waterfall::setStreakCount);
    waterfall.addFunc("getStreakCount", &Waterfall::getStreakCount);
    waterfall.addFunc("setStreakScale", &Waterfall::setStreakScale);
    waterfall.addFunc("getStreakScale", &Waterfall::getStreakScale);
    waterfall.addFunc("setTopFoam", &Waterfall::setTopFoam);
    waterfall.addFunc("getTopFoam", &Waterfall::getTopFoam);
    waterfall.addFunc("setBottomFoam", &Waterfall::setBottomFoam);
    waterfall.addFunc("getBottomFoam", &Waterfall::getBottomFoam);
    waterfall.addFunc("setFoamAmount", &Waterfall::setFoamAmount);
    waterfall.addFunc("getFoamAmount", &Waterfall::getFoamAmount);
    waterfall.addFunc("setWaterColor", &Waterfall::setWaterColor);
    waterfall.addFunc("setReflectionIntensity", &Waterfall::setReflectionIntensity);
    waterfall.addFunc("getReflectionIntensity", &Waterfall::getReflectionIntensity);
    waterfall.addFunc("setSunIntensity", &Waterfall::setSunIntensity);
    waterfall.addFunc("getSunIntensity", &Waterfall::getSunIntensity);
    waterfall.addFunc("bindParams", &Waterfall::bindParams);
    waterfall.addFunc("draw", &Waterfall::draw);
    waterfall.addFunc("getShader", &Waterfall::getShader);
    waterfall.addFunc("getMesh", &Waterfall::getMesh);
    auto water = table.addClass<Water>("Water", std::function<Water*()>([]() -> Water* { return nullptr; }), true);
    water.addFunc("createPlane", &Water::createPlane);
    water.addFunc("update", &Water::update);
    water.addFunc("setTime", &Water::setTime);
    water.addFunc("getTime", &Water::getTime);
    water.addFunc("setWaveSpeed", &Water::setWaveSpeed);
    water.addFunc("getWaveSpeed", &Water::getWaveSpeed);
    water.addFunc("setWaveAmplitude", &Water::setWaveAmplitude);
    water.addFunc("getWaveAmplitude", &Water::getWaveAmplitude);
    water.addFunc("setRippleAmplitude", &Water::setRippleAmplitude);
    water.addFunc("getRippleAmplitude", &Water::getRippleAmplitude);
    water.addFunc("setEdgeFalloff", &Water::setEdgeFalloff);
    water.addFunc("getEdgeFalloff", &Water::getEdgeFalloff);
    water.addFunc("setRippleCount", &Water::setRippleCount);
    water.addFunc("getRippleCount", &Water::getRippleCount);
    water.addFunc("setRippleInterval", &Water::setRippleInterval);
    water.addFunc("getRippleInterval", &Water::getRippleInterval);
    water.addFunc("setWaveScale", &Water::setWaveScale);
    water.addFunc("getWaveScale", &Water::getWaveScale);
    water.addFunc("setWaterColor", &Water::setWaterColor);
    water.addFunc("setReflectionTint", &Water::setReflectionTint);
    water.addFunc("setReflectionIntensity", &Water::setReflectionIntensity);
    water.addFunc("getReflectionIntensity", &Water::getReflectionIntensity);
    water.addFunc("setSunIntensity", &Water::setSunIntensity);
    water.addFunc("getSunIntensity", &Water::getSunIntensity);
    water.addFunc("setScreenSpaceReflection", &Water::setScreenSpaceReflection);
    water.addFunc("getScreenSpaceReflection", &Water::getScreenSpaceReflection);
    water.addFunc("getScreenSpaceReflectionStrength", &Water::getScreenSpaceReflectionStrength);
    water.addFunc("setViewport", &Water::setViewport);
    water.addFunc("getViewportWidth", &Water::getViewportWidth);
    water.addFunc("getViewportHeight", &Water::getViewportHeight);
    water.addFunc("bindParams", &Water::bindParams);
    water.addFunc("draw", &Water::draw);
    water.addFunc("getShader", &Water::getShader);
    water.addFunc("getMesh", &Water::getMesh);

    auto ao = table.addClass<AmbientOcclusion>(
        "AmbientOcclusion", std::function<AmbientOcclusion*()>([]() -> AmbientOcclusion* { return nullptr; }), true);
    ao.addFunc("setQuality", &AmbientOcclusion::setQuality);
    ao.addFunc("getQuality", &AmbientOcclusion::getQuality);
    ao.addFunc("setMode", &AmbientOcclusion::setMode);
    ao.addFunc("getMode", &AmbientOcclusion::getMode);
    ao.addFunc("setCamera", &AmbientOcclusion::setCamera);
    ao.addFunc("setRadius", &AmbientOcclusion::setRadius);
    ao.addFunc("setBias", &AmbientOcclusion::setBias);
    ao.addFunc("setIntensity", &AmbientOcclusion::setIntensity);
    ao.addFunc("setPower", &AmbientOcclusion::setPower);
    ao.addFunc("setThickness", &AmbientOcclusion::setThickness);
    ao.addFunc("getRadius", &AmbientOcclusion::getRadius);
    ao.addFunc("getBias", &AmbientOcclusion::getBias);
    ao.addFunc("getIntensity", &AmbientOcclusion::getIntensity);
    ao.addFunc("getPower", &AmbientOcclusion::getPower);
    ao.addFunc("hasParam", &AmbientOcclusion::hasParam);
    ao.addFunc("setFloat", &AmbientOcclusion::setFloat);
    ao.addFunc("getFloat", &AmbientOcclusion::getFloat);
    ao.addFunc("getSampleCount", &AmbientOcclusion::getSampleCount);
    ao.addFunc("getDownscale", &AmbientOcclusion::getDownscale);
    ao.addFunc("resolutionFor", &AmbientOcclusion::resolutionFor);
    ao.addFunc("compute", &AmbientOcclusion::compute);
    ao.addFunc("computeTo", &AmbientOcclusion::computeTo);
    ao.addFunc("blur", &AmbientOcclusion::blur);
    ao.addFunc("blurTo", &AmbientOcclusion::blurTo);
    ao.addFunc("applyOverlay", &AmbientOcclusion::applyOverlay);
    ao.addFunc("applyOverlayTo", &AmbientOcclusion::applyOverlayTo);
    ao.addFunc("applyFromDepth", &AmbientOcclusion::applyFromDepth);
    ao.addFunc("applyFromDepthTo", &AmbientOcclusion::applyFromDepthTo);
    ao.addFunc("applyFromGBuffer", &AmbientOcclusion::applyFromGBuffer);
    ao.addFunc("getShader", &AmbientOcclusion::getShader);
    ao.addFunc("getSsaoShader", &AmbientOcclusion::getSsaoShader);
    ao.addFunc("getHbaoShader", &AmbientOcclusion::getHbaoShader);
    ao.addFunc("getGtaoShader", &AmbientOcclusion::getGtaoShader);
    ao.addFunc("getBlurShader", &AmbientOcclusion::getBlurShader);
    ao.addFunc("getOverlayShader", &AmbientOcclusion::getOverlayShader);
    ao.addFunc("getFromDepthShader", &AmbientOcclusion::getFromDepthShader);

    auto outline =
        table.addClass<Outline>("Outline", std::function<Outline*()>([]() -> Outline* { return nullptr; }), true);
    outline.addFunc("setColor", &Outline::setColor);
    outline.addFunc("getColorR", &Outline::getColorR);
    outline.addFunc("getColorG", &Outline::getColorG);
    outline.addFunc("getColorB", &Outline::getColorB);
    outline.addFunc("setWidth", &Outline::setWidth);
    outline.addFunc("getWidth", &Outline::getWidth);
    outline.addFunc("setDepthThreshold", &Outline::setDepthThreshold);
    outline.addFunc("getDepthThreshold", &Outline::getDepthThreshold);
    outline.addFunc("setDepthSensitivity", &Outline::setDepthSensitivity);
    outline.addFunc("getDepthSensitivity", &Outline::getDepthSensitivity);
    outline.addFunc("setNormalThreshold", &Outline::setNormalThreshold);
    outline.addFunc("getNormalThreshold", &Outline::getNormalThreshold);
    outline.addFunc("setSoftness", &Outline::setSoftness);
    outline.addFunc("getSoftness", &Outline::getSoftness);
    outline.addFunc("setClip", &Outline::setClip);
    outline.addFunc("hasParam", &Outline::hasParam);
    outline.addFunc("setFloat", &Outline::setFloat);
    outline.addFunc("getFloat", &Outline::getFloat);
    outline.addFunc("apply", &Outline::apply);
    outline.addFunc("applyTo", &Outline::applyTo);
    outline.addFunc("getShader", &Outline::getShader);

    auto gi = table.addClass<GlobalIllumination>(
        "GlobalIllumination", std::function<GlobalIllumination*()>([]() -> GlobalIllumination* { return nullptr; }),
        true);
    gi.addFunc("setQuality", &GlobalIllumination::setQuality);
    gi.addFunc("getQuality", &GlobalIllumination::getQuality);
    gi.addFunc("setCamera", &GlobalIllumination::setCamera);
    gi.addFunc("setRadius", &GlobalIllumination::setRadius);
    gi.addFunc("setIntensity", &GlobalIllumination::setIntensity);
    gi.addFunc("setLightDirection", &GlobalIllumination::setLightDirection);
    gi.addFunc("setLightColor", &GlobalIllumination::setLightColor);
    gi.addFunc("getRadius", &GlobalIllumination::getRadius);
    gi.addFunc("getIntensity", &GlobalIllumination::getIntensity);
    gi.addFunc("hasParam", &GlobalIllumination::hasParam);
    gi.addFunc("setFloat", &GlobalIllumination::setFloat);
    gi.addFunc("getFloat", &GlobalIllumination::getFloat);
    gi.addFunc("getSampleCount", &GlobalIllumination::getSampleCount);
    gi.addFunc("applyFromDepth", &GlobalIllumination::applyFromDepth);
    gi.addFunc("applyFromDepthTo", &GlobalIllumination::applyFromDepthTo);
    gi.addFunc("applyFromScene", &GlobalIllumination::applyFromScene);
    gi.addFunc("getShader", &GlobalIllumination::getShader);

    auto ssr = table.addClass<ScreenSpaceReflection>(
        "ScreenSpaceReflection",
        std::function<ScreenSpaceReflection*()>([]() -> ScreenSpaceReflection* { return nullptr; }), true);
    ssr.addFunc("setCamera", &ScreenSpaceReflection::setCamera);
    ssr.addFunc("setEnabled", &ScreenSpaceReflection::setEnabled);
    ssr.addFunc("getEnabled", &ScreenSpaceReflection::getEnabled);
    ssr.addFunc("setMaxDistance", &ScreenSpaceReflection::setMaxDistance);
    ssr.addFunc("setStepLength", &ScreenSpaceReflection::setStepLength);
    ssr.addFunc("setMaxSteps", &ScreenSpaceReflection::setMaxSteps);
    ssr.addFunc("setThickness", &ScreenSpaceReflection::setThickness);
    ssr.addFunc("setStrength", &ScreenSpaceReflection::setStrength);
    ssr.addFunc("getStrength", &ScreenSpaceReflection::getStrength);
    ssr.addFunc("hasParam", &ScreenSpaceReflection::hasParam);
    ssr.addFunc("setFloat", &ScreenSpaceReflection::setFloat);
    ssr.addFunc("getFloat", &ScreenSpaceReflection::getFloat);
    ssr.addFunc("applyFromScene", &ScreenSpaceReflection::applyFromScene);
    ssr.addFunc("applyFromSceneTo", &ScreenSpaceReflection::applyFromSceneTo);
    ssr.addFunc("getShader", &ScreenSpaceReflection::getShader);

    auto aa = table.addClass<AntiAliasing>(
        "AntiAliasing", std::function<AntiAliasing*()>([]() -> AntiAliasing* { return nullptr; }), true);
    aa.addFunc("setQuality", &AntiAliasing::setQuality);
    aa.addFunc("getQuality", &AntiAliasing::getQuality);
    aa.addFunc("setMode", &AntiAliasing::setMode);
    aa.addFunc("getMode", &AntiAliasing::getMode);
    aa.addFunc("hasParam", &AntiAliasing::hasParam);
    aa.addFunc("setFloat", &AntiAliasing::setFloat);
    aa.addFunc("getFloat", &AntiAliasing::getFloat);
    aa.addFunc("suggestScale", &AntiAliasing::suggestScale);
    aa.addFunc("resolutionFor", &AntiAliasing::resolutionFor);
    aa.addFunc("apply", &AntiAliasing::apply);
    aa.addFunc("applyTo", &AntiAliasing::applyTo);
    aa.addFunc("applyCanvas", &AntiAliasing::applyCanvas);
    aa.addFunc("applyCanvasTo", &AntiAliasing::applyCanvasTo);
    aa.addFunc("getShader", &AntiAliasing::getShader);
    aa.addFunc("getFxaaShader", &AntiAliasing::getFxaaShader);
    aa.addFunc("getSmaaShader", &AntiAliasing::getSmaaShader);
    aa.addFunc("getSsaaShader", &AntiAliasing::getSsaaShader);
    aa.addFunc("getNfaaShader", &AntiAliasing::getNfaaShader);
}

void Graphics::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Graphics::getName);
    cls.addFunc("reset", &Graphics::reset);
    cls.addFunc("present", &Graphics::present);
    cls.addFunc("clear", &Graphics::clearScreen);
    cls.addFunc("setBackgroundColor", &Graphics::setBackgroundColorRGBA);
    cls.addFunc("drawSolidRect", &Graphics::drawSolidRectRGBA);
    cls.addFunc("drawTexturedRect", &Graphics::drawTexturedRectRGBA);
    cls.addFunc("renderSprites", &Graphics::renderSprites);
    cls.addFunc("newSprite2D", &Graphics::newSprite2D);
    cls.addFunc("drawTexturedRectRotated", &Graphics::drawTexturedRectRotatedRGBA);
    cls.addFunc("newTextureFromFile", &Graphics::newTextureFromFile);
    cls.addFunc("newTexture",
                static_cast<Texture* (Graphics::*)(image::ImageData*, bool, bool)>(&Graphics::newTextureFromImageData));
    cls.addFunc("newTextureFromFile", &Graphics::newTextureFromFile);
    cls.addFunc("newTextureFromFileRepeated", &Graphics::newTextureFromFileRepeated);
    cls.addFunc("newTextureWithSampler", &Graphics::newTextureWithSampler);
    cls.addFunc("setTextureSampler",
                static_cast<void (Graphics::*)(Texture *, const std::string &, const std::string &, float, float)>(
                    &Graphics::setTextureSampler));
    cls.addFunc("getMaxAnisotropy", &Graphics::getMaxAnisotropy);
    cls.addFunc("newMeshSphere", &Graphics::newMeshSphere);
    cls.addFunc("newMeshCylinder", &Graphics::newMeshCylinder);
    cls.addFunc("newMeshCube", &Graphics::newMeshCube);
    cls.addFunc("newMeshFromArrays",
                std::function<Mesh *(Graphics *, ssq::Array, ssq::Array, ssq::Array, int,
                                    ssq::Array, int)>(newMeshFromArraysScript));
    cls.addFunc("updateMeshVertices",
                std::function<bool(Graphics *, Mesh *, ssq::Array, ssq::Array, ssq::Array,
                                   int, ssq::Array, int)>(updateMeshVerticesScript));
    cls.addFunc("bakeMeshMorph", &Graphics::bakeMeshMorph);
    cls.addFunc("newShader", static_cast<Shader* (Graphics::*)(const std::string&)>(&Graphics::newShader));
    cls.addFunc("newShaderFromWgsl", &Graphics::newShaderFromWgsl);
    cls.addFunc("newMeshShader", static_cast<Shader* (Graphics::*)(const std::string&)>(&Graphics::newMeshShader));
    cls.addFunc("newHairShader", &Graphics::newHairShader);
    cls.addFunc("newGrassShader", &Graphics::newGrassShader);
    cls.addFunc("newGrassField", &Graphics::newGrassField);
    cls.addFunc("newWaterfall", &Graphics::newWaterfall);
    cls.addFunc("newWater", &Graphics::newWater);
    cls.addFunc("newShaderFromSpvFile",
                static_cast<Shader* (Graphics::*)(const std::string&)>(&Graphics::newShaderFromSpvFile));
    cls.addFunc("setShader", static_cast<void (Graphics::*)(Shader*)>(&Graphics::setShader));
    cls.addFunc("getShader", &Graphics::getShader);
    cls.addFunc("render3D", &Graphics::render3D);
    cls.addFunc("begin3DFrame", &Graphics::begin3DFrame);
    cls.addFunc("setMesh3DViewProj",
                std::function<void(Graphics *, ssq::Array)>(setMesh3DViewProjScript));
    cls.addFunc("setMesh3DView",
                std::function<void(Graphics *, ssq::Array)>(setMesh3DViewScript));
    cls.addFunc("setMesh3DCameraPos",
                std::function<void(Graphics *, float, float, float)>(setMesh3DCameraPosScript));
    cls.addFunc("renderScene3DToCanvas", &Graphics::renderScene3DToCanvas);
    cls.addFunc("saveFramePng", &Graphics::saveFramePng);
    cls.addFunc("drawScene3D", &Graphics::drawScene3D);
    cls.addFunc("drawCanvas", &Graphics::drawCanvas);
    cls.addFunc("newCanvas", &Graphics::newCanvas);
    cls.addFunc("setCanvas", std::function<void(Graphics *, ssq::Object)>(setCanvasScript));
    cls.addFunc("getCanvas", &Graphics::getCanvas);
    cls.addFunc("getWidth", &Graphics::getWidth);
    cls.addFunc("getHeight", &Graphics::getHeight);
    cls.addFunc("setDirectionalLight", &Graphics::setDirectionalLight);
    cls.addFunc("newMaterial", &Graphics::newMaterial);
    cls.addFunc("getRenderControl", &Graphics::getRenderControl);
    cls.addFunc("setMsaaSamples", &Graphics::setMsaaSamples);
    cls.addFunc("getMsaaSamples", &Graphics::getMsaaSamples);
    cls.addFunc("getSceneColorTexture", &Graphics::getSceneColorTexture);
    cls.addFunc("newQuad", &Graphics::newQuad);
    cls.addFunc("newVolumetric", &Graphics::newVolumetric);
    cls.addFunc("newAmbientOcclusion", &Graphics::newAmbientOcclusion);
    cls.addFunc("newOutline", &Graphics::newOutline);
    cls.addFunc("getOutline", &Graphics::pipelineOutline);
    cls.addFunc("newGlobalIllumination", &Graphics::newGlobalIllumination);
    cls.addFunc("newScreenSpaceReflection", &Graphics::newScreenSpaceReflection);
    cls.addFunc("newAntiAliasing", &Graphics::newAntiAliasing);
    cls.addFunc("drawOcclusionSolid", &Graphics::drawOcclusionSolid);
    cls.addFunc("drawOcclusionTexture", &Graphics::drawOcclusionTexture);
}

void Graphics::reset() {
    currentShader = nullptr;
    currentFont   = nullptr;
}

Renderable2D *Graphics::newSprite2D() { return Renderable2D::create(); }

void Graphics::renderSprites() {
    std::vector<DrawItem2D> items;
    RenderSystem::collectSprites(items);
    RenderSystem::drawItems(*this, items, false);
}

void Graphics::initHeadless(int width, int height) {
    (void)width;
    (void)height;
    throw Exception("Graphics::initHeadless: not supported on this backend");
}

void Graphics::setShader(Shader* shader) { currentShader = shader; }

void Graphics::setShader() { currentShader = nullptr; }

Volumetric* Graphics::newVolumetric() { return new Volumetric(this); }

AmbientOcclusion* Graphics::newAmbientOcclusion() { return new AmbientOcclusion(this); }

Outline* Graphics::newOutline() { return new Outline(this); }

GlobalIllumination* Graphics::newGlobalIllumination() { return new GlobalIllumination(this); }

ScreenSpaceReflection* Graphics::newScreenSpaceReflection() { return new ScreenSpaceReflection(this); }

AmbientOcclusion* Graphics::pipelineAmbientOcclusion() {
    if (!pipelineAO_) pipelineAO_ = std::make_unique<AmbientOcclusion>(this);
    return pipelineAO_.get();
}

GlobalIllumination* Graphics::pipelineGlobalIllumination() {
    if (!pipelineGI_) pipelineGI_ = std::make_unique<GlobalIllumination>(this);
    return pipelineGI_.get();
}

AntiAliasing* Graphics::pipelineAntiAliasing() {
    if (!pipelineAA_) pipelineAA_ = std::make_unique<AntiAliasing>(this);
    return pipelineAA_.get();
}

Outline* Graphics::pipelineOutline() {
    if (!pipelineOutline_) pipelineOutline_ = std::make_unique<Outline>(this);
    return pipelineOutline_.get();
}

AntiAliasing* Graphics::newAntiAliasing() { return new AntiAliasing(this); }

Shader* Graphics::newHairShader() { return hair::createShader(this); }

Shader* Graphics::newGrassShader() { return grass::createShader(this); }

GrassField* Graphics::newGrassField() { return new GrassField(this); }

Waterfall* Graphics::newWaterfall() { return new Waterfall(this); }
Water*     Graphics::newWater() { return new Water(this); }

Mesh* Graphics::newMeshCube(float size) {
    const float h = size * 0.5f;
    // 6 faces x 4 corners (per-face normal + full 0..1 UV), outward CCW for RH Y-up.
    const float kFaces[6][4][3] = {
        {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}},      // +Z
        {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}},  // -Z
        {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}},      // +X
        {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}},  // -X
        {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}},      // +Y
        {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}},  // -Y
    };
    const float kN[6][3]  = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const float kUV[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    std::vector<float> pos, nrm, uv;
    pos.reserve(6 * 4 * 3);
    nrm.reserve(6 * 4 * 3);
    uv.reserve(6 * 4 * 2);
    std::vector<uint32_t> indices;
    indices.reserve(6 * 6);
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(f * 4);
        for (int c = 0; c < 4; ++c) {
            pos.insert(pos.end(), kFaces[f][c], kFaces[f][c] + 3);
            nrm.insert(nrm.end(), kN[f], kN[f] + 3);
            uv.insert(uv.end(), kUV[c], kUV[c] + 2);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
    return newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), indices.data(),
                             int(indices.size()));
}

bool Graphics::saveFramePng(const std::string& path) {
    if (!screenReadbackEnabled) setScreenReadbackEnabled(true);
    std::unique_ptr<eve::image::ImageData> frame;
    try {
        frame.reset(newImageData());
    } catch (...) {
        return false;  // no presented frame yet
    }
    if (!frame) return false;
    std::unique_ptr<eve::filesystem::FileData> png(
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false));
    if (!png) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out.good()) return false;
    out.write(static_cast<const char*>(png->getData()), static_cast<std::streamsize>(png->getSize()));
    return out.good();
}

void Graphics::draw(Drawable* drawable, const glm::mat4& m) {
    if (drawable) drawable->draw(this, m);
}

void Graphics::drawOcclusion(Drawable* drawable, const glm::mat4& m) {
    if (drawable && drawable->getCastOcclusion()) drawable->drawOcclusion(this, m);
}

void Graphics::drawOcclusionSolid(float x, float y, float w, float h) {
    drawSolidRect(x, y, w, h, Color(0.f, 0.f, 0.f, 1.f));
}

void Graphics::drawOcclusionTexture(Texture* texture, float x, float y, float w, float h) {
    // Black RGB keeps silhouette; texture alpha cuts soft edges (same idea as shadow masks).
    drawTexturedRect(texture, x, y, w, h, Color(0.f, 0.f, 0.f, 1.f));
}

void Graphics::clearScreen() { clear(std::nullopt, std::nullopt, std::nullopt); }

void Graphics::setBackgroundColorRGBA(float r, float g, float b, float a) { setBackgroundColor(Color(r, g, b, a)); }

void Graphics::drawSolidRectRGBA(float x, float y, float w, float h, float r, float g, float b, float a) {
    drawSolidRect(x, y, w, h, Color(r, g, b, a));
}

void Graphics::drawTexturedRectRGBA(Texture* texture, float x, float y, float w, float h, float r, float g, float b,
                                    float a) {
    drawTexturedRect(texture, x, y, w, h, Color(r, g, b, a));
}

void Graphics::drawTexturedRectRotatedRGBA(Texture* texture, float cx, float cy, float w, float h,
                                           float degrees, float r, float g, float b, float a) {
    drawTexturedRectShaderUVRotated(texture, nullptr, cx, cy, w, h, degrees, 0.f, 0.f, 1.f, 1.f,
                                    Color(r, g, b, a), false, BlendMode::Alpha);
}

void Graphics::drawSolidRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    drawSolidRectRGBA(x, y, w, h, r, g, b, a);
}

void Graphics::drawTexturedRect(Texture* texture, float x, float y, float w, float h, float r, float g, float b,
                                float a) {
    drawTexturedRectRGBA(texture, x, y, w, h, r, g, b, a);
}

Texture* Graphics::newTextureFromImageData(image::ImageData* data, bool repeatU, bool repeatV) {
    if (!data) throw eve::Exception("newTextureFromImageData: null ImageData");
    if (data->getFormat() != "RGBA8") throw eve::Exception("newTextureFromImageData: only RGBA8 supported");
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(data->getWidth(), data->getHeight(), static_cast<const uint8_t*>(data->getData()), info);
}

Texture* Graphics::newTextureFromImageData(image::ImageData* data, const TextureCreateInfo& info) {
    if (!data) throw eve::Exception("newTextureFromImageData: null ImageData");
    if (data->getFormat() != "RGBA8") throw eve::Exception("newTextureFromImageData: only RGBA8 supported");
    return newTexture(data->getWidth(), data->getHeight(), static_cast<const uint8_t*>(data->getData()), info);
}

Texture* Graphics::newTextureFromFileRepeated(const std::string& filename, bool repeatU, bool repeatV) {
    if (filename.empty()) throw eve::Exception("newTextureFromFileRepeated: empty filename");
    auto*                                      fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw eve::Exception("newTextureFromFileRepeated: failed to read '%s'", filename.c_str());
    auto*                                  imgMod = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> data(imgMod->newImageData(fileData.get()));
    return newTextureFromImageData(data.get(), repeatU, repeatV);
}

Texture* Graphics::newTextureWithSampler(image::ImageData* data, bool repeatU, bool repeatV, bool generateMipmaps,
                                         float maxAnisotropy, const std::string& filter, const std::string& mipmap,
                                         float lodBias) {
    TextureCreateInfo info;
    info.generateMipmaps = generateMipmaps;
    info.sampler.min     = TextureSampler::parseFilter(filter);
    info.sampler.mag     = info.sampler.min;
    info.sampler.mipmap  = TextureSampler::parseMipmap(mipmap);
    if (generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled) info.sampler.mipmap = MipmapMode::Linear;
    info.sampler.repeatU       = repeatU;
    info.sampler.repeatV       = repeatV;
    info.sampler.maxAnisotropy = maxAnisotropy;
    info.sampler.lodBias       = lodBias;
    return newTextureFromImageData(data, info);
}

void Graphics::setTextureSampler(Texture* texture, const std::string& filter, const std::string& mipmap,
                                 float maxAnisotropy, float lodBias) {
    if (!texture) return;
    // Fail fast on typos instead of silently treating an unknown filter as linear.
    if (filter != "nearest" && filter != "Nearest" && filter != "NEAREST" &&
        filter != "linear" && filter != "Linear" && filter != "LINEAR") {
        throw eve::Exception("Graphics::setTextureSampler: unknown filter '%s'", filter.c_str());
    }
    if (mipmap != "none" && mipmap != "None" && mipmap != "NONE" &&
        mipmap != "nearest" && mipmap != "Nearest" && mipmap != "NEAREST" &&
        mipmap != "linear" && mipmap != "Linear" && mipmap != "LINEAR") {
        throw eve::Exception("Graphics::setTextureSampler: unknown mipmap mode '%s'", mipmap.c_str());
    }
    TextureSampler s = texture->getSampler();
    s.min            = TextureSampler::parseFilter(filter);
    s.mag            = s.min;
    s.mipmap         = TextureSampler::parseMipmap(mipmap);
    s.maxAnisotropy  = maxAnisotropy;
    s.lodBias        = lodBias;
    setTextureSampler(texture, s);
}

void Graphics::setTextureSamplerParams(Texture* texture, const std::string& filter, const std::string& mipmap,
                                       float maxAnisotropy, float lodBias) {
    setTextureSampler(texture, filter, mipmap, maxAnisotropy, lodBias);
}

Quad* Graphics::newQuad(int x, int y, int w, int h) { return new Quad(x, y, w, h); }

#ifndef EVENGINE_WEBGPU
Font* Graphics::newFont(font::FontData* data, std::string charset) { return new Font(this, data, std::move(charset)); }

void Graphics::print(const std::string& text, float x, float y, const Color& color, float scale) {
    if (currentFont == nullptr) {
        eve::debug::rtDraw("print", "no-font");
        throw eve::Exception("Graphics::print: no font set (call setFont first)");
    }
    eve::debug::rtBind("font", "current");
    eve::debug::rtDraw("print", text.empty() ? "" : "text");

    font::FontData* data          = currentFont->getData();
    float           penX          = x;
    float           baseline      = y + currentFont->getBaseline() * scale;
    int             prevCodepoint = -1;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = nextCodepointUtf8(text, i);
        if (cp == 0) continue;
        int code = static_cast<int>(cp);

        if (prevCodepoint >= 0) penX += data->getKerning(prevCodepoint, code) * scale;

        if (const Font::Glyph* g = currentFont->findGlyph(code)) {
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
#endif  // !EVENGINE_WEBGPU

}  // namespace eve::graphics
