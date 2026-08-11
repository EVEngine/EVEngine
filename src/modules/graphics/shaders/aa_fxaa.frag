#version 450
// FXAA 3.11-style (Timothy Lottes / NVIDIA) — luminance edge search + subpixel blend.
// Push constants (declareFloat order):
//  0 texelW, 1 texelH
//  2 edgeThreshold      — min local contrast to trigger AA (quality)
//  3 edgeThresholdMin   — absolute luma floor
//  4 subpix             — subpixel aliasing removal amount [0..1]
//  5 quality            — 0=low 1=medium 2=high (search steps)

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = vec2(u.data[0], u.data[1]);
  float edgeThreshold = max(u.data[2], 1e-4);
  float edgeThresholdMin = max(u.data[3], 0.0);
  float subpix = clamp(u.data[4], 0.0, 1.0);
  int quality = int(clamp(u.data[5], 0.0, 2.0));

  vec3 rgbM = texture(MainTex, fragUV).rgb;
  float lumaM = luma(rgbM);

  float lumaN = luma(texture(MainTex, fragUV + vec2(0.0, -texel.y)).rgb);
  float lumaS = luma(texture(MainTex, fragUV + vec2(0.0,  texel.y)).rgb);
  float lumaE = luma(texture(MainTex, fragUV + vec2( texel.x, 0.0)).rgb);
  float lumaW = luma(texture(MainTex, fragUV + vec2(-texel.x, 0.0)).rgb);

  float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
  float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
  float lumaRange = lumaMax - lumaMin;

  if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold)) {
    outColor = vec4(rgbM * fragColor.rgb, 1.0);
    return;
  }

  float lumaNW = luma(texture(MainTex, fragUV + vec2(-texel.x, -texel.y)).rgb);
  float lumaNE = luma(texture(MainTex, fragUV + vec2( texel.x, -texel.y)).rgb);
  float lumaSW = luma(texture(MainTex, fragUV + vec2(-texel.x,  texel.y)).rgb);
  float lumaSE = luma(texture(MainTex, fragUV + vec2( texel.x,  texel.y)).rgb);

  float lumaNS = lumaN + lumaS;
  float lumaWE = lumaW + lumaE;
  float edgeHorz = abs(-2.0 * lumaW + lumaNW + lumaSW) + abs(-2.0 * lumaM + lumaN + lumaS) * 2.0
                 + abs(-2.0 * lumaE + lumaNE + lumaSE);
  float edgeVert = abs(-2.0 * lumaN + lumaNW + lumaNE) + abs(-2.0 * lumaM + lumaW + lumaE) * 2.0
                 + abs(-2.0 * lumaS + lumaSW + lumaSE);
  bool horzSpan = edgeHorz >= edgeVert;

  float luma1 = horzSpan ? lumaN : lumaW;
  float luma2 = horzSpan ? lumaS : lumaE;
  float gradient1 = luma1 - lumaM;
  float gradient2 = luma2 - lumaM;
  bool pair1 = abs(gradient1) >= abs(gradient2);
  float gradient = max(abs(gradient1), abs(gradient2));
  float stepLength = horzSpan ? texel.y : texel.x;
  if (!pair1) stepLength = -stepLength;

  float lumaLocalAvg = 0.5 * (pair1 ? luma1 : luma2) + 0.5 * lumaM;

  vec2 offset = horzSpan ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
  vec2 pos = fragUV + (horzSpan ? vec2(0.0, stepLength * 0.5) : vec2(stepLength * 0.5, 0.0));

  int maxSteps = (quality >= 2) ? 12 : ((quality >= 1) ? 8 : 4);
  float end1 = 0.0;
  float end2 = 0.0;
  bool done1 = false;
  bool done2 = false;
  vec2 pos1 = pos;
  vec2 pos2 = pos;

  for (int i = 0; i < 12; ++i) {
    if (i >= maxSteps) break;
    if (!done1) {
      pos1 -= offset;
      float l = luma(texture(MainTex, pos1).rgb) - lumaLocalAvg;
      if (abs(l) >= gradient * 0.25) {
        end1 = horzSpan ? (pos1.x - fragUV.x) : (pos1.y - fragUV.y);
        done1 = true;
      }
    }
    if (!done2) {
      pos2 += offset;
      float l = luma(texture(MainTex, pos2).rgb) - lumaLocalAvg;
      if (abs(l) >= gradient * 0.25) {
        end2 = horzSpan ? (pos2.x - fragUV.x) : (pos2.y - fragUV.y);
        done2 = true;
      }
    }
    if (done1 && done2) break;
  }

  if (!done1) end1 = horzSpan ? -float(maxSteps) * texel.x : -float(maxSteps) * texel.y;
  if (!done2) end2 = horzSpan ?  float(maxSteps) * texel.x :  float(maxSteps) * texel.y;

  float dist1 = abs(end1);
  float dist2 = abs(end2);
  float dist = min(dist1, dist2);
  float spanLen = dist1 + dist2;
  float pixelOffset = (dist < 1e-5 || spanLen < 1e-5) ? 0.0 : (-dist / spanLen) + 0.5;

  bool goodSpan = ((lumaM - lumaLocalAvg) < 0.0) != pair1;
  float finalOffset = goodSpan ? pixelOffset : 0.0;

  float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaNS + lumaWE) + lumaNW + lumaNE + lumaSW + lumaSE);
  float subpixA = clamp(abs(lumaAvg - lumaM) / max(lumaRange, 1e-5), 0.0, 1.0);
  float subpixB = (-2.0 * subpixA + 3.0) * subpixA * subpixA;
  float subpixC = subpixB * subpixB * subpix;

  float aaOffset = max(finalOffset, subpixC);
  vec2 uvAA = fragUV;
  if (horzSpan) uvAA.y += aaOffset * stepLength;
  else uvAA.x += aaOffset * stepLength;

  vec3 rgbAA = texture(MainTex, uvAA).rgb;
  outColor = vec4(rgbAA * fragColor.rgb, 1.0);
}
