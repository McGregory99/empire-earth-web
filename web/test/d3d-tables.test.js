import test from 'node:test';
import assert from 'node:assert/strict';
import {
  GL, RS, TSS, TOP, TARG, blendFactors, compareFunc, cullMode, addressMode, minFilter, magFilter,
  f32FromBits, unpackColor, classifyRenderState, classifyTextureStageState, primitiveMode, UnsupportedError,
} from '../src/d3d-tables.js';

test('blend factors: the two pairs the golden trace uses', () => {
  assert.deepEqual(blendFactors(2, 1), [GL.ONE, GL.ZERO]);
  assert.deepEqual(blendFactors(5, 6), [GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA]);
  assert.deepEqual(blendFactors(12, 1), [GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA], 'BOTHSRCALPHA overrides dest');
  assert.deepEqual(blendFactors(13, 1), [GL.ONE_MINUS_SRC_ALPHA, GL.SRC_ALPHA]);
  assert.throws(() => blendFactors(0, 1), UnsupportedError);
});

test('comparison functions D3DCMP -> GL', () => {
  assert.equal(compareFunc(1), GL.NEVER);
  assert.equal(compareFunc(4), GL.LEQUAL);
  assert.equal(compareFunc(5), GL.GREATER);
  assert.equal(compareFunc(8), GL.ALWAYS);
  assert.throws(() => compareFunc(9), UnsupportedError);
});

test('cull modes: D3D front faces are clockwise, so CW culls the GL front face under frontFace(CW)', () => {
  assert.deepEqual(cullMode(1), { enable: false });
  assert.deepEqual(cullMode(2), { enable: true, face: GL.FRONT });
  assert.deepEqual(cullMode(3), { enable: true, face: GL.BACK });
  assert.throws(() => cullMode(0), UnsupportedError);
});

test('texture addressing and filters', () => {
  assert.equal(addressMode(1), GL.REPEAT);
  assert.equal(addressMode(2), GL.MIRRORED_REPEAT);
  assert.equal(addressMode(3), GL.CLAMP_TO_EDGE);
  assert.throws(() => addressMode(4), /border/i);
  assert.equal(magFilter(1), GL.NEAREST);
  assert.equal(magFilter(2), GL.LINEAR);
  // min filter x mip filter (D3DTFP: NONE=1 POINT=2 LINEAR=3)
  assert.equal(minFilter(2, 1), GL.LINEAR);
  assert.equal(minFilter(2, 2), GL.LINEAR_MIPMAP_NEAREST);
  assert.equal(minFilter(2, 3), GL.LINEAR_MIPMAP_LINEAR);
  assert.equal(minFilter(1, 1), GL.NEAREST);
  assert.equal(minFilter(1, 2), GL.NEAREST_MIPMAP_NEAREST);
  assert.equal(minFilter(1, 3), GL.NEAREST_MIPMAP_LINEAR);
  assert.throws(() => minFilter(3, 1), UnsupportedError);
});

test('primitive types', () => {
  assert.equal(primitiveMode('triangleList'), GL.TRIANGLES);
  assert.equal(primitiveMode('lineList'), GL.LINES);
  assert.throws(() => primitiveMode('triangleFan'), UnsupportedError);
});

test('float and colour helpers', () => {
  assert.equal(f32FromBits(0xbf400000), -0.75);
  assert.equal(f32FromBits(0x3f800000), 1);
  assert.deepEqual(unpackColor(0xff102030).map(v => Math.round(v * 255)), [0x10, 0x20, 0x30, 0xff]);
  assert.deepEqual(unpackColor(0x80000000), [0, 0, 0, 128 / 255]);
});

test('classifyRenderState: states of the golden summary are accepted', () => {
  const ok = [
    [RS.SRCBLEND, 2], [RS.DESTBLEND, 6], [RS.ALPHABLENDENABLE, 1], [RS.CLIPPING, 1], [RS.LIGHTING, 1],
    [RS.CULLMODE, 2], [RS.ZFUNC, 4], [RS.SPECULARENABLE, 0], [RS.DITHERENABLE, 1], [RS.COLORKEYENABLE, 1],
    [RS.AMBIENT, 0xff000000], [RS.ALPHATESTENABLE, 1], [RS.ZWRITEENABLE, 0], [RS.ZENABLE, 0],
    [RS.SPECULARMATERIALSOURCE, 0], [RS.SHADEMODE, 2], [RS.NORMALIZENORMALS, 1], [RS.LOCALVIEWER, 0],
    [RS.FOGVERTEXMODE, 3], [RS.FOGENABLE, 0], [RS.FOGCOLOR, 0xff000000], [RS.FILLMODE, 3], [RS.EXTENTS, 0],
    [RS.EMISSIVEMATERIALSOURCE, 0], [RS.DIFFUSEMATERIALSOURCE, 1], [RS.COLORVERTEX, 1], [RS.ANTIALIAS, 0],
    [RS.AMBIENTMATERIALSOURCE, 1], [RS.ALPHAREF, 0], [RS.ALPHAFUNC, 5],
  ];
  assert.equal(ok.length, 30, 'the summary lists 30 distinct render states');
  for (const [id, v] of ok) assert.doesNotThrow(() => classifyRenderState(id, v), `state ${id}=${v}`);
});

test('classifyRenderState: unsupported states and values fail loudly', () => {
  assert.throws(() => classifyRenderState(RS.FILLMODE, 4), /FILLMODE/);
  assert.throws(() => classifyRenderState(RS.SHADEMODE, 0), /SHADEMODE/);
  assert.throws(() => classifyRenderState(RS.SPECULARENABLE, 2), /SPECULARENABLE/);
  assert.throws(() => classifyRenderState(RS.FOGTABLEMODE, 4), /FOGTABLEMODE/);
  assert.throws(() => classifyRenderState(RS.STENCILENABLE, 2), /STENCILENABLE/);
  assert.throws(() => classifyRenderState(RS.ZENABLE, 2), /w-buffer|usew/i);
  assert.throws(() => classifyRenderState(9999, 0), /unknown render state 9999/);
});

test('classifyTextureStageState', () => {
  for (const [id, v] of [[TSS.COLOROP, TOP.MODULATE], [TSS.ALPHAOP, TOP.DISABLE], [TSS.COLORARG1, TARG.TEXTURE],
    [TSS.TEXCOORDINDEX, 0], [TSS.ADDRESS, 3], [TSS.MAGFILTER, 2], [TSS.MINFILTER, 2], [TSS.MIPFILTER, 2],
    [TSS.MIPMAPLODBIAS, 0xbf400000]]) {
    assert.doesNotThrow(() => classifyTextureStageState(0, id, v));
  }
  assert.throws(() => classifyTextureStageState(0, TSS.COLORARG1, TARG.TEXTURE | 0x10), /complement/i);
  assert.throws(() => classifyTextureStageState(0, TSS.COLORARG1, 5), /unknown texture argument/i);
  assert.throws(() => classifyTextureStageState(0, TSS.COLOROP, 11), /ADDSMOOTH|operation/i);
  assert.throws(() => classifyTextureStageState(0, TSS.TEXCOORDINDEX, 2), /texcoord/i);
  assert.throws(() => classifyTextureStageState(0, TSS.TEXTURETRANSFORMFLAGS, 2), /transform/i);
  assert.throws(() => classifyTextureStageState(0, 999, 0), /unknown texture stage state/i);
});
