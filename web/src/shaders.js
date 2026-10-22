// GLSL ES 3.00 sources that emulate the D3D7 fixed-function pipeline for the states in the golden summary.
//
// Matrices arrive exactly as D3D stores them (row-major, row-vector convention) and are uploaded WITHOUT
// transposing: GLSL then sees M^T, and `M^T * v` equals the D3D `v * M`. Clip depth is remapped from D3D
// [0, w] to GL [-w, w]. Screen positions are shifted by half a pixel because D3D (and the D3D9-based stack the original
// ran on) puts pixel centres on integer coordinates, so the game's -0.5 vertex offsets are exact texel-to-pixel maps. View-space depth |z| drives vertex fog (range fog: distance from the eye; table fog: depth-buffer value per pixel).

// D3D fog factor (1 = no fog); same formulas as fogFactor() in d3d-tables.js. Shared by both stages.
const FOG_FN = `float fogFactor(int mode, float d, float start, float end, float density) {
  float f = 1.0;
  if (mode == 3) f = end == start ? (d <= start ? 1.0 : 0.0) : (end - d) / (end - start);
  else if (mode == 1) f = exp(-d * density);
  else if (mode == 2) f = exp(-(d * density) * (d * density));
  return clamp(f, 0.0, 1.0);
}
`;

export const MAIN_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;   // bytes B,G,R,A in memory (D3DCOLOR), normalised
layout(location = 3) in vec2 aUV;
layout(location = 4) in vec4 aSpec;      // bytes B,G,R,A of the vertex specular colour (0 when the format has none)
layout(location = 5) in vec2 aUV1;      // second texture coordinate set (0 when the vertex format has none)

uniform mat4 uWorld, uView, uProj;
uniform vec2 uHalfPixel;         // (+1/viewport width, -1/viewport height)
uniform bool uLighting, uNormalize;
uniform vec4 uGlobalAmbient;
uniform vec4 uMatDiffuse, uMatAmbient, uMatEmissive;
uniform int uDiffuseSrc, uAmbientSrc, uEmissiveSrc, uSpecularSrc;   // D3DMCS: 0 material, 1 vertex diffuse, 2 vertex specular
uniform bool uSpecular, uLocalViewer;
uniform vec4 uMatSpecular;
uniform float uMatPower;
uniform int uNumLights;
uniform vec3 uLightDir[4];       // world space, direction the light travels
uniform vec4 uLightDiffuse[4], uLightAmbient[4], uLightSpecular[4];
uniform int uFogMode;            // D3DFOG: 0 none, 1 exp, 2 exp2, 3 linear
uniform bool uFogTable, uFogRange;   // table (pixel) fog is evaluated in the fragment shader; range fog measures the distance from the eye
uniform float uFogStart, uFogEnd, uFogDensity;

out vec4 vColor;
out vec4 vSpec;
out vec2 vUV0;
out vec2 vUV1;
out float vFog;
${FOG_FN}

vec4 pick(int src, vec4 material, vec4 color1, vec4 color2) {
  return src == 1 ? color1 : (src == 2 ? color2 : material);
}

void main() {
  vec4 pv = uView * (uWorld * vec4(aPos, 1.0));
  vec4 clip = uProj * pv;
  clip.z = 2.0 * clip.z - clip.w;
  clip.xy += uHalfPixel * clip.w;   // D3D pixel centres are on integers, GL's on +0.5: shift by half a pixel
  gl_Position = clip;

  vec4 vcol = aColor.bgra;
  vec4 vspec = aSpec.bgra;
  vec3 specular = vec3(0.0);
  if (uLighting) {
    vec3 n = mat3(uView * uWorld) * aNormal;
    if (uNormalize) n = normalize(n);
    vec4 diff = pick(uDiffuseSrc, uMatDiffuse, vcol, vspec);
    vec4 amb = pick(uAmbientSrc, uMatAmbient, vcol, vspec);
    vec4 emi = pick(uEmissiveSrc, uMatEmissive, vcol, vspec);
    vec4 spc = pick(uSpecularSrc, uMatSpecular, vcol, vspec);
    vec3 view = uLocalViewer ? normalize(-pv.xyz) : vec3(0.0, 0.0, -1.0);   // unit vector from the vertex to the eye
    vec3 accA = uGlobalAmbient.rgb;
    vec3 accD = vec3(0.0);
    vec3 accS = vec3(0.0);
    for (int i = 0; i < 4; i++) {
      if (i >= uNumLights) break;
      vec3 l = normalize(mat3(uView) * uLightDir[i]);
      accA += uLightAmbient[i].rgb;
      float nl = dot(n, -l);
      accD += max(nl, 0.0) * uLightDiffuse[i].rgb;
      if (nl > 0.0) {
        float nh = dot(n, normalize(-l + view));   // half vector between the light and the eye
        if (nh > 0.0) accS += pow(nh, uMatPower) * uLightSpecular[i].rgb;
      }
    }
    specular = clamp(spc.rgb * accS, 0.0, 1.0);
    vColor = vec4(clamp(emi.rgb + amb.rgb * accA + diff.rgb * accD, 0.0, 1.0), clamp(diff.a, 0.0, 1.0));
  } else {
    vColor = vcol;
  }
  if (!uLighting) specular = vspec.rgb;     // unlit: the vertex specular colour as it is
  vSpec = vec4(uSpecular ? specular : vec3(0.0), 0.0);
  vUV0 = aUV;
  vUV1 = aUV1;

  vFog = uFogTable ? 1.0 : fogFactor(uFogMode, uFogRange ? length(pv.xyz) : abs(pv.z), uFogStart, uFogEnd, uFogDensity);
}
`;

export const MAIN_FS = `#version 300 es
precision highp float;
precision highp int;
in vec4 vColor;
in vec4 vSpec;
in vec2 vUV0;
in vec2 vUV1;
in float vFog;

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform bool uHasTex0, uHasTex1;
uniform vec4 uTFactor;
uniform int uNumStages;          // stages processed: 0 (stage 0 disabled), 1 or 2
uniform int uColorOp[2], uColorArg1[2], uColorArg2[2], uAlphaOp[2], uAlphaArg1[2], uAlphaArg2[2];
uniform int uTexSet[2];          // texture coordinate set each stage reads
uniform float uLodBias[2];
uniform bool uAlphaTest;
uniform int uAlphaFunc;          // D3DCMP 1..8
uniform float uAlphaRef;
uniform vec4 uFogColor;
uniform int uFogMode;
uniform bool uFogTable;
uniform float uFogStart, uFogEnd, uFogDensity;
${FOG_FN}
out vec4 outColor;

// D3DTA: 0 DIFFUSE, 1 CURRENT, 2 TEXTURE, 3 TFACTOR
vec4 arg(int a, vec4 tex, vec4 cur) {
  if (a == 2) return tex;
  if (a == 3) return uTFactor;
  if (a == 1) return cur;
  if (a == 4) return vSpec;
  return vColor;
}

vec4 applyOp(int op, vec4 a1, vec4 a2, float b) {
  if (op == 2) return a1;
  if (op == 3) return a2;
  if (op == 4) return a1 * a2;
  if (op == 5) return a1 * a2 * 2.0;
  if (op == 6) return a1 * a2 * 4.0;
  if (op == 7) return a1 + a2;
  if (op == 8) return a1 + a2 - 0.5;
  if (op == 9) return (a1 + a2 - 0.5) * 2.0;
  if (op == 10) return a1 - a2;
  if (op >= 12) return a1 * b + a2 * (1.0 - b);   // BLENDDIFFUSEALPHA / TEXTUREALPHA / FACTORALPHA
  return a1;
}

float blendScalar(int op, vec4 tex) {
  if (op == 12) return vColor.a;
  if (op == 13) return tex.a;
  return uTFactor.a;
}

bool alphaPass(int av, int rv) {
  if (uAlphaFunc == 1) return false;
  if (uAlphaFunc == 2) return av < rv;
  if (uAlphaFunc == 3) return av == rv;
  if (uAlphaFunc == 4) return av <= rv;
  if (uAlphaFunc == 5) return av > rv;
  if (uAlphaFunc == 6) return av != rv;
  if (uAlphaFunc == 7) return av >= rv;
  return true;
}

// A stage-N TEXTURE argument with no texture bound evaluates to opaque white.
vec4 sampleStage(int i) {
  vec2 uv = uTexSet[i] == 1 ? vUV1 : vUV0;
  if (i == 0) return uHasTex0 ? texture(uTex0, uv, uLodBias[0]) : vec4(1.0);
  return uHasTex1 ? texture(uTex1, uv, uLodBias[1]) : vec4(1.0);
}

void main() {
  vec4 cur = vColor;               // CURRENT at stage 0 is the diffuse colour
  for (int i = 0; i < 2; i++) {
    if (i >= uNumStages) break;
    vec4 tex = sampleStage(i);
    vec3 rgb = clamp(applyOp(uColorOp[i], arg(uColorArg1[i], tex, cur), arg(uColorArg2[i], tex, cur), blendScalar(uColorOp[i], tex)).rgb, 0.0, 1.0);
    float a = cur.a;
    if (uAlphaOp[i] != 1) {
      a = clamp(applyOp(uAlphaOp[i], arg(uAlphaArg1[i], tex, cur), arg(uAlphaArg2[i], tex, cur), blendScalar(uAlphaOp[i], tex)).a, 0.0, 1.0);
    }
    cur = vec4(rgb, a);
  }
  vec3 rgb = clamp(cur.rgb + vSpec.rgb, 0.0, 1.0);   // SPECULARENABLE: the specular colour is added after texturing
  float a = cur.a;
  if (uAlphaTest && !alphaPass(int(a * 255.0 + 0.5), int(uAlphaRef * 255.0 + 0.5))) discard;
  // table fog: per pixel on the depth-buffer value (viewport depth range applied)
  rgb = mix(uFogColor.rgb, rgb, uFogTable ? fogFactor(uFogMode, gl_FragCoord.z, uFogStart, uFogEnd, uFogDensity) : vFog);
  outColor = vec4(rgb, a);
}
`;

// 2D blits (IDirectDrawSurface7::Blt): a pixel-space rectangle, nearest sampling, optional colour-key discard.
export const BLIT_VS = `#version 300 es
precision highp float;
uniform vec4 uDst;     // x, y, w, h in target pixels, origin top-left
uniform vec4 uSrc;     // u0, v0, u1, v1
uniform vec2 uTarget;  // target size in pixels
out vec2 vUV;
void main() {
  vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
  vec2 px = uDst.xy + c * uDst.zw;
  gl_Position = vec4(px.x / uTarget.x * 2.0 - 1.0, 1.0 - px.y / uTarget.y * 2.0, 0.0, 1.0);
  vUV = mix(uSrc.xy, uSrc.zw, c);
}
`;

export const BLIT_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
uniform bool uKeyed;   // the bound variant has alpha 0 on key-coloured texels
out vec4 outColor;
void main() {
  vec4 t = texture(uTex, vUV);
  if (uKeyed && t.a < 0.5) discard;
  outColor = vec4(t.rgb, 1.0);   // Blt copies colour; destination alpha is irrelevant
}
`;
