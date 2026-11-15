'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'capabilities.json'),'utf8'));
assert.equal(manifest.runtimeReady,null);assert.equal(manifest.physicalHdrVerified,false);
assert.equal(manifest.capabilities.nativeHdr.gpuExecuted,false);
assert.equal(manifest.capabilities.sdrEnhancement.nativeHdrRoute,'bypass');
assert.equal(manifest.capabilities.cameraCoefficientPatch.applied,false);
assert.equal(manifest.capabilities.bareHandTracking.implementation,'not-implemented');
const dep=JSON.parse(fs.readFileSync(path.join(root,'dependencies.lock.json'),'utf8'));
const bridge=fs.readFileSync(path.join(root,'adapters/toolkit-capi/toolkit-helper.cs'),'utf8');
assert(bridge.includes(dep.toolkit.capiSha256));assert(bridge.includes(dep.toolkit.crossIpcSha256));
assert(bridge.includes('--connect'));assert(bridge.includes('args.Length==0'));
const sdr=JSON.parse(execFileSync(process.execPath,[path.join(root,'modules/sdr-enhancement/test_reference.cjs')],{encoding:'utf8'}));
const camera=JSON.parse(execFileSync(process.execPath,[path.join(root,'patches/psvr2toolkit/reproduce-coefficient-payload.cjs')],{encoding:'utf8'}));
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.name==='.git'||e.name==='.local'||e.name==='deps'?[]:e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
const files=walk(root);
for(const file of files){
  const relative=path.relative(root,file);
  assert(!/\.(exe|dll|obj|pdb|bin|cso|dxil|zip)$/i.test(file),`unexpected distributed binary: ${relative}`);
  if(/\.(cs|cpp|hpp|hlsl|ps1|md|json|cjs)$/i.test(file)){
    const text=fs.readFileSync(file,'utf8');
    assert(!/\b[A-Za-z]:[\\/]/.test(text),`machine-specific drive path: ${relative}`);
    assert(!/AIinfr[\\/]infr[\\/]local-work/.test(text),`old staging dependency: ${relative}`);
  }
}
console.log(JSON.stringify({scope:'CPU-only; no native runtime/helper execution',manifestAssertions:6,bridgePinAndDefaultAssertions:4,sdrTests:sdr.testCount,cameraAssertions:camera.assertionsPassed,portableFilesChecked:files.length,physicalHdrVerified:false},null,2));
