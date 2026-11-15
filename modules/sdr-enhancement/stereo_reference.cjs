'use strict';
// CPU reference only. No graphics API, loader, hook, registry, or service access.
// Input: decoded linear BT.709, RGB in [0,1], straight alpha.
// Output: relative linear BT.709; 1 = referenceWhiteNits (NOT implicit scRGB/PQ).
const weights = [0.2126, 0.7152, 0.0722];
function parameters(p = {}) {
  const v = { referenceWhiteNits: 200, peakNits: 800, knee: 0.75, strength: 0.5, ...p };
  if (!Object.values(v).every(Number.isFinite) || v.referenceWhiteNits <= 0 ||
      v.peakNits < v.referenceWhiteNits || v.knee < 0 || v.knee >= 1 ||
      v.strength < 0 || v.strength > 1) throw Error('invalid parameters');
  // This reference targets a GPU float32 Settings buffer and an RGBA16F output.
  // Keep double-precision reference math; reject values unrepresentable by that contract.
  const f = Object.fromEntries(Object.entries(v).map(([k,x]) => [k,Math.fround(x)]));
  const ratio = v.peakNits / v.referenceWhiteNits;
  const gpuRatio = Math.fround(f.peakNits / f.referenceWhiteNits);
  if (!Object.values(f).every(Number.isFinite) || f.referenceWhiteNits < 2 ** -126 ||
      f.peakNits < f.referenceWhiteNits || f.knee < 0 || f.knee >= 1 ||
      !Number.isFinite(ratio) || ratio > 65504 || !Number.isFinite(gpuRatio) || gpuRatio > 65504)
    throw Error('parameters exceed float32 controls or RGBA16F output range');
  return v;
}
function enhance(pixel, p, ui = false) {
  if (!Array.isArray(pixel) || pixel.length !== 4 ||
      !pixel.every(x => Number.isFinite(x) && x >= 0 && x <= 1))
    throw Error('expected SDR linear RGB and straight alpha in [0,1]');
  if (ui) return pixel.slice();
  const y = weights.reduce((sum, w, i) => sum + w * pixel[i], 0);
  if (y === 0) return pixel.slice();
  const ratio = p.peakNits / p.referenceWhiteNits;
  const t = Math.max(0, Math.min(1, (y - p.knee) / (1 - p.knee)));
  const smooth = t * t * (3 - 2 * t);
  const mapped = y + (ratio - 1) * p.strength * smooth * smooth;
  const scale = mapped / y;
  const rgb = pixel.slice(0, 3).map(x => x * scale);
  const peakScale = Math.max(1, Math.max(...rgb) / ratio);
  return [...rgb.map(x => x / peakScale), pixel[3]];
}
function stereo(eyes, settings, masks) {
  if (!Array.isArray(eyes) || eyes.length !== 2 || eyes[0].length !== eyes[1].length)
    throw Error('exactly two equal-size eye fixtures required');
  const p = parameters(settings); // One immutable parameter set for BOTH eyes.
  if (masks && (masks.length !== 2 || masks.some((m, e) => m.length !== eyes[e].length)))
    throw Error('invalid per-eye UI masks');
  return eyes.map((eye, e) => eye.map((pixel, i) => enhance(pixel, p, !!masks?.[e][i])));
}
function validateOutput(c) {
  if (c.format !== 'RGBA16F') throw Error('extended linear reference requires RGBA16F output');
  if (c.inputEncoding !== 'linear_bt709_sdr') throw Error('input must be decoded exactly once');
  if (c.alphaMode !== 'straight') throw Error('premultiplied alpha needs separate handling');
  if (c.colorSemantics !== 'linear_bt709_relative_white') throw Error('explicit output interpretation required');
  if (!c.runtimeColorInterpretationAccepted) throw Error('runtime color interpretation not negotiated');
  // Legacy synthetic fixture gate only; the real C++ adapter has no runtime-ready bool.
  // An offline reference cannot promote a caller flag into a hardware verification claim.
  if (c.runtimeOutputHdrVerified) throw Error('offline reference cannot verify HMD HDR');
  return 'algorithm-only-HMD-unverified';
}
module.exports = { parameters, enhance, stereo, validateOutput };
