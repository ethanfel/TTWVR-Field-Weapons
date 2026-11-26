// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "Host.h"

namespace host {
namespace {
struct GrabSlot { unsigned active, hand, object; };
struct Layout {
    const char* version;
    unsigned timestamp, imageSize;
    unsigned xrTable, vrTable, xrValidStore, vrValidStore;
    GrabSlot primary, secondary;
    unsigned take, draw, drawHand, physicalPose, manager;
};
constexpr Layout layouts[] = {
    {"1.1.8", 0x6b07a53e, 0x1ffa000,
     0xcf62e8, 0xcf6450, 0x8302bb, 0x80b0f4,
     {0x1744d05,0x1240ed8,0x1745ae0}, {0x1744dd4,0x1744dd8,0x1744ddc},
     0x1dc4a0, 0x237590, 0x1243e84, 0x84d1b0, 0x1f8cbe0},
    {"1.1.7", 0x6afa9b4f, 0x1d84000,
     0xaf3568, 0xaf36d0, 0x6f0e9b, 0x6cbbb4,
     {0x1538805,0x1035e60,0x15395e0}, {0x15388d4,0x15388d8,0x15388dc},
     0x1cd9e0, 0x2259b0, 0x1038d74, 0x70acb0, 0x1d1fab0},
};
Ptr base;
const Layout* layout;

bool matches(const Layout& candidate) {
    const unsigned char validStore[]{0x88,0x86,0x2c,0x01,0,0};
    const unsigned char takeStart[]{0x53,0x55,0x56,0xe8};
    const unsigned char takeAfterCall[]{0x8b,0xe8,0x85,0xed,0x0f,0x84};
    const unsigned char drawStart[]{0x55,0x8b,0xec,0x6a,0xfe,0x68};
    const unsigned char poseStart[]{0x83,0xec,0x70,0xa1};
    return !std::memcmp(base+candidate.xrValidStore,validStore,sizeof validStore) &&
           !std::memcmp(base+candidate.vrValidStore,validStore,sizeof validStore) &&
           !std::memcmp(base+candidate.take,takeStart,sizeof takeStart) &&
           !std::memcmp(base+candidate.take+8,takeAfterCall,sizeof takeAfterCall) &&
           !std::memcmp(base+candidate.draw,drawStart,sizeof drawStart) &&
           !std::memcmp(base+candidate.physicalPose,poseStart,sizeof poseStart);
}
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
    if (layout) return true;
    base = reinterpret_cast<Ptr>(GetModuleHandleW(L"TTWVR.dll"));
    if (!base) return false;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    for (const auto& candidate : layouts) {
        if (nt->FileHeader.TimeDateStamp!=candidate.timestamp ||
            nt->OptionalHeader.SizeOfImage!=candidate.imageSize || !matches(candidate)) continue;
        bool xr=Reader<0>::attach(candidate.xrTable);
        bool vr=Reader<1>::attach(candidate.vrTable);
        if (xr || vr) layout=&candidate;
        break;
    }
    return layout!=nullptr;
}
const char* version() { return layout ? layout->version : "unavailable"; }
Input input(int hand) {
    DWORD value=sample.load();
    if (((GetTickCount()-(value>>8))&clockMask)>250) return {};
    value >>= hand;
    return {bool(value&1),bool(value&4),bool(value&16)};
}
Ptr held(int hand) {
    for (const GrabSlot* slot : {&layout->primary,&layout->secondary}) {
        if (base[slot->active] && *reinterpret_cast<int*>(base+slot->hand)==hand)
            return *reinterpret_cast<Ptr*>(base+slot->object);
    }
    return nullptr;
}
bool pose(int hand,Vec& point,Vec& angles) {
    // TTWVR's world-space pose used by its own physical pickup query.
    using Fn=bool (__thiscall*)(Ptr,int,int,Vec*,float*);
    float matrix[9]{};
    if (!reinterpret_cast<Fn>(base+layout->physicalPose)(base+layout->manager,hand,0,&point,matrix)) return false;
    angles={std::atan2(matrix[5],matrix[8]),std::asin(std::clamp(-matrix[2],-1.f,1.f)),
            std::atan2(matrix[1],matrix[0])};
    return point.valid() && angles.valid();
}
bool take(int hand) { return reinterpret_cast<bool (__cdecl*)(int)>(base+layout->take)(hand); }
bool draw(int hand, Ptr gun) { return reinterpret_cast<bool (__cdecl*)(int,Ptr)>(base+layout->draw)(hand,gun); }
int drawHand() { return *reinterpret_cast<int*>(base+layout->drawHand)==0 ? 0 : 1; }
}
