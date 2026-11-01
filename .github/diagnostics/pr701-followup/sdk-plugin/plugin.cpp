#include <cstdio>
#include <SDL.h>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <glm/glm.hpp>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "common/Export.h"
#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "model3d/ModelRenderer.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "filesystem/FileData.h"
namespace eve::diagnostic {
using namespace graphics;
#include "helpers.h"
static void stage(const char* s,int frame=-1) {std::printf("UV_STAGE %s frame=%d\n",s,frame);std::fflush(stdout);}
class GpuProbe : public Module {
public:
 Module_REG(GpuProbe);
 int run() {
  stage("begin");
  auto* gfx=Graphics::create();resetScene3D();
  std::string mode=std::getenv("UV_MODE")?std::getenv("UV_MODE"):"baseline";
  std::printf("UV_MODE %s\n",mode.c_str());std::fflush(stdout);
  auto* rc=gfx->getRenderControl();
  for(const auto* feature : {"shadow","ao","aa","msaa","atmosphere"}) {
    if(mode==feature || mode=="forward_only") {
      if(!rc || !rc->requestFeature(feature,false).ok()) throw std::runtime_error(feature);
    }
  }
  std::vector<uint8_t> px(4*2*4);
  for(int y=0;y<2;++y)for(int x=0;x<4;++x){auto* p=px.data()+(y*4+x)*4;p[0]=y==0?255:0;p[1]=0;p[2]=y==0?0:255;p[3]=255;}
  image::Image::create();
  image::ImageData img(4,2,"RGBA8",px.data(),false);
  std::unique_ptr<filesystem::FileData> png(img.encode(image::ImageData::FormatHandler::ENCODED_PNG,"gltf_uv.png",false));
  if(!png) throw std::runtime_error("png");
  const auto gltf=makeUvOrientationGltf(base64Encode((const uint8_t*)png->getData(),png->getSize()));
  data::ByteData data(gltf.data(),gltf.size());
  auto* mod=model3d::Model3D::create();
  auto* md=mod->newModelData(&data,".gltf");
  if(!md) throw std::runtime_error("model");
  stage("model_loaded");
  auto* ent=mod->createRenderable(gfx,md,0);
  if(!ent) throw std::runtime_error("entity");
  auto* cam=Camera3D::createCamera(*ent->table);cam->setEye(0,0,3);cam->setTarget(0,0,0);cam->setUp(0,1,0);cam->data()->nearZ=.05f;cam->data()->farZ=100;
  if(mode=="tilted_light")RenderSystem3D::setDirectionalLight(.2f,1,.4f,1.5f,1.5f,1.5f);
  else RenderSystem3D::setDirectionalLight(0,0,-1,1.5f,1.5f,1.5f);
  gfx->setScreenReadbackEnabled(true);gfx->setBackgroundColor(Color(0,0,0,1));
  for(int i=0;i<8;++i){stage("render3D_start",i);RenderSystem3D::render(*gfx);stage("render2D_start",i);RenderSystem::render(*gfx);stage("poll_start",i);SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)break;}stage("frame_done",i);}
  stage("pixels");
  auto top=gfx->getPixel(gfx->getWidth()/2,gfx->getHeight()/4);auto bot=gfx->getPixel(gfx->getWidth()/2,gfx->getHeight()*3/4);
  std::printf("UV_PIXEL top=%f,%f,%f bottom=%f,%f,%f\n",top.r,top.g,top.b,bot.r,bot.g,bot.b);std::fflush(stdout);
  return top.r>top.b && bot.b>bot.r && (top.r+top.g+top.b)/3>.05f && (bot.r+bot.g+bot.b)/3>.05f ? 0:1;
 }
};
Module_IMPL(GpuProbe,new GpuProbe());
void GpuProbe::expose(ssq::Table& table){auto cls=table.addClass(name,GpuProbe::create,false);expose(cls);}
void GpuProbe::expose(ssq::Class& cls){cls.addFunc("run",&GpuProbe::run);}
}
EVE_PLUGIN_EXPORT int eve_plugin_init(){return 0;}
