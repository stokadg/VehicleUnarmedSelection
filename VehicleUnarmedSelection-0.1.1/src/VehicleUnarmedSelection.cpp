#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include "HookCode.h"

static_assert(sizeof(void*)==4,"Build for x86, not x64");
static HANDLE logFile=INVALID_HANDLE_VALUE;
static volatile LONG counts[4]{};
static bool installed=false;

static void Log(const char* fmt,...) {
    if(logFile==INVALID_HANDLE_VALUE)return;
    char line[1024]; SYSTEMTIME t; GetLocalTime(&t);
    int n=snprintf(line,sizeof(line),"[%02u:%02u:%02u] ",t.wHour,t.wMinute,t.wSecond);
    va_list args;va_start(args,fmt);
    vsnprintf(line+n,sizeof(line)-n-3,fmt,args);va_end(args);
    strcat_s(line,"\r\n");DWORD written;
    WriteFile(logFile,line,DWORD(strlen(line)),&written,nullptr);FlushFileBuffers(logFile);
}
static bool Version1080() {
    wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))return false;
    DWORD ignored=0,n=GetFileVersionInfoSizeW(path,&ignored);if(!n)return false;
    std::vector<uint8_t> data(n);
    if(!GetFileVersionInfoW(path,0,n,data.data()))return false;
    VS_FIXEDFILEINFO* v=nullptr;UINT size=0;
    if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&v),&size)||size<sizeof(*v))return false;
    Log("EXE version %u.%u.%u.%u",HIWORD(v->dwFileVersionMS),LOWORD(v->dwFileVersionMS),HIWORD(v->dwFileVersionLS),LOWORD(v->dwFileVersionLS));
    return v->dwSignature==0xFEEF04BD && v->dwFileVersionMS==0x10000 && v->dwFileVersionLS==0x80000;
}
static bool InExecutableSection(uint8_t* base,uint32_t rva,size_t length) {
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        if((section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE) && rva>=section[i].VirtualAddress && uint64_t(rva)+length<=uint64_t(section[i].VirtualAddress)+section[i].Misc.VirtualSize)return true;
    return false;
}
static void Init(HMODULE self) {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(self,path,32768);
    if(!n||n>=32768)return;
    std::wstring prefix(path);auto dot=prefix.find_last_of(L'.');if(dot!=std::wstring::npos)prefix.resize(dot);
    logFile=CreateFileW((prefix+L".log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    Log("VehicleUnarmedSelection 0.1.1 beta / x86 / GTA IV 1.0.8.0");
    auto ini=prefix+L".ini";
    if(!GetPrivateProfileIntW(L"MAIN",L"Enabled",1,ini.c_str())){Log("Disabled by INI; no patches installed.");return;}
    if(!Version1080()){Log("Unsupported EXE; requires 1.0.8.0 EFIGS. No patches installed.");return;}
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    Log("Game base=%p",base);
    auto sites=vus::Sites();
    // Validate all sites BEFORE allocating or modifying any game code. Unknown hooks are refused.
    for(const auto& s:sites) {
        if(!InExecutableSection(base,s.rva,s.expected.size()) || memcmp(base+s.rva,s.expected.data(),s.expected.size())) {
            Log("CONFLICT/mismatch at %s RVA=%08X. No patches installed; do not force compatibility.",s.name,s.rva);return;
        }
    }
    // Guard the common commit target and both loop-entry contexts too.
    const uint8_t commit[]={0x85,0xF6,0x74,0x36};
    const uint8_t contextA[]={0x8B,0x5C,0x24,0x44};
    if(memcmp(base+0x5CC71B,commit,sizeof(commit)) || memcmp(base+0x5CC2D0,contextA,4) || memcmp(base+0x5CC37D,contextA,4)) {
        Log("Selector context mismatch; no patches installed.");return;
    }
    auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code){Log("VirtualAlloc failed (%lu)",GetLastError());return;}
    for(size_t i=0;i<sites.size();++i) {
        auto stub=vus::Stub(sites[i],uint32_t(uintptr_t(code+i*256)),uint32_t(uintptr_t(base)),uint32_t(uintptr_t(&counts[i])));
        if(stub.size()>256){Log("Internal stub size error");VirtualFree(code,0,MEM_RELEASE);return;}
        memcpy(code+i*256,stub.data(),stub.size());
    }
    DWORD oldCode=0;
    if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&oldCode)){Log("Cannot make stubs executable");VirtualFree(code,0,MEM_RELEASE);return;}
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    // All four sites are in one 4 KiB page. Acquire once, then commit together.
    auto page=base+0x5CC000;DWORD old=0;
    if(!VirtualProtect(page,4096,PAGE_EXECUTE_READWRITE,&old)){Log("Cannot write selector page");VirtualFree(code,0,MEM_RELEASE);return;}
    // ASI loader startup only: no hot-load/unload support. No delayed worker patching.
    for(size_t i=0;i<sites.size();++i) {
        auto patch=vus::Jump(uint32_t(uintptr_t(base+sites[i].rva)),uint32_t(uintptr_t(code+i*256)),sites[i].expected.size());
        memcpy(base+sites[i].rva,patch.data(),patch.size());
    }
    DWORD ignored=0;
    if(!VirtualProtect(page,4096,old,&ignored))Log("WARNING: failed to restore code page protection (%lu)",GetLastError());
    FlushInstructionCache(GetCurrentProcess(),page,4096);
    // Keep DLL/counters alive as long as the game; uninstall by deleting files while game is closed.
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Init),&pinned);
    installed=true;
    for(const auto& s:sites)Log("Installed %s at RVA=%08X address=%p",s.name,s.rva,base+s.rva);
    Log("No pad, attack, aim, reticle, script-tick or weapon-table patches. Counters logged on orderly exit.");
}
BOOL WINAPI DllMain(HMODULE self,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(self);Init(self);}
    if(reason==DLL_PROCESS_DETACH){
        if(installed)Log("Exit counters: cycle-decreasing=%ld cycle-increasing=%ld retained=%ld driveby-task-retained=%ld",counts[0],counts[1],counts[2],counts[3]);
        if(logFile!=INVALID_HANDLE_VALUE){CloseHandle(logFile);logFile=INVALID_HANDLE_VALUE;}
    }
    return TRUE;
}

