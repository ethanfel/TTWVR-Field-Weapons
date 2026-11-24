// SPDX-License-Identifier: GPL-3.0-only
#include "Stagger.h"
#include "Game.h"
#include "Impact.h"
#include <map>
#include <string>

namespace stagger {
using namespace game;
namespace {
struct Reaction { unsigned id; int rank; };
std::map<std::string,Reaction> reactions;
Scripts* api;
void *getSkeleton,*playIdle;

std::string meshPath(const char* text) {
    std::string path=text ? text : "";
    for (char& c:path) {
        if (c=='/') c='\\';
        else if (c>='A' && c<='Z') c=char(c-'A'+'a');
    }
    if (path.compare(0,7,"meshes\\")==0) path.erase(0,7);
    return path;
}
// Rank one-shot torso reactions ahead of generic hit reactions. Compare complete
// filenames so limb injuries, deaths and first-person clips cannot be picked.
int priority(const std::string& name) {
    static const char* const files[]={
        "mt_hittorso.kf","specialidle_hittorso.kf","mtspecialidle_hittorso.kf",
        "hitreaction_torso.kf","h2hhitbody.kf","specialidlehitreact.kf",
        "mtspecialidle_hit.kf","specialidle_hit.kf","mtspecialidle_hitleft.kf"
    };
    for (unsigned i=0;i<sizeof(files)/sizeof(*files);++i)
        if (name==files[i]) return int(i);
    return -1;
}
void indexIdle(Ptr form) {
    if (type(form)!=0x48 || (id(form)>>24)==0xff || ptr(form,0x40) ||
        (field<unsigned>(form,8)&0x20)) return;
    std::string path=meshPath(field<const char*>(form,0x1c));
    const auto dir=path.rfind("\\idleanims\\");
    if (dir==std::string::npos) return;
    const int rank=priority(path.substr(dir+11));
    if (rank<0) return;
    auto rig=path.substr(0,dir);
    auto it=reactions.find(rig);
    if (it==reactions.end() || rank<it->second.rank || (rank==it->second.rank && id(form)<it->second.id))
        reactions[rig]={id(form),rank};
}
}

unsigned initialize(Scripts* scripts, Arrays* arrays) {
    api=scripts;
    if (!arrays || !arrays->size || !arrays->get) return 0;
    auto list=api->compile("begin Function {}\n SetFunctionValue (GetLoadedTypeArray 72)\nend");
    getSkeleton=api->compile("ref actorRef\nref base\nbegin Function {actorRef}\n let base := actorRef.GetLeveledActorBase\n if eval base\n  SetFunctionValue (GetSkeletonModel base)\n endif\nend");
    playIdle=api->compile("ref actorRef\nref idle\nbegin Function {actorRef idle}\n actorRef.PlayIdleEx idle\n SetFunctionValue 1\nend");
    Value forms;
    if (!list || !getSkeleton || !playIdle || !api->call(list,nullptr,nullptr,&forms,0) ||
        forms.type!=4 || !forms.object) return 0;
    unsigned count=arrays->size(forms.object);
    if (count==~0u) return 0;
    Value key; key.type=1;
    for (unsigned i=0;i<count;++i) {
        key.number=i;
        Value element;
        if (arrays->get(forms.object,key,element) && element.type==2) indexIdle(element.object);
    }
    return static_cast<unsigned>(reactions.size());
}
float targetBulk(Ptr actor) {
    Ptr process=ptr(actor,0x68);
    if (!process || field<unsigned char>(process,0x28)>1) return 0;
    Ptr controller=ptr(process,0x138);
    // Use the standing collision dimensions, independent of equipment and pose.
    return impact::relativeBulk(field<float>(controller,0x5f0),field<float>(controller,0x564));
}
unsigned play(Ptr actor) {
    Ptr process=ptr(actor,0x68);
    unsigned life=field<unsigned>(actor,0x108);
    if (reactions.empty() || !process || field<unsigned char>(process,0x28)!=0 ||
        field<unsigned char>(process,0x13c)!=0 || (life!=0 && life!=5)) return 0;
    Value skeleton;
    if (!api->call(getSkeleton,nullptr,nullptr,&skeleton,1,actor) ||
        skeleton.type!=3 || !skeleton.object) return 0;
    std::string rig=meshPath(reinterpret_cast<const char*>(skeleton.object));
    // xNVSE's returned string element owns a copy allocated on the game heap.
    using Free=void (__thiscall*)(void*,void*);
    reinterpret_cast<Free>(0xaa4060)(reinterpret_cast<void*>(0x11f6238),skeleton.object);
    const auto slash=rig.rfind('\\');
    if (slash==std::string::npos) return 0;
    auto it=reactions.find(rig.substr(0,slash));
    if (it==reactions.end()) return 0;
    Ptr idle=lookup(it->second.id);
    Value unused;
    if (type(idle)!=0x48 || !api->call(playIdle,nullptr,nullptr,&unused,2,actor,idle)) return 0;
    return id(idle);
}
}
