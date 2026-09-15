#include "procgen/GtsTerrainLod.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace eve::procgen {
namespace {
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, message, "procgen.gtsTerrainLod"));
}
}

Result<void> buildGtsTerrainBaseMesh(MeshBuild&output,const Heightmap&heightmap,
 GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ){
 const int exponent=static_cast<int>(resolution);
 const int sourceWidth=heightmap.getWidth(),sourceHeight=heightmap.getHeight();
 if(exponent<0||exponent>4||sourceWidth<2||sourceHeight<2||!std::isfinite(sizeX)||sizeX<=0.f||
    !std::isfinite(sizeY)||sizeY<=0.f||!std::isfinite(sizeZ)||sizeZ<=0.f)
  return invalid<void>("GTS terrain heightmap or export dimensions are invalid");
 const int step=1<<exponent;
 if((sourceWidth-1)%step!=0||(sourceHeight-1)%step!=0)
  return invalid<void>("GTS terrain heightmap intervals must be divisible by the save-resolution stride");
 const int columns=(sourceWidth-1)/step+1,rows=(sourceHeight-1)/step+1;
 const std::uint64_t vertexCount=std::uint64_t(columns)*std::uint64_t(rows);
 const std::uint64_t indexCount=std::uint64_t(columns-1)*std::uint64_t(rows-1)*6u;
 if(vertexCount>8u*1024u*1024u||indexCount>48u*1024u*1024u||
    vertexCount>std::uint64_t(std::numeric_limits<int>::max())||indexCount>std::uint64_t(std::numeric_limits<int>::max()))
  return invalid<void>("GTS terrain base mesh exceeds the native export budget");
 for(float value:heightmap.data())if(!std::isfinite(value))return invalid<void>("GTS terrain heightmap contains a non-finite sample");
 MeshBuild candidate;candidate.reserve(static_cast<int>(vertexCount),static_cast<int>(indexCount));candidate.setActiveGroup("terrain");
 for(int z=0;z<rows;++z)for(int x=0;x<columns;++x){const int gx=x*step,gz=z*step;
  candidate.addVertex(sizeX*float(gx)/float(sourceWidth-1),heightmap.height(gx,gz)*sizeY,
   sizeZ*float(gz)/float(sourceHeight-1),0.f,0.f,0.f,float(gx)/float(sourceWidth-1),float(gz)/float(sourceHeight-1));}
 for(int z=0;z+1<rows;++z)for(int x=0;x+1<columns;++x){const std::uint32_t a=std::uint32_t(z*columns+x),b=a+1,c=a+std::uint32_t(columns),d=c+1;
  candidate.addTriangle(a,c,b);candidate.addTriangle(b,c,d);}
 auto&normals=candidate.normals();const auto&positions=candidate.positions();const auto&indices=candidate.indices();
 for(size_t i=0;i<indices.size();i+=3){const auto a=indices[i],b=indices[i+1],c=indices[i+2];
  const float abx=positions[b*3]-positions[a*3],aby=positions[b*3+1]-positions[a*3+1],abz=positions[b*3+2]-positions[a*3+2];
  const float acx=positions[c*3]-positions[a*3],acy=positions[c*3+1]-positions[a*3+1],acz=positions[c*3+2]-positions[a*3+2];
  const float nx=aby*acz-abz*acy,ny=abz*acx-abx*acz,nz=abx*acy-aby*acx;
  for(auto vertex:{a,b,c}){normals[vertex*3]+=nx;normals[vertex*3+1]+=ny;normals[vertex*3+2]+=nz;}}
 for(size_t i=0;i<normals.size();i+=3){const float length=std::sqrt(normals[i]*normals[i]+normals[i+1]*normals[i+1]+normals[i+2]*normals[i+2]);
  if(!(length>0.f)||!std::isfinite(length))return invalid<void>("GTS terrain base mesh contains a degenerate vertex normal");
  normals[i]/=length;normals[i+1]/=length;normals[i+2]/=length;}
 candidate.setMeta("kind","gts.terrain.base");candidate.setMeta("saveResolution",std::to_string(exponent));
 candidate.setMeta("sourceWidth",std::to_string(sourceWidth));candidate.setMeta("sourceHeight",std::to_string(sourceHeight));
 output=std::move(candidate);return Result<void>::success();
}

const GtsTerrainLodTile* GtsTerrainLodSet::tileAt(int index) const {
    return index >= 0 && index < static_cast<int>(tiles_.size()) ? &tiles_[index] : nullptr;
}

const GtsTerrainLodLevelSettings* GtsTerrainLodSet::levelAt(int index) const {
    return index >= 0 && index < static_cast<int>(settings_.size()) ? &settings_[index] : nullptr;
}

Result<void> GtsTerrainMeshSettings::setSaveResolution(int value){if(value<0||value>4)return invalid<void>("GTS save resolution must be in the range 0 through 4");saveResolution_=value;return Result<void>::success();}
Result<void> GtsTerrainMeshSettings::setLodCount(int value){if(value<1||value>4)return invalid<void>("GTS LOD count must be in the range 1 through 4");lodCount_=value;return Result<void>::success();}
Result<void> GtsTerrainMeshSettings::setSubTiles(int value){if(value<0||value>5)return invalid<void>("GTS sub-tile split count must be in the range 0 through 5");subTiles_=value;return Result<void>::success();}
float GtsTerrainMeshSettings::getLodQuality(int index)const noexcept{return index>=0&&index<4?lodQuality_[index]:-1.f;}
Result<void> GtsTerrainMeshSettings::setLodQuality(int index,float percent){if(index<0||index>=4||!std::isfinite(percent)||percent<0.f||percent>100.f)return invalid<void>("GTS LOD quality requires a slot from 0 through 3 and percentage from 0 through 100");lodQuality_[index]=percent;return Result<void>::success();}
float GtsTerrainMeshSettings::getLodTransitionHeight(int index)const noexcept{if(index<0||index>=lodCount_)return -1.f;constexpr float transitions[]{.95f,.7f,.6f,.02f};return index==lodCount_-1?.02f:transitions[index];}
Result<std::vector<GtsTerrainLodLevelSettings>> GtsTerrainMeshSettings::compileLevels()const{
 std::vector<GtsTerrainLodLevelSettings>levels;levels.reserve(size_t(lodCount_));for(int i=0;i<lodCount_;++i){GtsTerrainLodLevelSettings level;level.quality=lodQuality_[i]/100.f;level.screenRelativeTransitionHeight=getLodTransitionHeight(i);levels.push_back(level);}return Result<std::vector<GtsTerrainLodLevelSettings>>::success(std::move(levels));
}

int GtsTerrainLodSet::selectLevel(float relativeHeight) const {
    if (!std::isfinite(relativeHeight) || relativeHeight < 0.0f || settings_.empty()) return -2;
    for (int i = 0; i < static_cast<int>(settings_.size()); ++i)
        if (relativeHeight >= settings_[i].screenRelativeTransitionHeight) return i;
    return -1;
}

float GtsTerrainLodSet::getLevelSwitchDistance(int level, float worldDiameter, float verticalFovDegrees) const {
    if (level < 0 || level >= static_cast<int>(settings_.size()) || !std::isfinite(worldDiameter) ||
        worldDiameter <= 0.0f || !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.0f ||
        verticalFovDegrees >= 180.0f) return -1.0f;
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    const float tangent = std::tan(verticalFovDegrees * 0.5f * radiansPerDegree);
    return worldDiameter /
           (2.0f * tangent * settings_[level].screenRelativeTransitionHeight);
}

int GtsTerrainLodSet::selectLevelForCamera(float distance, float worldDiameter, float verticalFovDegrees) const {
    if (!std::isfinite(distance) || distance <= 0.0f || !std::isfinite(worldDiameter) ||
        worldDiameter <= 0.0f || !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.0f ||
        verticalFovDegrees >= 180.0f) return -2;
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    const float relativeHeight = worldDiameter /
        (2.0f * distance * std::tan(verticalFovDegrees * 0.5f * radiansPerDegree));
    return selectLevel(relativeHeight);
}

std::vector<GtsTerrainLodLevelSettings> defaultGtsTerrainLodLevels() {
    std::vector<GtsTerrainLodLevelSettings> levels(4);
    levels[0].quality = 1.0f;
    levels[1].quality = 0.5f;
    levels[2].quality = 0.25f;
    levels[3].quality = 0.125f;
    levels[0].screenRelativeTransitionHeight = 0.95f;
    levels[1].screenRelativeTransitionHeight = 0.7f;
    levels[2].screenRelativeTransitionHeight = 0.6f;
    levels[3].screenRelativeTransitionHeight = 0.02f;
    return levels;
}

Result<std::vector<GtsTerrainLodAssetEntry>> planGtsTerrainLodAssets(
    const GtsTerrainLodSet&lods,const std::string&terrainName,const std::string&meshFolder){
    auto safe=[](const std::string&v,bool folder){if(v.empty()||v=="."||v==".."||v.find("..")!=std::string::npos)return false;
        constexpr std::string_view invalid="\\:*?\"<>|";if(v.find_first_of(invalid)!=std::string::npos)return false;
        return folder||v.find('/')==std::string::npos;};
    if(!safe(terrainName,false)||!safe(meshFolder,true)||meshFolder.front()=='/'||meshFolder.back()=='/')
        return invalid<std::vector<GtsTerrainLodAssetEntry>>("terrain name or mesh folder is not a safe relative export name");
    std::vector<GtsTerrainLodAssetEntry>entries;
    for(int tileIndex=0;tileIndex<lods.getTileCount();++tileIndex){auto*tile=lods.tileAt(tileIndex);if(!tile)continue;
        for(int level=0;level<static_cast<int>(tile->levels.size());++level){if(tile->levels[size_t(level)].empty())continue;
            GtsTerrainLodAssetEntry e;e.tileIndex=tileIndex;e.levelIndex=level;
            e.objectName=terrainName+" SubTile"+std::to_string(tileIndex)+"_LOD_"+std::to_string(level);
            e.meshName=terrainName+"_SubTile_"+std::to_string(tileIndex)+std::to_string(level);
            e.relativePath=meshFolder+"/"+e.meshName+".eve.mesh.json";entries.push_back(std::move(e));}}
    return Result<std::vector<GtsTerrainLodAssetEntry>>::success(std::move(entries));
}
int GtsTerrainLodAssetPlan::getEntryCount()const{return static_cast<int>(entries_.size());}
int GtsTerrainLodAssetPlan::getTileIndex(int i)const{return i>=0&&i<int(entries_.size())?entries_[size_t(i)].tileIndex:-1;}
int GtsTerrainLodAssetPlan::getLevelIndex(int i)const{return i>=0&&i<int(entries_.size())?entries_[size_t(i)].levelIndex:-1;}
std::string GtsTerrainLodAssetPlan::getObjectName(int i)const{return i>=0&&i<int(entries_.size())?entries_[size_t(i)].objectName:std::string{};}
std::string GtsTerrainLodAssetPlan::getMeshName(int i)const{return i>=0&&i<int(entries_.size())?entries_[size_t(i)].meshName:std::string{};}
std::string GtsTerrainLodAssetPlan::getRelativePath(int i)const{return i>=0&&i<int(entries_.size())?entries_[size_t(i)].relativePath:std::string{};}
Result<void> planGtsTerrainLodAssetsInto(GtsTerrainLodAssetPlan&output,const GtsTerrainLodSet&lods,const std::string&name,const std::string&folder){auto plan=planGtsTerrainLodAssets(lods,name,folder);if(!plan)return Result<void>::failure(*plan.error());GtsTerrainLodAssetPlan candidate;candidate.entries_=std::move(plan).takeValue();output=std::move(candidate);return Result<void>::success();}

Result<GtsTerrainLodSet> buildGtsTerrainLods(
    const MeshBuild& source, int xSplits, int zSplits, GtsMeshPivot pivot,
    const std::vector<GtsTerrainLodLevelSettings>& levels) {
    if (levels.empty()) return invalid<GtsTerrainLodSet>("at least one terrain LOD level is required");
    float previousTransition = 1.0f;
    for (const auto& level : levels) {
        if (!std::isfinite(level.quality) || level.quality < 0.0f || level.quality > 1.0f ||
            !std::isfinite(level.screenRelativeTransitionHeight) ||
            level.screenRelativeTransitionHeight < 0.0f || level.screenRelativeTransitionHeight > 1.0f ||
            level.screenRelativeTransitionHeight >= previousTransition) {
            return invalid<GtsTerrainLodSet>("LOD quality or transition sequence is invalid");
        }
        previousTransition = level.screenRelativeTransitionHeight;
    }

    auto split = splitGtsMesh(source, xSplits, zSplits, pivot);
    if (!split) return Result<GtsTerrainLodSet>::failure(*split.error());

    GtsTerrainLodSet result;
    result.columns_ = split.value().getColumnCount();
    result.rows_ = split.value().getRowCount();
    result.settings_ = levels;
    result.tiles_.reserve(split.value().getTileCount());
    for (int tileIndex = 0; tileIndex < split.value().getTileCount(); ++tileIndex) {
        const auto* sourceTile = split.value().tileAt(tileIndex);
        GtsTerrainLodTile tile;
        tile.offsetX = sourceTile->offsetX;
        tile.offsetZ = sourceTile->offsetZ;
        tile.levels.reserve(levels.size());
        MeshBuild previous = sourceTile->mesh;
        for (const auto& level : levels) {
            if (previous.empty()) {
                tile.levels.emplace_back();
                continue;
            }
            MeshBuild simplified;
            auto simplification = simplifyGtsMesh(simplified, previous, level.quality, level.simplification);
            if (!simplification) return Result<GtsTerrainLodSet>::failure(*simplification.error());
            tile.levels.push_back(simplified);
            previous = std::move(simplified);
        }
        result.tiles_.push_back(std::move(tile));
    }
    return Result<GtsTerrainLodSet>::success(std::move(result));
}

Result<GtsTerrainLodSet> buildGtsTerrainLodsFromHeightmap(const Heightmap&heightmap,
 GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,int subTileSplits,
 GtsMeshPivot pivot,const std::vector<GtsTerrainLodLevelSettings>&levels){
 if(subTileSplits<0||subTileSplits>5)return invalid<GtsTerrainLodSet>("GTS sub-tile split count must be in the profile range 0 through 5");
 MeshBuild base;auto built=buildGtsTerrainBaseMesh(base,heightmap,resolution,sizeX,sizeY,sizeZ);
 if(!built)return Result<GtsTerrainLodSet>::failure(*built.error());
 return buildGtsTerrainLods(base,subTileSplits,subTileSplits,pivot,levels);
}

Result<void> buildDefaultGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet&output,const Heightmap&heightmap,
 GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,int subTileSplits,GtsMeshPivot pivot){
 auto built=buildGtsTerrainLodsFromHeightmap(heightmap,resolution,sizeX,sizeY,sizeZ,subTileSplits,pivot,defaultGtsTerrainLodLevels());
 if(!built)return Result<void>::failure(*built.error());
 output=std::move(built).takeValue();
 return Result<void>::success();
}

Result<void> buildGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet&output,const Heightmap&heightmap,
 const GtsTerrainMeshSettings&settings,float sizeX,float sizeY,float sizeZ,GtsMeshPivot pivot){
 auto levels=settings.compileLevels();if(!levels)return Result<void>::failure(*levels.error());
 auto built=buildGtsTerrainLodsFromHeightmap(heightmap,static_cast<GtsTerrainSaveResolution>(settings.getSaveResolution()),
  sizeX,sizeY,sizeZ,settings.getSubTiles(),pivot,levels.value());if(!built)return Result<void>::failure(*built.error());
 output=std::move(built).takeValue();return Result<void>::success();
}

}
