#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include "resource_heap.h"

namespace {
bool verified_executable() {
    wchar_t path[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool valid=false;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
       BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
        uint8_t buffer[65536];DWORD read=0;bool read_ok=true;
        for(;;) {
            if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)) {read_ok=false;break;}
            if(!read)break;
            if(BCryptHashData(hash,buffer,read,0)<0) {read_ok=false;break;}
        }
        uint8_t digest[32]{};
        constexpr uint8_t expected[]={0x04,0x03,0x98,0x1f,0xd2,0xe8,0xad,0x18,0xbd,0x8c,0x56,0x98,0xdf,0xf0,0x3c,0x8d,
            0x50,0x5b,0x5a,0x46,0x67,0x78,0x32,0xc4,0x96,0xdb,0x9c,0x64,0x55,0x13,0xf2,0xa6};
        valid=read_ok && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0 && std::memcmp(digest,expected,sizeof(digest))==0;
    }
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);return valid;
}
bool write_constant(uint8_t* code,size_t size,size_t offset,uint32_t value) {
    DWORD previous=0;
    if(!VirtualProtect(code,size,PAGE_EXECUTE_READWRITE,&previous))return false;
    std::memcpy(code+offset,&value,sizeof(value));
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),code,size)!=FALSE;
    DWORD unused=0;
    const bool protected_again=VirtualProtect(code,size,previous,&unused)!=FALSE;
    return flushed && protected_again;
}
bool prepare() {
    if(!verified_executable())return false;
    auto* base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* heap=base+speedmod::kHeapInitRva;
    if(speedmod::heap_instruction_matches(heap,true))return true;
    MEMORY_BASIC_INFORMATION arena{};
    if(!VirtualQuery(reinterpret_cast<void*>(0x20000000000ULL),&arena,sizeof(arena)) || arena.State==MEM_COMMIT ||
       !speedmod::heap_instruction_matches(heap))return false;
    return write_constant(heap,sizeof(speedmod::kOriginalHeapInit),7,speedmod::kResourceHeapBytes);
}
}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID) {
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    // Runs synchronously in EMF's chain, before its asynchronous mod loader and game Memory initialization.
    // The original executable is never edited. Exact SHA-256 + full instruction guard required.
    if(prepare())return TRUE;
    MessageBoxW(nullptr,L"SpeedMod resource heap bootstrap could not verify this game executable or loaded too late.\nSupported client build: 1283531.\nSee SpeedMod README.",L"SpeedMod startup error",MB_OK|MB_ICONERROR);
    ExitProcess(ERROR_BAD_EXE_FORMAT);
}
