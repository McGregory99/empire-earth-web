// D3D7 light formulas as plain JavaScript. The vertex shader implements the same maths; this module is the oracle that the unit tests and the
// synthetic scenes compute their expected pixels from, and it documents the formulas.
//
// Lights are given in eye space here (the renderer transforms world space lights by the view matrix); `light` is a frame-model light:
// { type: 'directional' | 'point' | 'spot', position, direction, range, attenuation: [a0, a1, a2], falloff, theta, phi, ... }.

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const len = (a) => Math.hypot(a[0], a[1], a[2]);
const scale = (a, k) => [a[0] * k, a[1] * k, a[2] * k];

/** Distance attenuation 1 / (a0 + a1 d + a2 d^2). */
export function attenuation([a0, a1, a2], d) {
  const den = a0 + a1 * d + a2 * d * d;
  return den > 0 ? 1 / den : 0;
}

/**
 * Spot cone factor for rho = the cosine of the angle between the light direction and the vector from the light to the vertex:
 * 1 inside theta / 2, 0 outside phi / 2, ((rho - cos(phi / 2)) / (cos(theta / 2) - cos(phi / 2)))^falloff in between.
 */
export function spotFactor({ theta, phi, falloff }, rho) {
  const cosOuter = Math.cos(phi / 2);
  const cosInner = Math.cos(theta / 2);
  if (rho > cosInner) return 1;
  if (rho <= cosOuter) return 0;
  return ((rho - cosOuter) / (cosInner - cosOuter)) ** falloff;
}

/**
 * Per vertex diffuse and ambient scale of one light at eye space position `pos` with unit normal `normal`:
 * diffuse = atten * spot * max(N.L, 0), ambient = atten * spot. Point and spot lights give 0 beyond their range.
 */
export function lightFactor(light, pos, normal) {
  let toLight;
  let k = 1;
  if (light.type === 'directional') {
    toLight = scale(light.direction, -1 / len(light.direction));
  } else {
    const d = sub(light.position, pos);
    const dist = len(d);
    if (dist > light.range) return { diffuse: 0, ambient: 0 };
    toLight = scale(d, 1 / dist);
    k = attenuation(light.attenuation, dist);
    if (light.type === 'spot') k *= spotFactor(light, dot(scale(toLight, -1), scale(light.direction, 1 / len(light.direction))));
  }
  return { diffuse: k * Math.max(dot(normal, toLight), 0), ambient: k };
}
