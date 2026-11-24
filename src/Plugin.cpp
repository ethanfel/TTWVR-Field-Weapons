// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "Api.h"
#include "Game.h"
#include "Host.h"
#include "Impact.h"
#include <cstdio>
#include <cstdarg>

namespace {
using namespace game;
Scripts* scripts;
using MakeRef=Ptr (__stdcall*)(Ptr,Ptr,int,Ptr);
using ReadRef=Ptr (__cdecl*)(unsigned);
MakeRef makeRef;
ReadRef readRef;
void *hasContact;
void *gameplay,*materialize,*holster,*readClip,*readAmmo,*writeClip,*available;
constexpr DWORD keepHoldMs=1000;
bool initialized,ready,loaded;
unsigned gated, reported{};
struct Clip { Ptr ammo{}; int rounds{}; };
struct Equipment { Ptr form{},stack{}; bool out{}; int hand{}; Clip clip; unsigned healthBits{}; bool plain{}; };
struct Loan {
    Ptr base{}, stack{},cell{};
    int hand{};
    Equipment before;
    HandMotion motion;
    Vec angles{};
    bool armedKeep{}, holdingKeep{};
    Clip clip{}, returningClip{};
    DWORD keepSince{}, equipSince{};
    unsigned source{};
    bool equipping{};
    Stacks protectedStacks;
} loan;
struct PendingTake {
    unsigned reference{};
    int hand{};
    Ptr cell{};
    Equipment previous;
    DWORD since{};
} pendingTake;
struct Restoration {
    Equipment wanted;
    bool merged{};
    Ptr cell{}, initial{};
    DWORD since{};
} restoring;
struct Flight {
    unsigned ref, base;
    Ptr cell;
    Vec releaseVelocity;
    DWORD since, lastSample{}, lastMoving{};
    float incomingSpeed{};
    bool launched{};
};
std::vector<Flight> flights;
void log(const char* format,...) {
    FILE* f=std::fopen("Data/NVSE/Plugins/TTWVRFieldWeapons.log","a");
    if (!f) return;
    std::fprintf(f,"%lu ",GetTickCount());
    va_list args; va_start(args,format); std::vfprintf(f,format,args); va_end(args);
    std::fputc('\n',f); std::fclose(f);
}
template<class... Args> Value call(void* script,Args... args) {
    Value v;
    if (!scripts->call(script,nullptr,nullptr,&v,sizeof...(args),args...)) return {};
    return v;
}
bool initialize() {
    gameplay=scripts->compile("int m\nbegin Function {}\n let m := GetActiveMenuMode\n SetFunctionValue (eval (m == 0 || m == 1004) && player.GetDead == 0)\nend");
    materialize=scripts->compile("ref item\nfloat hp\nbegin Function {item}\n let hp := item.GetItemRefCurrentHealth\n SetFunctionValue (item.SetItemRefCurrentHealth hp)\nend");
    holster=scripts->compile("begin Function {}\n player.HolsterWeapon\nend");
    // Fallout accounts for loaded rounds in carried ammo. Store only magazine state.
    // A zero-length magazine must be distinguishable from an untouched world gun.
    readClip=scripts->compile(R"(ref item
begin Function {item}
 if eval (item.AuxVarSize "_TTWVRFieldMagazine") == 2
  SetFunctionValue (item.AuxVarGetFlt "_TTWVRFieldMagazine")
 else
  SetFunctionValue -1
 endif
end)");
    readAmmo=scripts->compile("ref item\nbegin Function {item}\n SetFunctionValue (item.AuxVarGetRef \"_TTWVRFieldMagazine\" 1)\nend");
    writeClip=scripts->compile(R"(ref item
int rounds
ref ammo
begin Function {item rounds ammo}
 item.AuxVarSetFlt "_TTWVRFieldMagazine" rounds
 item.AuxVarSetRef "_TTWVRFieldMagazine" ammo 1
end)");
    available=scripts->compile("ref ammo\nbegin Function {ammo}\n SetFunctionValue (player.GetItemCount ammo)\nend");
    // JIP exposes the actor controller and the loose rigid body's contact lists separately.
    hasContact=scripts->compile("ref item\nref actorRef\nbegin Function {item actorRef}\n SetFunctionValue (eval (item.GetHasContact actorRef) || (actorRef.GetHasContact item))\nend");
    return gameplay && materialize && holster && readClip && readAmmo && writeClip && available && host::connect();
}
Clip magazine(Ptr pc) {
    Ptr info=ammoInfo(pc);
    return {ptr(info,8),field<int>(info,4)};
}
void setMagazine(Ptr pc,Clip clip) {
    Ptr info=ammoInfo(pc);
    if (!clip.ammo || ptr(info,8)!=clip.ammo || clip.rounds<0) return;
    Value count=call(available,clip.ammo);
    if (count.type!=1) return;
    int rounds=std::clamp(clip.rounds,0,std::max(0,int(count.number)));
    std::memcpy(info+4,&rounds,sizeof rounds);
}
Equipment equipment(Ptr pc) {
    Ptr form=equipped(pc);
    Equipment old={form,form ? stacks(pc,form).worn() : nullptr,drawn(pc),host::drawHand(),magazine(pc)};
    old.plain=plainCondition(old.stack,old.healthBits);
    return old;
}
void finishRestore(Ptr pc,DWORD now) {
    if (!restoring.wanted.form) return;
    Equipment old=restoring.wanted;
    Ptr current=equipped(pc);
    bool timedOut=now-restoring.since>650;
    if (ptr(pc,0x40)!=restoring.cell || timedOut ||
        (current && current!=old.form && current!=restoring.initial && id(current)!=0x1f4)) {
        restoring={};
        return;
    }
    if (current!=old.form) return;
    auto items=stacks(pc,old.form);
    if (old.stack && items.worn()!=old.stack) {
        unsigned bits{};
        if (!restoring.merged || !plainCondition(items.worn(),bits) || bits!=old.healthBits) return;
    }
    restoring={};
    if (old.out) host::draw(old.hand,old.form); else call(holster);
    setMagazine(pc,old.clip);
}
void restore(Ptr pc,const Equipment& old) {
    restoring={};
    if (!old.form) {
        if (!old.out && (!equipped(pc) || id(equipped(pc))==0x1f4)) call(holster);
        return;
    }
    auto items=stacks(pc,old.form);
    Ptr exact=old.stack;
    bool merged=false;
    if (!items.contains(exact) && old.plain) {
        // Unequipping may fold this instance into a single stack of identical
        // plain weapons. Match the original condition and reject other metadata.
        exact=nullptr;
        for (Ptr candidate:items.listed) {
            unsigned bits{};
            if (!plainCondition(candidate,bits) || bits!=old.healthBits) continue;
            if (exact) { exact=nullptr; break; }
            exact=candidate;
        }
        merged=exact!=nullptr;
    }
    if (!items.contains(exact)) {
        // A worn-only list can disappear on unequip. A unique remaining copy is unambiguous.
        Value count=call(available,old.form);
        if (count.type!=1 || count.number!=1) { log("restore skipped ambiguous previous weapon"); return; }
        exact=items.listed.empty()?nullptr:items.listed.front();
    }
    DWORD now=GetTickCount();
    restoring={old,merged,ptr(pc,0x40),equipped(pc),now};
    restoring.wanted.stack=exact;
    equip(pc,old.form,exact);
    finishRestore(pc,now);
}
Ptr resolvePickup(Ptr pc,Ptr form,const Stacks& before,const Stacks& after) {
    if (after.delta-before.delta!=1) return nullptr;
    Ptr added=nullptr;
    for (Ptr p:after.listed) {
        int growth=after.count(p)-before.count(p);
        if (!growth) continue;
        if (growth!=1 || added) return nullptr;
        added=p;
    }
    for (Ptr p:before.listed) if (!after.contains(p)) return nullptr;
    if (added) return quantity(added)==1 ? added : splitOne(itemEntry(pc,form),added);
    if (after.tagged!=before.tagged) return nullptr;
    // Allocate one inventory reference for the new plain unit. JIP materializes
    // one real stack at its current condition, even when other plain copies exist.
    Ptr item=makeRef(pc,form,1,nullptr);
    if (!item) return nullptr;
    unsigned refID=id(item);
    if (call(materialize,item).number!=1) return nullptr;
    Ptr exact=ptr(readRef(refID),8);
    auto now=stacks(pc,form);
    return exact && !before.contains(exact) && now.contains(exact) && quantity(exact)==1 ? exact : nullptr;
}
void borrow(Ptr pc,Ptr ref,int hand,const Equipment& previous) {
    Ptr form=ptr(ref,0x20);
    unsigned source=id(ref);
    const bool sameEquipped=previous.form==form && previous.stack;
    auto before=stacks(pc,form);
    Vec angles=field<Vec>(ref,0x24);
    Value saved=call(readClip,ref);
    // Read the ammo reference before TTWVR transfers/deletes the world reference.
    Clip clip{};
    if (readAmmo && saved.type==1 && saved.number>=0) {
        Value ammo=call(readAmmo,ref);
        if (ammo.type==2) clip={ammo.object,int(saved.number)};
    }
    if (!host::take(hand)) {
        if (sameEquipped) restore(pc,previous);
        return;
    }
    auto after=stacks(pc,form);
    Ptr exact=resolvePickup(pc,form,before,after);
    if (!exact) {
        if (sameEquipped) restore(pc,previous);
        log("native_take source=%08x before=%d after=%d no unique stack",source,before.delta,after.delta);
        return;
    }
    loan={}; loan.base=form; loan.stack=exact; loan.before=previous;
    loan.angles=angles; loan.hand=hand; loan.cell=ptr(pc,0x40);
    loan.armedKeep=!host::input(hand).keep;
    loan.equipSince=GetTickCount(); loan.equipping=true; loan.source=source;
    loan.protectedStacks=stacks(pc,form); loan.returningClip=clip;
    equip(pc,form,exact);
    log("pickup source=%08x base=%08x hand=%d count_before=%d",source,id(form),hand,before.delta);

}
void beginPickup(Ptr pc,Ptr ref,int hand,DWORD now) {
    Equipment previous=equipment(pc);
    // Equip can remove a worn-only list. Give that *specific* previous copy its
    // existing health so it can still be identified among duplicates on return.
    if (previous.stack && !extra(previous.stack,0x25)) {
        if (Ptr item=makeRef(pc,previous.form,1,previous.stack)) call(materialize,item);
    }
    if (previous.form==ptr(ref,0x20) && previous.stack) {
        // The player queues equipment changes. Keep holding the world reference
        // until the old copy has left the process weapon slot, then transfer it.
        pendingTake={id(ref),hand,ptr(pc,0x40),previous,now};
        unequip(pc,previous.form,previous.stack);
    } else borrow(pc,ref,hand,previous);
}
void finishPickup(Ptr pc,DWORD now) {
    const auto pending=pendingTake;
    Ptr ref=host::held(pending.hand);
    auto input=host::input(pending.hand);
    Value mode=call(gameplay);
    Ptr current=equipped(pc);
    bool sameCell=ptr(pc,0x40)==pending.cell;
    bool otherWeapon=current && current!=pending.previous.form && id(current)!=0x1f4;
    if (!sameCell || !input.tracked || !input.grip || mode.type!=1 || !mode.number ||
        otherWeapon || id(ref)!=pending.reference || !eligible(ref) || now-pending.since>650) {
        pendingTake={};
        if (sameCell && !otherWeapon) restore(pc,pending.previous);
        return;
    }
    if (now-pending.since<65 || current==pending.previous.form) return;
    pendingTake={};
    borrow(pc,ref,pending.hand,pending.previous);
}
void cancel(const char* reason) { log("end base=%08x reason=%s",id(loan.base),reason); loan={}; }
void release(Ptr pc,Vec* point,Vec velocity,DWORD now) {
    auto items=stacks(pc,loan.base);
    if (!items.contains(loan.stack) || quantity(loan.stack)!=1) { cancel("instance no longer available"); return; }
    const Equipment previous=loan.before;
    const bool ours=equipped(pc)==loan.base && items.worn()==loan.stack;
    Clip clip=ours ? magazine(pc) : loan.clip;
    Ptr dropped=drop(pc,loan.base,loan.stack,point,&loan.angles);
    unsigned base=id(loan.base);
    loan={};
    if (dropped && loose(dropped) && id(ptr(dropped,0x20))==base) {
        if (clip.ammo) call(writeClip,dropped,clip.rounds,clip.ammo);
        flights.push_back({id(dropped),base,ptr(pc,0x40),velocity,now});
        log("release ref=%08x base=%08x speed=%.2f clip=%d",id(dropped),base,velocity.length(),clip.ammo?clip.rounds:-1);
    } else log("native drop returned no loose weapon base=%08x",base);
    Ptr current=equipped(pc);
    if (ours && (!current || id(current)==0x1f4 || id(current)==base || current==previous.form))
        restore(pc,previous);
    else if (!current) restore(pc,previous);
}
bool finishEquip(Ptr pc,DWORD now) {
    auto active=stacks(pc,loan.base);
    Ptr worn=active.worn();
    // Equip can complete on the next game frame, replacing the extra-data
    // list supplied to EquipItem. Only follow a replacement that did not
    // belong to another copy and whose original list has disappeared.
    bool replacement=worn && !active.contains(loan.stack) && !loan.protectedStacks.contains(worn);
    if (equipped(pc)==loan.base && worn && quantity(worn)==1 && (worn==loan.stack || replacement)) {
        loan.stack=worn;
        if (host::draw(loan.hand,loan.base)) {
            setMagazine(pc,loan.returningClip); loan.clip=magazine(pc);
            loan.equipping=false; loan.protectedStacks={};
            log("borrow source=%08x base=%08x hand=%d previous=%08x",loan.source,id(loan.base),loan.hand,id(loan.before.form));
            return true;
        }
    }
    if (now-loan.equipSince<650) return false;
    log("draw declined source=%08x current=%08x original=%p worn=%p listed=%d",loan.source,id(equipped(pc)),loan.stack,worn,active.contains(loan.stack));
    if (active.contains(loan.stack) && quantity(loan.stack)==1) {
        Vec point{},angles{};
        bool located=host::pose(loan.hand,point,angles);
        drop(pc,loan.base,loan.stack,located?&point:nullptr,&loan.angles);
    }
    if (equipped(pc)==loan.base || id(equipped(pc))==0x1f4) restore(pc,loan.before);
    loan={};
    return false;
}
Ptr struckActor(Ptr pc, Ptr ref, Ptr node) {
    // JIP's GetActorsByProcessingLevel 0 uses this same list. Only ask Havok for
    // exact contact for actors near the weapon's rendered world bounds.
    Ptr manager=reinterpret_cast<Ptr>(0x11e0e80);
    Ptr actors=ptr(manager,8);
    unsigned begin=field<unsigned>(manager,0x14), end=field<unsigned>(manager,0x24);
    if (!actors || end>field<unsigned short>(manager,0xc)) return nullptr;
    for (unsigned i=begin;i<end;++i) {
        Ptr actor=ptr(actors,4*i);
        if (actor==pc || (type(actor)!=0x3b && type(actor)!=0x3c)) continue;
        unsigned life=field<unsigned>(actor,0x108);
        Ptr actorNode=root(actor);
        if (!actorNode || life==1 || life==2 || (field<unsigned>(actor,8)&(0x20|0x800))) continue;
        if (!boundsNear(node,actorNode)) continue;
        Value touching=call(hasContact,ref,actor);
        if (touching.type==1 && touching.number) return actor;
    }
    return nullptr;
}
// Return true when this release no longer needs tracking.
bool finishFlight(Flight& flight, Ptr pc, DWORD now) {
    Ptr ref=lookup(flight.ref);
    if (ptr(pc,0x40)!=flight.cell || !loose(ref) || id(ptr(ref,0x20))!=flight.base ||
        host::held(0)==ref || host::held(1)==ref) return true;
    if (now-flight.since>(flight.launched?3000u:700u)) return true;
    Ptr node=root(ref);
    Ptr body=mobileBody(node);
    if (!body) return flight.launched;
    float weight=field<float>(ptr(ref,0x20),0x90);
    if (!flight.launched) {
        float speed=flight.releaseVelocity.length();
        if (speed>.18f) launch(body,flight.releaseVelocity);
        log("world_body ref=%08x speed=%.2f",flight.ref,speed);
        if (!hasContact || impact::damage(weight,speed)<=0) return true;
        flight.launched=true;
        flight.since=flight.lastSample=flight.lastMoving=now;
        flight.incomingSpeed=speed;
        return false;
    }
    if (now-flight.lastSample>250) return true;
    // Havok can already have stopped or deflected the item on the contact frame.
    float damage=impact::damage(weight,flight.incomingSpeed);
    if (damage>0) {
        if (Ptr target=struckActor(pc,ref,node)) {
            unsigned targetID=id(target);
            dealImpactDamage(target,pc,damage);
            log("throw_impact ref=%08x target=%08x weight=%.2f incoming=%.2f damage=%.2f",
                flight.ref,targetID,weight,flight.incomingSpeed,damage);
            return true;
        }
    }
    flight.lastSample=now;
    flight.incomingSpeed=field<Vec>(body,0x1b0).length();
    if (!std::isfinite(flight.incomingSpeed)) return true;
    // An upward throw can briefly stop at the top of its arc.
    if (flight.incomingSpeed>=.18f) flight.lastMoving=now;
    return now-flight.lastMoving>150;
}
void frame() {
    Ptr pc=player();
    if (!pc || !ptr(pc,0x40)) {
        flights.clear();
        return;
    }
    DWORD now=GetTickCount();
    flights.erase(std::remove_if(flights.begin(),flights.end(),
        [pc,now](Flight& flight){return finishFlight(flight,pc,now);}),flights.end());
    finishRestore(pc,now);
    if (pendingTake.reference) { finishPickup(pc,now); return; }
    if (loan.base) {
        int h=loan.hand;
        auto input=host::input(h);
        if (ptr(pc,0x40)!=loan.cell) { cancel("cell change"); return; }
        Value mode=call(gameplay);
        if (!input.tracked || mode.type!=1 || !mode.number) {
            loan.motion.clear(); loan.holdingKeep=false; loan.armedKeep=false; return;
        }
        if (loan.equipping && !finishEquip(pc,now)) return;
        Vec point{},angles{},origin=field<Vec>(pc,0x30);
        bool validPoint=host::pose(h,point,angles) && (point-origin).length()<220;
        if (validPoint) loan.angles=angles;
        if (equipped(pc)==loan.base) loan.clip=magazine(pc);
        Vec velocity{};
        if (validPoint) velocity=loan.motion.step(point,origin,now);
        else loan.motion.clear();
        if (!input.grip) {
            if (loan.holdingKeep && input.keep && now-loan.keepSince>=keepHoldMs) cancel("kept");
            else release(pc,validPoint?&point:nullptr,velocity,now);
            return;
        }
        if (!input.keep) { loan.armedKeep=true; loan.holdingKeep=false; }
        if (input.keep && loan.armedKeep) {
            if (!loan.holdingKeep) { loan.keepSince=now; loan.holdingKeep=true; }
        }
        return;
    }
    if (restoring.wanted.form) return;
    for (int h=0;h<2;++h) {
        auto input=host::input(h);
        if (!input.tracked || !input.grip) { gated&=~(1u<<h); continue; }
        if (gated&(1u<<h)) continue;
        if (!(reported&(1u<<h))) {
            Vec point{},angles{};
            if (host::pose(h,point,angles)) {
                log("first_grip hand=%d world=%.2f,%.2f,%.2f",h,point.x,point.y,point.z);
                reported|=1u<<h;
            }
        }
        Ptr ref=host::held(h);
        if (!ref || !eligible(ref)) continue;
        Value mode=call(gameplay);
        if (mode.type!=1 || !mode.number) continue;
        gated|=1u<<h;
        beginPickup(pc,ref,h,now);
        if (loan.base || pendingTake.reference) return;
    }
}
void __cdecl message(Message* msg) {
    switch (msg->type) {
    case 1: case 2: case 6: case 7: case 8: case 14:
        loan={}; flights.clear(); pendingTake={}; restoring={}; reported=0; gated=3;
        loaded=msg->type==14 || (msg->type==8 && msg->data); return;
    case 20:
        if (!loaded) return;
        if (!initialized) {
            initialized=true; ready=initialize();
            log("version=0.1.2 ready=%d impacts=%d",ready,hasContact!=nullptr);
        }
        if (ready) frame();
    }
}
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSE* n,Info* info) {
    if (!n || !info) return false;
    *info={1,"TTWVR Field Weapons",3};
    return !n->isEditor && n->runtime==0x040020d0 && n->version>=6;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSE* n) {
    scripts=static_cast<Scripts*>(n->query(6));
    auto data=static_cast<Data*>(n->query(7));
    auto messages=static_cast<Messages*>(n->query(2));
    if (!scripts || !scripts->compile || !data || !messages) return false;
    makeRef=reinterpret_cast<MakeRef>(data->get(7));
    readRef=reinterpret_cast<ReadRef>(data->get(2));
    return makeRef && readRef && messages->listen(n->handle(),"NVSE",message);
}
