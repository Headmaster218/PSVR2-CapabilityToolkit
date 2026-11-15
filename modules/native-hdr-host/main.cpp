#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "color_contract.hpp"
#include "pattern_vs.hpp"
#include "pattern_ps.hpp"
#include "stereo_adapter.hpp"
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
static_assert(sizeof(void*)==8,"x64 host required");
static_assert(sizeof(XrExtensionProperties)==152,"OpenXR x64 ABI");
static std::wstring loaderPath,runtimePath,manifestPath;
static std::string expectedLoaderHash,expectedRuntimeHash,expectedManifestHash;
static std::wstring Wide(const std::string& value) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.c_str(),-1,nullptr,0);if(!n)throw std::runtime_error("invalid UTF-8 path");
    std::wstring result(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.c_str(),-1,result.data(),n);result.pop_back();return result;
}
static std::atomic<bool> stopRequested{false};
static BOOL WINAPI Signal(DWORD x) { if(x==CTRL_C_EVENT||x==CTRL_BREAK_EVENT) { stopRequested=true; return TRUE; } return FALSE; }
static std::string Escape(const std::string& s) { std::string o; for(unsigned char c:s) { if(c=='"'||c=='\\') {o+='\\';o+=static_cast<char>(c);} else if(c=='\n')o+="\\n"; else if(c>=32)o+=static_cast<char>(c); } return o; }
static void Log(const std::string& event,const std::string& text) { std::cout<<"{\"event\":\""<<Escape(event)<<"\",\"detail\":\""<<Escape(text)<<"\"}"<<std::endl; }
static void Check(XrResult r,const char* name) { if(XR_FAILED(r)) throw std::runtime_error(std::string(name)+" result="+std::to_string(r)); }
static void Hr(HRESULT h,const char* name) { if(FAILED(h)) throw std::runtime_error(std::string(name)+" HRESULT="+std::to_string(static_cast<unsigned long>(h))); }
static bool ProcessExists(const wchar_t* name) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(snap==INVALID_HANDLE_VALUE)throw std::runtime_error("process snapshot failed");
    PROCESSENTRY32W p{}; p.dwSize=sizeof(p); bool found=false;
    if(Process32FirstW(snap,&p)) do {if(_wcsicmp(p.szExeFile,name)==0)found=true;} while(Process32NextW(snap,&p)); CloseHandle(snap); return found;
}
static std::wstring RegistryRuntime(HKEY root) {
    wchar_t value[2048]{}; DWORD bytes=sizeof(value);
    LSTATUS r=RegGetValueW(root,L"SOFTWARE\\Khronos\\OpenXR\\1",L"ActiveRuntime",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes);
    if(r==ERROR_FILE_NOT_FOUND)return L""; if(r!=ERROR_SUCCESS)throw std::runtime_error("cannot read runtime registry"); return value;
}
static std::string Hash(const wchar_t* file) {
    std::ifstream f(file,std::ios::binary); if(!f)throw std::runtime_error("hash input unavailable");
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; std::array<unsigned char,32> digest{}; std::array<char,65536> buffer{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA provider failed");
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0) {BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("SHA create failed");}
    bool ok=true; while(f) {f.read(buffer.data(),buffer.size()); auto n=f.gcount(); if(n>0 && BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(n),0)<0)ok=false;}
    if(f.bad()||BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)ok=false;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0); if(!ok)throw std::runtime_error("SHA input failed");
    std::ostringstream out; for(auto b:digest)out<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b); return out.str();
}
static void GuardRun() {
    wchar_t overrideValue[2]{}; if(GetEnvironmentVariableW(L"XR_RUNTIME_JSON",overrideValue,2)>0)throw std::runtime_error("XR_RUNTIME_JSON override present");
    auto machine=RegistryRuntime(HKEY_LOCAL_MACHINE), user=RegistryRuntime(HKEY_CURRENT_USER);
    if(_wcsicmp(machine.c_str(),manifestPath.c_str())!=0||(!user.empty()&&_wcsicmp(user.c_str(),manifestPath.c_str())!=0))throw std::runtime_error("reviewed SteamVR runtime not active");
    if(!ProcessExists(L"vrserver.exe")||!ProcessExists(L"vrcompositor.exe"))throw std::runtime_error("SteamVR must already be running; host never starts it");
    auto loaderHash=Hash(loaderPath.c_str()), runtimeHash=Hash(runtimePath.c_str()), manifestHash=Hash(manifestPath.c_str());
    if(loaderHash!=expectedLoaderHash||runtimeHash!=expectedRuntimeHash||manifestHash!=expectedManifestHash)throw std::runtime_error("runtime files/manifest changed; review local profile");
    Log("guard.runtime_hashes",loaderHash+" / "+runtimeHash+" manifest="+manifestHash);
}
template<class T,class F,class Init> static std::vector<T> Enumerate(F f,Init init) {
    uint32_t n=0; Check(f(0,&n,nullptr),"enumerate.count");
    for(int attempt=0;attempt<3;attempt++) {
        if(n>4096)throw std::runtime_error("enumeration exceeds bound");
        std::vector<T> values(n); for(auto& x:values)init(x); uint32_t written=0;
        XrResult r=f(n,&written,values.empty()?nullptr:values.data());
        if(r==XR_ERROR_SIZE_INSUFFICIENT) {n=written;continue;}
        Check(r,"enumerate.fill"); if(written>n)throw std::runtime_error("enumeration overflow"); values.resize(written);return values;
    } throw std::runtime_error("enumeration changed repeatedly");
}
struct Runtime {
    // These COM owners survive the destructor body and every XR destroy call.
    ComPtr<ID3D11Device> keepDevice;
    ComPtr<ID3D11DeviceContext> keepContext;
    HMODULE module{}; PFN_xrGetInstanceProcAddr gipa{}; XrInstance instance{}; XrSession session{}; XrSpace space{};
    bool begun=false, frameBegun=false; XrTime displayTime=0; XrSessionState state=XR_SESSION_STATE_UNKNOWN; bool quit=false;
    struct Chain {XrSwapchain handle{};uint32_t width=0,height=0;bool acquired=false,waited=false;std::vector<XrSwapchainImageD3D11KHR> images;};
    std::array<Chain,2> chains{};
    PFN_xrEnumerateInstanceExtensionProperties enumExt{}; PFN_xrCreateInstance createInstance{};
#define XR_FN(name) PFN_##name name{}
    XR_FN(xrDestroyInstance);XR_FN(xrGetInstanceProperties);XR_FN(xrGetSystem);XR_FN(xrGetD3D11GraphicsRequirementsKHR);
    XR_FN(xrCreateSession);XR_FN(xrDestroySession);XR_FN(xrEnumerateSwapchainFormats);XR_FN(xrEnumerateColorSpacesSONY);XR_FN(xrSetHdrMetadataSONY);
    XR_FN(xrEnumerateViewConfigurationViews);XR_FN(xrEnumerateEnvironmentBlendModes);XR_FN(xrCreateReferenceSpace);XR_FN(xrDestroySpace);
    XR_FN(xrCreateSwapchain);XR_FN(xrDestroySwapchain);XR_FN(xrEnumerateSwapchainImages);XR_FN(xrAcquireSwapchainImage);XR_FN(xrWaitSwapchainImage);XR_FN(xrReleaseSwapchainImage);
    XR_FN(xrPollEvent);XR_FN(xrBeginSession);XR_FN(xrEndSession);XR_FN(xrRequestExitSession);XR_FN(xrWaitFrame);XR_FN(xrBeginFrame);XR_FN(xrEndFrame);XR_FN(xrLocateViews);
#undef XR_FN
    template<class T> void Get(T& out,const char* name,XrInstance which) {PFN_xrVoidFunction raw{};Check(gipa(which,name,&raw),name);static_assert(sizeof(out)==sizeof(raw));std::memcpy(&out,&raw,sizeof(out));if(!out)throw std::runtime_error("null function");}
    void Load() {
        GuardRun(); module=LoadLibraryExW(loaderPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("loader LoadLibraryEx failed");
        FARPROC raw=GetProcAddress(module,"xrGetInstanceProcAddr");static_assert(sizeof(raw)==sizeof(gipa));std::memcpy(&gipa,&raw,sizeof(gipa));if(!gipa)throw std::runtime_error("missing gipa");
        Get(enumExt,"xrEnumerateInstanceExtensionProperties",XR_NULL_HANDLE);Get(createInstance,"xrCreateInstance",XR_NULL_HANDLE);
    }
    void Functions(bool metadata) {
#define GET(name) Get(name,#name,instance)
        GET(xrDestroyInstance);GET(xrGetInstanceProperties);GET(xrGetSystem);GET(xrGetD3D11GraphicsRequirementsKHR);GET(xrCreateSession);GET(xrDestroySession);
        GET(xrEnumerateSwapchainFormats);GET(xrEnumerateColorSpacesSONY);if(metadata)GET(xrSetHdrMetadataSONY);
        GET(xrEnumerateViewConfigurationViews);GET(xrEnumerateEnvironmentBlendModes);GET(xrCreateReferenceSpace);GET(xrDestroySpace);
        GET(xrCreateSwapchain);GET(xrDestroySwapchain);GET(xrEnumerateSwapchainImages);GET(xrAcquireSwapchainImage);GET(xrWaitSwapchainImage);GET(xrReleaseSwapchainImage);
        GET(xrPollEvent);GET(xrBeginSession);GET(xrEndSession);GET(xrRequestExitSession);GET(xrWaitFrame);GET(xrBeginFrame);GET(xrEndFrame);GET(xrLocateViews);
#undef GET
    }
    void Poll(bool mayBegin) {
        for(;;) {XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER};XrResult r=xrPollEvent(instance,&e);if(r==XR_EVENT_UNAVAILABLE)return;Check(r,"xrPollEvent");
            if(e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)quit=true;
            if(e.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {const auto& s=*reinterpret_cast<XrEventDataSessionStateChanged*>(&e);if(s.session!=session)continue;state=s.state;Log("session.state",std::to_string(state));
                if(state==XR_SESSION_STATE_READY&&mayBegin&&!begun) {XrSessionBeginInfo b{XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;Check(xrBeginSession(session,&b),"xrBeginSession");begun=true;}
                if(state==XR_SESSION_STATE_STOPPING&&begun) {Check(xrEndSession(session),"xrEndSession");begun=false;quit=true;}
                if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)quit=true;
            }
        }
    }
    ~Runtime() {
        if(keepContext){keepContext->ClearState();keepContext->Flush();}
        for(auto& c:chains)if(c.handle&&c.acquired&&c.waited&&xrReleaseSwapchainImage) {XrSwapchainImageReleaseInfo r{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(c.handle,&r);c.acquired=false;}
        // Destroy a never-waited acquired image before closing a pending frame.
        for(auto& c:chains)if(c.handle&&c.acquired&&!c.waited&&xrDestroySwapchain) {xrDestroySwapchain(c.handle);c.handle=XR_NULL_HANDLE;}
        if(frameBegun&&session&&xrEndFrame) {XrFrameEndInfo e{XR_TYPE_FRAME_END_INFO};e.displayTime=displayTime;e.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&e);}
        for(auto& c:chains)if(c.handle&&xrDestroySwapchain)xrDestroySwapchain(c.handle);
        if(space&&xrDestroySpace)xrDestroySpace(space);
        if(session&&xrDestroySession)xrDestroySession(session);
        if(instance&&xrDestroyInstance)xrDestroyInstance(instance);
        if(module)FreeLibrary(module);
    }
};
struct Options {bool run=false,pattern=false,self=false,help=false;double seconds=10,white=80,peak=200;};
static double Number(const char* p) {size_t used=0;double x=std::stod(p,&used);if(used!=std::strlen(p)||!std::isfinite(x))throw std::runtime_error("invalid numeric option");return x;}
static Options Parse(int argc,char** argv) {
    Options o;if(argc==1)o.help=true;
    bool capabilities=false;
    for(int i=1;i<argc;i++) {std::string a=argv[i];if(a=="--help")o.help=true;else if(a=="--self-test")o.self=true;else if(a=="--run")o.run=true;else if(a=="--pattern")o.pattern=true;else if(a=="--capabilities")capabilities=true;
        else if((a=="--seconds"||a=="--reference-white"||a=="--peak")&&i+1<argc) {double x=Number(argv[++i]);if(a=="--seconds")o.seconds=x;else if(a=="--reference-white")o.white=x;else o.peak=x;}
        else if((a=="--loader"||a=="--runtime"||a=="--manifest"||a=="--loader-sha256"||a=="--runtime-sha256"||a=="--manifest-sha256")&&i+1<argc) {
            std::string value=argv[++i];if(a=="--loader")loaderPath=Wide(value);else if(a=="--runtime")runtimePath=Wide(value);else if(a=="--manifest")manifestPath=Wide(value);else if(a=="--loader-sha256")expectedLoaderHash=value;else if(a=="--runtime-sha256")expectedRuntimeHash=value;else expectedManifestHash=value;
        }else throw std::runtime_error("unknown or incomplete option");}
    if((o.help||o.self)&&(o.run||o.pattern||capabilities|| (o.help&&o.self)))throw std::runtime_error("offline and VR modes cannot be mixed");
    if(o.pattern&&capabilities)throw std::runtime_error("choose capabilities or pattern");
    if(!o.help&&!o.self&&(!o.run||(!o.pattern&&!capabilities)))throw std::runtime_error("VR mode requires --run and --capabilities or --pattern");
    if(o.seconds<1||o.seconds>30||o.white<20||o.white>200||o.peak<o.white||o.peak>400)throw std::runtime_error("bounded settings: seconds 1..30, white 20..200, peak white..400");
    if(o.run&&(loaderPath.empty()||runtimePath.empty()||manifestPath.empty()||expectedLoaderHash.size()!=64||expectedRuntimeHash.size()!=64||expectedManifestHash.size()!=64))throw std::runtime_error("explicit paths and three SHA256 pins required; use reviewed local runtime profile");
    return o;
}
static int SelfTest() {
    if(GetModuleHandleW(L"openxr_loader.dll")||GetModuleHandleW(L"vrclient_x64.dll"))throw std::runtime_error("unexpected preloaded runtime in offline test");
    auto NearValue=[](double a,double b){if(std::abs(a-b)>1e-6)throw std::runtime_error("color test mismatch");};
    NearValue(native_hdr::PqFromNits(100),0.5080784215);NearValue(native_hdr::PqFromNits(1000),0.7518270962);NearValue(native_hdr::PqFromNits(10000),1);
    auto w=native_hdr::Relative709ToPq2020({1,1,1},80);for(auto x:w)NearValue(x,native_hdr::PqFromNits(80));
    for(double invalid:{-1.0,10001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try {native_hdr::PqFromNits(invalid);}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("invalid luminance accepted");
    }
    for(double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity()}) {
        bool rejected=false;try {native_hdr::Relative709ToPq2020({1,1,1},invalid);}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("invalid reference white accepted");
    }
    auto RejectOptions=[](std::initializer_list<const char*> args) {
        std::vector<std::string> text{"offline-test"};for(auto arg:args)text.emplace_back(arg);
        std::vector<char*> pointers;for(auto& item:text)pointers.push_back(item.data());
        bool rejected=false;try{Parse(static_cast<int>(pointers.size()),pointers.data());}catch(const std::exception&){rejected=true;}
        if(!rejected)throw std::runtime_error("invalid or non-opt-in arguments accepted");
    };
    RejectOptions({"--pattern"});RejectOptions({"--run"});
    RejectOptions({"--self-test","--run","--capabilities"});
    RejectOptions({"--run","--pattern","--reference-white","nan"});
    RejectOptions({"--run","--pattern","--peak","10001"});
    RejectOptions({"--run","--pattern","--reference-white","100","--peak","80"});
    RejectOptions({"--run","--capabilities","--seconds","0"});
    vr_hdr::PassRequest request{};request.content=vr_hdr::Content::NativeHdrLinearBt709;
    if(vr_hdr::MakePlan(request).status!=vr_hdr::Status::BypassNativeHdr)throw std::runtime_error("B native-HDR bypass contract mismatch");
    static_assert(sizeof(vr_hdr::Settings)==48,"B settings contract");
    Log("offline.self_test","PASS: PQ100/PQ1000/PQ10000, neutral BT709->BT2020, 7 invalid luminance/white cases, 7 invalid/opt-in CLI cases, B48/nativeHDR bypass; runtime not loaded; no device created");return 0;
}
static int Run(const Options& o) {
    Runtime xr;xr.Load();
    auto extensions=Enumerate<XrExtensionProperties>([&](uint32_t n,uint32_t* c,XrExtensionProperties* p){return xr.enumExt(nullptr,n,c,p);},[](auto& e){e.type=XR_TYPE_EXTENSION_PROPERTIES;});
    bool color=false,metadata=false,d3d=false;
    for(const auto& e:extensions) {if(!std::memchr(e.extensionName,0,XR_MAX_EXTENSION_NAME_SIZE))throw std::runtime_error("unterminated extension name");std::string name=e.extensionName;Log("extension",name+" version="+std::to_string(e.extensionVersion));color|=name==XR_SONY_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;metadata|=name==XR_SONY_HDR_METADATA_EXTENSION_NAME;d3d|=name==XR_KHR_D3D11_ENABLE_EXTENSION_NAME;}
    if(!color||!d3d) {Log("unsupported","required D3D11/SONY color-space extension absent; no instance/device/session created; OpenVR private paths not excluded");return 3;}
    if(o.pattern&&!metadata) {Log("unsupported","pattern requires explicit SONY HDR metadata as well as color space");return 3;}
    std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_SONY_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME};if(metadata)enabled.push_back(XR_SONY_HDR_METADATA_EXTENSION_NAME);
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};strcpy_s(ci.applicationInfo.applicationName,"Isolated native HDR evidence host");ci.applicationInfo.applicationVersion=1;strcpy_s(ci.applicationInfo.engineName,"No game injection");ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=static_cast<uint32_t>(enabled.size());ci.enabledExtensionNames=enabled.data();Check(xr.createInstance(&ci,&xr.instance),"xrCreateInstance");xr.Functions(metadata);
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};Check(xr.xrGetInstanceProperties(xr.instance,&ip),"xrGetInstanceProperties");Log("runtime",std::string(ip.runtimeName)+" version="+std::to_string(ip.runtimeVersion));
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system{};Check(xr.xrGetSystem(xr.instance,&si,&system),"xrGetSystem");
    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};Check(xr.xrGetD3D11GraphicsRequirementsKHR(xr.instance,system,&req),"xrGetD3D11GraphicsRequirementsKHR");
    Log("adapter.required_luid",std::to_string(req.adapterLuid.HighPart)+":"+std::to_string(req.adapterLuid.LowPart));
    ComPtr<IDXGIFactory1> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");ComPtr<IDXGIAdapter1> chosen;
    for(UINT index=0;;index++) {ComPtr<IDXGIAdapter1> a;HRESULT h=factory->EnumAdapters1(index,&a);if(h==DXGI_ERROR_NOT_FOUND)break;Hr(h,"EnumAdapters1");DXGI_ADAPTER_DESC1 desc{};Hr(a->GetDesc1(&desc),"GetDesc1");if(desc.AdapterLuid.HighPart==req.adapterLuid.HighPart&&desc.AdapterLuid.LowPart==req.adapterLuid.LowPart){chosen=a;break;}}
    if(!chosen)throw std::runtime_error("required physical adapter LUID not found");
    std::vector<D3D_FEATURE_LEVEL> levels;for(auto l:{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0})if(l>=req.minFeatureLevel)levels.push_back(l);if(levels.empty())throw std::runtime_error("required feature level exceeds host");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    Hr(D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,levels.data(),static_cast<UINT>(levels.size()),D3D11_SDK_VERSION,&device,&level,&context),"D3D11CreateDevice");
    xr.keepDevice=device;xr.keepContext=context;
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;Check(xr.xrCreateSession(xr.instance,&sessionInfo,&xr.session),"xrCreateSession");
    auto formats=Enumerate<int64_t>([&](uint32_t n,uint32_t* c,int64_t* p){return xr.xrEnumerateSwapchainFormats(xr.session,n,c,p);},[](auto&){});
    int64_t selected=0;
    for(auto f:formats) {XrColorSpacesEnumerateInfoSONY info{XR_TYPE_COLOR_SPACES_ENUMERATE_INFO_SONY};info.format=f;
        auto spaces=Enumerate<XrColorSpaceSONY>([&](uint32_t n,uint32_t* c,XrColorSpaceSONY* p){return xr.xrEnumerateColorSpacesSONY(xr.session,&info,n,c,p);},[](auto&){});
        std::string detail="DXGI="+std::to_string(f)+" color_spaces=";for(auto s:spaces)detail+=std::to_string(s)+",";Log("format.color_spaces",detail);
        bool pq=std::find(spaces.begin(),spaces.end(),XR_COLOR_SPACE_BT2020_PQ_SONY)!=spaces.end();
        if(pq&&(f==DXGI_FORMAT_R10G10B10A2_UNORM||f==DXGI_FORMAT_R16G16B16A16_FLOAT)) {
            UINT support=0;Hr(device->CheckFormatSupport(static_cast<DXGI_FORMAT>(f),&support),"CheckFormatSupport");if((support&D3D11_FORMAT_SUPPORT_RENDER_TARGET)&&(!selected||f==DXGI_FORMAT_R10G10B10A2_UNORM))selected=f;
        }
    }
    if(!selected){Log("unsupported","no render-target format with explicitly enumerated BT2020_PQ; FP16/10bit alone is insufficient");return 3;}
    Log("negotiation.selected","DXGI="+std::to_string(selected)+" SONY_color_space=9 BT2020_PQ; not physical HDR verification");
    if(!o.pattern){Log("capabilities.complete","no swapchain or frames created; destroying session/device/instance");return 0;}
    auto blend=Enumerate<XrEnvironmentBlendMode>([&](uint32_t n,uint32_t* c,XrEnvironmentBlendMode* p){return xr.xrEnumerateEnvironmentBlendModes(xr.instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,n,c,p);},[](auto&){});
    if(std::find(blend.begin(),blend.end(),XR_ENVIRONMENT_BLEND_MODE_OPAQUE)==blend.end())throw std::runtime_error("opaque blend mode required");
    auto views=Enumerate<XrViewConfigurationView>([&](uint32_t n,uint32_t* c,XrViewConfigurationView* p){return xr.xrEnumerateViewConfigurationViews(xr.instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,n,c,p);},[](auto& v){v.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;});if(views.size()!=2)throw std::runtime_error("two stereo views required");
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;spaceInfo.poseInReferenceSpace.orientation.w=1;Check(xr.xrCreateReferenceSpace(xr.session,&spaceInfo,&xr.space),"xrCreateReferenceSpace");
    for(size_t eye=0;eye<2;eye++) {auto& chain=xr.chains[eye];chain.width=views[eye].recommendedImageRectWidth;chain.height=views[eye].recommendedImageRectHeight;if(!chain.width||!chain.height||chain.width>8192||chain.height>8192)throw std::runtime_error("unexpected eye extent");
        XrSwapchainCreateInfoColorSpaceSONY cs{XR_TYPE_SWAPCHAIN_CREATE_INFO_COLOR_SPACE_SONY};cs.colorSpace=XR_COLOR_SPACE_BT2020_PQ_SONY;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.next=&cs;sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;sc.format=selected;sc.sampleCount=1;sc.width=chain.width;sc.height=chain.height;sc.faceCount=1;sc.arraySize=1;sc.mipCount=1;Check(xr.xrCreateSwapchain(xr.session,&sc,&chain.handle),"xrCreateSwapchain");
        chain.images=Enumerate<XrSwapchainImageD3D11KHR>([&](uint32_t n,uint32_t* c,XrSwapchainImageD3D11KHR* p){return xr.xrEnumerateSwapchainImages(chain.handle,n,c,reinterpret_cast<XrSwapchainImageBaseHeader*>(p));},[](auto& i){i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;});
        XrHdrMetadataSONY m{XR_TYPE_HDR_METADATA_SONY};m.displayPrimaryRed={0.708f,0.292f};m.displayPrimaryGreen={0.170f,0.797f};m.displayPrimaryBlue={0.131f,0.046f};m.whitePoint={0.3127f,0.3290f};m.maxLuminance=static_cast<float>(o.peak);m.minLuminance=0;m.maxContentLightLevel=static_cast<float>(o.peak);m.maxFrameAverageLightLevel=static_cast<float>(o.peak);Check(xr.xrSetHdrMetadataSONY(chain.handle,&m),"xrSetHdrMetadataSONY");
    }
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Hr(device->CreateVertexShader(g_PatternVS,sizeof(g_PatternVS),nullptr,&vs),"CreateVertexShader");Hr(device->CreatePixelShader(g_PatternPS,sizeof(g_PatternPS),nullptr,&ps),"CreatePixelShader");
    struct alignas(16) Pattern {float white,peak,pad[2];};Pattern pattern{static_cast<float>(o.white),static_cast<float>(o.peak),{0,0}};
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(pattern);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=&pattern;ComPtr<ID3D11Buffer> cb;Hr(device->CreateBuffer(&bd,&initial,&cb),"CreateBuffer");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;Hr(device->CreateRasterizerState(&rd,&raster),"CreateRasterizerState");
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->PSSetConstantBuffers(0,1,cb.GetAddressOf());context->RSSetState(raster.Get());
    Log("pattern.source","two eyes, identical known linear BT709 patches; white="+std::to_string(o.white)+" peak="+std::to_string(o.peak)+" encoded nits, no physical luminance claim; B enhancement bypassed");
    SetConsoleCtrlHandler(Signal,TRUE);auto deadline=Clock::now()+std::chrono::milliseconds(static_cast<int>(o.seconds*1000));uint64_t frames=0;
    while(!stopRequested&&!xr.quit&&Clock::now()<deadline) {xr.Poll(true);if(!xr.begun){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
        XrFrameWaitInfo fw{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};Check(xr.xrWaitFrame(xr.session,&fw,&fs),"xrWaitFrame");XrFrameBeginInfo fb{XR_TYPE_FRAME_BEGIN_INFO};Check(xr.xrBeginFrame(xr.session,&fb),"xrBeginFrame");xr.frameBegun=true;xr.displayTime=fs.predictedDisplayTime;
        std::array<XrView,2> located{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=fs.predictedDisplayTime;locate.space=xr.space;XrViewState viewState{XR_TYPE_VIEW_STATE};uint32_t count=0;Check(xr.xrLocateViews(xr.session,&locate,&viewState,2,&count,located.data()),"xrLocateViews");
        std::array<XrCompositionLayerProjectionView,2> projections{};XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=xr.space;projection.viewCount=2;projection.views=projections.data();const XrCompositionLayerBaseHeader* layer=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
        const XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;bool render=fs.shouldRender&&count==2&&(viewState.viewStateFlags&valid)==valid;
        if(render)for(size_t eye=0;eye<2;eye++){auto& chain=xr.chains[eye];uint32_t image=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};Check(xr.xrAcquireSwapchainImage(chain.handle,&ai,&image),"xrAcquireSwapchainImage");chain.acquired=true;chain.waited=false;
            XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=1000000000;XrResult waited=xr.xrWaitSwapchainImage(chain.handle,&wi);if(waited!=XR_SUCCESS)throw std::runtime_error("xrWaitSwapchainImage not ready, result="+std::to_string(waited));chain.waited=true;if(image>=chain.images.size())throw std::runtime_error("image index exceeds capacity");
            D3D11_RENDER_TARGET_VIEW_DESC tv{};tv.Format=static_cast<DXGI_FORMAT>(selected);tv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;ComPtr<ID3D11RenderTargetView> target;Hr(device->CreateRenderTargetView(chain.images[image].texture,&tv,&target),"CreateRenderTargetView");D3D11_VIEWPORT viewport{0,0,static_cast<float>(chain.width),static_cast<float>(chain.height),0,1};context->RSSetViewports(1,&viewport);context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);context->Flush();
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};Check(xr.xrReleaseSwapchainImage(chain.handle,&release),"xrReleaseSwapchainImage");chain.acquired=false;chain.waited=false;
            auto& p=projections[eye];p.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;p.pose=located[eye].pose;p.fov=located[eye].fov;p.subImage.swapchain=chain.handle;p.subImage.imageRect.extent={static_cast<int32_t>(chain.width),static_cast<int32_t>(chain.height)};
        }
        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};fe.displayTime=fs.predictedDisplayTime;fe.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;fe.layerCount=render?1u:0u;fe.layers=render?&layer:nullptr;Check(xr.xrEndFrame(xr.session,&fe),"xrEndFrame");xr.frameBegun=false;if(render)frames++;
    }
    context->ClearState();context->Flush();if(xr.begun&&!xr.quit){Check(xr.xrRequestExitSession(xr.session),"xrRequestExitSession");auto end=Clock::now()+std::chrono::seconds(2);while(xr.begun&&Clock::now()<end){xr.Poll(false);std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
    SetConsoleCtrlHandler(Signal,FALSE);Log("pattern.finished","submitted_frames="+std::to_string(frames)+"; physical HDR unverified");return frames?0:4;
}
int wmain(int argc,wchar_t** wideArgv) {
    std::vector<std::string> utf8;std::vector<char*> argv;
    for(int i=0;i<argc;i++){int n=WideCharToMultiByte(CP_UTF8,0,wideArgv[i],-1,nullptr,0,nullptr,nullptr);std::string s(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,wideArgv[i],-1,s.data(),n,nullptr,nullptr);s.pop_back();utf8.push_back(std::move(s));}
    for(auto& s:utf8)argv.push_back(s.data());
    try {auto o=Parse(argc,argv.data());if(o.help){std::cout<<"Offline defaults: --help | --self-test\nExplicit VR via launcher: --run --capabilities | --run --pattern (requires profile paths and three SHA256 pins)\nNo automatic SteamVR start; no API layer registration; no game injection.\n";return 0;}if(o.self)return SelfTest();int result=Run(o);Log("cleanup.completed","owned XR/D3D resources destroyed; existing SteamVR left running");return result;
    }catch(const std::exception& e){Log("failure",e.what());return 2;}
}
