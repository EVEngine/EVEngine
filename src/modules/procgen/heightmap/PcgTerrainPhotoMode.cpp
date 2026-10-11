#include "procgen/heightmap/PcgTerrainPhotoMode.h"
#include "common/Capability.h"
#include <cmath>

namespace eve::procgen {
PcgTerrainPhotoModeAuthority::~PcgTerrainPhotoModeAuthority(){if(authority_)cap::removeListener<IPhotoModeFieldSink>(this);}
void PcgTerrainPhotoModeAuthority::setAuthority(bool enabled){if(enabled==authority_)return;if(enabled)cap::addListener<IPhotoModeFieldSink>(this);else cap::removeListener<IPhotoModeFieldSink>(this);authority_=enabled;}
PhotoModeFieldAcceptance PcgTerrainPhotoModeAuthority::acceptsPhotoModeField(const PhotoModeAssignment&a)const noexcept{
 return a.domain==PhotoModeDomain::Terrain?PhotoModeFieldAcceptance::Accepted:PhotoModeFieldAcceptance::Rejected;
}
Result<void> PcgTerrainPhotoModeAuthority::applyPhotoModeField(const PhotoModeAssignment&a){
 auto fail=[&](const char*m){return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,a.field,{},"procgen.pcgTerrainPhotoMode"));};
 auto next=state_;auto f=std::get_if<float>(&a.value);auto b=std::get_if<bool>(&a.value);
 if(a.field=="m_drawInstanced"){if(!b)return fail("draw-instanced requires a bool");next.drawInstanced=*b;}
 else if(a.field=="m_terrainDetailDensity"){if(!f||!std::isfinite(*f)||*f<0||*f>1)return fail("detail density must be finite in [0,1]");next.detailDensity=*f;}
 else if(a.field=="m_terrainDetailDistance"){if(!f||!std::isfinite(*f)||*f<0)return fail("detail distance must be finite and non-negative");next.detailDistance=*f;}
 else if(a.field=="m_terrainPixelError"){if(!f||!std::isfinite(*f)||*f<=0)return fail("pixel error must be positive and finite");next.pixelError=*f;}
 else if(a.field=="m_terrainBasemapDistance"){if(!f||!std::isfinite(*f)||*f<0)return fail("basemap distance must be finite and non-negative");next.basemapDistance=*f;}
 else return fail("unsupported terrain photo-mode field");
 state_=next;return Result<void>::success();
}
int PcgTerrainPhotoModeAuthority::selectLod(const Heightmap&h,TerrainMeshSettings s,int maxLod,float distance,float viewportHeight,float fov)const{
 return TerrainLodSelector::select(h,s,maxLod,distance,viewportHeight,fov,state_.pixelError);
}
PcgTerrainTextureTier PcgTerrainPhotoModeAuthority::textureTier(float distance)const noexcept{
 return std::isfinite(distance)&&distance>=state_.basemapDistance?PcgTerrainTextureTier::Basemap:PcgTerrainTextureTier::Detailed;
}
}
