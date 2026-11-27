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
    unsigned holsterActionCall, holsterAction, handSlots, nearestHolster;
    unsigned magazineReturn, spareReturn;
};
constexpr Layout layouts[] = {
    {"1.1.8", 0x6b07a53e, 0x1ffa000,
     0xcf62e8, 0xcf6450, 0x8302bb, 0x80b0f4,
     {0x1744d05,0x1240ed8,0x1745ae0}, {0x1744dd4,0x1744dd8,0x1744ddc},
     0x1dc4a0, 0x237590, 0x1243e84, 0x84d1b0, 0x1f8cbe0, 0x237aab, 0x23e7c0, 0x1243fcc, 0x23cbd0, 0x3d2132, 0x3d4c80},
    {"1.1.7", 0x6afa9b4f, 0x1d84000,
     0xaf3568, 0xaf36d0, 0x6f0e9b, 0x6cbbb4,
     {0x1538805,0x1035e60,0x15395e0}, {0x15388d4,0x15388d8,0x15388dc},
     0x1cd9e0, 0x2259b0, 0x1038d74, 0x70acb0, 0x1d1fab0, 0x225ecb, 0x22a440, 0x1038e1c, 0x229680, 0x3b4a4c, 0x3b729e},
};
Ptr base;
const Layout* layout;
bool reloadCache;
using WeaponSlot=int (__cdecl*)(Ptr);
int (*wornReloadSlot)(Ptr);
int __cdecl reloadSlot(Ptr gun) {
    int exact=wornReloadSlot ? wornReloadSlot(gun) : -2;
    return exact>=-1 ? exact : reinterpret_cast<WeaponSlot>(base+0x23a000)(gun);
}
bool reloadActive(Ptr gun) {
    return reloadCache && base[0x1c11f3d] && *reinterpret_cast<Ptr*>(base+0x1c11fa4)==gun;
}

// Observe only TTWVR's physical slot action. Wheel, keyboard and inventory equips
// never enter this call. The game thread checks which exact stack was drawn.
constexpr DWORD clockMask = 0x00ffffff;
using SlotAction=void (__cdecl*)(int,int,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
SlotAction slotAction{};
std::atomic<DWORD> holsterEvents[2]{};
void __cdecl observeSlot(int hand,int slot,unsigned a,unsigned b,unsigned c,unsigned d,
                         unsigned e,unsigned f,unsigned g,unsigned h) {
    slotAction(hand,slot,a,b,c,d,e,f,g,h);
    if (unsigned(hand)>=2) return;
    int selected;
    std::memcpy(&selected,base+layout->handSlots+hand*4,4);
    const bool accepted=unsigned(slot)<8 && selected==slot;
    holsterEvents[hand].store(accepted ? ((GetTickCount()&clockMask)<<8)|unsigned(slot+1) : 0);
}
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
        if (xr || vr) {
            layout=&candidate;
            // Older hosts use Fallout's equipped clip. 1.1.8 also restores a
            // per-slot reload record, including live/spent shells and the action.
            const unsigned char capture[]{0x83,0xec,0x34,0x80,0x3d};
            const unsigned char remember[]{0x56,0xff,0x74,0x24,0x10,0xe8};
            const unsigned char restore[]{0x83,0xec,0x38,0x83,0x3d};
            const unsigned char slot[]{0x83,0xec,0x1c,0xa1};
            reloadCache=candidate.timestamp==0x6b07a53e &&
                !std::memcmp(base+0x3a9ad0,capture,sizeof capture) &&
                !std::memcmp(base+0x3b8f30,remember,sizeof remember) &&
                !std::memcmp(base+0x39a9a0,restore,sizeof restore) &&
                !std::memcmp(base+0x23a000,slot,sizeof slot);
        }
        break;
    }
    return layout!=nullptr;
}
bool readReload(Ptr gun,ReloadState& state) {
    using Fn=bool (__cdecl*)(ReloadState*);
    return reloadActive(gun) && reinterpret_cast<Fn>(base+0x3a9ad0)(&state);
}
bool observeReloadSlots(int (*slotForWornCopy)(Ptr)) {
    if (!reloadCache || !slotForWornCopy) return false;
    if (wornReloadSlot) return true;
    // TTWVR saves its active magazine under the first matching base form's
    // holster. Use the equipped instance's key when Field Weapons handles a copy.
    Ptr site=base+0x3b8c0d, thunk=base+0x2f63a;
    int rel,tail;
    std::memcpy(&rel,site+1,4);
    std::memcpy(&tail,thunk+1,4);
    Ptr currentKey;
    std::memcpy(&currentKey,site+10,4);
    if (site[0]!=0xe8 || site+5+rel!=thunk || thunk[0]!=0xe9 ||
        thunk+5+tail!=base+0x23a000 || site[9]!=0xa3 || currentKey!=base+0x124c05c) return false;
    DWORD old,ignored;
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)) return false;
    wornReloadSlot=slotForWornCopy;
    auto disp=DWORD(reinterpret_cast<uintptr_t>(reloadSlot)-reinterpret_cast<uintptr_t>(site+5));
    std::memcpy(site+1,&disp,4);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    VirtualProtect(site,5,old,&ignored);
    return true;
}
bool rememberReload(Ptr gun,Ptr ammo,const ReloadState& state,int numberedSlot) {
    if (!reloadCache || !gun || !ammo) return false;
    using Remember=void (__cdecl*)(Ptr,int,Ptr,const ReloadState*);
    int slot=numberedSlot>=1 && numberedSlot<=8 ? numberedSlot-1 : reloadSlot(gun);
    reinterpret_cast<Remember>(base+0x3b8f30)(gun,slot,ammo,&state);
    return true;
}
void applyReload(Ptr player,Ptr gun) {
    using Apply=void (__cdecl*)(Ptr);
    if (!reloadActive(gun)) return;
    if (wornReloadSlot) {
        int exact=wornReloadSlot(gun);
        if (exact>=-1) std::memcpy(base+0x124c05c,&exact,sizeof exact);
    }
    reinterpret_cast<Apply>(base+0x39a9a0)(player);
}
bool observeHolsters() {
    if (!layout) return false;
    if (slotAction) return true;
    Ptr call=base+layout->holsterActionCall;
    const unsigned char args[]{0x53,0x56,0x57,0xe8}, cleanup[]{0x83,0xc4,0x28};
    int previous;
    std::memcpy(&previous,call+1,4);
    if (std::memcmp(call-3,args,sizeof args) || std::memcmp(call+5,cleanup,sizeof cleanup) ||
        call+5+previous!=base+layout->holsterAction) return false;
    DWORD old,ignored;
    if (!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old)) return false;
    slotAction=reinterpret_cast<SlotAction>(base+layout->holsterAction);
    auto displacement=DWORD(reinterpret_cast<uintptr_t>(observeSlot)-reinterpret_cast<uintptr_t>(call+5));
    std::memcpy(call+1,&displacement,4);
    FlushInstructionCache(GetCurrentProcess(),call,5);
    VirtualProtect(call,5,old,&ignored);
    return true;
}
int holsterAt(Ptr player,int hand) {
    if (!layout || !player || unsigned(hand)>=2) return 0;
    // Use the same enabled spheres, anchors, radii and hand eligibility as a native draw.
    Ptr fn=base+layout->nearestHolster;
    const unsigned char start[]{0x83,0xec,0x68,0xa1};
    if (std::memcmp(fn,start,sizeof start)) return 0;
    int index=reinterpret_cast<int (__cdecl*)(Ptr,int)>(fn)(player,hand);
    return unsigned(index)<8 ? index+1 : 0;
}
// These two branches are taken only when TTWVR accepts a release in its pouch.
// The neighbouring world-drop path supplies the active ammo and round count.
struct PouchReturn {
    Ptr resume;
    const int* rounds;
    int ammoFrameOffset;
    LONG pendingId;
};
static_assert(sizeof(PouchReturn)==16);
extern "C" {
PouchReturn fwPouchReturns[2]{};
void fwMagazineReturned();
void fwSpareReturned();
}
// Run inside the host's existing frame. Preserve flags and both scratch registers,
// leave the FP/SIMD state alone, and defer sound playback to the NVSE game tick.
asm(R"(
.text
_fwMagazineReturned:
    pushl %edx
    movl $_fwPouchReturns, %edx
    call .LfwReadPouchAmmo
    popl %edx
    jmp *_fwPouchReturns
_fwSpareReturned:
    pushl %edx
    movl $_fwPouchReturns+16, %edx
    call .LfwReadPouchAmmo
    popl %edx
    jmp *_fwPouchReturns+16
.LfwReadPouchAmmo:
    pushfl
    pushl %eax
    movl 4(%edx), %eax
    cmpl $0, (%eax)
    jle .LfwPouchDone
    movl 8(%edx), %eax
    movl (%ebp,%eax), %eax
    testl %eax, %eax
    jz .LfwPouchDone
    cmpb $0x29, 4(%eax)
    jne .LfwPouchDone
    movl 12(%eax), %eax
    xchgl %eax, 12(%edx)
.LfwPouchDone:
    popl %eax
    popfl
    ret
)");
bool observePouchReturns() {
    if (!layout) return false;
    Ptr branches[]{base+layout->magazineReturn,base+layout->spareReturn};
    void (*observers[])()={fwMagazineReturned,fwSpareReturned};
    // Verify both destinations and the push instructions for the native drop arguments.
    for (unsigned i=0;i<2;++i) {
        Ptr branch=branches[i];
        int span;
        std::memcpy(&span,branch+2,4);
        if (branch[0]!=0x0f || branch[1]!=(i ? 0x87 : 0x85) || span!=(i ? 0xe4 : 0x82)) return false;
        Ptr resume=branch+6+span;
        Ptr args=resume-(i ? 32 : 21);
        if (args[0]!=0xff || args[1]!=0x35 || args[6]!=0xff || args[7]!=0xb5) return false;
        auto& event=fwPouchReturns[i];
        event.resume=resume;
        std::memcpy(&event.rounds,args+2,4);
        std::memcpy(&event.ammoFrameOffset,args+8,4);
        auto roundsRva=reinterpret_cast<uintptr_t>(event.rounds)-reinterpret_cast<uintptr_t>(base);
        if (roundsRva>layout->imageSize-sizeof(int)) return false;
        if (event.ammoFrameOffset>=0 || event.ammoFrameOffset < -0x4000) return false;
    }
    // Both conditional jumps lie in this single range. Only change their destinations.
    SIZE_T size=branches[1]+6-branches[0];
    DWORD old,ignored;
    if (!VirtualProtect(branches[0],size,PAGE_EXECUTE_READWRITE,&old)) return false;
    for (unsigned i=0;i<2;++i) {
        auto offset=DWORD(reinterpret_cast<uintptr_t>(observers[i])-reinterpret_cast<uintptr_t>(branches[i]+6));
        std::memcpy(branches[i]+2,&offset,sizeof offset);
    }
    FlushInstructionCache(GetCurrentProcess(),branches[0],size);
    VirtualProtect(branches[0],size,old,&ignored);
    return true;
}
unsigned takePouchReturn(unsigned kind) {
    return kind<2 ? static_cast<unsigned>(InterlockedExchange(&fwPouchReturns[kind].pendingId,0)) : 0;
}
int recentHolster(int hand) {
    auto event=holsterEvents[hand].load();
    if (!event || ((GetTickCount()-(event>>8))&clockMask)>=3000) return 0;
    int selected;
    std::memcpy(&selected,base+layout->handSlots+hand*4,4);
    return selected==int(event&255)-1 ? selected+1 : 0;
}
void forgetHolsters() { for (auto& event:holsterEvents) event.store(0); }
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
