// PSVR2Toolkit Experimental#3 (9900be1) ABI. Only the two pinned DLLs are loaded.
// No firmware, camera, calibration, head-rumble, shell, network or file-data APIs.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using System.Web.Script.Serialization;

public static class Psvr2CapiHelper {
  static string DllRoot;
  static readonly Stopwatch Clock = Stopwatch.StartNew();
  static readonly object Gate = new object();
  static readonly ConcurrentQueue<Dictionary<string,object>> Commands = new ConcurrentQueue<Dictionary<string,object>>();
  static readonly BlockingCollection<string> Output = new BlockingCollection<string>(128);
  static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength=8192 };
  static volatile bool Stop, GazeEnabled, CleanupComplete;
  static long HeartbeatAt, BusySince;
  static bool Initialized;
  static IntPtr Library, GazeBuffer, TriggerBuffer;
  static readonly byte[] Silence = new byte[32];
  static readonly string[] TriggerModes = new string[2];
  static readonly string[] PcmModes = new string[2];
  static readonly long[] ModeSample = new long[2];
  static readonly PcmEffect[] Pcm = {new PcmEffect(),new PcmEffect()};
  sealed class PcmEffect { public long Until; public int Duration; public double Frequency,Amplitude,Phase; public string CommandId; }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string path,IntPtr reserved,uint flags);
  [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
  [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int InitFn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void DeinitFn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return:MarshalAs(UnmanagedType.I1)] delegate bool ActiveFn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return:MarshalAs(UnmanagedType.I1)] delegate bool GazeFn(IntPtr destination,uint timeoutMs);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int TriggerFn(byte controller,IntPtr command);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int PcmFn(byte controller,[In,MarshalAs(UnmanagedType.LPArray,SizeConst=32)]byte[] samples);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int WaitPcmFn();
  static InitFn Init; static DeinitFn Deinit; static ActiveFn Active; static GazeFn Gaze; static TriggerFn Trigger; static PcmFn WritePcm; static WaitPcmFn WaitPcm;
  static long Now { get {return Clock.ElapsedMilliseconds;} }
  static double UnixMs { get {return (DateTime.UtcNow-new DateTime(1970,1,1)).TotalMilliseconds;} }
  static Dictionary<string,object> Obj(params object[] entries) {var d=new Dictionary<string,object>(); for(int i=0;i<entries.Length;i+=2)d[(string)entries[i]]=entries[i+1]; return d;}
  static void Emit(Dictionary<string,object> value) {string line; lock(Json)line=Json.Serialize(value); if(!Output.TryAdd(line) && Convert.ToString(value["type"])!="gaze")Stop=true;}
  static T Export<T>(string name) where T:class {var p=GetProcAddress(Library,name);if(p==IntPtr.Zero)throw new Exception("missing_export");return Marshal.GetDelegateForFunctionPointer(p,typeof(T)) as T;}
  static void Pin(string name,string expected) {using(var f=File.OpenRead(Path.Combine(DllRoot,name)))using(var h=SHA256.Create()){if(BitConverter.ToString(h.ComputeHash(f)).Replace("-","")!=expected)throw new Exception("dll_hash_mismatch");}}
  static void Load() {
    Pin("psvr2_toolkit_capi.dll","25F1679F76B33F11C4F4A85C00B32C5D04815D9E7B349FA7718B700CA3882671");
    Pin("libcrossipc.dll","0ED1B00CCAD8C777005187F1569D7D9268C3813E57BF757FC9C85751DC302F5E");
    Library=LoadLibraryEx(Path.Combine(DllRoot,"psvr2_toolkit_capi.dll"),IntPtr.Zero,0x100|0x800);
    if(Library==IntPtr.Zero)throw new Exception("dll_load_failed");
    Init=Export<InitFn>("psvr2_toolkit_init");Deinit=Export<DeinitFn>("psvr2_toolkit_deinit");Active=Export<ActiveFn>("psvr2_toolkit_get_driver_active");
    Gaze=Export<GazeFn>("psvr2_toolkit_gaze_status");Trigger=Export<TriggerFn>("psvr2_toolkit_set_trigger_effect");
    WritePcm=Export<PcmFn>("psvr2_toolkit_write_pcm");WaitPcm=Export<WaitPcmFn>("psvr2_toolkit_wait_for_pcm");
    if(Init()!=0)throw new Exception("capi_no_slot");Initialized=true;
    GazeBuffer=Marshal.AllocHGlobal(4096);Marshal.Copy(new byte[4096],0,GazeBuffer,4096);
    TriggerBuffer=Marshal.AllocHGlobal(56);Marshal.Copy(new byte[56],0,TriggerBuffer,56);
  }
  static string Text(Dictionary<string,object> d,string key) {object v;return d.TryGetValue(key,out v)?Convert.ToString(v,CultureInfo.InvariantCulture):"";}
  static bool Boolean(Dictionary<string,object> d,string key) {object v;return d.TryGetValue(key,out v)&&v is bool&&(bool)v;}
  static void Reader() {
    try {string line;while((line=Console.ReadLine())!=null){if(line.Length>4096||Commands.Count>32){Stop=true;break;} Dictionary<string,object> command;lock(Json)command=Json.Deserialize<Dictionary<string,object>>(line);string type=Text(command,"type");if(type=="heartbeat")Interlocked.Exchange(ref HeartbeatAt,Now);else Commands.Enqueue(command);}}catch{Stop=true;}finally{Stop=true;}
  }
  static void Writer() {try{foreach(var line in Output.GetConsumingEnumerable()){Console.WriteLine(line);Console.Out.Flush();}}catch{Stop=true;}}
  static void NativeStart() {Interlocked.Exchange(ref BusySince,Now+1);}
  static void NativeDone() {Interlocked.Exchange(ref BusySince,0);}
  static void ResetAll(string reason) {
    Marshal.Copy(new byte[56],0,TriggerBuffer,56);
    int left=-1,right=-1,pcm=-1;
    try{left=Trigger(0,TriggerBuffer);}finally{try{right=Trigger(1,TriggerBuffer);}finally{pcm=WritePcm(2,Silence);}}
    bool accepted=left==0&&right==0&&pcm==0;
    if(accepted)for(int h=0;h<2;h++){TriggerModes[h]=null;PcmModes[h]=null;Pcm[h].Until=0;}
    else Stop=true;
    Emit(Obj("type","reset","reason",reason,"leftResult",left,"rightResult",right,"pcmResult",pcm,"accepted",accepted,"status",accepted?"reset":"error","selectedModes",SelectedModes()));
  }
  static void ExpireEffects() {
    long now=Now;
    for(int h=0;h<2;h++){
      if(Pcm[h].Until>0&&now>=Pcm[h].Until){int code=WritePcm((byte)h,LoopBlock(h));if(code==0)Pcm[h].Until=0;else Stop=true;Emit(Obj("type","effect-expired","kind","pcm","commandId",Pcm[h].CommandId,"accepted",code==0,"result",code,"status",code==0?"completed":"error","reason",PcmModes[h]==null?"feedback-finished":"feedback-finished-mode-restored"));}
    }
  }
  static void Watchdog() {
    long cleanupDeadline=0;
    while(!CleanupComplete){
      long now=Now;
      if(now-Interlocked.Read(ref HeartbeatAt)>1500){Stop=true;}
      // This thread NEVER enters Gate or a native function. Cleanup is performed
      // by the owner thread. It can still abandon a blocked native client slot.
      for(int h=0;h<2;h++){
        long pcmEnd=Interlocked.Read(ref Pcm[h].Until);
        // Persistent mode selections have no duration limit. Only transient
        // feedback must finish promptly; heartbeat/native-stall guards stay active.
        if(pcmEnd>0&&now>=pcmEnd+200)Environment.Exit(4);
      }
      if(Stop&&cleanupDeadline==0)cleanupDeadline=now+2200;
      if(cleanupDeadline>0&&now>=cleanupDeadline)Environment.Exit(4);
      long busy=Interlocked.Read(ref BusySince);
      // If an upstream mutex/native call stalls, abandon this client slot rather
      // than keep an effect alive. Toolkit driver clears dead slots in its10ms loop.
      if(busy>0&&now-busy>2000)Environment.Exit(4);
      Thread.Sleep(20);
    }
  }
  static void PcmLoop() {
    try{while(!Stop){bool armed;lock(Gate)armed=Pcm[0].Until>Now||Pcm[1].Until>Now||PcmModes[0]!=null||PcmModes[1]!=null;
      if(!armed){Thread.Sleep(10);continue;}
      // This wait is independent of the serialized command/gaze calls. It only
      // waits for the driver's sample broadcast and mutates no CAPI client globals.
      if(WaitPcm()!=0){Thread.Sleep(10);continue;}if(Stop)break;
      lock(Gate){NativeStart();for(int h=0;h<2;h++){var effect=Pcm[h];byte[] samples=LoopBlock(h);long remaining=effect.Until-Now;
        if(remaining>0){double envelope=Math.Min(1.0,remaining/20.0);for(int i=0;i<32;i++){samples[i]=unchecked((byte)(sbyte)Math.Round(Math.Sin(effect.Phase)*127*effect.Amplitude*envelope));effect.Phase=(effect.Phase+2*Math.PI*effect.Frequency/3000)%(2*Math.PI);}}
        if(WritePcm((byte)h,samples)!=0){Stop=true;Emit(Obj("type","error","code","pcm-write-failed","message","手柄反馈连接已结束"));}
      }NativeDone();}
    }}catch{Stop=true;}
  }
  static Dictionary<string,object> Effect(Dictionary<string,object> c) {
    string id=Text(c,"commandId"),kind=Text(c,"kind"),controller=Text(c,"controller"),preset=Text(c,"preset");int duration=0;
    bool valid=kind=="pcm"&&(preset=="ui-click"||preset=="click"||preset=="soft-pulse");
    int first=controller=="right"?1:0,last=controller=="left"?0:1;byte target=controller=="both"?(byte)2:(byte)first;
    string reason=null;int code=-4;
    if(!valid||(controller!="left"&&controller!="right"&&controller!="both"))reason="unsupported_preset";
    else if(!Active())reason="driver_inactive";
    else{
      duration=preset=="ui-click"?80:(preset=="click"?150:300);
      for(int h=first;h<=last;h++)if(Pcm[h].Until>Now)reason="effect_busy";
      if(reason==null){
        code=WritePcm(target,Silence);
        if(code==0)for(int h=first;h<=last;h++){Pcm[h].Duration=duration;Pcm[h].Until=Now+Math.Min(duration,500);Pcm[h].Frequency=preset=="ui-click"?180:(preset=="click"?160:100);Pcm[h].Amplitude=preset=="ui-click"?0.08:(preset=="click"?0.18:0.12);Pcm[h].Phase=0;Pcm[h].CommandId=id;}
        if(code!=0)reason="capi_result_"+code;
      }
    }
    return Obj("type","effect","commandId",id,"accepted",reason==null,"kind",kind,"controller",controller,"preset",preset,"durationMs",duration,"mode","native-capi","status",reason==null?"accepted":"rejected","reason",reason,"physicalEffectMeasured",false);
  }
  static Dictionary<string,object> SelectedModes(){return Obj("pcm",Obj("left",PcmModes[0],"right",PcmModes[1]),"trigger",Obj("left",TriggerModes[0],"right",TriggerModes[1]));}
  static byte[] TriggerBytes(string preset){
    var bytes=new byte[56];int mode;
    if(preset=="normal")mode=0;
    else if(preset=="light-resistance"){mode=1;bytes[8]=5;bytes[9]=1;}
    else if(preset=="detent"){mode=2;bytes[8]=3;bytes[9]=6;bytes[10]=2;}
    else if(preset=="trigger-vibration"){mode=3;bytes[8]=4;bytes[9]=1;bytes[10]=90;}
    else if(preset=="multi-position"){mode=4;byte[] strength={0,0,1,1,0,1,2,1,1,0};Array.Copy(strength,0,bytes,8,10);}
    else if(preset=="slope"){mode=5;bytes[8]=2;bytes[9]=8;bytes[10]=1;bytes[11]=2;}
    else if(preset=="multi-vibration"){mode=6;bytes[8]=90;byte[] strength={0,0,1,0,1,1,0,1,1,0};Array.Copy(strength,0,bytes,9,10);}
    else return null;
    Array.Copy(BitConverter.GetBytes(mode),0,bytes,0,4);return bytes;
  }
  static bool IsPcmMode(string name){return name=="normal"||name=="smooth-wave"||name=="low-rumble"||name=="fine-buzz"||name=="double-pulse"||name=="rhythm"||name=="grain";}
  static double Pulse(double t,double start,double width){double x=(t-start)/width;return x>=0&&x<1?Math.Sin(Math.PI*x):0;}
  static double LoopValue(string mode,long sample){
    double t=sample/3000.0;
    if(mode=="smooth-wave")return .12*(.25+.75*(.5-.5*Math.Cos(2*Math.PI*.8*t)))*Math.Sin(2*Math.PI*110*t);
    if(mode=="low-rumble")return .14*Math.Sin(2*Math.PI*55*t);
    if(mode=="fine-buzz")return .10*Math.Sin(2*Math.PI*220*t);
    if(mode=="double-pulse")return .16*(Pulse(t%.9,0,.07)+Pulse(t%.9,.16,.07))*Math.Sin(2*Math.PI*140*t);
    if(mode=="rhythm")return .14*Pulse(t%.4,0,.08)*Math.Sin(2*Math.PI*120*t);
    if(mode=="grain"){long grain=(long)(t/.07);return .12*Pulse(t%.07,0,.012)*Math.Sin(2*Math.PI*(150+(grain*17%61))*t);}
    return 0;
  }
  static byte[] LoopBlock(int hand){var bytes=new byte[32];for(int i=0;i<32;i++){double value=LoopValue(PcmModes[hand],ModeSample[hand]++);bytes[i]=unchecked((byte)(sbyte)Math.Round(Math.Max(-.18,Math.Min(.18,value))*127));}return bytes;}
  static void DriverLost(){try{ResetAll("driver-disconnected");}finally{for(int h=0;h<2;h++){TriggerModes[h]=null;PcmModes[h]=null;Pcm[h].Until=0;}Stop=true;}}
  static Dictionary<string,object> Mode(Dictionary<string,object> c){
    string id=Text(c,"commandId"),kind=Text(c,"kind"),controller=Text(c,"controller"),preset=Text(c,"preset");
    byte[] trigger=kind=="trigger"?TriggerBytes(preset):null;bool valid=(kind=="trigger"&&trigger!=null)||(kind=="pcm"&&IsPcmMode(preset));
    int first=controller=="right"?1:0,last=controller=="left"?0:1;byte target=controller=="both"?(byte)2:(byte)first;string reason=null;int resetCode=-4,applyCode=-4;
    if(!valid||(controller!="left"&&controller!="right"&&controller!="both"))reason="unsupported_preset";
    else if(!Active())reason="driver_inactive";
    else if(kind=="trigger"){
      Marshal.Copy(new byte[56],0,TriggerBuffer,56);resetCode=Trigger(2,TriggerBuffer);
      if(resetCode==0){TriggerModes[0]=null;TriggerModes[1]=null;Marshal.Copy(trigger,0,TriggerBuffer,56);applyCode=preset=="normal"?0:Trigger(target,TriggerBuffer);if(applyCode==0&&preset!="normal")for(int h=first;h<=last;h++)TriggerModes[h]=preset;}
    }else{
      resetCode=WritePcm(2,Silence);
      if(resetCode==0){for(int h=0;h<2;h++){PcmModes[h]=null;ModeSample[h]=0;}applyCode=0;
        if(preset!="normal"){for(int h=first;h<=last;h++)PcmModes[h]=preset;for(int h=first;h<=last;h++){int result=WritePcm((byte)h,LoopBlock(h));if(result!=0)applyCode=result;}}
      }
    }
    if(reason==null&&(resetCode!=0||applyCode!=0)){reason="capi_mode_not_accepted";Stop=true;}
    return Obj("type","mode","commandId",id,"kind",kind,"controller",controller,"preset",preset,"persistent",true,"accepted",reason==null,"status",reason==null?"selected":"rejected","mode","native-capi","reason",reason,"resetResult",resetCode,"applyResult",applyCode,"selectedModes",SelectedModes(),"physicalEffectMeasured",false);
  }
  static float Float(int offset){byte[] b=new byte[4];Marshal.Copy(IntPtr.Add(GazeBuffer,offset),b,0,4);return BitConverter.ToSingle(b,0);}
  static bool Finite(double x){return !double.IsNaN(x)&&!double.IsInfinity(x);}
  static int TestModes(){
    var writes=new List<string>();bool failPcm=false;TriggerBuffer=Marshal.AllocHGlobal(56);
    Active=delegate{return true;};Trigger=delegate(byte hand,IntPtr command){writes.Add("t:"+hand+":"+Marshal.ReadInt32(command));return 0;};
    WritePcm=delegate(byte hand,byte[] samples){if(samples.Length!=32)throw new Exception("block-size");return failPcm?-1:0;};
    try{
      string[] triggerNames={"light-resistance","detent","trigger-vibration","multi-position","slope","multi-vibration"};
      string[] pcmNames={"smooth-wave","low-rumble","fine-buzz","double-pulse","rhythm","grain"};
      for(int i=0;i<6;i++){var bytes=TriggerBytes(triggerNames[i]);if(bytes.Length!=56||BitConverter.ToInt32(bytes,0)!=i+1)throw new Exception("trigger-encoding");}
      foreach(var name in pcmNames){double peak=0;for(int i=0;i<9000;i++)peak=Math.Max(peak,Math.Abs(LoopValue(name,i)));if(peak<=0||peak>.180001)throw new Exception("wave-range");}
      Mode(Obj("commandId","t1","kind","trigger","controller","left","preset","light-resistance"));
      Mode(Obj("commandId","t2","kind","trigger","controller","right","preset","detent"));
      if(TriggerModes[0]!=null||TriggerModes[1]!="detent"||writes[writes.Count-2]!="t:2:0"||writes[writes.Count-1]!="t:1:2")throw new Exception("retarget");
      Mode(Obj("commandId","p1","kind","pcm","controller","both","preset","smooth-wave"));
      if(TriggerModes[1]!="detent")throw new Exception("cross-kind");
      Effect(Obj("commandId","e1","kind","pcm","controller","left","preset","ui-click"));
      if(PcmModes[0]!="smooth-wave"||PcmModes[1]!="smooth-wave"||TriggerModes[1]!="detent")throw new Exception("feedback-selection");
      Thread.Sleep(15);Pcm[0].Until=Now-1;ExpireEffects();if(PcmModes[0]!="smooth-wave"||Pcm[0].Until!=0)throw new Exception("feedback-restore");
      Thread.Sleep(2100);ExpireEffects();if(PcmModes[0]!="smooth-wave"||TriggerModes[1]!="detent")throw new Exception("persistent-timeout");
      Mode(Obj("commandId","p2","kind","pcm","controller","right","preset","low-rumble"));if(PcmModes[0]!=null||PcmModes[1]!="low-rumble"||TriggerModes[1]!="detent")throw new Exception("pcm-retarget");
      ResetAll("requested");if(PcmModes[0]!=null||PcmModes[1]!=null||TriggerModes[0]!=null||TriggerModes[1]!=null)throw new Exception("reset-selection");
      Mode(Obj("commandId","p3","kind","pcm","controller","both","preset","grain"));DriverLost();if(!Stop||PcmModes[0]!=null||PcmModes[1]!=null)throw new Exception("disconnect-selection");
      Stop=false;Mode(Obj("commandId","t3","kind","trigger","controller","both","preset","slope"));failPcm=true;ResetAll("requested");
      string line,last="";while(Output.TryTake(out line))last=line;var reset=Json.Deserialize<Dictionary<string,object>>(last);if((bool)reset["accepted"]||!Stop)throw new Exception("false-reset-ack");
      Console.WriteLine("{\"selfTestModes\":true,\"triggerCategories\":6,\"pcmWaveforms\":6,\"retargetOldHandOff\":true,\"crossKindIndependent\":true,\"feedbackRestoresLoop\":true,\"persistentBeyond2100ms\":true,\"resetAndDisconnectClearSelection\":true,\"failedResetNotAccepted\":true,\"hardwareLoaded\":false}");return 0;
    }finally{Marshal.FreeHGlobal(TriggerBuffer);TriggerBuffer=IntPtr.Zero;}
  }
  static uint LastSequence;static bool HaveSequence;static long AnchorSource;static double AnchorUnix;
  static void Sample() {
    if(!GazeEnabled||!Gaze(GazeBuffer,0)||Marshal.ReadInt32(GazeBuffer,4)!=328)return;
    uint sequence=unchecked((uint)Marshal.ReadInt32(GazeBuffer,88));if(HaveSequence&&sequence==LastSequence)return;
    HaveSequence=true;LastSequence=sequence;long source=Marshal.ReadInt64(GazeBuffer,80);double received=UnixMs;
    if(AnchorSource==0||Math.Abs((received-AnchorUnix)-(source-AnchorSource)/1000.0)>1000){AnchorSource=source;AnchorUnix=received;}
    double[] origin={-Float(240)/1000.0,Float(244)/1000.0,-Float(248)/1000.0};
    double[] direction={-Float(256),Float(260),-Float(264)};
    double length=Math.Sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
    bool valid=Marshal.ReadInt32(GazeBuffer,236)==1&&Marshal.ReadInt32(GazeBuffer,252)==1&&Finite(length)&&length>0.5&&length<1.5;
    for(int i=0;i<3;i++){if(!Finite(origin[i])||Math.Abs(origin[i])>1||!Finite(direction[i]))valid=false;}
    if(valid)for(int i=0;i<3;i++)direction[i]/=length;else{origin=new double[3];direction=new double[3];}
    Emit(Obj("type","gaze","sequence",sequence,"valid",valid,"origin",origin,"direction",direction,"space","head-local","sampleAtMs",AnchorUnix+(source-AnchorSource)/1000.0,"receivedAtMs",received,"ageMs",0,"sourceTimestampUs",source.ToString(CultureInfo.InvariantCulture),"timestampBasis","source-relative-receipt-anchor"));
  }
  public static int Main(string[] args) {
    if(args.Length==0||(args.Length==1&&args[0]=="--help")) {
      Console.WriteLine("Offline: --help | --self-test | --self-test-modes | --self-test-watchdog\nExplicit connection: --connect --dll-dir <installed Toolkit bin directory>\nJSON-lines client must send heartbeat; no SteamVR/driver installation or launch.");return 0;
    }
    if(args.Length==1&&args[0]=="--self-test-modes")return TestModes();
    if(args.Length==1&&args[0]=="--self-test-watchdog"){
      HeartbeatAt=Now;BusySince=Now+1;new Thread(Watchdog){IsBackground=true}.Start();
      lock(Gate){Thread.Sleep(10000);}return 9;
    }
    if(args.Length==1&&args[0]=="--self-test"){Console.WriteLine("{\"selfTest\":true,\"gazeSize\":328,\"triggerSize\":56,\"pcmSamples\":32,\"hardwareLoaded\":false}");return 0;}
    if(args.Length!=3||args[0]!="--connect"||args[1]!="--dll-dir") {Console.WriteLine("{\"error\":\"explicit_connect_and_dll_directory_required\"}");return 2;}
    DllRoot=Path.GetFullPath(args[2]);
    if(Process.GetProcessesByName("vrserver").Length==0) {Console.WriteLine("{\"error\":\"steamvr_must_already_be_running\"}");return 2;}
    Console.OutputEncoding=new System.Text.UTF8Encoding(false);Console.InputEncoding=new System.Text.UTF8Encoding(false);
    var writer=new Thread(Writer){IsBackground=true};writer.Start();Thread pcmThread=null,watchdog=null;
    try{
      Load();HeartbeatAt=Now;bool seenActive=Active();Emit(Obj("type","ready","driverActive",seenActive,"gaze",true,"trigger",true,"pcm",true,"selectedModes",SelectedModes()));
      new Thread(Reader){IsBackground=true}.Start();watchdog=new Thread(Watchdog){IsBackground=true};watchdog.Start();pcmThread=new Thread(PcmLoop){IsBackground=true};pcmThread.Start();
      long nextSample=0,nextState=0;
      while(!Stop){Dictionary<string,object> c;int drained=0;
        lock(Gate){NativeStart();ExpireEffects();NativeDone();}
        while(drained++<8&&Commands.TryDequeue(out c)){
          string type=Text(c,"type");lock(Gate){NativeStart();if(type=="configure")GazeEnabled=Boolean(c,"gaze");else if(type=="effect")Emit(Effect(c));else if(type=="mode")Emit(Mode(c));else if(type=="reset")ResetAll("requested");else if(type=="close"){Stop=true;ResetAll("closed");}NativeDone();}
        }
        if(Now>=nextSample){lock(Gate){NativeStart();Sample();NativeDone();}nextSample=Now+17;}
        if(Now>=nextState){bool active;lock(Gate){NativeStart();active=Active();if(seenActive&&!active)DriverLost();NativeDone();}seenActive=seenActive||active;Emit(Obj("type","state","driverActive",active,"selectedModes",SelectedModes()));nextState=Now+500;}
        Thread.Sleep(2);
      }
      return 0;
    }catch(Exception){Emit(Obj("type","error","code","native_unavailable","message","Toolkit device interface unavailable"));return 2;}
    finally{
      Stop=true;
      if(Initialized){lock(Gate){try{ResetAll("process-exit");}catch{}}}
      if(pcmThread!=null)pcmThread.Join(1200);
      if(Initialized){lock(Gate){try{Deinit();}catch{}}}
      if(GazeBuffer!=IntPtr.Zero){Marshal.Copy(new byte[4096],0,GazeBuffer,4096);Marshal.FreeHGlobal(GazeBuffer);}if(TriggerBuffer!=IntPtr.Zero)Marshal.FreeHGlobal(TriggerBuffer);
      // A still-blocked broadcast waiter must not call into an unloaded DLL.
      if(Library!=IntPtr.Zero&&(pcmThread==null||!pcmThread.IsAlive))FreeLibrary(Library);
      CleanupComplete=true;if(watchdog!=null)watchdog.Join(200);
      Output.CompleteAdding();writer.Join(200);
    }
  }
}
