#include "graphics/Graphics.h"
#include "graphics/HairShader.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderSystem.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/Quad.h"
#include "graphics/Font.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Volumetric.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "font/FontData.h"
#include "image/ImageData.h"
#include "common/Exception.h"
#include "common/RenderTrace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>

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

Material *Graphics::newMaterial() { return new Material(); }

RenderControl *Graphics::getRenderControl() {
    if (!renderControl_) {
        renderControl_ = std::make_unique<RenderControl>();
        renderControl_->attach(this);
        renderControl_->compile();
    }
    return renderControl_.get();
}

void Graphics::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Graphics::create, false);
    expose(cls);

    auto texCls =
        table.addClass<Texture>("Texture", std::function<Texture *()>([]() -> Texture * { return nullptr; }),
                                true);
    texCls.addFunc("setCastOcclusion", &Texture::setCastOcclusion);
    texCls.addFunc("getCastOcclusion", &Texture::getCastOcclusion);
    texCls.addFunc("getWidth", &Texture::getWidth);
    texCls.addFunc("getHeight", &Texture::getHeight);
    texCls.addFunc("getMipmapCount", &Texture::getMipmapCount);

    // Texture / Mesh expose occlusion flags used by volumetric light shafts.
    // (create returns null — instances come from Graphics::newTexture / newMesh*)

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
    meshCls.addFunc("setCastOcclusion", &Mesh::setCastOcclusion);
    meshCls.addFunc("getCastOcclusion", &Mesh::getCastOcclusion);

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
    light.addFunc("setVolumetric", &Light2D::setVolumetric);
    light.addFunc("getVolumetric", &Light2D::getVolumetric);
    light.addFunc("setVolumetricIntensity", &Light2D::setVolumetricIntensity);
    light.addFunc("getVolumetricIntensity", &Light2D::getVolumetricIntensity);
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
    light3d.addFunc("setVolumetric", &Light3D::setVolumetric);
    light3d.addFunc("getVolumetric", &Light3D::getVolumetric);
    light3d.addFunc("setVolumetricIntensity", &Light3D::setVolumetricIntensity);
    light3d.addFunc("getVolumetricIntensity", &Light3D::getVolumetricIntensity);

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

    auto material = table.addClass<Material>(
        "Material", std::function<Material *()>([]() -> Material * { return nullptr; }), true);
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
    material.addFunc("hasParam", &Material::hasParam);
    material.addFunc("setFloat", &Material::setFloat);
    material.addFunc("getFloat", &Material::getFloat);

    auto gbuffer = table.addClass<GBuffer>(
        "GBuffer", std::function<GBuffer *()>([]() -> GBuffer * { return nullptr; }), true);
    gbuffer.addFunc("isValid", &GBuffer::isValid);
    gbuffer.addFunc("getWidth", &GBuffer::getWidth);
    gbuffer.addFunc("getHeight", &GBuffer::getHeight);
    gbuffer.addFunc("getDepthTexture", &GBuffer::getDepthTexture);
    gbuffer.addFunc("getNormalTexture", &GBuffer::getNormalTexture);
    gbuffer.addFunc("getAlbedoTexture", &GBuffer::getAlbedoTexture);
    gbuffer.addFunc("hasBuffer", &GBuffer::hasBuffer);
    gbuffer.addFunc("getBuffer", &GBuffer::getBuffer);

    auto rctrl = table.addClass<RenderControl>(
        "RenderControl", std::function<RenderControl *()>([]() -> RenderControl * { return nullptr; }),
        true);
    rctrl.addFunc("supports", &RenderControl::supports);
    rctrl.addFunc("enable", &RenderControl::enable);
    rctrl.addFunc("disable", &RenderControl::disable);
    rctrl.addFunc("isEnabled", &RenderControl::isEnabled);
    rctrl.addFunc("compile", &RenderControl::compile);
    rctrl.addFunc("isCompiled", &RenderControl::isCompiled);
    rctrl.addFunc("getPassCount", &RenderControl::getPassCount);
    rctrl.addFunc("getPassName", &RenderControl::getPassName);
    rctrl.addFunc("hasPass", &RenderControl::hasPass);
    rctrl.addFunc("getGBuffer", static_cast<GBuffer *(RenderControl::*)()>(&RenderControl::getGBuffer));

    auto vol = table.addClass<Volumetric>(
        "Volumetric", std::function<Volumetric *()>([]() -> Volumetric * { return nullptr; }), true);
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
    vol.addFunc("applyFog", &Volumetric::applyFog);
    vol.addFunc("applyFogTo", &Volumetric::applyFogTo);
    vol.addFunc("getShader", &Volumetric::getShader);
    vol.addFunc("getRayMarchShader", &Volumetric::getRayMarchShader);
    vol.addFunc("getFogShader", &Volumetric::getFogShader);

    auto ao = table.addClass<AmbientOcclusion>(
        "AmbientOcclusion",
        std::function<AmbientOcclusion *()>([]() -> AmbientOcclusion * { return nullptr; }), true);
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
    ao.addFunc("getShader", &AmbientOcclusion::getShader);
    ao.addFunc("getSsaoShader", &AmbientOcclusion::getSsaoShader);
    ao.addFunc("getHbaoShader", &AmbientOcclusion::getHbaoShader);
    ao.addFunc("getGtaoShader", &AmbientOcclusion::getGtaoShader);
    ao.addFunc("getBlurShader", &AmbientOcclusion::getBlurShader);
    ao.addFunc("getOverlayShader", &AmbientOcclusion::getOverlayShader);

    auto aa = table.addClass<AntiAliasing>(
        "AntiAliasing", std::function<AntiAliasing *()>([]() -> AntiAliasing * { return nullptr; }),
        true);
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

void Graphics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Graphics::getName);
    cls.addFunc("reset", &Graphics::reset);
    cls.addFunc("present", &Graphics::present);
    cls.addFunc("clear", &Graphics::clearScreen);
    cls.addFunc("setBackgroundColor", &Graphics::setBackgroundColorRGBA);
    cls.addFunc("drawSolidRect", &Graphics::drawSolidRectRGBA);
    cls.addFunc("drawTexturedRect", &Graphics::drawTexturedRectRGBA);
    cls.addFunc("newTexture",
                static_cast<Texture *(Graphics::*)(image::ImageData *, bool, bool)>(
                    &Graphics::newTextureFromImageData));
    cls.addFunc("newTextureWithSampler", &Graphics::newTextureWithSampler);
    cls.addFunc("setTextureSampler", &Graphics::setTextureSamplerParams);
    cls.addFunc("getMaxAnisotropy", &Graphics::getMaxAnisotropy);
    cls.addFunc("newMeshSphere", &Graphics::newMeshSphere);
    cls.addFunc("newMeshCylinder", &Graphics::newMeshCylinder);
    cls.addFunc("bakeMeshMorph", &Graphics::bakeMeshMorph);
    cls.addFunc("newShader",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newShader));
    cls.addFunc("newMeshShader",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newMeshShader));
    cls.addFunc("newHairShader", &Graphics::newHairShader);
    cls.addFunc("newShaderFromSpvFile",
                static_cast<Shader *(Graphics::*)(const std::string &)>(&Graphics::newShaderFromSpvFile));
    cls.addFunc("setShader", static_cast<void (Graphics::*)(Shader *)>(&Graphics::setShader));
    cls.addFunc("getShader", &Graphics::getShader);
    cls.addFunc("render3D", &Graphics::render3D);
    cls.addFunc("setDirectionalLight", &Graphics::setDirectionalLight);
    cls.addFunc("newMaterial", &Graphics::newMaterial);
    cls.addFunc("getRenderControl", &Graphics::getRenderControl);
    cls.addFunc("newQuad", &Graphics::newQuad);
    cls.addFunc("newVolumetric", &Graphics::newVolumetric);
    cls.addFunc("newAmbientOcclusion", &Graphics::newAmbientOcclusion);
    cls.addFunc("newAntiAliasing", &Graphics::newAntiAliasing);
    cls.addFunc("drawOcclusionSolid", &Graphics::drawOcclusionSolid);
    cls.addFunc("drawOcclusionTexture", &Graphics::drawOcclusionTexture);
}

void Graphics::reset() {
    currentShader = nullptr;
    currentFont   = nullptr;
}

void Graphics::setShader(Shader *shader) { currentShader = shader; }

void Graphics::setShader() { currentShader = nullptr; }

Volumetric *Graphics::newVolumetric() { return new Volumetric(this); }

AmbientOcclusion *Graphics::newAmbientOcclusion() { return new AmbientOcclusion(this); }

AntiAliasing *Graphics::newAntiAliasing() { return new AntiAliasing(this); }

Shader *Graphics::newHairShader() { return hair::createShader(this); }

void Graphics::draw(Drawable *drawable, const glm::mat4 &m) {
    if (drawable) drawable->draw(this, m);
}

void Graphics::drawOcclusion(Drawable *drawable, const glm::mat4 &m) {
    if (drawable && drawable->getCastOcclusion()) drawable->drawOcclusion(this, m);
}

void Graphics::drawOcclusionSolid(float x, float y, float w, float h) {
    drawSolidRect(x, y, w, h, Color(0.f, 0.f, 0.f, 1.f));
}

void Graphics::drawOcclusionTexture(Texture *texture, float x, float y, float w, float h) {
    // Black RGB keeps silhouette; texture alpha cuts soft edges (same idea as shadow masks).
    drawTexturedRect(texture, x, y, w, h, Color(0.f, 0.f, 0.f, 1.f));
}

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
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}

Texture *Graphics::newTextureFromImageData(image::ImageData *data, const TextureCreateInfo &info) {
    if (!data) throw eve::Exception("newTextureFromImageData: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw eve::Exception("newTextureFromImageData: only RGBA8 supported");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}

Texture *Graphics::newTextureWithSampler(image::ImageData *data, bool repeatU, bool repeatV,
                                         bool generateMipmaps, float maxAnisotropy,
                                         const std::string &filter, const std::string &mipmap,
                                         float lodBias) {
    TextureCreateInfo info;
    info.generateMipmaps = generateMipmaps;
    info.sampler.min = TextureSampler::parseFilter(filter);
    info.sampler.mag = info.sampler.min;
    info.sampler.mipmap = TextureSampler::parseMipmap(mipmap);
    if (generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled)
        info.sampler.mipmap = MipmapMode::Linear;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    info.sampler.maxAnisotropy = maxAnisotropy;
    info.sampler.lodBias = lodBias;
    return newTextureFromImageData(data, info);
}

void Graphics::setTextureSamplerParams(Texture *texture, const std::string &filter,
                                       const std::string &mipmap, float maxAnisotropy,
                                       float lodBias) {
    if (!texture) return;
    TextureSampler s = texture->getSampler();
    s.min = TextureSampler::parseFilter(filter);
    s.mag = s.min;
    s.mipmap = TextureSampler::parseMipmap(mipmap);
    s.maxAnisotropy = maxAnisotropy;
    s.lodBias = lodBias;
    setTextureSampler(texture, s);
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
