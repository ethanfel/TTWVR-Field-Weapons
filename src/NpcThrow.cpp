// SPDX-License-Identifier: GPL-3.0-only
#include "NpcThrow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace npc {
using namespace game;
namespace {
Scripts* scripts;
Logger trace;
void *startAnimation, *countItem, *canSee;
constexpr char clipName[]="1gtattackthrow6.kf";
struct Attempt {
    unsigned actor, target, weapon, ammo;
    Ptr stack;
    uint32_t started;
    bool waiting, released;
};
std::vector<Attempt> attempts;
uint32_t lastScan;
Ptr currentCell;

bool nearOpponent(Ptr actor,Ptr target,Ptr cell) {
    if (type(actor)!=0x3b || actor==player() || field<bool>(actor,0x18d) ||
        !field<bool>(actor,0x104) || ptr(actor,0x128)!=target ||
        !target || (type(target)!=0x3b && type(target)!=0x3c) ||
        field<unsigned>(actor,0x108)!=0 || field<unsigned>(target,0x108)!=0 ||
        ptr(actor,0x40)!=cell || ptr(target,0x40)!=cell) return false;
    Ptr process=ptr(actor,0x68);
    if (!process || field<unsigned char>(process,0x28)!=0 || field<unsigned char>(process,0x13c)!=0)
        return false;
    float distance=(field<Vec>(actor,0x30)-field<Vec>(target,0x30)).length();
    return distance>=55.f && distance<=350.f;
}
bool pistol(Ptr weapon) {
    if (type(weapon)!=0x28) return false;
    unsigned kind=field<unsigned char>(weapon,0xf4);
    return (kind==3 || kind==4) &&
           field<unsigned char>(weapon,0x102)>0 && field<float>(weapon,0x1a4)==0 &&
           !(field<unsigned>(weapon,8)&0x400) && !ptr(weapon,0x6c) &&
           !(field<unsigned>(weapon,0x12c)&1u) &&
           !(field<unsigned char>(weapon,0x100)&0x80);
}
bool noRounds(Ptr actor,Ptr ammo) {
    if (type(ammo)!=0x29) return false;
    Value count;
    return scripts->call(countItem,nullptr,nullptr,&count,2,actor,ammo) && count.type==1 && count.number==0;
}
bool empty(Ptr actor,Ptr weapon) {
    Ptr magazine=ammoInfo(actor);
    if (!magazine || field<int>(magazine,4)!=0) return false;
    Ptr ammo=ptr(weapon,0xa8);
    if (type(ammo)!=0x55) return noRounds(actor,ammo);
    bool found=false;
    for (Ptr node=ammo+0x18; node; node=ptr(node,4)) {
        Ptr item=ptr(node,0);
        if (!item) continue;
        if (!noRounds(actor,item)) return false;
        found=true;
    }
    return found;
}
bool visible(Ptr actor,Ptr target) {
    Value result;
    return scripts->call(canSee,nullptr,nullptr,&result,2,actor,target) && result.type==1 && result.number>0;
}
Ptr bone(Ptr node,const char* name) {
    if (!node) return nullptr;
    auto label=field<const char*>(node,8);
    if (label && !_stricmp(label,name)) return node;
    Children children(node);
    for (unsigned i=0;i<children.size();++i)
        if (auto match=bone(children[i],name)) return match;
    return nullptr;
}
bool atRelease(Ptr actor) {
    Ptr anim=ptr(ptr(actor,0x68),0x1c0),manager=ptr(anim,0xd8);
    Ptr sequences=ptr(manager,0x38);
    const auto count=field<unsigned short>(manager,0x3e);
    if (!sequences || count>field<unsigned short>(manager,0x3c)) return false;
    for (unsigned i=0;i<count;++i) {
        Ptr seq=ptr(sequences,4*i);
        unsigned state=field<unsigned>(seq,0x44);
        if (state!=1 && state!=2 && state!=5) continue;
        const char* path=field<const char*>(seq,8);
        if (!path) continue;
        const char* filename=std::strrchr(path,'\\');
        if (_stricmp(filename?filename+1:path,clipName)!=0) continue;
        Ptr text=ptr(seq,0x20),keys=ptr(text,0x10);
        unsigned n=field<unsigned>(text,0xc);
        for (unsigned j=0;keys && j<n;++j) {
            const char* key=field<const char*>(keys,8*j+4);
            if (key && !_stricmp(key,"release"))
                return field<float>(seq,0x3c)>=field<float>(keys,8*j);
        }
    }
    return false;
}
void advance(Attempt& attempt,Ptr cell,Released released) {
    Ptr actor=lookup(attempt.actor),target=lookup(attempt.target),weapon=lookup(attempt.weapon);
    if (!nearOpponent(actor,target,cell) || !pistol(weapon) || equipped(actor)!=weapon) {
        trace("npc_throw_cancel actor=%08x reason=combat_or_equipment",attempt.actor);
        attempt.waiting=false;
        return;
    }
    if (!atRelease(actor)) return;
    attempt.waiting=false;
    Ptr stack=stacks(actor,weapon).worn();
    if (!stack || stack!=attempt.stack || quantity(stack)!=1) {
        trace("npc_throw_cancel actor=%08x reason=equipped_stack_changed",attempt.actor);
        return;
    }
    if (!empty(actor,weapon) || !visible(actor,target)) {
        trace("npc_throw_cancel actor=%08x reason=ammo_or_sight",attempt.actor);
        return;
    }
    Ptr hand=bone(root(actor),"Bip01 R Hand");
    if (!hand) {
        trace("npc_throw_cancel actor=%08x reason=missing_hand",attempt.actor);
        return;
    }
    Vec from=field<Vec>(hand,0x8c),aim=field<Vec>(target,0x30);
    if (Ptr chest=bone(root(target),"Bip01 Spine2")) aim=field<Vec>(chest,0x8c);
    else aim.z+=65.f;
    if (!from.valid() || (from-field<Vec>(actor,0x30)).length()>180.f || !aim.valid()) {
        trace("npc_throw_cancel actor=%08x reason=invalid_pose",attempt.actor);
        return;
    }
    Vec delta=(aim-from)*(1.f/motion::gameUnitsPerMetre);
    const float travel=std::clamp(std::hypot(delta.x,delta.y)/6.5f,.15f,.8f);
    Vec velocity=delta*(1.f/travel);
    velocity.z+=4.905f*travel;
    // Use the actor's DropItem virtual so its equipped instance and condition transfer together.
    using Drop=Ptr (__thiscall*)(Ptr,Ptr,Ptr,int,Vec*,Vec*);
    trace("npc_drop_request actor=%08x base=%08x kind=%u",attempt.actor,attempt.weapon,field<unsigned char>(weapon,0xf4));
    Ptr item=field<Drop>(ptr(actor,0),0x3cc)(actor,weapon,stack,1,&from,nullptr);
    if (loose(item) && ptr(item,0x20)==weapon) {
        attempt.released=true;
        released(actor,item,lookup(attempt.ammo),velocity);
    } else trace("npc_throw_cancel actor=%08x reason=drop_failed",attempt.actor);
}
void consider(Ptr actor,Ptr cell,uint32_t now) {
    Ptr target=ptr(actor,0x128);
    if (!nearOpponent(actor,target,cell)) return;
    Ptr weapon=equipped(actor);
    if (!pistol(weapon) || !drawn(actor)) return;
    auto previous=std::find_if(attempts.begin(),attempts.end(),
        [actor](const Attempt& a){return a.actor==id(actor);});
    if (previous!=attempts.end()) {
        // A newly loaded pistol starts a new firing sequence, even during the retry delay.
        if (previous->released && field<int>(ammoInfo(actor),4)>0) attempts.erase(previous);
        return;
    }
    if (!empty(actor,weapon) || !visible(actor,target)) return;
    Ptr stack=stacks(actor,weapon).worn();
    if (!stack || quantity(stack)!=1) return;
    Ptr ammo=ptr(ammoInfo(actor),8);
    if (!ammo) {
        ammo=ptr(weapon,0xa8);
        if (type(ammo)==0x55) ammo=ptr(ammo,0x18);
    }
    Value result;
    trace("npc_throw_request actor=%08x target=%08x base=%08x kind=%u",id(actor),id(target),id(weapon),field<unsigned char>(weapon,0xf4));
    bool started=scripts->call(startAnimation,nullptr,nullptr,&result,1,actor) && result.type==1 && result.number>0;
    trace("npc_throw_animation actor=%08x base=%08x started=%d",id(actor),id(weapon),started);
    attempts.push_back({id(actor),id(target),id(weapon),id(ammo),stack,now,started,false});
}
}
bool initialize(Scripts* api,Logger logger) {
    trace=logger;
    scripts=api;
    startAnimation=api->compile(R"(ref who
begin Function {who}
 SetFunctionValue (who.PlayAnimationPath "characters\_male\1gtattackthrow6.kf" 0)
end)");
    countItem=api->compile("ref who\nref item\nbegin Function {who item}\n SetFunctionValue (who.GetItemCount item)\nend");
    canSee=api->compile(R"(ref who
ref opponent
int menu
begin Function {who opponent}
 let menu := GetActiveMenuMode
 if eval menu == 0 || menu == 1004
  SetFunctionValue (who.GetLOS opponent)
 endif
end)");
    return startAnimation && countItem && canSee;
}
void reset() { attempts.clear(); currentCell=nullptr; lastScan=0; }
void update(Ptr pc,uint32_t now,Released released) {
    Ptr cell=ptr(pc,0x40);
    if (currentCell!=cell) { reset(); currentCell=cell; }
    for (auto& attempt:attempts) {
        if (attempt.waiting && now-attempt.started>2200u) {
            trace("npc_throw_cancel actor=%08x reason=release_key_timeout",attempt.actor);
            attempt.waiting=false;
        }
        if (attempt.waiting) advance(attempt,cell,released);
    }
    if (now-lastScan<150u) return;
    lastScan=now;
    attempts.erase(std::remove_if(attempts.begin(),attempts.end(),
        [now](const Attempt& a){return now-a.started>8000u;}),attempts.end());
    HighActors actors;
    for (unsigned i=actors.begin;i<actors.end;++i) consider(actors[i],cell,now);
}
}
