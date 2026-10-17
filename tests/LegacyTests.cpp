#define main OldTransactionCases
#include "TransactionTests.cpp"
#undef main
#include "CompatibilityHookFixtures.h"
constexpr ActorIdentity localId{0x100,42,77,ActorRole::Local},serverId{0x200,42,77,ActorRole::Server};
struct LegacyFixture {
    Fixture native;
    IntegratedBridge bridge;
    std::vector<int> steps;
    StackStorage actualSource{},actualDestination{};
    alignas(8) std::byte slotHead[24]{},slotNodes[2][24]{},legacyHead[88]{},legacyNode[88]{};
    alignas(8) std::byte nativeMap[88]{},dedup[512]{};
    std::array<std::uintptr_t,229> playerTable{};
    bool registerSlots=true;
    int slotCount{};
    int sends{},expectedTotal=25;
    int automaticSends{},nativeBalance{},nativeSlots{};
    bool keepPending{};
};
LegacyFixture* lf;
void Scope(void* manager,void* scope){
    CHECK(manager==f->manager && Read<int>(manager,80)==0);lf->steps.push_back(3);
    Put(scope,0,base+FixtureRva(0xE828410));Put(scope,8,manager);Put(scope,16,static_cast<void*>(f->component));
    Put(scope,56,scope);Put(scope,64,std::uint8_t{1});Put(manager,80,int{-4});
    Put(f->component,8,static_cast<void*>(lf->legacyHead));Put(f->component,16,std::size_t{});
    Put(lf->legacyHead,0,static_cast<void*>(lf->legacyHead));
}
void EndScope(void* p){
    CHECK(Read<void*>(p,8)==f->manager);CHECK(Read<int>(f->manager,80)==-4);
    Put(f->manager,80,int{});Put(f->component,16,std::size_t{});
    Put(lf->legacyHead,0,static_cast<void*>(lf->legacyHead));lf->steps.push_back(7);
}
void LegacySet(void* p,int slot,void const* stack){
    CHECK(p==f->wrapper && (slot==9 || slot==2));CHECK(lf->bridge.Pending());
    CHECK(std::find(f->owned.begin(),f->owned.end(),const_cast<void*>(stack))!=f->owned.end());
    auto out=slot==9?&f->source:&f->destination;
    // 7C9ACD0 -> manager vslot16 (25AF540) uses manager+96, NOT
    // manager+80. A legacy scope alone therefore produces modern ID0.
    auto request=Read<void*>(f->manager,96);
    auto modernId=request?Read<int>(request,8):0;
    std::memcpy(out,stack,152);Put(out,128,base+FixtureRva(0xE827E10));Put(out,136,modernId);Put(out,144,std::uint8_t{1});
    ++f->sets;lf->steps.push_back(slot==9?4:5);
    if(lf->registerSlots){Put(f->sentinel,0,static_cast<void*>(f->node));Put(f->node,0,static_cast<void*>(f->sentinel));
        Put(f->component,80,std::size_t{1});Put(f->node,24,modernId);Put(f->node,32,f->tracked);Put(f->node,40,f->tracked+1);}
}
void const* ActualGet(void const* p,int slot){
    CHECK(p==f->inventory && (slot==9 || slot==2));
    return slot==9?&lf->actualSource:&lf->actualDestination;
}
void ActualSet(void* p,int slot,void const* stack,bool network){
    CHECK(p==f->inventory && !network && lf->bridge.Pending());
    CHECK(Read<int>(f->manager,80)==-4 && !Read<void*>(f->manager,96));
    CHECK(std::find(f->owned.begin(),f->owned.end(),const_cast<void*>(stack))!=f->owned.end());
    auto out=slot==9?&lf->actualSource:&lf->actualDestination;
    auto before=*out,after=*static_cast<StackStorage const*>(stack);
    // 1D5170 -> 25ABB00 -> 25A4880: native legacy kind2, native first-map
    // container29 slot set. Empty stacks do not enter 25ABB00 bookkeeping.
    if(!Empty(stack)){
        Put(&after,128,base+FixtureRva(0xE827E30));Put(&after,136,int{-4});Put(&after,144,std::uint8_t{2});
        if(lf->registerSlots){
            Put(f->component,16,std::size_t{1});Put(lf->legacyHead,0,static_cast<void*>(lf->legacyNode));
            Put(lf->legacyNode,0,static_cast<void*>(lf->legacyHead));Put(lf->legacyNode,16,std::uint8_t{29});
            Put(lf->legacyNode,32,static_cast<void*>(lf->slotHead));
            auto node=lf->slotNodes[lf->slotCount++];Put(node,16,static_cast<std::uint8_t>(slot));
            Put(node,0,static_cast<void*>(lf->slotHead));
            if(lf->slotCount==1)Put(lf->slotHead,0,static_cast<void*>(node));
            else Put(lf->slotNodes[0],0,static_cast<void*>(node));
            Put(lf->legacyNode,40,static_cast<std::size_t>(lf->slotCount));
        }
    }
    // 25A4880 -> 20BA30 -> 266FD10 records BEFORE the actual slot copy.
    // With network=false, first action stays pending; second balanced action
    // automatically sends once through player vslot1808 and consumes the map.
    Put(f->player,2680,static_cast<void*>(lf->nativeMap));
    lf->nativeBalance+=int(Read<std::uint8_t>(&after,34))-Read<std::uint8_t>(&before,34);
    lf->nativeSlots|=slot==9?1:2;lf->steps.push_back(10);
    if(!lf->nativeBalance && !lf->keepPending){
        CHECK(lf->nativeSlots==3 && Read<int>(f->manager,80)==-4 && lf->bridge.Pending());
        CHECK(f->sets==1); // send precedes second actual slot copy, not follows it
        CHECK(Read<std::uint8_t>(&lf->actualSource,34)+Read<std::uint8_t>(&after,34)==lf->expectedTotal);
        ++lf->sends;++lf->automaticSends;lf->steps.push_back(6);
        Put(f->player,2680,static_cast<void*>(nullptr));
    }
    *out=after;
    // Original container notification makes a non-predicted wrapper read
    // the real underlying inventory; no modern wrapper setter is called.
    std::memcpy(slot==9?&f->source:&f->destination,out,152);
    ++f->sets;lf->steps.push_back(slot==9?4:5);
}
void* CopyWhileNativeBecomesBusy(void* out,void const* in){
    Copy(out,in);Put(f->player,2680,static_cast<void*>(lf->nativeMap));return out;
}
void* CopyWhileServerCountsChange(void* out,void const* in){
    Copy(out,in);InventoryCounts c{};c[9]=20;c[2]=6;lf->bridge.EndServer(serverId,{},1,102,c);return out;
}
NativeApi LegacyNative(){auto api=Api();api.profile=FindNativeProfile(fixtureVersion==45?GameVersion::V45:fixtureVersion==51?GameVersion::V51:GameVersion::V52);api.setItem=LegacySet;api.legacy={Scope,EndScope,ActualGet,ActualSet};return api;}
InventoryCounts FixtureCounts(){
    InventoryCounts counts{};counts[9]=Read<std::uint8_t>(&f->source,34);counts[2]=Read<std::uint8_t>(&f->destination,34);return counts;
}
void PrepareLegacy(LegacyFixture& x,bool pair=true){
    lf=&x;f=&x.native;
    Put(f->manager,0,base+FixtureRva(0xE827EC0));Put(f->adapter,0,base+FixtureRva(0xE827EA8));
    Put(f->wrapper,0,base+FixtureRva(0xEA05D70));Put(f->callbacks[0],0,base+FixtureRva(0xEA05EE0));
    Put(f->callbacks[1],0,base+FixtureRva(0xEA05CF0));x.actualSource=f->source;x.actualDestination=f->destination;
    Put(f->inventory,0,base+FixtureRva(0xE6AFE10));Put(f->inventory,8,std::uint8_t{255});
    Put(f->inventory,408,static_cast<void*>(&x.actualSource));
    Put(f->inventory,416,reinterpret_cast<std::uintptr_t>(&x.actualSource)+36*152);
    Put(f->inventory,432,static_cast<void*>(f->player));
    x.playerTable[1808/8]=base+FixtureRva(0x476A510);x.playerTable[1816/8]=base+FixtureRva(0x476AA30);x.playerTable[1824/8]=base+FixtureRva(0x476ABD0);
    Put(f->player,0,x.playerTable.data());Put(f->player,2672,static_cast<void*>(f->player));
    x.bridge.Local(localId,100);if(pair)x.bridge.EndServer(serverId,{},1,101,FixtureCounts());
}
#ifndef AUTORESTOCK_LEGACY_FIXTURE_ONLY
int main(int argc,char** argv){try{
    fixtureVersion=argc>1?std::stoi(argv[1]):52;
    CHECK(fixtureVersion==45 || fixtureVersion==51 || fixtureVersion==52);
    for(int mismatch:{0,1,2}){
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        InventoryCounts counts{};counts[9]=20;counts[2]=5;
        if(mismatch==0)x.bridge.EndServer(serverId,{},1,101); // paired but no valid inventory read
        else{if(mismatch==1)counts[2]=6;else counts[9]=21;x.bridge.EndServer(serverId,{},1,101,counts);}
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(x.sends==0 && f->sets==0 && x.steps.empty() && f->owned.empty() && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;auto api=LegacyNative();api.stack.copy=CopyWhileServerCountsChange;
        CHECK(ExecuteIntegratedTransfer(api,base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(x.sends==0 && f->sets==0 && x.steps.empty() && f->owned.empty() && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        auto result=ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102);
        CHECK(result==TransferResult::Queued); // old backend/stub fails: must send ordinary balanced native transaction
        CHECK(x.sends==1 && x.automaticSends==1 && f->owned.empty());
        CHECK(x.steps==std::vector<int>({3,10,4,10,6,5,7}));
        CHECK(Read<std::uint8_t>(&x.actualDestination,144)==2 && Read<int>(&x.actualDestination,136)==-4);
        CHECK(Read<std::size_t>(f->component,80)==0); // must not populate the modern map
        CHECK(!x.bridge.Poll()); // native send and prediction alone never succeed
        auto t=x.bridge.BeginServer(serverId,-4,103);x.bridge.EndServer(serverId,t,1,104);
        auto done=x.bridge.Poll();CHECK(done && done->result==Completion::Accepted);
        CHECK(Read<std::uint8_t>(&s,34)==20 && Read<std::uint8_t>(&d,34)==5);
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        auto api=LegacyNative();api.stack.copy=CopyWhileNativeBecomesBusy;
        CHECK(ExecuteIntegratedTransfer(api,base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending() && f->owned.empty());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        // Empty but allocated native dedup storage is an idle manager too.
        Put(f->player,2688,static_cast<void*>(x.dedup));Put(f->player,2696,static_cast<void*>(x.dedup));
        Put(f->player,2704,static_cast<void*>(x.dedup+512));
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Queued);
        CHECK(x.sends==1 && x.automaticSends==1);
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        Put(f->player,2680,static_cast<void*>(x.nativeMap)); // unrelated transaction must not be merged
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        Put(f->player,2688,static_cast<void*>(x.dedup));Put(f->player,2696,static_cast<void*>(x.dedup+512));
        Put(f->player,2704,static_cast<void*>(x.dedup+512));
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        Put(f->player,2672,static_cast<void*>(nullptr));
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        x.playerTable[1808/8]=0;
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;x.keepPending=true;
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Fault);
        CHECK(x.sends==0 && x.bridge.Pending() && Read<void*>(f->player,2680)==x.nativeMap);
        CHECK(f->owned.empty()); // never clear or force-send a native pending map
    }
    {
        LegacyFixture x;PrepareLegacy(x,false);auto s=f->source,d=f->destination;
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(x.steps.empty() && f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);x.registerSlots=false;auto s=f->source,d=f->destination;
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Fault);
        CHECK(x.sends==1 && x.automaticSends==1 && f->owned.empty()); // leave original auto-send/reconciliation alone
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        Put(&x.actualSource,34,std::uint8_t{19}); // stale prediction cannot overwrite real inventory
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Rejected);
        CHECK(f->sets==0 && x.sends==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,10,1},&s,&d,x.bridge,localId,102)==TransferResult::Queued);
        CHECK(x.slotCount==2 && Read<std::uint8_t>(&x.actualSource,34)==10 && Read<std::uint8_t>(&x.actualDestination,34)==15);
    }
    {
        LegacyFixture x;PrepareLegacy(x);Put(f->inventory,0,base+FixtureRva(0xEA05D70));
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&f->source,&f->destination,x.bridge,localId,102)==TransferResult::Deferred);
        CHECK(x.sends==0 && f->sets==0 && !x.bridge.Pending());
    }
    {
        LegacyFixture x;PrepareLegacy(x);Put(&f->destination,34,std::uint8_t{});Put(&f->destination,144,std::uint8_t{255});
        x.actualDestination=f->destination;x.expectedTotal=20;auto s=f->source,d=f->destination;
        x.bridge.EndServer(serverId,{},1,101,FixtureCounts());
        // Destination is new; native setter assigns kind2 and source empty
        // needs no legacy modified-slot entry of its own.
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Queued);
        CHECK(Read<std::uint8_t>(&x.actualDestination,34)==20 && x.slotCount==1);
    }
    {
        LegacyFixture x;PrepareLegacy(x);auto s=f->source,d=f->destination;Put(&s,34,std::uint8_t{19});
        CHECK(ExecuteIntegratedTransfer(LegacyNative(),base,f->manager,f->player,{9,2,20,1},&s,&d,x.bridge,localId,102)==TransferResult::Rejected);
        CHECK(x.sends==0 && f->sets==0);
    }
    std::cout<<"Legacy transaction tests passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
