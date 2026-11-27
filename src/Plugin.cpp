// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "Api.h"
#include "Game.h"
#include "Host.h"
#include "Impact.h"
#include "Stagger.h"
#include "NpcThrow.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

namespace {
using namespace game;
Scripts* scripts;
Arrays* arrays;
using MakeRef=Ptr (__stdcall*)(Ptr,Ptr,int,Ptr);
using ReadRef=Ptr (__cdecl*)(unsigned);
MakeRef makeRef;
ReadRef readRef;
void *hasContact;
void *gameplay,*materialize,*holster,*readClip,*readAmmo,*fundedClip,*writeClip,*available;
void *addAmmo,*removeAmmo,*readSlot,*writeSlot,*keyOwner,*selectAmmo;
void *readAction,*writeAction,*ownedLabel,*assignSlot,*pouchSound;
void *getClaim,*exchangeClaim;
unsigned lastFocus;
Ptr watchedReloadForm;
int wornReloadSlot(Ptr form) {
    if (!form || form!=watchedReloadForm) return -2; // use TTWVR's ordinary lookup
    Ptr entry=ptr(ptr(player(),0x68),0x114);
    if (ptr(entry,8)!=form) return -2;
    for (Ptr node=ptr(entry,0);node;node=ptr(node,4)) {
        Ptr stack=ptr(node,0);
        if (extra(stack,0x16)) return hotkey(stack)-1; // -1 is the host's unslotted key
    }
    return -2;
}
bool extended,physicalStore,anyOwnedSlot,holdToKeep,watchDraws;
constexpr DWORD keepHoldMs=1000;
bool initialized,ready,loaded,npcReady;
unsigned gated, reported{};
struct Clip {
    Ptr ammo{};
    int rounds{};
    host::ReloadState action{};
    bool hasAction{};
};
struct Equipment { Ptr form{},stack{}; bool out{}; int hand{}; Clip clip; unsigned healthBits{}; bool plain{}; };
struct Loan {
    Ptr base{}, stack{},cell{};
    int hand{}, slot{};
    bool nativeHolster{};
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
    unsigned ref, base, thrower;
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
    // Loaded rounds count toward carried ammo until the gun is dropped.
    // A zero-length magazine must be distinguishable from an untouched world gun.
    readClip=scripts->compile(R"(ref item
begin Function {item}
 if eval (item.AuxVarSize "_TTWVRFieldMagazine") >= 2
  SetFunctionValue (item.AuxVarGetFlt "_TTWVRFieldMagazine")
 else
  SetFunctionValue -1
 endif
end)");
    readAmmo=scripts->compile("ref item\nbegin Function {item}\n SetFunctionValue (item.AuxVarGetRef \"_TTWVRFieldMagazine\" 1)\nend");
    fundedClip=scripts->compile("ref item\nbegin Function {item}\n SetFunctionValue (item.AuxVarSize \"_TTWVRFieldMagazine\")\nend");
    writeClip=scripts->compile(R"(ref item
int rounds
ref ammo
begin Function {item rounds ammo}
 item.AuxVarSetFlt "_TTWVRFieldMagazine" rounds
 item.AuxVarSetRef "_TTWVRFieldMagazine" ammo 1
 item.AuxVarSetFlt "_TTWVRFieldMagazine" 1 2
end)");
    // Each world reference owns its reload snapshot. Store unsigned words as
    // exact doubles in JIP; split the input because xNVSE script ints are signed.
    readAction=scripts->compile(R"(ref item
int word
begin Function {item word}
 if eval (item.AuxVarSize "_TTWVRFieldAction") == 13
  SetFunctionValue (item.AuxVarGetFlt "_TTWVRFieldAction" word)
 else
  SetFunctionValue -1
 endif
end)");
    writeAction=scripts->compile(R"(ref item
int word
int lo
int hi
begin Function {item word lo hi}
 item.AuxVarSetFlt "_TTWVRFieldAction" (lo + (hi * 65536.0)) word
end)");
    available=scripts->compile("ref ammo\nbegin Function {ammo}\n SetFunctionValue (player.GetItemCount ammo)\nend");
    // JIP exposes the actor controller and the loose rigid body's contact lists separately.
    hasContact=scripts->compile("ref item\nref actorRef\nbegin Function {item actorRef}\n SetFunctionValue (eval (item.GetHasContact actorRef) || (actorRef.GetHasContact item))\nend");
    addAmmo=scripts->compile("ref ammo\nint n\nbegin Function {ammo n}\n player.AddItem ammo n 1\nend");
    removeAmmo=scripts->compile("ref ammo\nint n\nbegin Function {ammo n}\n player.RemoveItem ammo n 1\nend");
    selectAmmo=scripts->compile("ref ammo\nbegin Function {ammo}\n player.EquipItem ammo 0 1\nend");
    const char* ini=".\\Data\\NVSE\\Plugins\\TTWVRFieldWeapons.ini";
    log("stagger_rigs=%u",stagger::initialize(scripts,arrays));
    npcReady=GetPrivateProfileIntA("NPC","ThrowEmptyPistols",1,ini) &&
        GetModuleHandleW(L"kNVSE.dll") && npc::initialize(scripts,log);
    if (!gameplay || !materialize || !holster || !readClip || !readAmmo || !fundedClip || !writeClip ||
        !available || !addAmmo || !removeAmmo || !selectAmmo || !host::connect()) return false;
    host::observeReloadSlots(wornReloadSlot);
    pouchSound=scripts->compile(R"(ref ammo
ref cue
begin Function {ammo}
 let cue := GetPickupSound ammo
 if eval !cue
  let cue := ITMAmmunitionUp
 endif
 PlaySound cue
 SetFunctionValue cue
end)");
    log("ammo_pouch_audio=%d",pouchSound && host::observePouchReturns());
    holdToKeep=GetPrivateProfileIntA("Holsters","HoldToKeep",1,ini)!=0;
    physicalStore=GetPrivateProfileIntA("Holsters","PhysicalHolstering",0,ini)!=0;
    anyOwnedSlot=GetPrivateProfileIntA("Holsters","ReturnOwnedAtAnySlot",0,ini)!=0;
    keyOwner=scripts->compile("int slot\nbegin Function {slot}\n SetFunctionValue (GetHotkeyItemRef slot)\nend");
    bool throwable=GetPrivateProfileIntA("Holsters","ThrowableWeapons",0,ini)!=0;
    if (throwable || physicalStore) {
        readSlot=scripts->compile("ref item\nbegin Function {item}\n SetFunctionValue (item.AuxVarGetFlt \"_TTWVRFieldSlot\")\nend");
        writeSlot=scripts->compile("ref item\nint slot\nbegin Function {item slot}\n item.AuxVarSetFlt \"_TTWVRFieldSlot\" slot\nend");
        // A null entry is a pre-r7 slot. The player reference means that the slot
        // was filled again, even if its new weapon has since been dropped.
        getClaim=scripts->compile(R"(int slot
begin Function {slot}
 SetFunctionValue (player.AuxVarGetRef "_TTWVRFieldSlotClaims" (slot - 1))
end)");
        exchangeClaim=scripts->compile(R"(int slot
ref nextClaim
ref previous
int index
int size
begin Function {slot nextClaim}
 let index := slot - 1
 let previous := player.AuxVarGetRef "_TTWVRFieldSlotClaims" index
 let size := player.AuxVarSize "_TTWVRFieldSlotClaims"
 while size < index
  player.AuxVarSetFlt "_TTWVRFieldSlotClaims" 0 -1
  let size += 1
 loop
 player.AuxVarSetRef "_TTWVRFieldSlotClaims" nextClaim index
 SetFunctionValue previous
end)");
        ownedLabel=scripts->compile(R"(ref item
int owned
int pos
string_var displayName
begin Function {item owned}
 let displayName := item.LNGetName
 let pos := sv_Find " [Yours]" displayName
 if owned
  if eval pos == -1 && (sv_Length displayName) <= 119
   item.SetRefName (sv_Construct "%z [Yours]" displayName)
  endif
 elseif eval pos >= 0 && pos + 8 == (sv_Length displayName)
  sv_Erase displayName pos 8
  item.SetRefName
  if eval (item.LNGetName) != displayName
   item.SetRefName $displayName
  endif
 endif
 sv_Destruct displayName
end)");
        if (physicalStore)
            assignSlot=scripts->compile("ref item\nint slot\nbegin Function {item slot}\n SetFunctionValue (item.SetHotkeyItemRef slot)\nend");
        bool ownership=keyOwner && readSlot && writeSlot && getClaim && exchangeClaim && ownedLabel;
        physicalStore=physicalStore && assignSlot && ownership;
        watchDraws=ownership && (throwable || physicalStore) && host::observeHolsters();
        extended=throwable && watchDraws;
    }
    return true;
}
Clip magazine(Ptr pc) {
    Ptr info=ammoInfo(pc);
    return {ptr(info,8),field<int>(info,4)};
}
Ptr defaultAmmo(Ptr weapon) {
    if (field<unsigned char>(weapon,0xf4)<3) return nullptr;
    Ptr a=ptr(weapon,0xa8);
    if (type(a)==0x55) a=ptr(a,0x18);
    return type(a)==0x29 ? a : nullptr;
}
Clip physicalMagazine(Ptr pc,Ptr weapon,Ptr knownAmmo=nullptr) {
    Clip clip=magazine(pc);
    clip.hasAction=host::readReload(weapon,clip.action) && clip.action.live>=0;
    // Fallout can clear its ammo type when the final round leaves inventory.
    // An active, empty reload mechanism still has a state worth storing.
    if (!clip.ammo && clip.hasAction && !clip.action.live)
        clip.ammo=knownAmmo ? knownAmmo : defaultAmmo(weapon);
    if (clip.hasAction) clip.rounds=clip.action.live;
    return clip;
}
void readWorldAction(Ptr ref,Clip& clip) {
    if (!clip.ammo || !readAction) return;
    unsigned words[13];
    for (int i=0;i<13;++i) {
        Value word=call(readAction,ref,i);
        if (word.type!=1 || !std::isfinite(word.number) || word.number<0 || word.number>4294967295.) return;
        words[i]=static_cast<unsigned>(word.number);
    }
    std::memcpy(static_cast<void*>(&clip.action),words,sizeof words);
    const auto& s=clip.action;
    clip.hasAction=s.live==clip.rounds && s.magazine<=1 && s.chamber<=1 &&
        s.magazineRounds>=0 && s.magazineRounds<=s.live && s.spent>=0 && s.spent<=65535 &&
        std::isfinite(s.action) && s.action>=0 && s.action<=1 && s.chamberIndex>=0 && s.chamberIndex<16 &&
        std::all_of(s.chambers,s.chambers+16,[](unsigned char value){return value<=2;});
}
void setMagazine(Ptr pc,Clip clip,Ptr weapon) {
    Ptr info=ammoInfo(pc);
    Ptr activeAmmo=ptr(info,8);
    if (!clip.ammo || clip.rounds<0) return;
    if (activeAmmo!=clip.ammo) {
        if (clip.rounds || (activeAmmo && !supportsAmmo(weapon,activeAmmo))) return;
        if (activeAmmo) clip.ammo=activeAmmo;
    }
    Value count=call(available,clip.ammo);
    if (count.type!=1) return;
    int rounds=std::clamp(clip.rounds,0,std::max(0,int(count.number)));
    if (info) std::memcpy(info+4,&rounds,sizeof rounds);
    host::ReloadState action{};
    if (clip.hasAction && clip.action.live==rounds) action=clip.action;
    else {
        action.live=rounds;
        action.chamber=rounds>0;
        action.magazineRounds=std::max(0,rounds-1);
        std::fill_n(action.chambers,std::min(rounds,16),1);
    }
    if (action.reconcileMagazine())
        log("magazine_reseated base=%08x rounds=%d at=equip",id(weapon),rounds);
    if (host::rememberReload(weapon,clip.ammo,action)) host::applyReload(pc,weapon);
}
int inventory(Ptr ammo) {
    Value v=call(available,ammo);
    return v.type==1 ? std::max(0,int(v.number)) : -1;
}
void restoreSlot(Ptr pc,Ptr form,Ptr stack,int slot) {
    if (!keyOwner || slot<1 || slot>8 || !stacks(pc,form).contains(stack) || hotkey(stack)) return;
    Value owner;
    if (!scripts->call(keyOwner,nullptr,nullptr,&owner,1,slot) || (owner.type==2 && owner.object)) return;
    if (setHotkey(stack,slot)) log("holster_restored base=%08x slot=%d",id(form),slot);
}
void storeMagazine(Ptr dropped,Clip clip,int before) {
    if (!clip.ammo || before<0) return;
    int after=inventory(clip.ammo);
    if (after<0) return;
    int wanted=std::clamp(clip.rounds,0,before);
    Ptr node=worldAmmo(dropped,clip.ammo);
    if (!node) return;
    int stillOwed=std::max(0,wanted-std::max(0,before-after));
    if (stillOwed) call(removeAmmo,clip.ammo,stillOwed);
    int finalCount=inventory(clip.ammo);
    int moved=std::clamp(before-(finalCount<0 ? after : finalCount),0,wanted);
    std::memcpy(node+0x10,&moved,4);
    call(writeClip,dropped,moved,clip.ammo);
    if (writeAction && clip.hasAction && moved==clip.action.live) {
        clip.action.reconcileMagazine();
        std::fill_n(clip.action.padding,3,0);
        unsigned words[13];
        std::memcpy(words,&clip.action,sizeof words);
        for (int i=0;i<13;++i) call(writeAction,dropped,i,int(words[i]&65535),int(words[i]>>16));
    }
    log("ammo_drop ref=%08x ammo=%08x rounds=%d carried=%d",id(dropped),id(clip.ammo),moved,finalCount);
}
Equipment equipment(Ptr pc) {
    Ptr form=equipped(pc);
    Equipment old={form,form ? stacks(pc,form).worn() : nullptr,drawn(pc),host::drawHand(),physicalMagazine(pc,form)};
    // A holstered weapon can remain equipped after TTWVR has shut down its
    // active reload mechanism. Read the numbered slot before the loan adds ammo.
    unsigned ammoID{};
    host::ReloadState saved{};
    if (!old.out && host::readHolsterReload(form,hotkey(old.stack),ammoID,saved)) {
        Ptr ammo=lookup(ammoID);
        if (!ammoID && !saved.live && !saved.magazineRounds)
            ammo=old.clip.ammo ? old.clip.ammo : defaultAmmo(form);
        if (supportsAmmo(form,ammo) && saved.live>=0)
            old.clip={ammo,saved.live,saved,true};
    }
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
    int before=magazine(pc).rounds;
    setMagazine(pc,old.clip,old.form);
    if (!old.out && old.clip.ammo && before!=magazine(pc).rounds)
        log("holster_restore base=%08x slot=%d auto_clip=%d restored_clip=%d",
            id(old.form),hotkey(items.worn()),before,magazine(pc).rounds);
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
    watchedReloadForm=old.form;
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
int markedSlot(Ptr ref) {
    if (!readSlot) return 0;
    auto marker=call(readSlot,ref);
    return marker.type==1 && marker.number>=1 && marker.number<=8 ? int(marker.number) : 0;
}
void clearHome(Ptr ref,int slot) {
    call(writeSlot,ref,0);
    call(ownedLabel,ref,0);
    log("holster_claim_released ref=%08x slot=%d",id(ref),slot);
}
int worldHome(Ptr ref) {
    int slot=markedSlot(ref);
    if (!slot || !getClaim || !keyOwner || !ownedLabel || !writeSlot) return 0;
    Value claim,occupant;
    if (!scripts->call(getClaim,nullptr,nullptr,&claim,1,slot) ||
        !scripts->call(keyOwner,nullptr,nullptr,&occupant,1,slot)) return 0;
    // Fallout may transfer the hotkey to an identical inventory copy on drop.
    // An explicit physical replacement changes the saved claim, even for the same model.
    bool claimedHere=claim.type==2 && claim.object==ref;
    bool superseded=claim.type==2 && claim.object && !claimedHere;
    bool occupied=occupant.type==2 && occupant.object;
    if (superseded || (occupied && (!claimedHere || ptr(occupant.object,0x20)!=ptr(ref,0x20)))) {
        clearHome(ref,slot);
        return 0;
    }
    return slot;
}
void claimSlot(int slot,Ptr claimant) {
    auto previous=call(exchangeClaim,slot,claimant);
    if (previous.type==2 && previous.object!=claimant && loose(previous.object) && markedSlot(previous.object)==slot)
        clearHome(previous.object,slot);
}
void rememberEmptyHolsters(Ptr receivedAmmo,const Equipment& previous) {
    if (!keyOwner) return;
    for (int slot=1;slot<=8;++slot) {
        Value owner=call(keyOwner,slot);
        if (owner.type!=2 || !owner.object) continue;
        Ptr gun=ptr(owner.object,0x20);
        if (!eligibleForm(gun) || !supportsAmmo(gun,receivedAmmo)) continue;
        // The newly acquired rounds are accounted for separately. Existing
        // alternate ammo makes the previously loaded count ambiguous.
        Ptr ammo=ptr(gun,0xa8);
        if (type(ammo)==0x55) {
            bool otherRounds=false;
            for (Ptr node=ammo+0x18;node;node=ptr(node,4)) {
                Ptr other=ptr(node,0);
                if (other && other!=receivedAmmo && inventory(other)!=0) { otherRounds=true; break; }
            }
            if (otherRounds) continue;
        }
        Ptr stack=ptr(readRef(id(owner.object)),8);
        if (!stack || field<unsigned>(extra(stack,0x6e),0x10)>0) continue;
        host::ReloadState state{};
        if (previous.form==gun && stack==previous.stack) {
            if (previous.clip.rounds>0) continue;
            if (previous.clip.hasAction) state=previous.clip.action;
        }
        if (host::rememberEmptyHolster(gun,receivedAmmo,slot,state))
            log("holster_empty_recorded base=%08x slot=%d ammo=%08x",id(gun),slot,id(receivedAmmo));
    }
}
void borrow(Ptr pc,Ptr ref,int hand,const Equipment& previous) {
    Ptr form=ptr(ref,0x20);
    unsigned source=id(ref);
    const bool sameEquipped=previous.form==form && previous.stack;
    auto before=stacks(pc,form);
    Vec angles=field<Vec>(ref,0x24);
    int slot=worldHome(ref);
    Clip clip{};
    bool storedLoad=false, legacy=false;
    Ptr native=extra(ref+0x44,0x6e);
    if (native && supportsAmmo(form,ptr(native,0xc))) {
        clip={ptr(native,0xc),int(std::min(field<unsigned>(native,0x10),65535u))};
    } else if (field<unsigned char>(form,0xf4)>=3) {
        auto stored=call(readClip,ref);
        if (stored.type==1 && stored.number>=0) {
            auto kind=call(readAmmo,ref);
            if (kind.type==2 && supportsAmmo(form,kind.object)) {
                clip={kind.object,int(std::min(stored.number,65535.))};
                storedLoad=true;
                auto fields=call(fundedClip,ref);
                legacy=fields.type!=1 || fields.number<3; // r1 left these rounds in carried ammo.
            }
        }
        if (!clip.ammo) clip.ammo=defaultAmmo(form);
    }
    readWorldAction(ref,clip);
    int carried=clip.ammo ? inventory(clip.ammo) : -1;
    if (!host::take(hand)) {
        if (sameEquipped) restore(pc,previous);
        return;
    }
    if (carried>=0 && !legacy) {
        int afterTake=inventory(clip.ammo);
        if (afterTake>=0) {
            int received=std::max(0,afterTake-carried);
            if (!native && !storedLoad) clip.rounds=received;
            else if (received<clip.rounds) call(addAmmo,clip.ammo,clip.rounds-received);
            int total=inventory(clip.ammo);
            if (!carried && total>0) rememberEmptyHolsters(clip.ammo,previous);
            log("ammo_pickup source=%08x ammo=%08x rounds=%d native_credit=%d carried=%d",
                source,id(clip.ammo),clip.rounds,received,total);
        }
    }
    auto after=stacks(pc,form);
    Ptr exact=resolvePickup(pc,form,before,after);
    if (!exact) {
        if (sameEquipped) restore(pc,previous);
        log("native_take source=%08x before=%d after=%d no unique stack",source,before.delta,after.delta);
        return;
    }
    watchedReloadForm=form;
    loan={}; loan.base=form; loan.stack=exact; loan.before=previous;
    loan.slot=slot; loan.angles=angles; loan.hand=hand; loan.cell=ptr(pc,0x40);
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
bool storeAtHolster(Ptr pc,int touched) {
    if (!touched) return false;
    auto items=stacks(pc,loan.base);
    if (!items.contains(loan.stack) || quantity(loan.stack)!=1 || items.worn()!=loan.stack ||
        equipped(pc)!=loan.base) return false;
    if (loan.slot && touched!=loan.slot && !anyOwnedSlot) return false;
    int slot=loan.slot ? loan.slot : touched;
    Clip clip=physicalMagazine(pc,loan.base,loan.clip.ammo);
    if (hotkey(loan.stack)!=slot) {
        Value owner;
        if (!scripts->call(keyOwner,nullptr,nullptr,&owner,1,slot)) return false;
        if (owner.type==2 && owner.object) {
            // Only the old hotkey moves. JIP leaves the previous item in inventory.
            Ptr item=makeRef(pc,loan.base,1,loan.stack);
            if (!item || call(assignSlot,item,slot).number!=1) return false;
        } else if (!setHotkey(loan.stack,slot)) return false;
    }
    watchedReloadForm=loan.base;
    claimSlot(slot,pc);
    if (clip.hasAction) {
        if (clip.action.reconcileMagazine())
            log("magazine_reseated base=%08x rounds=%d at=store",id(loan.base),clip.rounds);
        host::rememberReload(loan.base,clip.ammo,clip.action,slot);
    }
    call(holster);
    // Fallout can retain the holstered copy as the equipped weapon even when
    // another numbered slot draws a different copy of the same model.
    if (std::any_of(items.listed.begin(),items.listed.end(),[](Ptr other) {
        return other!=loan.stack && hotkey(other);
    })) unequip(pc,loan.base,loan.stack);
    log("holster_store base=%08x hand=%d touched=%d home=%d assigned=%d clip=%d magazine_in=%d magazine_rounds=%d",
        id(loan.base),loan.hand,touched,loan.slot,slot,clip.ammo?clip.rounds:-1,
        clip.hasAction?int(clip.action.magazine):-1,clip.hasAction?clip.action.magazineRounds:-1);
    cancel("holstered");
    return true;
}
void markHolsterDrop(Ptr ref,int slot) {
    if (!slot) {
        if (int stale=markedSlot(ref)) clearHome(ref,stale);
        return;
    }
    claimSlot(slot,ref);
    call(writeSlot,ref,slot);
    call(ownedLabel,ref,1);
}
void release(Ptr pc,Vec* point,Vec velocity,DWORD now) {
    auto items=stacks(pc,loan.base);
    if (!items.contains(loan.stack) || quantity(loan.stack)!=1) { cancel("instance no longer available"); return; }
    const Equipment previous=loan.before;
    const bool ours=equipped(pc)==loan.base && items.worn()==loan.stack;
    Clip clip=ours ? physicalMagazine(pc,loan.base,loan.clip.ammo) : loan.clip;
    if (!supportsAmmo(loan.base,clip.ammo)) clip={defaultAmmo(loan.base),0};
    int carried=clip.ammo ? inventory(clip.ammo) : -1;
    if (ours && clip.hasAction && magazine(pc).rounds!=clip.rounds)
        log("ammo_release_mismatch base=%08x engine=%d physical=%d",id(loan.base),magazine(pc).rounds,clip.rounds);
    int slot=loan.slot, hand=loan.hand;
    Ptr form=loan.base, stack=loan.stack;
    // Fallout otherwise gives this hotkey to another owned copy during DropItem.
    bool unbound=slot && hotkey(stack)==slot;
    if (unbound) removeExtra(stack,0x4a);
    Ptr dropped=drop(pc,loan.base,loan.stack,point,&loan.angles);
    unsigned base=id(loan.base);
    loan={};
    if (dropped && loose(dropped) && id(ptr(dropped,0x20))==base) {
        if (extra(dropped+0x44,0x4a)) removeExtra(dropped+0x44,0x4a);
        markHolsterDrop(dropped,slot);
        storeMagazine(dropped,clip,carried);
        flights.push_back({id(dropped),base,id(pc),ptr(pc,0x40),velocity,now});
        log("release ref=%08x base=%08x speed=%.2f clip=%d velocity_mps=%.2f,%.2f,%.2f hand=%d slot=%d",
            id(dropped),base,velocity.length(),clip.ammo?clip.rounds:-1,velocity.x,velocity.y,velocity.z,hand,slot);
    } else {
        if (unbound) restoreSlot(pc,form,stack,slot);
        log("native drop returned no loose weapon base=%08x",base);
    }
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
            Clip load=loan.returningClip;
            if (load.ammo) {
                load.rounds=std::min(load.rounds,capacity(loan.base,worn));
                if (magazine(pc).ammo!=load.ammo && load.rounds>0) call(selectAmmo,load.ammo);
                setMagazine(pc,load,loan.base);
            }
            loan.clip=load;
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
        Clip clip=loan.returningClip;
        int before=clip.ammo ? inventory(clip.ammo) : -1;
        Ptr dropped=drop(pc,loan.base,loan.stack,located?&point:nullptr,&loan.angles);
        if (loose(dropped)) {
            markHolsterDrop(dropped,loan.slot);
            storeMagazine(dropped,clip,before);
        }
    }
    if (equipped(pc)==loan.base || id(equipped(pc))==0x1f4) restore(pc,loan.before);
    loan={};
    return false;
}
void npcReleased(Ptr actor,Ptr item,Ptr ammo,Vec velocity) {
    // DropItem can attach bonus ammunition even after the last round was fired.
    // The engine transfers this extra to whoever next picks up the world reference.
    Ptr extraList=item+0x44;
    Ptr bonus=extra(extraList,0x6e); // ExtraAmmo
    unsigned pickupRounds=field<unsigned>(bonus,0x10);
    if (bonus) removeExtra(extraList,0x6e);
    if (ammo) call(writeClip,item,0,ammo);
    flights.push_back({id(item),id(ptr(item,0x20)),id(actor),ptr(actor,0x40),velocity,GetTickCount()});
    log("npc_throw actor=%08x target=%08x ref=%08x base=%08x speed=%.2f clip=0 reserve=0 pickup_rounds_removed=%u",
        id(actor),id(ptr(actor,0x128)),id(item),id(ptr(item,0x20)),velocity.length(),pickupRounds);
}
bool touchingActor(Ptr actor,Ptr ref,Ptr node) {
    if (type(actor)!=0x3b && type(actor)!=0x3c) return false;
    unsigned life=field<unsigned>(actor,0x108);
    Ptr actorNode=root(actor);
    if (!actorNode || life==1 || life==2 || (field<unsigned>(actor,8)&(0x20|0x800))) return false;
    if (!boundsNear(node,actorNode)) return false;
    Value touching=call(hasContact,ref,actor);
    return touching.type==1 && touching.number;
}
Ptr struckActor(Ptr owner,Ptr pc,Ptr ref,Ptr node) {
    if (owner!=pc && touchingActor(pc,ref,node)) return pc;
    HighActors actors;
    for (unsigned i=actors.begin;i<actors.end;++i) {
        Ptr actor=actors[i];
        if (actor!=owner && actor!=pc && touchingActor(actor,ref,node)) return actor;
    }
    return nullptr;
}
// Return true when this release no longer needs tracking.
bool finishFlight(Flight& flight, Ptr pc, DWORD now) {
    Ptr ref=lookup(flight.ref);
    Ptr owner=lookup(flight.thrower);
    if (!owner || ptr(pc,0x40)!=flight.cell || !loose(ref) || id(ptr(ref,0x20))!=flight.base ||
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
        if (Ptr target=struckActor(owner,pc,ref,node)) {
            unsigned targetID=id(target);
            float bulk=stagger::targetBulk(target);
            dealImpactDamage(target,owner,damage);
            unsigned reaction=0;
            if (target!=pc && impact::staggers(weight,flight.incomingSpeed,bulk) && lookup(targetID)==target)
                reaction=stagger::play(target);
            log("throw_impact ref=%08x target=%08x weight=%.2f incoming=%.2f damage=%.2f bulk=%.2f stagger_idle=%08x attacker=%08x",
                flight.ref,targetID,weight,flight.incomingSpeed,damage,bulk,reaction,flight.thrower);
            return true;
        }
    }
    flight.lastSample=now;
    flight.incomingSpeed=motion::fromHavokVelocity(field<Vec>(body,0x1b0)).length();
    if (!std::isfinite(flight.incomingSpeed)) return true;
    // An upward throw can briefly stop at the top of its arc.
    if (flight.incomingSpeed>=.18f) flight.lastMoving=now;
    return now-flight.lastMoving>150;
}
void followHolster(Ptr pc,DWORD now) {
    if (!watchDraws || !drawn(pc)) return;
    int slots[2]={host::recentHolster(0),host::recentHolster(1)};
    if (!slots[0] && !slots[1]) return;
    Ptr form=equipped(pc);
    if (!eligibleForm(form)) return;
    Ptr worn=stacks(pc,form).worn();
    int slot=hotkey(worn);
    if (!slot) return;
    // The host can leave the primary draw-hand override unset after a native draw.
    // The accepted holster action identifies the controller that actually drew it.
    int hand=-1;
    for (int h=0;h<2;++h) {
        if (slots[h]!=slot || host::held(h)) continue;
        auto grip=host::input(h);
        if (!grip.tracked || !grip.grip) continue;
        if (hand>=0) return;
        hand=h;
    }
    if (hand<0) return;
    auto input=host::input(hand);
    Value mode=call(gameplay);
    if (mode.type!=1 || !mode.number) return;
    watchedReloadForm=form;
    Clip clip=physicalMagazine(pc,form);
    if (clip.hasAction && clip.action.reconcileMagazine()) {
        host::rememberReload(form,clip.ammo,clip.action,slot);
        host::applyReload(pc,form);
        log("magazine_reseated base=%08x rounds=%d at=draw",id(form),clip.rounds);
    }
    host::forgetHolsters();
    if (!extended || (quantity(worn)!=1 && !splitOne(itemEntry(pc,form),worn,true))) return;
    loan={};
    loan.base=form; loan.stack=worn; loan.hand=hand; loan.slot=slot; loan.nativeHolster=true;
    loan.cell=ptr(pc,0x40); loan.clip=clip; loan.armedKeep=!input.keep;
    loan.equipSince=now;
    log("holster_draw base=%08x hand=%d slot=%d clip=%d magazine_in=%d magazine_rounds=%d",
        id(form),hand,slot,clip.ammo?clip.rounds:-1,
        clip.hasAction?int(clip.action.magazine):-1,clip.hasAction?clip.action.magazineRounds:-1);
}
void frame() {
    Ptr pc=player();
    if (!pc || !ptr(pc,0x40)) {
        flights.clear();
        return;
    }
    DWORD now=GetTickCount();
    for (unsigned kind=0;kind<2;++kind) {
        unsigned ammo=host::takePouchReturn(kind);
        if (!ammo) continue;
        Ptr form=lookup(ammo);
        if (type(form)!=0x29 || !pouchSound) continue;
        Value cue=call(pouchSound,form);
        if (cue.type==2 && cue.object)
            log("ammo_pouch_sound ammo=%08x sound=%08x",ammo,id(cue.object));
    }
    flights.erase(std::remove_if(flights.begin(),flights.end(),
        [pc,now](Flight& flight){return finishFlight(flight,pc,now);}),flights.end());
    // JIP display names are session-local. Reapply the persistent slot marker on focus after loading.
    if (ownedLabel && readSlot) {
        Ptr ui=*reinterpret_cast<Ptr*>(0x11d8a80);
        Ptr target=ptr(ui,0xfc);
        if (id(target)!=lastFocus) {
            lastFocus=id(target);
            if (eligible(target)) {
                if (worldHome(target)) call(ownedLabel,target,1);
            }
        }
    }
    if (npcReady) npc::update(pc,now,npcReleased);
    finishRestore(pc,now);
    if (pendingTake.reference) { finishPickup(pc,now); return; }
    if (loan.base) {
        if (loan.nativeHolster && (equipped(pc)!=loan.base || !drawn(pc))) { cancel("native holster or equipment change"); return; }
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
        if (equipped(pc)==loan.base) loan.clip=physicalMagazine(pc,loan.base,loan.clip.ammo);
        Vec velocity{};
        if (validPoint) velocity=loan.motion.step(point,origin,now);
        else loan.motion.clear();
        if (!input.grip) {
            // A Pip-Boy assignment can also replace the home while the field
            // copy is held. The exact worn stack owns a slot only if it carries its hotkey.
            if (loan.slot && hotkey(loan.stack)!=loan.slot) {
                Value owner=call(keyOwner,loan.slot);
                if (owner.type==2 && owner.object && ptr(owner.object,0x20)!=loan.base) loan.slot=0;
            }
            if (physicalStore) {
                int touched=host::holsterAt(pc,h);
                if (storeAtHolster(pc,touched)) return;
                log("holster_miss hand=%d home=%d touched=%d pose=%d",h,loan.slot,touched,validPoint);
            }
            if (holdToKeep && loan.holdingKeep && input.keep && now-loan.keepSince>=keepHoldMs) {
                if (loan.slot) { restoreSlot(pc,loan.base,loan.stack,loan.slot); call(holster); }
                cancel("kept");
            }
            else release(pc,validPoint?&point:nullptr,velocity,now);
            return;
        }
        if (!holdToKeep) return;
        if (!input.keep) { loan.armedKeep=true; loan.holdingKeep=false; }
        if (input.keep && loan.armedKeep) {
            if (!loan.holdingKeep) { loan.keepSince=now; loan.holdingKeep=true; }
        }
        return;
    }
    if (restoring.wanted.form) return;
    followHolster(pc,now);
    if (loan.base) return;
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
        host::takePouchReturn(0); host::takePouchReturn(1);
        watchedReloadForm=nullptr;
        host::forgetHolsters(); npc::reset(); loan={}; flights.clear(); pendingTake={}; restoring={}; reported=0; lastFocus=0; gated=3;
        loaded=msg->type==14 || (msg->type==8 && msg->data); return;
    case 20:
        if (!loaded) return;
        if (!initialized) {
            initialized=true; ready=initialize();
            log("version=%s host=%s ready=%d impacts=%d npc_throws=%d throwable_holsters=%d physical_holstering=%d any_owned_slot=%d hold_to_keep=%d",
                FIELD_WEAPONS_VERSION,host::version(),ready,hasContact!=nullptr,npcReady,extended,physicalStore,anyOwnedSlot,holdToKeep);
        }
        if (ready) frame();
    }
}
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSE* n,Info* info) {
    if (!n || !info) return false;
    *info={1,"TTWVR Field Weapons",18};
    return !n->isEditor && n->runtime==0x040020d0 && n->version>=6;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSE* n) {
    scripts=static_cast<Scripts*>(n->query(6));
    arrays=static_cast<Arrays*>(n->query(5));
    auto data=static_cast<Data*>(n->query(7));
    auto messages=static_cast<Messages*>(n->query(2));
    if (!scripts || !scripts->compile || !data || !messages) return false;
    makeRef=reinterpret_cast<MakeRef>(data->get(7));
    readRef=reinterpret_cast<ReadRef>(data->get(2));
    return makeRef && readRef && messages->listen(n->handle(),"NVSE",message);
}
