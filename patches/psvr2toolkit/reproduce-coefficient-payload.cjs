'use strict';
// Pure CPU serialization model, not an OpenVR runtime/driver test.
// Mirrors memcpy length and DoublePropertyTag payload interpretation on Windows x64.
const assert = require('node:assert/strict');
const sourceCoefficients = [
  [0.11, 0.12, 0.13, 0.14, 0.15, 0.16, 0.17, 0.18],
  [0.21, 0.22, 0.23, 0.24, 0.25, 0.26, 0.27, 0.28]
];
const full = Buffer.alloc(2 * 8 * 8);
sourceCoefficients.flat().forEach((x,i)=>full.writeDoubleLE(x,i*8));
const originalBytes = Buffer.alloc(2 * 8 * 4); // float[2][8] destination capacity
full.copy(originalBytes,0,0,originalBytes.length); // memcpy(...,sizeof(float[2][8]))
const correctedBytes = Buffer.alloc(2 * 8 * 8); // double[2][8] candidate capacity
full.copy(correctedBytes,0,0,correctedBytes.length);
function readDoublePayload(buf) {
  return Array.from({length:buf.length/8},(_,i)=>buf.readDoubleLE(i*8));
}
const originalValues = readDoublePayload(originalBytes);
const correctedValues = readDoublePayload(correctedBytes);
assert.equal(originalBytes.length,64);
assert.equal(correctedBytes.length,128);
assert.deepEqual(originalValues,sourceCoefficients[0]); // camera 0 survives correctly as doubles
assert.equal(originalValues.length,8); // no bytes for camera 1
assert.deepEqual(correctedValues,sourceCoefficients.flat());
const result={
  scope:'CPU model only; no OpenVR load, no device, no firmware/driver modification',
  propertyTag:'k_unDoublePropertyTag (unchanged)',
  expectedStereoBytes:128,
  original:{reportedBytes:64,doubleCount:8,camera0ValuesPreserved:true,camera1Present:false},
  candidate:{reportedBytes:128,doubleCount:16,bothCamerasPreserved:true},
  assertionsPassed:5,
  limitations:[
    'No numeric double-to-float conversion occurs in memcpy',
    'No claim that every first-camera coefficient is numerically wrong',
    'Actual consumer behavior may reject short data or expose only one camera',
    'Runtime use of this property and relevance to either passthrough entry remain untested',
    'Not a diagnosis or validated near-field comfort fix'
  ]
};
process.stdout.write(JSON.stringify(result,null,2)+'\n');
