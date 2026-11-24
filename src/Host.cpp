// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "Host.h"

namespace host {
namespace {
Ptr base;
std::atomic<DWORD> sample{};
constexpr DWORD clockMask = 0x00ffffff;
using Poll = bool (__thiscall*)(void*, Ptr);
template<unsigned Backend> struct Reader {
    static inline Poll next;
    static bool __fastcall poll(void* self, void*, Ptr frame) {
        const bool ok = next(self, frame);
        unsigned bits = 0;
        if (ok && frame && frame[0x12c]) {
            for (int h=0; h<2; ++h) {
                Ptr pose = frame + 0x40 + h*0x40;
                if (!pose[0x1c] || !pose[0x1d]) continue;
                float grip;
                std::memcpy(&grip,frame+0x118+h*4,4);
                bits |= (1u | (unsigned(grip >= .55f)<<2) |
                         (unsigned(frame[0x120+h*5]!=0)<<4))<<h;
            }
        }
        sample.store(((GetTickCount()&clockMask)<<8)|bits);
        return ok;
    }
    static bool attach(unsigned rva) {
        auto slot = reinterpret_cast<void**>(base+rva)+6;
        void* previous = *slot;
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(previous,&page,sizeof page) || page.State!=MEM_COMMIT ||
            !(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
        DWORD old, ignored;
        if (!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old)) return false;
        next = reinterpret_cast<Poll>(previous);
        bool ok = InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(poll),previous)==previous;
        VirtualProtect(slot,sizeof(*slot),old,&ignored);
        return ok;
    }
};
}
bool connect() {
    base = reinterpret_cast<Ptr>(GetModuleHandleW(L"TTWVR.dll"));
    if (!base) return false;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const unsigned char takeStart[]{0x53,0x55,0x56,0xe8,0x48,0x46,0xff,0xff};
    const unsigned char drawStart[]{0x55,0x8b,0xec,0x6a,0xfe};
    if (nt->FileHeader.TimeDateStamp!=0x6afa9b4f || nt->OptionalHeader.SizeOfImage!=0x1d84000 ||
        std::memcmp(base+0x1cd9e0,takeStart,sizeof takeStart) ||
        std::memcmp(base+0x2259b0,drawStart,sizeof drawStart)) return false;
    bool vr=Reader<0>::attach(0xaf3568);
    return Reader<1>::attach(0xaf36d0) || vr;
}
Input input(int hand) {
    DWORD value=sample.load();
    if (((GetTickCount()-(value>>8))&clockMask)>250) return {};
    value >>= hand;
    return {bool(value&1),bool(value&4),bool(value&16)};
}
Ptr held(int hand) {
    if (base[0x1538805] && *reinterpret_cast<int*>(base+0x1035e60)==hand)
        return *reinterpret_cast<Ptr*>(base+0x15395e0);
    if (base[0x15388d4] && *reinterpret_cast<int*>(base+0x15388d8)==hand)
        return *reinterpret_cast<Ptr*>(base+0x15388dc);
    return nullptr;
}
bool pose(int hand,Vec& point,Vec& angles) {
    // TTWVR's world-space pose used by its own physical pickup query.
    using Fn=bool (__thiscall*)(Ptr,int,int,Vec*,float*);
    float matrix[9]{};
    if (!reinterpret_cast<Fn>(base+0x70acb0)(base+0x1d1fab0,hand,0,&point,matrix)) return false;
    angles={std::atan2(matrix[5],matrix[8]),std::asin(std::clamp(-matrix[2],-1.f,1.f)),
            std::atan2(matrix[1],matrix[0])};
    return point.valid() && angles.valid();
}
bool take(int hand) { return reinterpret_cast<bool (__cdecl*)(int)>(base+0x1cd9e0)(hand); }
bool draw(int hand, Ptr gun) { return reinterpret_cast<bool (__cdecl*)(int,Ptr)>(base+0x2259b0)(hand,gun); }
int drawHand() { return *reinterpret_cast<int*>(base+0x1038d74)==0 ? 0 : 1; }
}
