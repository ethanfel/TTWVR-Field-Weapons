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
// ProcessManager's high-process range contains the actors currently simulated in full.
struct HighActors {
    Ptr entries{};
    unsigned begin{},end{};
    HighActors() {
        Ptr manager=reinterpret_cast<Ptr>(0x11e0e80);
        entries=ptr(manager,8);
        begin=field<unsigned>(manager,0x14);
        end=field<unsigned>(manager,0x24);
        if (!entries || end>field<unsigned short>(manager,0xc)) end=0;
    }
    Ptr operator[](unsigned i) const { return ptr(entries,i*4); }
};
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
inline void removeExtra(Ptr list,unsigned kind) {
    using RemoveByType=void (__thiscall*)(Ptr,unsigned);
    reinterpret_cast<RemoveByType>(0x410140)(list,kind);
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
inline Ptr splitOne(Ptr entry,Ptr stack,bool keepOriginalSingle=false) {
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
    reinterpret_cast<Copy>(0x412490)(copy,stack,false);
    for (unsigned type:{0x16u,0x17u,0x4au}) if (extra(copy,type)) removeExtra(copy,type);
    Ptr single=keepOriginalSingle?stack:copy, rest=keepOriginalSingle?copy:stack;
    if (remain>1 && !extra(rest,0x24)) {
        field<void (__thiscall*)(Ptr,bool)>(ptr(copy,0),0)(copy,true);
        reinterpret_cast<Free>(0xaa4060)(heap,node);
        return nullptr;
    }
    if (extra(single,0x24)) removeExtra(single,0x24);
    if (remain==1) removeExtra(rest,0x24);
    else std::memcpy(extra(rest,0x24)+0xc,&remain,sizeof remain);
    // Prepend without changing the address of the embedded head node.
    std::memcpy(node,head,8);
    std::memcpy(head,&copy,4);
    std::memcpy(head+4,&node,4);
    return single;
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
    const Vec native = motion::toHavokVelocity(velocity);
    float v[4]{native.x,native.y,native.z,0};
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
inline bool eligibleForm(Ptr form) {
    // Leave quest items, scripted activations, explosives and nonplayable weapons with TTWVR.
    return type(form)==0x28 && field<unsigned char>(form,0xf4)<10 &&
           !(field<unsigned>(form,8)&0x400) && !ptr(form,0x6c) &&
           !(field<unsigned char>(form,0x100)&0x80);
}
inline bool eligible(Ptr ref) {
    return loose(ref) && quantity(ref+0x44)==1 && eligibleForm(ptr(ref,0x20));
}
inline int hotkey(Ptr stack) {
    Ptr key=extra(stack,0x4a);
    return key && field<unsigned char>(key,0xc)<8 ? int(key[0xc])+1 : 0;
}
inline bool supportsAmmo(Ptr weapon,Ptr ammo) {
    if (type(ammo)!=0x29) return false;
    Ptr list=ptr(weapon,0xa8);
    if (type(list)!=0x55) return list==ammo;
    for (Ptr n=list+0x18;n;n=ptr(n,4)) if (ptr(n,0)==ammo) return true;
    return false;
}
inline int capacity(Ptr weapon,Ptr stack) {
    float rounds=field<unsigned char>(weapon,0xb0);
    unsigned mods=field<unsigned char>(extra(stack,0x8d),0xc);
    for (unsigned i=0;i<3;++i)
        if ((mods&(1u<<i)) && field<unsigned>(weapon,0x180+4*i)==2)
            rounds+=field<float>(weapon,0x18c+4*i);
    return std::isfinite(rounds) ? int(std::clamp(rounds,0.f,65535.f)) : 0;
}
inline Ptr ensureExtra(Ptr list,unsigned char kind,unsigned size,unsigned vtable) {
    if (Ptr node=extra(list,kind)) return node;
    using Alloc=Ptr (__thiscall*)(void*,unsigned);
    Ptr node=reinterpret_cast<Alloc>(0xaa3e40)(reinterpret_cast<void*>(0x11f6238),size);
    if (!node) return nullptr;
    std::memset(node,0,size);
    std::memcpy(node,&vtable,sizeof vtable);
    node[4]=kind;
    using Add=Ptr (__thiscall*)(Ptr,Ptr);
    reinterpret_cast<Add>(0x40ff60)(list,node);
    return node;
}
// Attach to the recovered instance, including when several copies share a base form.
inline bool setHotkey(Ptr stack,int slot) {
    Ptr node=ensureExtra(stack,0x4a,0x10,0x101592c);
    if (!node) return false;
    node[0xc]=static_cast<unsigned char>(slot-1);
    return true;
}
// A native ExtraAmmo transfers with ordinary Fallout pickups and survives saves.
inline Ptr worldAmmo(Ptr ref,Ptr ammo) {
    Ptr node=ensureExtra(ref+0x44,0x6e,0x14,0x1015998);
    if (!node) return nullptr;
    std::memcpy(node+0xc,&ammo,sizeof ammo);
    return node;
}

}
