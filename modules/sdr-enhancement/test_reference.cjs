'use strict';
const assert = require('node:assert/strict');
const { parameters, enhance, stereo, validateOutput } = require('./stereo_reference.cjs');
const results = [];
function test(name, fn) { fn(); results.push({ name, passed: true }); }
const p = parameters();
test('identical stereo pixels remain identical', () => {
  const eye = [[0,0,0,1], [0.5,0.5,0.5,1], [0.96,0.96,0.96,1], [1,1,1,1]];
  const out = stereo([eye, eye]); assert.deepEqual(out[0], out[1]);
});
test('black stays black; alpha is preserved', () => {
  assert.deepEqual(enhance([0,0,0,0.25], p), [0,0,0,0.25]);
});
test('highlight expansion retains values above one in float path', () => {
  const out = enhance([0.96,0.96,0.96,1], p); assert(out[0] > 1); assert(out[0] <= 4);
});
test('zero strength is identity', () => {
  const v = [0.97,0.9,0.85,1]; assert.deepEqual(enhance(v, parameters({strength:0})), v);
});
test('separate UI mask bypasses highlight expansion', () => {
  const v = [1,1,1,0.4]; const out = stereo([[v],[v]], {}, [[true],[false]]);
  assert.deepEqual(out[0][0], v); assert(out[1][0][0] > 1);
});
test('grayscale ramp is monotonic and bounded', () => {
  let last = -1;
  for (let i=0;i<=4096;i++) { const x=i/4096; const v=enhance([x,x,x,1],p)[0];
    assert(v>=last && v<=4+1e-12); last=v; }
});
test('RGB grid stays finite, bounded, and hue-ratio preserving', () => {
  for (let r=0;r<=8;r++) for (let g=0;g<=8;g++) for (let b=0;b<=8;b++) {
    const input=[r/8,g/8,b/8,1], out=enhance(input,p);
    assert(out.every(Number.isFinite)); assert(out.slice(0,3).every(x=>x>=0 && x<=4+1e-12));
    for(let i=0;i<3;i++) for(let j=0;j<3;j++)
      assert(Math.abs(out[i]*input[j]-out[j]*input[i])<1e-12);
  }
});
test('RGBA16F range and GPU float32 controls are enforced', () => {
  const edge=parameters({referenceWhiteNits:1,peakNits:65504,strength:1});
  assert.equal(enhance([1,1,1,1],edge)[0],65504);
  assert.throws(()=>parameters({referenceWhiteNits:1,peakNits:65505}));
  assert.throws(()=>parameters({referenceWhiteNits:Number.MIN_VALUE,peakNits:Number.MAX_VALUE}));
  assert.throws(()=>parameters({referenceWhiteNits:1e-300,peakNits:1e-300}));
  assert.throws(()=>parameters({referenceWhiteNits:1e-40,peakNits:1e-40}));
  assert.throws(()=>parameters({referenceWhiteNits:1e300,peakNits:1e300}));
  assert.throws(()=>parameters({knee:1-1e-12}));
});
test('invalid SDR and invalid parameters fail closed', () => {
  for (const v of [[NaN,0,0,1],[-0.1,0,0,1],[1.1,0,0,1]]) assert.throws(()=>enhance(v,p));
  assert.throws(()=>parameters({peakNits:100})); assert.throws(()=>parameters({knee:1}));
  assert.throws(()=>stereo([[[0,0,0,1]],[]]));
});
const contract = {format:'RGBA16F',inputEncoding:'linear_bt709_sdr',alphaMode:'straight',
  colorSemantics:'linear_bt709_relative_white',runtimeColorInterpretationAccepted:true,
  runtimeOutputHdrVerified:false};
test('8-bit destination is rejected despite an HDR shader', () => {
  assert.throws(()=>validateOutput({...contract,format:'RGBA8_UNORM'}));
});
test('unnegotiated runtime is rejected', () => {
  assert.throws(()=>validateOutput({...contract,runtimeColorInterpretationAccepted:false}));
});
test('float plus negotiated semantics still does not claim HMD HDR', () => {
  assert.equal(validateOutput(contract),'algorithm-only-HMD-unverified');
  assert.throws(()=>validateOutput({...contract,runtimeOutputHdrVerified:true}));
});
const sample = [0,0.18,0.5,0.75,0.9,0.96,1].map(x=>({sdr:x,relativeLinear:enhance([x,x,x,1],p)[0]}));
process.stdout.write(JSON.stringify({testCount:results.length,results,sample,
  limitations:['CPU reference only','Shader offline compilation recorded separately; no GPU dispatch','No XR layer installed','No HMD output measurement']},null,2)+'\n');
