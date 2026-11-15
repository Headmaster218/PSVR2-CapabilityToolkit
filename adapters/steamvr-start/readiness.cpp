// SPDX-License-Identifier: Apache-2.0
// Application-side Background connection: no scene, frames, haptics or runtime launch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <openvr.h>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
static bool Exists(const wchar_t* name){HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return false;PROCESSENTRY32W p{};p.dwSize=sizeof(p);bool yes=false;if(Process32FirstW(h,&p))do{if(!_wcsicmp(p.szExeFile,name))yes=true;}while(Process32NextW(h,&p));CloseHandle(h);return yes;}
static std::string Hash(const wchar_t* path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("API unavailable");BCRYPT_ALG_HANDLE a{};BCRYPT_HASH_HANDLE h{};if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA provider");if(BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(a,0);throw std::runtime_error("SHA create");}std::array<char,65536>b{};bool good=true;while(f){f.read(b.data(),b.size());if(f.gcount()>0&&BCryptHashData(h,reinterpret_cast<PUCHAR>(b.data()),static_cast<ULONG>(f.gcount()),0)<0)good=false;}std::array<unsigned char,32>d{};if(f.bad()||BCryptFinishHash(h,d.data(),static_cast<ULONG>(d.size()),0)<0)good=false;BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);if(!good)throw std::runtime_error("SHA read");std::ostringstream s;for(auto v:d)s<<std::uppercase<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(v);return s.str();}
template<class T>static T Export(HMODULE m,const char* name){FARPROC raw=GetProcAddress(m,name);T fn{};static_assert(sizeof(raw)==sizeof(fn));std::memcpy(&fn,&raw,sizeof(fn));if(!fn)throw std::runtime_error("API export missing");return fn;}
static bool Ready(bool connected,bool pose,bool compositor,bool advancing){return connected&&pose&&compositor&&advancing;}
int wmain(int argc,wchar_t** argv){
    if(argc==1||(argc==2&&!wcscmp(argv[1],L"--help"))){std::cout<<"Offline --self-test; explicit --probe --api PATH --sha256 CURRENT_VERIFIED_HASH\n";return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--self-test")){if(Ready(false,true,true,true)||Ready(true,false,true,true)||Ready(true,true,false,true)||Ready(true,true,true,false)||!Ready(true,true,true,true))return 2;std::cout<<"{\"selfTest\":true,\"runtimeLoaded\":false,\"readinessTruthTable\":true}\n";return 0;}
    if(argc!=6||wcscmp(argv[1],L"--probe")||wcscmp(argv[2],L"--api")||wcscmp(argv[4],L"--sha256"))return 2;
    bool running=Exists(L"vrserver.exe"),compositorProcess=Exists(L"vrcompositor.exe");
    if(!running){std::cout<<"{\"ready\":false,\"runtimeRunning\":false,\"hmdConnected\":false,\"poseValid\":false,\"compositorTimingAdvancing\":false}\n";return 0;}
    HMODULE library{};bool initialized=false;using Shutdown=void(__cdecl*)();Shutdown shutdown{};
    try{
        std::string expected;for(const wchar_t* p=argv[5];*p;p++)expected+=static_cast<char>(*p);
        if(Hash(argv[3])!=expected)throw std::runtime_error("API changed after install verification");
        library=LoadLibraryExW(argv[3],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)throw std::runtime_error("API load failed");
        using Init=uint32_t(__cdecl*)(vr::EVRInitError*,vr::EVRApplicationType,const char*);using Get=void*(__cdecl*)(const char*,vr::EVRInitError*);
        auto init=Export<Init>(library,"VR_InitInternal2");shutdown=Export<Shutdown>(library,"VR_ShutdownInternal");auto get=Export<Get>(library,"VR_GetGenericInterface");
        vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Background,nullptr);
        if(error!=vr::VRInitError_None){std::cout<<"{\"ready\":false,\"runtimeRunning\":true,\"initError\":"<<error<<"}\n";FreeLibrary(library);return 0;}
        initialized=true;auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));if(!system||error!=vr::VRInitError_None)throw std::runtime_error("system interface unavailable");
        auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));bool connected=false,pose=false,advancing=false,left=false,right=false,leftPose=false,rightPose=false;uint32_t firstFrame=0;bool haveFrame=false;
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do{
            std::array<vr::TrackedDevicePose_t,vr::k_unMaxTrackedDeviceCount> poses{};system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses.data(),static_cast<uint32_t>(poses.size()));
            connected=system->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd);pose=poses[0].bDeviceIsConnected&&poses[0].bPoseIsValid;
            for(uint32_t i=1;i<poses.size();i++)if(system->GetTrackedDeviceClass(i)==vr::TrackedDeviceClass_Controller){auto role=system->GetControllerRoleForTrackedDeviceIndex(i);if(role==vr::TrackedControllerRole_LeftHand){left=poses[i].bDeviceIsConnected;leftPose=poses[i].bPoseIsValid;}if(role==vr::TrackedControllerRole_RightHand){right=poses[i].bDeviceIsConnected;rightPose=poses[i].bPoseIsValid;}}
            if(compositor){vr::Compositor_FrameTiming t{};t.m_nSize=sizeof(t);if(compositor->GetFrameTiming(&t,0)){if(haveFrame&&t.m_nFrameIndex!=firstFrame)advancing=true;else if(!haveFrame){firstFrame=t.m_nFrameIndex;haveFrame=true;}}}
            if(Ready(connected,pose,compositorProcess&&compositor,advancing))break;std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }while(std::chrono::steady_clock::now()<until);
        std::cout<<std::boolalpha<<"{\"ready\":"<<Ready(connected,pose,compositorProcess&&compositor,advancing)<<",\"runtimeRunning\":true,\"hmdConnected\":"<<connected<<",\"poseValid\":"<<pose<<",\"compositorConnected\":"<<(compositorProcess&&compositor)<<",\"compositorTimingAdvancing\":"<<advancing<<",\"leftControllerConnected\":"<<left<<",\"rightControllerConnected\":"<<right<<",\"leftControllerPoseValid\":"<<leftPose<<",\"rightControllerPoseValid\":"<<rightPose<<"}\n";
        shutdown();initialized=false;FreeLibrary(library);return 0;
    }catch(const std::exception&){if(initialized&&shutdown)shutdown();if(library)FreeLibrary(library);std::cout<<"{\"ready\":false,\"runtimeRunning\":true,\"probeError\":\"runtime-api-unavailable\"}\n";return 0;}
}
