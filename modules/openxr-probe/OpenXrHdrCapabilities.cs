// Derived from the machine's 20261112 OpenXrExtensionsReadOnly.cs.
// No XrInstance, session, graphics binding, swapchain, rendering or device commands.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;

public static class OpenXrHdrCapabilities {
    static IntPtr ownedLoader;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string file,IntPtr reserved,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [StructLayout(LayoutKind.Sequential)]
    struct ExtensionProperties {
        public int type;
        public IntPtr next;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=128)] public byte[] name;
        public uint version;
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] delegate int EnumerateFn(IntPtr layer,uint capacity,out uint count,IntPtr properties);
    static EnumerateFn xrEnumerateInstanceExtensionProperties;
    static object Abi() {
        int size=Marshal.SizeOf(typeof(ExtensionProperties));
        int next=(int)Marshal.OffsetOf(typeof(ExtensionProperties),"next");
        int name=(int)Marshal.OffsetOf(typeof(ExtensionProperties),"name");
        int version=(int)Marshal.OffsetOf(typeof(ExtensionProperties),"version");
        if(IntPtr.Size!=8 || size!=152 || next!=8 || name!=16 || version!=144)
            throw new Exception("Unexpected x64 OpenXR ABI");
        return new { PointerSize=IntPtr.Size, Size=size, NextOffset=next, NameOffset=name, VersionOffset=version };
    }
    static int[] Running(string name) {
        var ids=new List<int>();
        foreach(var p in Process.GetProcessesByName(name)) { using(p) ids.Add(p.Id); }
        return ids.ToArray();
    }
    static object Read(string loader,string expectedHash) {
        object abi=Abi();
        int[] server=Running("vrserver"), compositor=Running("vrcompositor");
        if(server.Length==0 || compositor.Length==0)
            throw new Exception("SteamVR server and compositor must already be running; probe will not start them");
        if(!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("XR_RUNTIME_JSON")))
            throw new Exception("XR_RUNTIME_JSON override present; refuse ambiguous runtime");
        using(var file=System.IO.File.OpenRead(loader))using(var sha=System.Security.Cryptography.SHA256.Create()) {
            if(BitConverter.ToString(sha.ComputeHash(file)).Replace("-","")!=expectedHash)throw new Exception("loader hash changed");
        }
        ownedLoader=LoadLibraryEx(System.IO.Path.GetFullPath(loader),IntPtr.Zero,0x100|0x800);
        if(ownedLoader==IntPtr.Zero)throw new Exception("loader load failed");
        IntPtr export=GetProcAddress(ownedLoader,"xrEnumerateInstanceExtensionProperties");
        if(export==IntPtr.Zero)throw new Exception("enumeration export missing");
        xrEnumerateInstanceExtensionProperties=(EnumerateFn)Marshal.GetDelegateForFunctionPointer(export,typeof(EnumerateFn));
        uint required=0; int first=xrEnumerateInstanceExtensionProperties(IntPtr.Zero,0,out required,IntPtr.Zero);
        var extensions=new List<object>(); var names=new HashSet<string>();
        int result=first; bool complete=false;
        for(int attempt=0; first>=0 && attempt<3; attempt++) {
            if(required>4096) throw new Exception("Unexpected extension count");
            if(required==0) { complete=true; break; }
            uint capacity=required; int bytes=checked((int)capacity*152);
            IntPtr buffer=Marshal.AllocHGlobal(bytes);
            try {
                Marshal.Copy(new byte[bytes],0,buffer,bytes);
                for(int i=0;i<capacity;i++) Marshal.WriteInt32(buffer,i*152,2); // XR_TYPE_EXTENSION_PROPERTIES
                result=xrEnumerateInstanceExtensionProperties(IntPtr.Zero,capacity,out required,buffer);
                if(result==-11) continue; // XR_ERROR_SIZE_INSUFFICIENT; bounded retry.
                if(result<0) break;
                if(required>capacity) throw new Exception("Runtime exceeded extension buffer");
                for(int i=0;i<required;i++) {
                    IntPtr entry=IntPtr.Add(buffer,i*152);
                    var item=(ExtensionProperties)Marshal.PtrToStructure(entry,typeof(ExtensionProperties));
                    int end=Array.IndexOf(item.name,(byte)0);
                    if(end<0) throw new Exception("Unterminated extension name");
                    string name=System.Text.Encoding.ASCII.GetString(item.name,0,end);
                    names.Add(name); extensions.Add(new { Name=name, Version=item.version });
                }
                complete=true; break;
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        var modules=new List<string>();
        using(var self=Process.GetCurrentProcess()) foreach(ProcessModule m in self.Modules) {
            string name=m.ModuleName.ToLowerInvariant();
            if(name.Contains("openxr") || name.Contains("vrclient") || name.Contains("api_layer")) modules.Add(m.FileName);
        }
        return new {
            AtUtc=DateTime.UtcNow.ToString("o"), Abi=abi, ServerPids=server, CompositorPids=compositor,
            CountResult=first, EnumerationResult=result, Complete=complete, Count=extensions.Count,
            SonySwapchainColorSpace=complete ? (bool?)names.Contains("XR_SONY_swapchain_color_space") : null,
            SonyHdrMetadata=complete ? (bool?)names.Contains("XR_SONY_hdr_metadata") : null,
            Extensions=extensions, LoadedModules=modules,
            Scope="Available extensions from loader with NULL layer; implicit layers may contribute. No instance/session created.",
            Formats="Not queried: xrEnumerateSwapchainFormats requires a valid session.",
            ColorSpaces="Not queried: xrEnumerateColorSpacesSONY requires enabled extension and a valid session.",
            PhysicalHdrOutput="Not measured; advertisement is not an HDR output test."
        };
    }
    public static int Main(string[] args) {
        try {
            bool self=args.Length==1&&args[0]=="--self-test";
            if(!self && (args.Length!=5||args[0]!="--enumerate"||args[1]!="--loader"||args[3]!="--sha256"))throw new Exception("Expected --self-test or explicit --enumerate --loader PATH --sha256 PIN");
            object result=self ? new { SelfTest=true, Abi=Abi(), NativeApiCalled=false } : Read(args[2],args[4]);
            Console.WriteLine(new JavaScriptSerializer().Serialize(result)); return 0;
        } catch(Exception e) {
            Console.WriteLine(new JavaScriptSerializer().Serialize(new { Error=e.GetType().Name, Message=e.Message })); return 2;
        } finally { if(ownedLoader!=IntPtr.Zero)FreeLibrary(ownedLoader); }
    }
}
