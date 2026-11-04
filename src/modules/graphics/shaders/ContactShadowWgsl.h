#pragma once

namespace eve::graphics::shaders {

inline constexpr const char* kContactShadowCommon = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f };
struct Externals { data: array<vec4f, 8> };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var normalTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
fn p(i: u32) -> f32 { return u.data[i / 4u][i % 4u]; }
fn inv_vp() -> mat4x4f { return mat4x4f(u.data[0], u.data[1], u.data[2], u.data[3]); }
fn depth(uv: vec2f) -> f32 { return textureSampleLevel(mainTex, mainSamp, clamp(uv, vec2f(0), vec2f(1)), 0).r; }
fn normal_load(uv:vec2f)->vec3f{let d=textureDimensions(normalTex);let q=vec2i(clamp(uv,vec2f(0),vec2f(1))*vec2f(d));return textureLoad(normalTex,clamp(q,vec2i(0),vec2i(d)-1),0).xyz;}
fn world(m: mat4x4f, uv: vec2f, z: f32) -> vec3f {
  let h = m * vec4f(uv * 2 - 1, clamp(z, 0, 1), 1);
  return h.xyz / max(h.w, 0.000001);
}
fn ndc_to_view_z(ndc: f32, near_z: f32, far_z: f32) -> f32 {
  return (near_z * far_z) / max(far_z - ndc * (far_z - near_z), 0.000001);
}
)wgsl";

inline constexpr const char* kContactShadow = R"wgsl(
@fragment fn fs_main(i: FSIn) -> @location(0) vec4f {
  let m = inv_vp();
  let near_z = max(p(16u), 0.001);
  let far_z = max(p(17u), near_z + 0.001);
  let ray_len = max(p(18u), 0.0001);
  let thickness = max(p(19u), 0.0001);
  let intensity = max(p(20u), 0.0);
  let steps = i32(clamp(p(21u), 4.0, 32.0));
  var light = vec3f(p(22u), p(23u), p(24u));
  let llen = length(light);
  light = select(vec3f(0.0, 1.0, 0.0), light / llen, llen > 0.001);
  let tw = max(p(25u), 0.00001);
  let th = max(p(26u), 0.00001);
  let has_normal = p(27u);
  let start_bias = max(p(28u), 0.0);
  let z = depth(i.uv);
  if (z >= 0.9999) { return vec4f(0.0); }
  let pos = world(m, i.uv, z);
  let eye_h = m * vec4f(0.0, 0.0, 0.0, 1.0);
  let cam = eye_h.xyz / max(eye_h.w, 0.000001);
  var n: vec3f;
  if (has_normal > 0.5) {
    let q = normal_load(i.uv) * 2.0 - 1.0;
    n = normalize(select(vec3f(0.0, 1.0, 0.0), q, length(q) > 0.001));
  } else {
    let pr = world(m, i.uv + vec2f(tw, 0.0), depth(i.uv + vec2f(tw, 0.0)));
    let pu = world(m, i.uv + vec2f(0.0, th), depth(i.uv + vec2f(0.0, th)));
    n = normalize(cross(pr - pos, pu - pos));
  }
  if (dot(n, cam - pos) < 0.0) { n = -n; }
  if (dot(n, light) <= 0.02) { return vec4f(0.0); }
  let view_proj = inverse(m);
  let origin = pos + n * start_bias + light * start_bias;
  var occlusion = 0.0;
  let step_size = ray_len / f32(steps);
  for (var s = 1; s <= steps; s = s + 1) {
    let sample_pos = origin + light * (step_size * f32(s));
    let clip = view_proj * vec4f(sample_pos, 1.0);
    if (abs(clip.w) < 0.000001) { continue; }
    let ndc = clip.xyz / clip.w;
    let uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { break; }
    let scene_z = depth(uv);
    if (scene_z >= 0.9999) { continue; }
    let scene_view = ndc_to_view_z(scene_z, near_z, far_z);
    let ray_view = ndc_to_view_z(clamp(ndc.z, 0.0, 1.0), near_z, far_z);
    let delta = ray_view - scene_view;
    if (delta > 0.0 && delta < thickness) {
      let fade = 1.0 - f32(s) / f32(steps);
      let soft = 1.0 - clamp(delta / thickness, 0.0, 1.0);
      occlusion = max(occlusion, fade * soft);
      break;
    }
  }
  return vec4f(0.0, 0.0, 0.0, clamp(occlusion * intensity, 0.0, 1.0));
}
)wgsl";

}  // namespace eve::graphics::shaders
