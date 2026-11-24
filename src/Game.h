// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Motion.h"
#include <algorithm>
#include <vector>
#include <cstring>
namespace game {
using Ptr = unsigned char*;
template<typename T> T field(Ptr p,unsigned offset) {
    T result{};
    if (p) std::memcpy(&result,p+offset,sizeof result);
    return result;
}
inline Ptr ptr(Ptr p,unsigned offset) { return field<Ptr>(p,offset); }
inline Ptr player() { return *reinterpret_cast<Ptr*>(0x11dea3c); }
inline unsigned id(Ptr ref) { return field<unsigned>(ref,0xc); }
inline Ptr lookup(unsigned formID) {
    auto map=*reinterpret_cast<Ptr*>(0x11c54c0);
    unsigned count=field<unsigned>(map,4);
    Ptr buckets=ptr(map,8);
    if (!formID || !buckets || !count) return nullptr;
    for (Ptr entry=ptr(buckets,4*(formID%count));entry;entry=ptr(entry,0))
        if (field<unsigned>(entry,4)==formID) return ptr(entry,8);
    return nullptr;
}
inline bool loose(Ptr ref) {
    return field<unsigned char>(ref,4)==0x3a &&
           (field<unsigned>(ref,8)&(0x20|0x800))==0; // deleted / initially disabled
}
inline Ptr root(Ptr ref) { return ptr(ptr(ref,0x64),0x14); }
inline Ptr as(Ptr object,unsigned slot) {
    if (!object) return nullptr;
    using Cast=Ptr(__thiscall *)(Ptr);
    return field<Cast>(ptr(object,0),slot*4)(object);
}
class Children {
    Ptr data{}; unsigned count{};
public:
    explicit Children(Ptr object) {
        if (Ptr node=as(object,3)) {
            data=ptr(node,0xa0);
            auto used=field<unsigned short>(node,0xa6);
            if (data && used<=field<unsigned short>(node,0xa4)) count=used;
        }
    }
    unsigned size() const { return count; }
    Ptr operator[](unsigned idx) const { return ptr(data,idx*4); }
};
inline Ptr mobileBody(Ptr n,unsigned depth=0) {
    if (!n || depth>14) return nullptr;
    Ptr collision=as(ptr(n,0x1c),14);
    Ptr wrapper=ptr(collision,0x10);
    Ptr body=ptr(wrapper,8);
    unsigned motion=field<unsigned char>(body,0xe8);
    if (field<unsigned char>(body,0x28)==1 && (motion==2 || motion==3 || motion==6 || motion==7))
        return body;
    Children children(n);
    for (unsigned i=0;i<children.size();++i)
        if (auto result=mobileBody(children[i],depth+1)) return result;
    return nullptr;
}

inline unsigned char type(Ptr form) { return field<unsigned char>(form,4); }
inline Ptr extra(Ptr list, unsigned kind) {
    for (Ptr n=ptr(list,4);n;n=ptr(n,8))
        if (type(n)==kind) return n;
    return nullptr;
}
inline int quantity(Ptr stack) {
    Ptr count=extra(stack,0x24);
    return count ? field<short>(count,0xc) : 1;
}
// Once unworn, Fallout can merge plain, identically conditioned weapons.
inline bool plainCondition(Ptr stack,unsigned& healthBits) {
    if (!stack || !extra(stack,0x25)) return false;
    for (Ptr n=ptr(stack,4);n;n=ptr(n,8)) {
        auto kind=type(n);
        if (kind!=0x25 && kind!=0x24 && kind!=0x16) return false;
    }
    healthBits=field<unsigned>(extra(stack,0x25),0xc);
    return true;
}
inline Ptr itemEntry(Ptr pc,Ptr form) {
    Ptr changes=extra(pc+0x44,0x15);
    for (Ptr n=ptr(ptr(changes,0xc),0);n;n=ptr(n,4)) {
        Ptr entry=ptr(n,0);
        if (ptr(entry,8)==form) return entry;
    }
    return nullptr;
}
struct Stacks {
    std::vector<Ptr> listed;
    std::vector<int> counts;
    int delta{}, tagged{};
    bool contains(Ptr p) const { return std::find(listed.begin(),listed.end(),p)!=listed.end(); }
    int count(Ptr p) const {
        auto it=std::find(listed.begin(),listed.end(),p);
        return it==listed.end()?0:counts[it-listed.begin()];
    }
    Ptr worn() const {
        for (Ptr p:listed) if (extra(p,0x16)) return p;
        return nullptr;
    }
};
inline Stacks stacks(Ptr pc,Ptr base) {
    Stacks out; Ptr entry=itemEntry(pc,base);
    out.delta=field<int>(entry,4);
    for (Ptr n=ptr(entry,0);n;n=ptr(n,4)) {
        if (Ptr stack=ptr(n,0)) {
            out.listed.push_back(stack);
            out.counts.push_back(quantity(stack));
            out.tagged+=out.counts.back();
        }
    }
    return out;
}
// The inventory merges identical guns on pickup. Separate a single unit using
// Fallout's own extra-data copier. Personal hotkeys and worn state belong to
// the existing stack. Its condition, mods and other item data belong to both.
inline Ptr splitOne(Ptr entry,Ptr stack) {
    Ptr head=ptr(entry,0),count=extra(stack,0x24);
    short remain=short(quantity(stack)-1);
    if (!head || !count || remain<1) return nullptr;
    using Alloc=Ptr (__thiscall*)(void*,unsigned);
    using Free=void (__thiscall*)(void*,Ptr);
    auto heap=reinterpret_cast<void*>(0x11f6238);
    auto alloc=reinterpret_cast<Alloc>(0xaa3e40);
    Ptr copy=alloc(heap,0x20);
    if (!copy) return nullptr;
    Ptr node=alloc(heap,8);
    if (!node) { reinterpret_cast<Free>(0xaa4060)(heap,copy); return nullptr; }
    std::memset(copy,0,0x20);
    *reinterpret_cast<unsigned*>(copy)=0x10143e8;
    using Copy=void (__thiscall*)(Ptr,Ptr,bool);
    using Remove=void (__thiscall*)(Ptr,unsigned);
    reinterpret_cast<Copy>(0x412490)(copy,stack,false);
    auto remove=reinterpret_cast<Remove>(0x410140);
    for (unsigned type:{0x24u,0x16u,0x17u,0x4au}) if (extra(copy,type)) remove(copy,type);
    if (remain==1) remove(stack,0x24);
    else std::memcpy(count+0xc,&remain,sizeof remain);
    // Prepend without changing the address of the embedded head node.
    std::memcpy(node,head,8);
    std::memcpy(head,&copy,4);
    std::memcpy(head+4,&node,4);
    return copy;
}
inline Ptr equipped(Ptr pc) { return ptr(ptr(ptr(pc,0x68),0x114),8); }
inline Ptr ammoInfo(Ptr pc) { return ptr(ptr(pc,0x68),0x118); }
inline bool drawn(Ptr pc) { return field<unsigned char>(ptr(pc,0x68),0x135)!=0; }
inline void equip(Ptr pc,Ptr item,Ptr stack) {
    using Fn=void (__thiscall*)(Ptr,Ptr,int,Ptr,int,int,int);
    reinterpret_cast<Fn>(0x88c650)(pc,item,1,stack,1,0,1);
}
inline void unequip(Ptr pc,Ptr item,Ptr stack) {
    using Fn=void (__thiscall*)(Ptr,Ptr,int,Ptr,int,int,int);
    reinterpret_cast<Fn>(0x88c790)(pc,item,1,stack,1,0,1);
}
inline Ptr drop(Ptr pc,Ptr item,Ptr stack,Vec* pos,Vec* angles) {
    using Fn=Ptr (__thiscall*)(Ptr,Ptr,Ptr,int,Vec*,Vec*);
    return reinterpret_cast<Fn>(0x954610)(pc,item,stack,1,pos,angles);
}
inline void launch(Ptr body,Vec velocity) {
    // World-space metres/second, followed by Havok activation (JIP SetLinearVelocity).
    float v[4]{velocity.x,velocity.y,velocity.z,0};
    std::memcpy(body+0x1b0,v,sizeof v);
    reinterpret_cast<void (__thiscall*)(Ptr)>(0xc9c1d0)(body);
}
inline bool boundsNear(Ptr a, Ptr b) {
    struct Bound { Vec center; float radius; };
    Ptr left=ptr(a,0x20), right=ptr(b,0x20);
    if (!left || !right) return true;
    Bound w=field<Bound>(left,0), actor=field<Bound>(right,0);
    if (!w.center.valid() || !actor.center.valid() || !(w.radius>0 && actor.radius>0)) return true;
    // Allow for movement between the renderer and physics callbacks.
    return (w.center-actor.center).length() <= w.radius+actor.radius+128.f;
}
inline void dealImpactDamage(Ptr actor, Ptr attacker, float amount) {
    // Actor::DamageHealthAndFatigue applies game difficulty and records the source.
    using Damage=void (__thiscall*)(Ptr,float,float,Ptr);
    field<Damage>(ptr(actor,0),0x338)(actor,amount,0.f,attacker);
}
inline bool eligible(Ptr ref) {
    if (!loose(ref) || quantity(ref+0x44)!=1) return false;
    Ptr form=ptr(ref,0x20);
    // Leave quest items, scripted activations, explosives and nonplayable weapons with TTWVR.
    return type(form)==0x28 && field<unsigned char>(form,0xf4)<10 &&
           !(field<unsigned>(form,8)&0x400) && !ptr(form,0x6c) &&
           !(field<unsigned char>(form,0x100)&0x80);
}
}
