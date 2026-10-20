// GLSL ES 3.00 sources that emulate the D3D7 fixed-function pipeline for the states in the golden summary.
//
// Matrices arrive exactly as D3D stores them (row-major, row-vector convention) and are uploaded WITHOUT
// transposing: GLSL then sees M^T, and `M^T * v` equals the D3D `v * M`. Clip depth is remapped from D3D
// [0, w] to GL [-w, w]. Screen positions are shifted by half a pixel because D3D (and the D3D9-based stack the original
// ran on) puts pixel centres on integer coordinates, so the game's -0.5 vertex offsets are exact texel-to-pixel maps. View-space depth |z| drives vertex fog (no range fog).

export const MAIN_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;   // bytes B,G,R,A in memory (D3DCOLOR), normalised
layout(location = 3) in vec2 aUV;

uniform mat4 uWorld, uView, uProj;
uniform vec2 uHalfPixel;         // (+1/viewport width, -1/viewport height)
uniform bool uLighting, uNormalize;
uniform vec4 uGlobalAmbient;
uniform vec4 uMatDiffuse, uMatAmbient, uMatEmissive;
uniform bool uDiffuseFromVertex, uAmbientFromVertex, uEmissiveFromVertex;
uniform int uNumLights;
uniform vec3 uLightDir[4];       // world space, direction the light travels
uniform vec4 uLightDiffuse[4], uLightAmbient[4];
uniform int uFogMode;            // D3DFOG: 0 none, 1 exp, 2 exp2, 3 linear
uniform float uFogStart, uFogEnd, uFogDensity;

out vec4 vColor;
out vec2 vUV;
out float vFog;

void main() {
  vec4 pv = uView * (uWorld * vec4(aPos, 1.0));
  vec4 clip = uProj * pv;
  clip.z = 2.0 * clip.z - clip.w;
  clip.xy += uHalfPixel * clip.w;   // D3D pixel centres are on integers, GL's on +0.5: shift by half a pixel
  gl_Position = clip;

  vec4 vcol = aColor.bgra;
  if (uLighting) {
    vec3 n = mat3(uView * uWorld) * aNormal;
    if (uNormalize) n = normalize(n);
    vec4 diff = uDiffuseFromVertex ? vcol : uMatDiffuse;
    vec4 amb = uAmbientFromVertex ? vcol : uMatAmbient;
    vec4 emi = uEmissiveFromVertex ? vcol : uMatEmissive;
    vec3 accA = uGlobalAmbient.rgb;
    vec3 accD = vec3(0.0);
    for (int i = 0; i < 4; i++) {
      if (i >= uNumLights) break;
      vec3 l = normalize(mat3(uView) * uLightDir[i]);
      accA += uLightAmbient[i].rgb;
      accD += max(dot(n, -l), 0.0) * uLightDiffuse[i].rgb;
    }
    vColor = vec4(clamp(emi.rgb + amb.rgb * accA + diff.rgb * accD, 0.0, 1.0), clamp(diff.a, 0.0, 1.0));
  } else {
    vColor = vcol;
  }
  vUV = aUV;

  float d = abs(pv.z);
  float f = 1.0;
  if (uFogMode == 3) f = (uFogEnd - d) / (uFogEnd - uFogStart);
  else if (uFogMode == 1) f = exp(-d * uFogDensity);
  else if (uFogMode == 2) f = exp(-(d * uFogDensity) * (d * uFogDensity));
  vFog = clamp(f, 0.0, 1.0);
}
`;

export const MAIN_FS = `#version 300 es
precision highp float;
in vec4 vColor;
in vec2 vUV;
in float vFog;

uniform sampler2D uTex;
uniform bool uHasTex;
uniform float uLodBias;
uniform vec4 uTFactor;
uniform int uColorOp, uColorArg1, uColorArg2, uAlphaOp, uAlphaArg1, uAlphaArg2;
uniform bool uAlphaTest;
uniform int uAlphaFunc;          // D3DCMP 1..8
uniform float uAlphaRef;
uniform vec4 uFogColor;

out vec4 outColor;

vec4 arg(int a, vec4 tex) {
  if (a == 2) return tex;
  if (a == 3) return uTFactor;
  return vColor;                 // DIFFUSE, and CURRENT (= diffuse at stage 0)
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

void main() {
  vec4 tex = uHasTex ? texture(uTex, vUV, uLodBias) : vec4(1.0);
  vec3 rgb = vColor.rgb;
  float a = vColor.a;
  if (uColorOp != 1) {
    rgb = clamp(applyOp(uColorOp, arg(uColorArg1, tex), arg(uColorArg2, tex), blendScalar(uColorOp, tex)).rgb, 0.0, 1.0);
    if (uAlphaOp != 1) {
      a = clamp(applyOp(uAlphaOp, arg(uAlphaArg1, tex), arg(uAlphaArg2, tex), blendScalar(uAlphaOp, tex)).a, 0.0, 1.0);
    }
  }
  if (uAlphaTest && !alphaPass(int(a * 255.0 + 0.5), int(uAlphaRef * 255.0 + 0.5))) discard;
  rgb = mix(uFogColor.rgb, rgb, vFog);
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
