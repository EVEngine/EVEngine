#include "procgen/algorithms/CastleMesh.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Builder {
    MeshBuild &mesh;
    float uv = 0.5f;

    void quad(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy,
              float cz, float dx, float dy, float dz, float nx, float ny, float nz) {
        const uint32_t b = uint32_t(mesh.getVertexCount());
        mesh.addVertex(ax, ay, az, nx, ny, nz, 0, 0);
        mesh.addVertex(bx, by, bz, nx, ny, nz, std::hypot(bx-ax, bz-az)*uv, 0);
        mesh.addVertex(cx, cy, cz, nx, ny, nz, std::hypot(cx-dx, cz-dz)*uv,
                       std::fabs(cy-by)*uv);
        mesh.addVertex(dx, dy, dz, nx, ny, nz, 0, std::fabs(dy-ay)*uv);
        mesh.addTriangle(b, b+1, b+2); mesh.addTriangle(b, b+2, b+3);
    }

    void box(float x0, float y0, float z0, float x1, float y1, float z1) {
        if (x1 <= x0 || y1 <= y0 || z1 <= z0) return;
        quad(x1,y0,z0, x1,y1,z0, x1,y1,z1, x1,y0,z1, 1,0,0);
        quad(x0,y0,z1, x0,y1,z1, x0,y1,z0, x0,y0,z0, -1,0,0);
        quad(x0,y1,z0, x0,y1,z1, x1,y1,z1, x1,y1,z0, 0,1,0);
        quad(x0,y0,z1, x0,y0,z0, x1,y0,z0, x1,y0,z1, 0,-1,0);
        quad(x1,y0,z1, x1,y1,z1, x0,y1,z1, x0,y0,z1, 0,0,1);
        quad(x0,y0,z0, x0,y1,z0, x1,y1,z0, x1,y0,z0, 0,0,-1);
    }

    void cylinder(float cx, float y0, float cz, float radius, float height, int sides) {
        const float y1 = y0 + height;
        for (int i=0; i<sides; ++i) {
            float a0=2*kPi*float(i)/float(sides), a1=2*kPi*float(i+1)/float(sides);
            float x0=cx+std::cos(a0)*radius, z0=cz+std::sin(a0)*radius;
            float x1=cx+std::cos(a1)*radius, z1=cz+std::sin(a1)*radius;
            float am=(a0+a1)*0.5f;
            quad(x0,y0,z0, x1,y0,z1, x1,y1,z1, x0,y1,z0,
                 std::cos(am),0,std::sin(am));
            quad(cx,y1,cz, x0,y1,z0, x1,y1,z1, cx,y1,cz, 0,1,0);
        }
    }
};

void crenelsX(Builder &b, float x0, float x1, float y, float z, float depth,
              float merlonW, float merlonH) {
    int n=std::max(2,int(std::floor((x1-x0)/merlonW)));
    float pitch=(x1-x0)/float(n);
    for (int i=0;i<n;i+=2) b.box(x0+i*pitch,y,z-depth*.5f,x0+(i+1)*pitch,y+merlonH,z+depth*.5f);
}

void crenelsZ(Builder &b, float z0, float z1, float y, float x, float depth,
              float merlonW, float merlonH) {
    int n=std::max(2,int(std::floor((z1-z0)/merlonW)));
    float pitch=(z1-z0)/float(n);
    for (int i=0;i<n;i+=2) b.box(x-depth*.5f,y,z0+i*pitch,x+depth*.5f,y+merlonH,z0+(i+1)*pitch);
}

void stairX(Builder &b, float x0, float y0, float z, float run, float rise, float width,
            float stepHeight, bool reverse=false) {
    int steps=std::max(2,int(std::ceil(rise/stepHeight)));
    float dx=run/float(steps), dy=rise/float(steps);
    for(int i=0;i<steps;++i) {
        // Both directions occupy the same [x0, x0 + run] footprint; reverse
        // changes only which end reaches the upper landing.
        float xa=reverse ? x0+(steps-1-i)*dx : x0+i*dx;
        b.box(xa,y0,z-width*.5f,xa+dx,y0+(i+1)*dy,z+width*.5f);
    }
}

void tower(Builder &b,float x,float z,float radius,float height,int sides,float merlonW,
           int detail,int &merlons) {
    b.cylinder(x,0,z,radius,height,sides);
    if(detail>0) {
        int n=std::max(6,int(std::floor(2*kPi*radius/merlonW)));
        for(int i=0;i<n;i+=2) {
            float a=2*kPi*(float(i)+.5f)/float(n), tang=2*kPi*radius/float(n)*.42f;
            float cx=x+std::cos(a)*(radius-tang*.35f), cz=z+std::sin(a)*(radius-tang*.35f);
            b.box(cx-tang*.5f,height,cz-tang*.5f,cx+tang*.5f,height+radius*.3f,cz+tang*.5f);
            ++merlons;
        }
    }
}

} // namespace

bool generateCastleMesh(const Params &p, MeshBuild &out, std::string &error) {
    const float width=std::max(16.f,p.getFloat("width",42.f));
    const float depth=std::max(16.f,p.getFloat("depth",36.f));
    const int rings=std::clamp(p.getInt("rings",2),1,4);
    const float wallH=std::max(2.f,p.getFloat("wallHeight",6.f));
    const float wallT=std::clamp(p.getFloat("wallThickness",1.8f),.5f,5.f);
    const float towerR=std::max(wallT,p.getFloat("towerRadius",3.3f));
    const float towerH=std::max(wallH+1.f,p.getFloat("towerHeight",9.f));
    const int sides=std::clamp(p.getInt("towerSides",12),6,32);
    const float spacing=std::max(8.f,p.getFloat("towerSpacing",16.f));
    const float gateW=std::clamp(p.getFloat("gateWidth",5.f),2.f,width*.3f);
    const float merlonW=std::max(.35f,p.getFloat("merlonWidth",1.25f));
    const float stairW=std::max(1.f,p.getFloat("stairWidth",2.f));
    const float stepH=std::clamp(p.getFloat("stepHeight",.28f),.12f,.5f);
    const int keepFloors=std::clamp(p.getInt("keepFloors",3),1,8);
    const int detail=std::clamp(p.getInt("detail",2),0,2);
    const float ringInset=std::max(wallT*2.5f,p.getFloat("ringInset",std::min(width,depth)*.13f));
    const float ringHeightStep=std::clamp(p.getFloat("ringHeightStep",.22f),-.25f,.75f);
    const float towerHeightStep=std::clamp(p.getFloat("towerHeightStep",.14f),-.25f,.75f);
    const float floorH=std::max(2.f,p.getFloat("floorHeight",3.2f));
    const float keepW=std::clamp(p.getFloat("keepWidth",width*.25f),6.f,width*.55f);
    const float keepD=std::clamp(p.getFloat("keepDepth",depth*.24f),6.f,depth*.55f);
    const int courtyardBuildings=std::clamp(p.getInt("courtyardBuildings",3),0,12);
    if(width <= 2*towerR+2*wallT || depth <= 2*towerR+2*wallT) {
        error="mesh.castle: width/depth too small for towerRadius and wallThickness"; return false;
    }
    out.clear(); out.reserve(40000,120000); Builder b{out,std::max(0.f,p.getFloat("uvRepeat",.5f))};
    std::mt19937 rng(p.getSeed());
    int towers=0,walls=0,stairs=0,merlons=0,actualRings=0;
    for(int ring=0;ring<rings;++ring) {
        float inset=float(ring)*ringInset;
        float hx=width*.5f-inset, hz=depth*.5f-inset;
        if(hx<towerR*1.8f || hz<towerR*1.8f) break;
        ++actualRings;
        float h=wallH*(1.f+ringHeightStep*float(ring));
        // South wall is split around a real gate opening.
        b.mesh.setActiveGroup("walls");
        b.box(-hx,0,-hz,-gateW*.5f,h,-hz+wallT); b.box(gateW*.5f,0,-hz,hx,h,-hz+wallT);
        b.box(-hx,0,hz-wallT,hx,h,hz); b.box(-hx,0,-hz+wallT,-hx+wallT,h,hz-wallT);
        b.box(hx-wallT,0,-hz+wallT,hx,h,hz-wallT); walls+=4;
        if(detail>0) {
            b.mesh.setActiveGroup("battlements");
            crenelsX(b,-hx,hx,h,-hz,wallT,merlonW,wallT*.7f);
            crenelsX(b,-hx,hx,h,hz,wallT,merlonW,wallT*.7f);
            crenelsZ(b,-hz,hz,h,-hx,wallT,merlonW,wallT*.7f);
            crenelsZ(b,-hz,hz,h,hx,wallT,merlonW,wallT*.7f);
        }
        float tr=towerR*(1.f-.12f*float(ring)), th=towerH*(1.f+towerHeightStep*float(ring));
        b.mesh.setActiveGroup("towers");
        for(float sx:{-1.f,1.f}) for(float sz:{-1.f,1.f}) {
            tower(b,sx*hx,sz*hz,tr,th,sides,merlonW,detail,merlons); ++towers;
        }
        // Interval towers make large castles read as defensible rather than four-corner boxes.
        if(detail>1) {
            int nx=std::max(0,int(std::floor(2*hx/spacing))-1);
            int nz=std::max(0,int(std::floor(2*hz/spacing))-1);
            for(int i=1;i<=nx;++i){float x=-hx+2*hx*i/float(nx+1); tower(b,x,hz,tr*.78f,th*.88f,sides,merlonW,detail,merlons); ++towers;}
            for(int i=1;i<=nz;++i){float z=-hz+2*hz*i/float(nz+1); tower(b,hx,z,tr*.78f,th*.88f,sides,merlonW,detail,merlons); ++towers;}
        }
        // Gatehouse towers and lintel preserve the opening below.
        const float gateTowerR=tr*.72f;
        const float gateTowerX=gateW*.5f+gateTowerR*.72f;
        b.mesh.setActiveGroup("gatehouses");
        tower(b,-gateTowerX,-hz,gateTowerR,th*.9f,sides,merlonW,detail,merlons);
        tower(b, gateTowerX,-hz,gateTowerR,th*.9f,sides,merlonW,detail,merlons); towers+=2;
        b.box(-gateW*.5f,h*.62f,-hz-wallT*.2f,gateW*.5f,h,-hz+wallT*1.2f);
        // Each ring has a physical flight onto its wall walk.
        b.mesh.setActiveGroup("stairs");
        stairX(b, -hx + wallT, 0.f, -hz + wallT + stairW * .55f,
               h * 1.35f, h, stairW, stepH);
        b.box(-hx+wallT+h*1.35f-stairW*.5f,h-wallT*.25f,-hz+wallT,
              -hx+wallT+h*1.35f+stairW*.5f,h,-hz+wallT+stairW);
        ++stairs;
    }
    // Central keep: stacked floors with setbacks, battlements, and an external stair.
    float kw=keepW,kd=keepD;
    b.mesh.setActiveGroup("keep");
    for(int f=0;f<keepFloors;++f) {
        float setback=float(f)*.35f, y=float(f)*floorH;
        b.box(-kw*.5f+setback,y,-kd*.5f+setback,kw*.5f-setback,y+floorH,kd*.5f-setback);
        if(f+1<keepFloors){
            b.mesh.setActiveGroup("stairs");
            stairX(b,-kw*.5f+setback,y,-kd*.5f-stairW*.55f,kw-2*setback,floorH,stairW,stepH,(f%2)==1);
            b.mesh.setActiveGroup("keep");
            ++stairs;
        }
    }
    float ky=keepFloors*floorH;
    crenelsX(b,-kw*.5f,kw*.5f,ky,-kd*.5f,wallT*.55f,merlonW*.8f,wallT*.65f);
    crenelsX(b,-kw*.5f,kw*.5f,ky,kd*.5f,wallT*.55f,merlonW*.8f,wallT*.65f);
    crenelsZ(b,-kd*.5f,kd*.5f,ky,-kw*.5f,wallT*.55f,merlonW*.8f,wallT*.65f);
    crenelsZ(b,-kd*.5f,kd*.5f,ky,kw*.5f,wallT*.55f,merlonW*.8f,wallT*.65f);
    // A continuous raised threshold makes the aligned passages through all
    // rings readable and guarantees a walkable route from outside to the keep.
    b.mesh.setActiveGroup("gatehouses");
    b.box(-gateW*.38f, .02f, -depth*.5f-wallT*2.5f,
          gateW*.38f, .18f, -kd*.5f);
    // Seeded courtyard buildings add controlled variation without changing topology.
    b.mesh.setActiveGroup("courtyard");
    if(detail>1) for(int i=0;i<courtyardBuildings;++i) {
        std::uniform_real_distribution<float> jitter(-1.f,1.f);
        const float lane=float(i)-float(courtyardBuildings-1)*.5f;
        float x=lane*kw*.72f+jitter(rng), z=kd*.85f+jitter(rng);
        b.box(x-2,0,z-1.8f,x+2,2.6f+float(i%3)*.35f,z+1.8f);
    }
    float scale=std::max(.01f,p.getFloat("scale",1.f));
    if(scale!=1.f) for(float &v:out.positions()) v*=scale;
    if(out.empty()){error="mesh.castle: generated empty mesh";return false;}
    out.setMeta("algorithm","mesh.castle"); out.setMeta("rings",std::to_string(actualRings));
    out.setMeta("wallSections",std::to_string(walls)); out.setMeta("towerCount",std::to_string(towers));
    out.setMeta("stairFlights",std::to_string(stairs)); out.setMeta("keepFloors",std::to_string(keepFloors));
    out.setMeta("merlonCount",std::to_string(merlons)); out.setMeta("seed",std::to_string(p.getSeed()));
    out.setMeta("detail",std::to_string(detail));
    out.setMeta("courtyardBuildings",std::to_string(detail>1?courtyardBuildings:0));
    return true;
}

void registerCastleMeshRecipe(MeshRecipeRegistry &r) {
    RecipeDescriptor schema{"mesh.castle", "Castle", "Mesh", {}};
    schema.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    auto addFloat = [&](const char *key, const char *label, float value, float minimum,
                        float maximum, float step, const char *description) {
        auto param = ParamDescriptor::floating(key, label, value, minimum, maximum, step);
        param.description = description;
        schema.params.push_back(std::move(param));
    };
    auto addInt = [&](const char *key, const char *label, int value, int minimum,
                      int maximum, const char *description) {
        auto param = ParamDescriptor::integer(key, label, value, minimum, maximum);
        param.description = description;
        schema.params.push_back(std::move(param));
    };
    addFloat("width", "Width", 42.f, 16.f, 1000.f, .5f, "Outer east-west extent");
    addFloat("depth", "Depth", 36.f, 16.f, 1000.f, .5f, "Outer north-south extent");
    addInt("rings", "Wall Rings", 2, 1, 4, "Concentric defensive wall tiers");
    addFloat("ringInset", "Ring Inset", 5.f, .5f, 100.f, .1f, "Distance between wall tiers");
    addFloat("ringHeightStep", "Ring Height Step", .22f, -.25f, .75f, .01f, "Height multiplier added per inner tier");
    addFloat("wallHeight", "Wall Height", 6.f, 2.f, 100.f, .1f, "Outer wall-walk height");
    addFloat("wallThickness", "Wall Thickness", 1.8f, .5f, 5.f, .1f, "Wall body and walk thickness");
    addFloat("towerRadius", "Tower Radius", 3.3f, .5f, 100.f, .1f, "Corner tower radius");
    addFloat("towerHeight", "Tower Height", 9.f, 3.f, 200.f, .1f, "Outer tower height");
    addFloat("towerHeightStep", "Tower Height Step", .14f, -.25f, .75f, .01f, "Height multiplier added per inner tier");
    addInt("towerSides", "Tower Sides", 12, 6, 32, "Radial tower tessellation");
    addFloat("towerSpacing", "Tower Spacing", 16.f, 8.f, 200.f, .5f, "Maximum interval-tower spacing");
    addFloat("gateWidth", "Gate Width", 5.f, 2.f, 100.f, .1f, "Aligned gate passage width");
    addFloat("merlonWidth", "Merlon Width", 1.25f, .35f, 20.f, .05f, "Battlement unit width");
    addFloat("keepWidth", "Keep Width", 10.5f, 6.f, 500.f, .5f, "Central keep width");
    addFloat("keepDepth", "Keep Depth", 8.64f, 6.f, 500.f, .5f, "Central keep depth");
    addInt("keepFloors", "Keep Floors", 3, 1, 8, "Number of accessible keep tiers");
    addFloat("floorHeight", "Floor Height", 3.2f, 2.f, 50.f, .1f, "Keep floor-to-floor height");
    addFloat("stairWidth", "Stair Width", 2.f, 1.f, 50.f, .1f, "Physical stair-flight width");
    addFloat("stepHeight", "Step Height", .28f, .12f, .5f, .01f, "Physical riser height");
    addInt("courtyardBuildings", "Courtyard Buildings", 3, 0, 12, "Seeded courtyard building count");
    addInt("detail", "Detail", 2, 0, 2, "0 structural, 1 battlements, 2 full");
    addFloat("uvRepeat", "UV Repeat", .5f, 0.f, 100.f, .05f, "Texture repeats per world unit");
    addFloat("scale", "Scale", 1.f, .01f, 100.f, .01f, "Uniform output scale");
    r.registerRecipe(std::move(schema), generateCastleMesh);
}

} // namespace eve::procgen
