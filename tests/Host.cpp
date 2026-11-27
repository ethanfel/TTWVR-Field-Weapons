// SPDX-License-Identifier: GPL-3.0-only
// Run once per installed TTWVR.dll in a fresh 32-bit process.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/Host.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using Poll = bool (__thiscall*)(void*,host::Ptr);
int calls;
bool result;
bool __fastcall precedingPoll(void*,void*,host::Ptr) {
    ++calls;
    return result;
}
void writeBytes(void* address,const void* bytes,unsigned length) {
    DWORD old,discard;
    assert(VirtualProtect(address,length,PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(address,bytes,length);
    assert(VirtualProtect(address,length,old,&discard));
    FlushInstructionCache(GetCurrentProcess(),address,length);
}
}

// Run the original conditional jump in this disposable process. A return at each
// successor stands in for the rest of the game. EBP points at a mock reload frame.
extern "C" void exercisePouch(void* site,void* frame,int inside);
asm("_exercisePouch: pushl %ebp; movl 12(%esp),%ebp; movl 8(%esp),%edx;"
    "cmpl $0,16(%esp); call *%edx; popl %ebp; ret");

void testPouch(host::Ptr base,bool current) {
    assert(host::observePouchReturns());
    unsigned char frame[0x2000]{}, ammo[0x10]{};
    const int offset=current ? 0x1730 : 0x17b4;
    auto ammoPtr=ammo;
    std::memcpy(frame+sizeof frame-offset,&ammoPtr,sizeof ammoPtr);
    ammo[4]=0x29;
    unsigned id=0x4241;
    std::memcpy(ammo+0xc,&id,4);
    for (unsigned kind=0;kind<2;++kind) {
        int site=current ? (kind ? 0x3d4c80 : 0x3d2132) : (kind ? 0x3b729e : 0x3b4a4c);
        auto rounds=reinterpret_cast<int*>(base+(current ? (kind?0x1c1226c:0x1c12130) : (kind?0x19f694c:0x19f6810)));
        const unsigned char ret=0xc3;
        writeBytes(base+site+6,&ret,1);
        writeBytes(base+site+6+(kind?0xe4:0x82),&ret,1);
        for (int count : {0,7}) for (int inside : {0,1}) {
            *rounds=count;
            exercisePouch(base+site,frame+sizeof frame,inside);
            assert(host::takePouchReturn(kind)==(inside && count ? id : 0));
            assert(host::takePouchReturn(kind)==0 && *rounds==count);
        }
    }
}

int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    auto base=reinterpret_cast<host::Ptr>(LoadLibraryW(argv[1]));
    if (!base) { std::printf("LoadLibrary failed: %lu\n",GetLastError()); return 3; }
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const bool current=nt->FileHeader.TimeDateStamp==0x6b07a53e;
    assert(current || nt->FileHeader.TimeDateStamp==0x6afa9b4f);
    void** slots[]{reinterpret_cast<void**>(base+(current?0xcf62e8:0xaf3568))+6,
                   reinterpret_cast<void**>(base+(current?0xcf6450:0xaf36d0))+6};
    auto prior=reinterpret_cast<void*>(precedingPoll);
    for (auto slot : slots) writeBytes(slot,&prior,sizeof prior);

    auto take=base+(current?0x1dc4a0:0x1cd9e0);
    const unsigned char original=*take,invalid=0;
    writeBytes(take,&invalid,1);
    assert(!host::connect() && *slots[0]==prior && *slots[1]==prior);
    writeBytes(take,&original,1);
    assert(host::connect() && host::connect());
    assert(std::strcmp(host::version(),current?"1.1.8":"1.1.7")==0);
    auto actionCall=base+(current?0x237aab:0x225ecb);
    int originalTarget, target;
    std::memcpy(&originalTarget,actionCall+1,4);
    const int foreign=originalTarget+1;
    writeBytes(actionCall+1,&foreign,4);
    assert(!host::observeHolsters());
    std::memcpy(&target,actionCall+1,4);
    assert(target==foreign && !host::recentHolster(0) && !host::recentHolster(1));
    writeBytes(actionCall+1,&originalTarget,4);
    assert(host::observeHolsters() && host::observeHolsters());
    std::memcpy(&target,actionCall+1,4);
    assert(target!=originalTarget);
    testPouch(base,current);
    for (auto slot : slots) {
        assert(*slot!=prior);
        auto poll=reinterpret_cast<Poll>(*slot);
        for (int hand : {0,1}) {
            unsigned char frame[0x130]{};
            frame[0x12c]=1;
            frame[0x40+hand*0x40+0x1c]=frame[0x40+hand*0x40+0x1d]=1;
            float grip=.75f;
            std::memcpy(frame+0x118+hand*4,&grip,sizeof grip);
            frame[0x120+hand*5]=1;
            int before=calls;
            result=true;
            assert(poll(nullptr,frame) && calls==before+1);
            auto in=host::input(hand);
            assert(in.tracked && in.grip && in.keep && !host::input(1-hand).tracked);
            result=false;
            assert(!poll(nullptr,frame) && !host::input(hand).tracked);
        }
    }
    std::printf("TTWVR %s: incompatible code rejected, both input chains and pouch return branches passed\n",host::version());
    return 0;
}
