#pragma once

namespace eve::graphics::shaders {

/** @lifetime Shader source has static storage for the process lifetime. */
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
fn hash12(q: vec2f) -> f32 {
  var p3 = fract(vec3f(q.xyx) * 0.1031);
  p3 = p3 + dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char* kContactShadow = R"wgsl(
fn march_contact(view_proj: mat4x4f, origin: vec3f, light: vec3f, ray_len: f32, thickness: f32,
                 near_z: f32, far_z: f32, steps: i32, dither: f32) -> f32 {
  let step_size = ray_len / f32(steps);
  var prev_t = 0.0;
  var hit_t = -1.0;
  var hit_delta = 0.0;
  let start_t = step_size * (0.35 + dither * 0.25);
  for (var s = 0; s < steps; s = s + 1) {
    let t = start_t + step_size * f32(s);
    if (t > ray_len) { break; }
    let sample_pos = origin + light * t;
    let clip = view_proj * vec4f(sample_pos, 1.0);
    if (abs(clip.w) < 0.000001) { prev_t = t; continue; }
    let ndc = clip.xyz / clip.w;
    let uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { break; }
    let scene_z = depth(uv);
    if (scene_z >= 0.9999) { prev_t = t; continue; }
    let scene_view = ndc_to_view_z(scene_z, near_z, far_z);
    let ray_view = ndc_to_view_z(clamp(ndc.z, 0.0, 1.0), near_z, far_z);
    let delta = ray_view - scene_view;
    if (delta > 0.0 && delta < thickness) {
      hit_t = t;
      hit_delta = delta;
      var lo = prev_t;
      var hi = t;
      for (var r = 0; r < 6; r = r + 1) {
        let mid = mix(lo, hi, 0.5);
        let mid_pos = origin + light * mid;
        let mid_clip = view_proj * vec4f(mid_pos, 1.0);
        if (abs(mid_clip.w) < 0.000001) { break; }
        let mid_ndc = mid_clip.xyz / mid_clip.w;
        let mid_uv = mid_ndc.xy * 0.5 + 0.5;
        let mid_scene = ndc_to_view_z(depth(mid_uv), near_z, far_z);
        let mid_ray = ndc_to_view_z(clamp(mid_ndc.z, 0.0, 1.0), near_z, far_z);
        let mid_delta = mid_ray - mid_scene;
        if (mid_delta > 0.0 && mid_delta < thickness) {
          hi = mid; hit_t = mid; hit_delta = mid_delta;
        } else {
          lo = mid;
        }
      }
      break;
    }
    prev_t = t;
  }
  if (hit_t < 0.0) { return -1.0; }
  let dist_u = clamp(hit_t / ray_len, 0.0, 1.0);
  let thick_u = clamp(hit_delta / thickness, 0.0, 1.0);
  let dist_fade = 1.0 - (dist_u * dist_u * (3.0 - 2.0 * dist_u));
  let soft = 1.0 - (thick_u * thick_u * (3.0 - 2.0 * thick_u));
  return dist_fade * soft;
}

fn occ_or_zero(o: f32) -> f32 { return select(o, 0.0, o < 0.0); }

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
  let n_dot_l = dot(n, light);
  if (n_dot_l <= 0.02) { return vec4f(0.0); }
  let view_proj = inverse(m);
  let origin = pos + n * start_bias + light * start_bias;
  let up_ref = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(light.y) < 0.99);
  let tangent = normalize(cross(light, up_ref));
  let soft_r = ray_len * 0.06;
  let side = select(-1.0, 1.0, hash12(i.uv * vec2f(733.0, 491.0)) < 0.5);
  let rnd = hash12(i.uv * vec2f(1024.0, 768.0) + vec2f(17.0, 9.0));
  let o0 = march_contact(view_proj, origin, light, ray_len, thickness, near_z, far_z, steps, rnd);
  let o1 = march_contact(view_proj, origin + tangent * (soft_r * side), light, ray_len, thickness, near_z, far_z,
                         steps, fract(rnd + 0.37));
  var occlusion = occ_or_zero(o0) * 0.62 + occ_or_zero(o1) * 0.38;
  occlusion = occlusion * smoothstep(0.02, 0.35, n_dot_l);
  occlusion = sqrt(clamp(occlusion, 0.0, 1.0));
  return vec4f(0.0, 0.0, 0.0, clamp(occlusion * intensity, 0.0, 1.0));
}
)wgsl";

}  // namespace eve::graphics::shaders
