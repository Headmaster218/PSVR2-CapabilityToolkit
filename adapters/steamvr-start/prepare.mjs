// SPDX-License-Identifier: Apache-2.0
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {fileURLToPath} from 'node:url';
import {randomUUID} from 'node:crypto';
const exec=promisify(execFile);
export class SteamVrPreparer {
  constructor({inspectScript=fileURLToPath(new URL('./inspect-start.ps1',import.meta.url)),inspect,start,onDiagnostic=()=>{},now=Date.now}={}){
    this.task=null;this.closed=false;this.onDiagnostic=onDiagnostic;this.now=now;this.lastLaunch=-Infinity;
    this.value={phase:'idle',busy:false,ready:null,hardwareReady:null,message:'打开VR视图后会自动请求启动SteamVR。'};
    this.inspect=inspect||(async()=>{const r=await exec('powershell.exe',['-NoLogo','-NoProfile','-NonInteractive','-File',inspectScript],{windowsHide:true,timeout:10000,maxBuffer:8192});return JSON.parse(r.stdout.replace(/^\uFEFF/,''));});
    this.start=start||(async install=>new Promise((resolve,reject)=>{const child=spawn(install.steam,['-applaunch','250820'],{windowsHide:true,stdio:'ignore'});child.once('error',reject);child.once('spawn',()=>{child.unref();resolve();});}));
  }
  state(){return structuredClone(this.value);}
  prepare({retry=false}={}){
    if(this.task)return this.task;
    if(this.closed)return Promise.reject(new Error('closed'));
    if(!retry&&(this.value.phase==='error'||(this.value.phase==='launch-requested'&&this.now()-this.lastLaunch<15000)))return Promise.resolve(this.state());
    this.task=this.execute().finally(()=>{this.task=null;});return this.task;
  }
  async execute(){
    this.value={phase:'checking',busy:true,ready:null,hardwareReady:null,message:'正在检查SteamVR是否已运行…'};
    try{
      const install=await this.inspect();
      if(this.closed){this.value={...this.value,phase:'cancelled',busy:false};return this.state();}
      if(install.runtimeRunning){this.value={...this.value,phase:'already-running',busy:false,runtimeRunning:true,launchRequested:false,message:'已复用现有SteamVR，点击进入VR；设备状态以SteamVR窗口为准。'};return this.state();}
      await this.start(install);
      this.lastLaunch=this.now();
      this.value={...this.value,phase:'launch-requested',busy:false,runtimeRunning:null,launchRequested:true,message:'已请求Steam启动SteamVR。点击进入VR；如未进入，请稍后重试或查看SteamVR窗口。'};
    }catch(error){const diagnosticId=randomUUID().slice(0,8);this.onDiagnostic({diagnosticId,exitCode:error.code??null,signal:error.signal??null,message:error.message,stderr:error.stderr??''});this.value={...this.value,phase:'error',busy:false,retryable:true,diagnosticId,message:`未能请求启动SteamVR，请重试（诊断 ${diagnosticId}）。`};}
    return this.state();
  }
  close(){this.closed=true;}
}
