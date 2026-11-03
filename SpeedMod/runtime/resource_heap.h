#pragma once
#include <cstdint>
#include <cstring>
namespace speedmod {
// Build 1283531: mov qword ptr [rbx+88h], 0x1ea00000 (GF_Resource).
// The 490 MiB arena overflows while loading the full variant table.
constexpr uint32_t kHeapInitRva = 0x8486f6;
constexpr uint32_t kResourceHeapBytes = 768u * 1024u * 1024u;
constexpr uint8_t kOriginalHeapInit[] = {0x48,0xc7,0x83,0x88,0,0,0,0,0,0xa0,0x1e};
inline bool heap_instruction_matches(const uint8_t* code, bool expanded=false) {
    uint8_t expected[sizeof(kOriginalHeapInit)];
    std::memcpy(expected,kOriginalHeapInit,sizeof(expected));
    if(expanded)std::memcpy(expected+7,&kResourceHeapBytes,sizeof(kResourceHeapBytes));
    return std::memcmp(code,expected,sizeof(expected))==0;
}
}
