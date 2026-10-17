#include "AutoRestock/Transaction.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
template<class T> void Put(void* p,std::size_t o,T x){std::memcpy(static_cast<std::byte*>(p)+o,&x,sizeof x);}
template<class T> T Read(void const* p,std::size_t o){T x;std::memcpy(&x,static_cast<std::byte const*>(p)+o,sizeof x);return x;}
constexpr std::uintptr_t base=0x140000000;
struct Fixture {
    alignas(8) std::byte manager[168]{},player[3448]{},supplies[232]{},inventory[440]{};
    alignas(8) std::byte deque[40]{},component[328]{},adapter[152]{},wrapper[968]{};
    alignas(8) std::byte callbacks[2][16]{},request[72]{},queue[24]{},node[56]{},sentinel[56]{},action[104]{};
    void* adapters[1]{adapter};void* tracked[1]{adapter};void* queued[1]{request};void* actions[1]{action};
    StackStorage source{},destination{},secondSource{};
    std::array<StackStorage,36> actualSlots{};
    std::vector<void*> owned;
    std::vector<int> order;
    std::array<std::byte,40> sourceInfo{},destinationInfo{};
    bool available=true,refuse=false,registerChanges=true,loseAvailability=false,mutateSource=false;
    bool useSecond=false;
    bool separateActual=false,mutateActual=false;
    int expectedAmount=20;
    int allocated{},released{},requests{},sets{};
    Fixture(){
        Put(manager,0,base+0xE827EC0);Put(manager,8,std::uint8_t{1});Put(manager,9,std::uint8_t{1});
        Put(manager,16,static_cast<void*>(player));Put(manager,56,static_cast<void*>(deque));
        Put(player,1464,static_cast<void*>(supplies));Put(supplies,16,int{2});Put(supplies,184,static_cast<void*>(inventory));
        Put(inventory,0,base+0xE6AFE10);Put(inventory,8,std::uint8_t{255});Put(inventory,432,static_cast<void*>(player));
        Put(inventory,408,actualSlots.data());Put(inventory,416,actualSlots.data()+36);
        Put(deque,32,std::size_t{1});Put(component,152,adapters);Put(component,160,adapters+1);
        Put(component,72,static_cast<void*>(sentinel));Put(sentinel,0,static_cast<void*>(sentinel));
        Put(adapter,0,base+0xE827EA8);Put(adapter,8,static_cast<void*>(wrapper));
        Put(wrapper,0,base+0xEA05D70);Put(wrapper,400,int{123});Put(inventory,400,int{123});
        Put(wrapper,416,int{1});Put(wrapper,424,static_cast<void*>(inventory));
        Put(wrapper,496,static_cast<void*>(callbacks[0]));Put(wrapper,504,static_cast<void*>(callbacks[1]));
        Put(wrapper,960,std::uint8_t{1});Put(wrapper,961,std::uint8_t{1});
        Put(callbacks[0],0,base+0xEA05EE0);Put(callbacks[0],8,static_cast<void*>(manager));
        Put(callbacks[1],0,base+0xEA05CF0);Put(callbacks[1],8,static_cast<void*>(player));
        static std::uintptr_t item=0x1234;
        for(auto p:{&source,&destination}) {
            Put(p,8,&item);Put(p,35,std::uint8_t{1});Put(p,64,std::uint8_t{64});Put(p,56,int{123});
            Put(p,144,std::uint8_t{0});
        }
        Put(&source,34,std::uint8_t{20});Put(&destination,34,std::uint8_t{5});
        Put(&source,128,int{501});Put(&destination,128,int{502});
        secondSource=source;Put(&secondSource,34,std::uint8_t{40});Put(&secondSource,128,int{503});
    }
};
Fixture* f;
void* Copy(void* out,void const* in){std::memcpy(out,in,152);f->owned.push_back(out);return out;}
void* Destroy(void* p,int flags){CHECK(flags==0);auto i=std::find(f->owned.begin(),f->owned.end(),p);CHECK(i!=f->owned.end());f->owned.erase(i);return p;}
bool Empty(void const* p){return !Read<std::uint8_t>(p,34);}
std::uint8_t Capacity(void const* p){return Read<std::uint8_t>(p,64);}
bool Equal(void const* a,void const* b){return Read<int>(a,56)==Read<int>(b,56);}
void Count(void* p,std::uint8_t count){CHECK(std::find(f->owned.begin(),f->owned.end(),p)!=f->owned.end());Put(p,34,count);}
void Migrate(void*,void*){} void Damage(void*,void*,std::int16_t){}
void* Context(void* p){CHECK(p==f->manager);return f->component;}
int Size(void const* p){CHECK(p==f->wrapper);return 36;}
void const* Get(void const* p,int slot){CHECK(p==f->wrapper);CHECK(slot==9 || slot==2 || (slot==10 && f->useSecond));return slot==9?&f->source:slot==10?&f->secondSource:&f->destination;}
void const* ActualItem(void const* p,int slot){
    CHECK(p==f->inventory && slot>=0 && slot<36);
    return f->separateActual?&f->actualSlots[slot]:Get(f->wrapper,slot);
}
bool CanBegin(void* p){CHECK(p==f->manager);return f->available;}
void Begin(void* p,bool back){CHECK(p==f->manager && !back);++f->requests;f->order.push_back(2);Put(p,96,static_cast<void*>(f->request));std::memset(f->request,0,72);Put(f->request,0,base+0xE827E10);Put(f->request,8,-1-2*f->requests);}
void** Place(void** out,std::uint8_t const* amount,void const* source,void const* destination){
    CHECK(*amount==f->expectedAmount);++f->allocated;f->order.push_back(1);
    std::memcpy(f->sourceInfo.data(),source,40);std::memcpy(f->destinationInfo.data(),destination,40);
    if(f->loseAvailability) f->available=false;
    if(f->mutateSource) Put(&f->source,34,std::uint8_t{19});
    if(f->mutateActual) Put(&f->actualSlots[9],34,std::uint8_t{19});
    Put(f->action,0,base+0xEA05C60);Put(f->action,8,std::uint8_t{1});*out=f->action;return out;
}
void Add(void* p,void** a){CHECK(p==f->manager && *a==f->action);f->order.push_back(3);
    if(f->refuse){++f->released;return;} // native rejection destroys but leaves dangling input
    *a=nullptr;Put(f->request,48,f->actions);Put(f->request,56,f->actions+1);Put(f->request,64,f->actions+1);
}
void Set(void* p,int slot,void const* stack){CHECK(p==f->wrapper);CHECK(slot==9 || slot==2 || (slot==10 && f->useSecond));
    // Production must copy/adjust owned clones, never edit the borrowed source.
    CHECK(std::find(f->owned.begin(),f->owned.end(),const_cast<void*>(stack))!=f->owned.end());
    ++f->sets;f->order.push_back(slot==2?5:4);auto out=slot==9?&f->source:slot==10?&f->secondSource:&f->destination;
    auto id=Read<int>(f->request,8);
    std::memcpy(out,stack,152);Put(out,128,base+0xE827E10);Put(out,136,id);Put(out,144,std::uint8_t{1});
    if(f->registerChanges){Put(f->sentinel,0,static_cast<void*>(f->node));Put(f->node,0,static_cast<void*>(f->sentinel));
        Put(f->component,80,std::size_t{1});Put(f->node,24,id);Put(f->node,32,f->tracked);Put(f->node,40,f->tracked+1);}
}
void End(void* p){CHECK(p==f->manager);f->order.push_back(6);Put(p,96,std::uintptr_t{});
    if(Read<void*>(f->request,48)!=Read<void*>(f->request,56)){Put(p,104,static_cast<void*>(f->queue));Put(f->queue,0,f->queued);Put(f->queue,8,f->queued+1);}
}
void* DestroyPlace(void* p,int flags){CHECK(p==f->action && flags==1);++f->released;return p;}
NativeApi Api();
std::array<std::byte,48> Reply(){std::array<std::byte,48> v{};Put(v.data(),16,int{-3});return v;}
int main(){try{
    Fixture fixture;f=&fixture;ResponseTracker tracker;auto api=Api();
    auto source=f->source,destination=f->destination;
    auto run=[&]{return ExecuteTransfer(api,base,f->manager,f->player,{9,2,20,17},&source,&destination,tracker);};
    CHECK(run()==TransferResult::Queued);
    CHECK(Read<std::uint8_t>(&f->source,34)==0 && Read<std::uint8_t>(&f->destination,34)==25);
    CHECK(f->requests==1 && f->sets==2 && f->allocated==1 && f->released==0 && f->owned.empty());
    CHECK(f->order==std::vector<int>({1,2,3,4,5,6}));
    CHECK(Read<std::uint8_t>(f->sourceInfo.data(),0)==29 && Read<std::uint8_t>(f->sourceInfo.data(),12)==9);
    CHECK(Read<std::uint8_t>(f->destinationInfo.data(),0)==28 && Read<std::uint8_t>(f->destinationInfo.data(),12)==2);
    CHECK(Read<int>(f->sourceInfo.data(),16)==501 && Read<int>(f->destinationInfo.data(),16)==502);
    CHECK(Read<std::uint8_t>(f->sourceInfo.data(),32)==0 && Read<std::uint8_t>(f->destinationInfo.data(),32)==0);
    CHECK(source.bytes!=f->source.bytes && Read<std::uint8_t>(&source,34)==20); // original IDs/counts not modified
    auto done=tracker.Decode(reinterpret_cast<std::uintptr_t>(f->manager),Reply());CHECK(done && done->requestId==17);
    // Every fixture below runs the real orchestrator. Native functions are the
    // unavoidable external boundary; bad ordering/ownership alters real results.
    {
        Fixture x;f=&x;ResponseTracker t;x.refuse=true;auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,18},&s,&d,t)==TransferResult::Rejected);
        CHECK(x.released==1 && x.sets==0 && x.owned.empty() && !t.Pending());
        CHECK(x.source.bytes==s.bytes && x.destination.bytes==d.bytes);
        CHECK(x.order==std::vector<int>({1,2,3,6}));
    }
    {
        Fixture x;f=&x;ResponseTracker t;x.loseAvailability=true;auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,19},&s,&d,t)==TransferResult::Deferred);
        CHECK(x.released==1 && x.requests==0 && x.sets==0 && x.owned.empty() && !t.Pending());
    }
    {
        Fixture x;f=&x;ResponseTracker t;x.mutateSource=true;auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,19},&s,&d,t)==TransferResult::Rejected);
        CHECK(x.released==1 && x.requests==0 && x.sets==0 && x.owned.empty());
    }
    {
        Fixture x;f=&x;ResponseTracker t;x.registerChanges=false;auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,20},&s,&d,t)==TransferResult::Fault);
        CHECK(x.sets==2 && x.order.back()==6 && t.Pending()); // queued for native reconciliation, never retry
    }
    for(auto transfer:std::vector<Transfer>{{8,2,20,21},{36,2,20,21},{9,9,20,21},{9,-1,20,21},{9,2,0,21},{9,2,65,21},{9,2,21,21},{9,2,20,0}}){
        Fixture x;f=&x;ResponseTracker t;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,transfer,&x.source,&x.destination,t)==TransferResult::Rejected);
        CHECK(x.requests==0 && x.allocated==0 && x.sets==0);
    }
    {
        Fixture x;f=&x;ResponseTracker t;auto s=x.source,d=x.destination;
        Put(&s,128,int{999}); // original network ID must still match, even with same item/count
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,22},&s,&d,t)==TransferResult::Rejected);
        CHECK(x.requests==0 && x.allocated==0);
    }
    for(int variant:{1,2}) {
        Fixture x;f=&x;ResponseTracker t;
        Put(&x.source,144,static_cast<std::uint8_t>(variant));Put(&x.source,128,base+(variant==1?0xE827E10:0xE827E30));Put(&x.source,136,int{-101});
        auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,23},&s,&d,t)==TransferResult::Queued);
        CHECK(Read<std::uint8_t>(x.sourceInfo.data(),32)==variant && Read<int>(x.sourceInfo.data(),24)==-101);
        CHECK(x.owned.empty());
    }
    {
        Fixture x;f=&x;ResponseTracker t;CHECK(t.Begin(reinterpret_cast<std::uintptr_t>(x.manager),-5,24));
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,25},&x.source,&x.destination,t)==TransferResult::Deferred);
        CHECK(x.requests==0 && x.allocated==0);
    }
    {
        Fixture x;f=&x;ResponseTracker t;Put(&x.destination,34,std::uint8_t{});Put(&x.destination,144,std::uint8_t{255});
        auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,26},&s,&d,t)==TransferResult::Queued);
        CHECK(Read<std::uint8_t>(x.destinationInfo.data(),32)==255 && Read<std::uint8_t>(&x.destination,34)==20);
    }
    // A cached prediction must never authorize a transfer while the real
    // inventory differs. Counts, full metadata, capacity AND original net IDs
    // are independent preflight inputs; removing any check breaks a case.
    for(int mismatch=0;mismatch<5;++mismatch){
        Fixture x;f=&x;ResponseTracker t;x.separateActual=true;
        x.actualSlots[9]=x.source;x.actualSlots[2]=x.destination;
        if(mismatch==0)Put(&x.actualSlots[9],34,std::uint8_t{19});
        if(mismatch==1)Put(&x.actualSlots[2],34,std::uint8_t{4});
        if(mismatch==2)Put(&x.actualSlots[9],56,int{124});
        if(mismatch==3)Put(&x.actualSlots[9],64,std::uint8_t{32});
        if(mismatch==4)Put(&x.actualSlots[2],128,int{999});
        auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,27},&s,&d,t)==TransferResult::Deferred);
        CHECK(x.requests==0 && x.allocated==0 && x.sets==0 && !t.Pending());
        CHECK(x.source.bytes==s.bytes && x.destination.bytes==d.bytes);
    }
    {
        Fixture x;f=&x;ResponseTracker t;x.separateActual=true;x.mutateActual=true;
        x.actualSlots[9]=x.source;x.actualSlots[2]=x.destination;auto s=x.source,d=x.destination;
        CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,28},&s,&d,t)==TransferResult::Deferred);
        CHECK(x.requests==0 && x.allocated==1 && x.released==1 && x.sets==0 && x.owned.empty() && !t.Pending());
    }
    for(int unavailable=0;unavailable<5;++unavailable){
        Fixture x;f=&x;ResponseTracker t;auto s=x.source,d=x.destination;auto incomplete=api;
        if(unavailable==0)incomplete.legacy.inventoryItem=nullptr;
        if(unavailable==1)Put(x.inventory,0,base+0xE6AFE18);
        if(unavailable==2)Put(x.inventory,416,x.actualSlots.data()+35);
        if(unavailable==3)Put(x.manager,104,static_cast<void*>(x.queue));
        if(unavailable==4)Put(x.manager,80,int{-4});
        CHECK(ExecuteTransfer(incomplete,base,x.manager,x.player,{9,2,20,29},&s,&d,t)==TransferResult::Deferred);
        CHECK(x.requests==0 && x.allocated==0 && x.sets==0 && !t.Pending());
    }
    std::cout<<"PASS: native transaction orchestration, original IDs, owned-only counts, prediction registration and response arming\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
NativeApi Api(){
    NativeApi api{{Copy,Destroy,Empty,Capacity,Equal,Equal,Count,Migrate,Damage},Context,Size,Get,Set,CanBegin,Begin,Place,Add,End,DestroyPlace};
    api.legacy.inventoryItem=ActualItem;api.profile=FindNativeProfile(GameVersion::V52);return api;
}
