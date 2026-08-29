#include "virtualgeometry/VirtualGeometryBackend.h"

#include "gpgpu/GpuBuffer.h"
#include "gpgpu/webgpu/WebGpuGpgpu.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace eve::virtualgeometry {
namespace webgpu {

struct VgState {
    gpgpu::WebGpuComputeShader* cull      = nullptr;
    gpgpu::WebGpuComputeShader* raster    = nullptr;
    gpgpu::GpuBuffer*           positions = nullptr;
    gpgpu::GpuBuffer*           triangles = nullptr;
    gpgpu::GpuBuffer*           clusters  = nullptr;
    gpgpu::GpuBuffer*           visible   = nullptr;
    gpgpu::GpuBuffer*           pixels    = nullptr;
    gpgpu::GpuBuffer*           uniforms  = nullptr;
    gpgpu::GpuBuffer*           stats     = nullptr;
    int                         viewW = 1, viewH = 1, pixelCapacity = 0, visibleCapacity = 0, lastVisible = 0;
};

namespace {

constexpr const char* kCullWgsl = R"wgsl(
struct U { viewProj: mat4x4f, model: mat4x4f, cameraPos: vec4f, params: vec4f,
           frustum: array<vec4f, 6>, misc: vec4f };
struct Visible { counter: atomic<u32>, ids: array<u32> };
@group(0) @binding(2) var<storage, read_write> cl: array<vec4u>;
@group(0) @binding(3) var<storage, read_write> vis: Visible;
@group(0) @binding(5) var<storage, read_write> u: U;
@group(0) @binding(6) var<storage, read_write> stats: array<atomic<u32>>;
fn center(id:u32)->vec3f { let v=cl[id*4u]; return vec3f(bitcast<f32>(v.x),bitcast<f32>(v.y),bitcast<f32>(v.z)); }
fn err(id:u32)->f32 { return bitcast<f32>(cl[id*4u+2u].y); }
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid:vec3u) {
 let id=gid.x; if(id>=u32(u.misc.x)){return;}
 let w4=u.model*vec4f(center(id),1); let w=w4.xyz/max(w4.w,1e-6);
 let sc=max(length(u.model[0].xyz),max(length(u.model[1].xyz),length(u.model[2].xyz)));
 let r=bitcast<f32>(cl[id*4u].w)*sc;
 for(var i=0u;i<6u;i=i+1u){let p=u.frustum[i];if(dot(p.xyz,w)+p.w < -r){return;}}
 let d=max(length(w-u.cameraPos.xyz),1e-4); let ec=err(id)/d;
 let parent=cl[id*4u+1u].w; var ep=1e30;
 if(parent!=0xffffffffu){let pw=u.model*vec4f(center(parent),1);ep=err(parent)/max(length(pw.xyz-u.cameraPos.xyz),1e-4);}
 let leaf=cl[id*4u+2u].z==0u;
 if(!((ec<=u.params.w||leaf)&&ep>u.params.w)){return;}
 let slot=atomicAdd(&vis.counter,1u);vis.ids[slot]=id;atomicAdd(&stats[0],1u);
})wgsl";

constexpr const char* kRasterWgsl = R"wgsl(
struct U { viewProj: mat4x4f, model: mat4x4f, cameraPos: vec4f, params: vec4f,
           frustum: array<vec4f, 6>, misc: vec4f };
struct Visible { counter: atomic<u32>, ids: array<u32> };
@group(0) @binding(0) var<storage,read_write> pos:array<u32>;
@group(0) @binding(1) var<storage,read_write> tri:array<u32>;
@group(0) @binding(2) var<storage,read_write> cl:array<vec4u>;
@group(0) @binding(3) var<storage,read_write> vis:Visible;
@group(0) @binding(4) var<storage,read_write> pix:array<atomic<u32>>;
@group(0) @binding(5) var<storage,read_write> u:U;
@group(0) @binding(6) var<storage,read_write> stats:array<atomic<u32>>;
fn p(i:u32)->vec3f{let b=i*3u;return vec3f(bitcast<f32>(pos[b]),bitcast<f32>(pos[b+1u]),bitcast<f32>(pos[b+2u]));}
@compute @workgroup_size(128)
fn main(@builtin(global_invocation_id) gid:vec3u){
 let slot=gid.x/124u;let t=gid.x%124u;if(slot>=atomicLoad(&vis.counter)){return;}
 let id=vis.ids[slot];let range=cl[id*4u+1u];if(t>=range.y){return;}
 let b=(range.x+t)*3u;let c0=u.viewProj*(u.model*vec4f(p(tri[b]),1));
 let c1=u.viewProj*(u.model*vec4f(p(tri[b+1u]),1));let c2=u.viewProj*(u.model*vec4f(p(tri[b+2u]),1));
 if(c0.w<=1e-6||c1.w<=1e-6||c2.w<=1e-6){return;}
 let s0=(c0.xy/c0.w*.5+vec2f(.5))*u.params.xy;let s1=(c1.xy/c1.w*.5+vec2f(.5))*u.params.xy;
 let s2=(c2.xy/c2.w*.5+vec2f(.5))*u.params.xy;
 let area=(s1.x-s0.x)*(s2.y-s0.y)-(s1.y-s0.y)*(s2.x-s0.x);if(area<=0){return;}
 let view=vec2i(u.params.xy);let lo=clamp(vec2i(floor(min(min(s0,s1),s2))),vec2i(0),view-vec2i(1));
 let hi=clamp(vec2i(ceil(max(max(s0,s1),s2))),vec2i(0),view-vec2i(1));let ia=1/area;
 for(var y=lo.y;y<=hi.y;y=y+1){for(var x=lo.x;x<=hi.x;x=x+1){let q=vec2f(f32(x)+.5,f32(y)+.5);
  let e0=(s1.x-s0.x)*(q.y-s0.y)-(s1.y-s0.y)*(q.x-s0.x);let e1=(s2.x-s1.x)*(q.y-s1.y)-(s2.y-s1.y)*(q.x-s1.x);
  let e2=(s0.x-s2.x)*(q.y-s2.y)-(s0.y-s2.y)*(q.x-s2.x);if(e0<0||e1<0||e2<0){continue;}
  let z=(e1*c0.z/c0.w+e2*c1.z/c1.w+e0*c2.z/c2.w)*ia;let d=u32(clamp(z,0,1)*65535);
  atomicMin(&pix[u32(y*view.x+x)],(min(d,0xffffu)<<16u)|(id&0xffffu));}}
 atomicAdd(&stats[4],1u);
})wgsl";

void bind(gpgpu::WebGpuComputeShader* s, VgState* v) {
    if (v->positions) s->bindBuffer(0, v->positions);
    if (v->triangles) s->bindBuffer(1, v->triangles);
    if (v->clusters) s->bindBuffer(2, v->clusters);
    if (v->visible) s->bindBuffer(3, v->visible);
    if (v->pixels) s->bindBuffer(4, v->pixels);
    if (v->uniforms) s->bindBuffer(5, v->uniforms);
    if (v->stats) s->bindBuffer(6, v->stats);
}
void pixels(VgState* s, int w, int h) {
    int n = std::max(1, w * h);
    if (n <= s->pixelCapacity) return;
    delete s->pixels;
    s->pixels        = gpgpu::webgpuNewBuffer(n * int(sizeof(uint32_t)), "storage");
    s->pixelCapacity = n;
    bind(s->cull, s);
    bind(s->raster, s);
}
std::vector<uint32_t> packed(const VirtualGeometryAsset& a) {
    std::vector<uint32_t> o;
    o.reserve(a.clusters.size() * 16);
    auto b = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };
    for (auto& c : a.clusters) {
        o.insert(o.end(), {b(c.cx), b(c.cy), b(c.cz), b(c.r), c.triStart, c.triCount, c.lodLevel, c.parent, b(c.errorR),
                           b(c.errorRScreen), c.childCount, 0});
        for (auto x : c.children) o.push_back(x);
    }
    return o;
}
}  // namespace
}  // namespace webgpu

void vgCreate(VgBackend& b) {
    auto* s = new webgpu::VgState();
    try {
        s->cull   = gpgpu::webgpuNewShaderFromWgsl(webgpu::kCullWgsl);
        s->raster = gpgpu::webgpuNewShaderFromWgsl(webgpu::kRasterWgsl);
        b.state   = s;
    } catch (...) {
        delete s->cull;
        delete s->raster;
        delete s;
        throw;
    }
}
void vgDestroy(VgBackend& b) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s) return;
    delete s->cull;
    delete s->raster;
    delete s->positions;
    delete s->triangles;
    delete s->clusters;
    delete s->visible;
    delete s->pixels;
    delete s->uniforms;
    delete s->stats;
    delete s;
    b.state = nullptr;
}
void vgUpload(VgBackend& b, const VirtualGeometryAsset& a) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s) return;
    delete s->positions;
    delete s->triangles;
    delete s->clusters;
    s->positions = gpgpu::webgpuNewBuffer(int(a.positions.size() * 4), "storage");
    s->positions->uploadBytes(a.positions.data(), a.positions.size() * 4);
    s->triangles = gpgpu::webgpuNewBuffer(int(a.triangles.size() * 4), "storage");
    s->triangles->uploadBytes(a.triangles.data(), a.triangles.size() * 4);
    auto p      = webgpu::packed(a);
    s->clusters = gpgpu::webgpuNewBuffer(int(p.size() * 4), "storage");
    s->clusters->uploadBytes(p.data(), p.size() * 4);
    if (!s->uniforms) s->uniforms = gpgpu::webgpuNewBuffer(int(sizeof(VgUniforms)), "storage");
    if (!s->visible) s->visible = gpgpu::webgpuNewBuffer((s->visibleCapacity + 1) * 4, "storage");
    if (!s->stats) s->stats = gpgpu::webgpuNewBuffer(32, "storage");
    webgpu::pixels(s, s->viewW, s->viewH);
    webgpu::bind(s->cull, s);
    webgpu::bind(s->raster, s);
}
void vgUploadUniforms(VgBackend& b, const VgUniforms& u) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s) return;
    if (!s->uniforms) s->uniforms = gpgpu::webgpuNewBuffer(int(sizeof(u)), "storage");
    s->uniforms->uploadBytes(&u, sizeof(u));
    s->cull->bindBuffer(5, s->uniforms);
    s->raster->bindBuffer(5, s->uniforms);
}
void vgReset(VgBackend& b, int n) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s || n <= 0) return;
    if (s->visibleCapacity == n && s->visible && s->stats) return;
    s->visibleCapacity = n;
    delete s->visible;
    delete s->stats;
    s->visible = gpgpu::webgpuNewBuffer((n + 1) * 4, "storage");
    s->stats   = gpgpu::webgpuNewBuffer(32, "storage");
    s->cull->bindBuffer(3, s->visible);
    s->cull->bindBuffer(6, s->stats);
    s->raster->bindBuffer(3, s->visible);
    s->raster->bindBuffer(6, s->stats);
}
int vgUpdate(VgBackend& b, int clusters, int capacity, int w, int h) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s || !s->visible) return 0;
    webgpu::pixels(s, w, h);
    s->viewW = w;
    s->viewH = h;
    s->visible->fillFloat32(0);
    s->stats->fillFloat32(0);
    std::vector<uint32_t> clear(size_t(w) * h, 0xffffffffu);
    s->pixels->uploadBytes(clear.data(), clear.size() * 4);
    gpgpu::webgpuDispatch(s->cull, (clusters + 63) / 64, 1, 1);
    gpgpu::webgpuDispatch(s->raster, (capacity * 124 + 127) / 128, 1, 1);
    uint32_t count = 0;
    s->visible->downloadBytes(&count, 4);
    s->lastVisible = int(std::min(count, uint32_t(capacity)));
    return s->lastVisible;
}
bool vgReadPixels(VgBackend& b, std::vector<uint32_t>& out) {
    auto* s = static_cast<webgpu::VgState*>(b.state);
    if (!s || !s->pixels) return false;
    out.resize(size_t(s->viewW) * s->viewH);
    s->pixels->downloadBytes(out.data(), out.size() * 4);
    return true;
}
}  // namespace eve::virtualgeometry
