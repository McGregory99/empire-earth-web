// Index rewrites for the fill and shade modes WebGL2 has no state for. Pure, unit-tested in Node.
//
// * Flat shading: D3D takes the colour of a primitive from its first vertex, GL from its last (WebGL2 cannot change the provoking vertex),
//   so each primitive is rotated to end with its first vertex. A cyclic rotation keeps the winding.
// * FILLMODE WIREFRAME / POINT: the triangles are drawn as line segments / points. D3D culls back faces before it rasterises them, whatever the
//   fill mode, which GL cannot do for lines, so the facing is decided here from the projected vertices.

export function flatIndices(primitive, indices) {
  const per = primitive === 'triangleList' ? 3 : 2;
  const out = new Uint16Array(indices.length);
  for (let i = 0; i < indices.length; i += per) {
    for (let k = 0; k < per; k++) out[i + k] = indices[i + ((k + 1) % per)];
  }
  return out;
}

/** The edges of the triangles as line pairs; an edge shared by several triangles is listed once. */
export function wireframeIndices(indices) {
  const seen = new Set();
  const out = [];
  const edge = (a, b) => {
    const key = a < b ? a * 65536 + b : b * 65536 + a;
    if (seen.has(key)) return;
    seen.add(key);
    out.push(a, b);
  };
  for (let i = 0; i < indices.length; i += 3) {
    edge(indices[i], indices[i + 1]);
    edge(indices[i + 1], indices[i + 2]);
    edge(indices[i + 2], indices[i]);
  }
  return Uint16Array.from(out);
}

/** Every vertex the primitives use, once, in first-seen order. */
export function pointIndices(indices) {
  return Uint16Array.from(new Set(indices));
}

/**
 * Drop the triangles D3D would cull. D3D front faces are clockwise on screen: 'cw' removes clockwise triangles, 'ccw' counter-clockwise ones.
 * ndcOf(i) is [x, y] in normalised device coordinates (y up) or null for a vertex behind the eye, which makes no decision (the triangle is kept,
 * as are degenerate ones).
 */
export function cullIndices(indices, ndcOf, cull) {
  if (cull === 'none') return indices;
  const out = [];
  for (let i = 0; i < indices.length; i += 3) {
    const [a, b, c] = [indices[i], indices[i + 1], indices[i + 2]];
    const p = [ndcOf(a), ndcOf(b), ndcOf(c)];
    let drop = false;
    if (p.every(Boolean)) {
      const area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]); // y up: > 0 counter-clockwise
      drop = cull === 'cw' ? area < 0 : area > 0;
    }
    if (!drop) out.push(a, b, c);
  }
  return Uint16Array.from(out);
}

/** pos * world * view * projection (row vectors, D3D layout), divided by w: [x, y] or null when w <= 0. */
export function projectToNdc(pos, world, view, proj) {
  let v = [pos[0], pos[1], pos[2], 1];
  for (const m of [world, view, proj]) {
    v = [0, 1, 2, 3].map((c) => v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + v[3] * m[12 + c]);
  }
  return v[3] > 1e-6 ? [v[0] / v[3], v[1] / v[3]] : null;
}
