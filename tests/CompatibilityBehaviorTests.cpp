#include "OrdinaryFixture.h"
// Literal ABI boundary fixtures derived from the exact IDA review. These do
// not call profile.Rva() to manufacture objects accepted by the implementation.
struct Contract {
    GameVersion version;std::uint32_t manager,inventory,client,adapter,wrapper,requestCallback,playerCallback,request;
    std::uint32_t sendAuto,sendNormal,sendPacket,use,incoming;
};
constexpr Contract contracts[]={
    {GameVersion::V45,0xE8201E0,0xE69DAB0,0xE774DB0,0xE8201C8,0xE8204A0,0xE820610,0xE820420,0xE820130,
        0x2F37D30,0x2F382E0,0x2F38480,0x2D3E830,0x1506C70},
    {GameVersion::V51,0xE827EC0,0xE6AFE10,0xE9731B0,0xE827EA8,0xEA05D70,0xEA05EE0,0xEA05CF0,0xE827E10,
        0x476A440,0x476A960,0x476AB00,0x25B68C0,0x1343420},
    {GameVersion::V52,0xE827EC0,0xE6AFE10,0xE9731B0,0xE827EA8,0xEA05D70,0xEA05EE0,0xEA05CF0,0xE827E10,
        0x476A510,0x476AA30,0x476ABD0,0x25B6660,0x13434A0}
};
Contract const* contract;
void Adapt(Fixture& x){
    Prepare(x);Put(x.manager,0,base+contract->manager);Put(x.inventory,0,base+contract->inventory);
    Put(client,0,base+contract->client);Put(x.adapter,0,base+contract->adapter);Put(x.wrapper,0,base+contract->wrapper);
    Put(x.callbacks[0],0,base+contract->requestCallback);Put(x.callbacks[1],0,base+contract->playerCallback);
}
void CompatBegin(void* manager,bool back){Begin(manager,back);Put(f->request,0,base+contract->request);}
void CompatSet(void* wrapper,int slot,void const* value){
    Set(wrapper,slot,value);Put(slot==9?&f->source:&f->destination,128,base+contract->request);
}
void CompatEnd(void* value){
    if(contract->version==GameVersion::V45){
        CHECK(value!=f->manager && Read<void*>(value,0)==f->manager && Read<std::uint8_t>(value,8)==1);
        End(f->manager);Put(value,0,std::uintptr_t{});Put(value,8,std::uint8_t{});
    }else End(value);
}
NativeApi Native(bool ordinary){
    auto api=ordinary?OrdinaryApi():Api();api.profile=FindNativeProfile(contract->version);
    if(!ordinary){api.begin=CompatBegin;api.setItem=CompatSet;api.end=CompatEnd;}
    return api;
}
WorldApi WorldBinding(){auto binding=OrdinaryWorld();binding.profile=FindNativeProfile(contract->version);return binding;}
void PrepareCompat(OrdinaryFixture& x){
    PrepareOrdinary(x);Adapt(x);
    x.playerTable[1808/8]=base+contract->sendAuto;x.playerTable[1816/8]=base+contract->sendNormal;
    x.playerTable[1824/8]=base+contract->sendPacket;
}
void Use(Runtime& runtime,OrdinaryFixture& x,int after){
    alignas(8) std::byte operation[64]{},implementation[16]{},table[24]{};
    void* player=x.player;
    if(contract->version==GameVersion::V45){
        Put(operation,56,static_cast<void*>(implementation));Put(implementation,0,static_cast<void*>(table));
        Put(implementation,8,player);Put(table,16,base+contract->use);
    }else {Put(operation,0,base+contract->use);Put(operation,8,&player);}
    auto capture=runtime.BeginUse(x.inventory,operation,true,true,Config{true,17},1);CHECK(capture);
    runtime.ObserveToolOperation(x.inventory,operation,true,100);
    Put(&x.actualSlots[2],34,static_cast<std::uint8_t>(after));runtime.EndUse(*capture,true,true,Config{true,17},1);
}
void Incoming(Runtime& runtime,OrdinaryFixture& x,int slot,std::uint64_t now){
    NativeWrite w;w.requestId=runtime.NativeWriterRequest(x.inventory,slot,1,now);CHECK(w.requestId);
    w.slot=slot;w.beginAt=w.endAt=now;w.callerKnown=true;w.callerRva=contract->incoming;
    w.before.readable=w.input.readable=w.after.readable=true;
    w.before.count=w.input.count=w.after.count=Read<std::uint8_t>(&x.actualSlots[slot],34);
    w.before.native=w.input.native=w.after.native={0,1,true};runtime.ObserveNativeWrite(x.inventory,1,w);
}
int main(){try{
    for(auto const& c:contracts){contract=&c;
        { // Exact-ID native working-copy break, applied later; identical
          // ordinary source/destination and original setters under all3 ABIs.
            OrdinaryFixture x;PrepareCompat(x);
            for(auto p:{&x.actualSlots[2],&x.actualSlots[9]}){Put(p,34,std::uint8_t{1});Put(p,64,std::uint8_t{1});}
            Put(&x.actualSlots[2],60,int{31});Put(&x.actualSlots[9],60,int{2});
            auto api=Native(true);api.stack.merge=[](void const*,void const*){return false;};
            Runtime runtime{api,WorldBinding(),base};auto now=GetTickCount64();
            CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,now));
            CHECK(!runtime.Tick(x.manager,true,true,Config{},now,1));
            OwnedStack working;CHECK(working.Copy(api.stack,&x.actualSlots[2]));
            auto capture=runtime.BeginToolHurt(working.Get(),x.player,true,true,Config{},1);CHECK(capture);
            Put(const_cast<void*>(working.Get()),34,std::uint8_t{});
            CHECK(runtime.EndToolHurt(*capture,working.Get(),true,true,Config{},1));
            CHECK(!runtime.Tick(x.manager,true,true,Config{},now+1,1) && x.sends==0);
            Put(&x.actualSlots[2],34,std::uint8_t{});
            CHECK(runtime.Tick(x.manager,true,true,Config{},now+2,1)==TransferResult::Queued);
            CHECK(x.sends==1 && x.requests==0 && Read<int>(&x.actualSlots[2],60)==2);
            runtime.Reset();
        }
        { // Native world-destroy capture must not depend on either operation ABI.
            OrdinaryFixture x;PrepareCompat(x);
            Put(&x.actualSlots[2],34,std::uint8_t{1});Put(&x.actualSlots[2],64,std::uint8_t{1});
            Runtime runtime{Native(true),WorldBinding(),base};CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,100));
            CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
            alignas(8) std::byte gameMode[16]{};Put(gameMode,8,static_cast<void*>(x.player));
            auto capture=runtime.BeginDestroy(gameMode,true,true,Config{},1);CHECK(capture);
            Put(&x.actualSlots[2],34,std::uint8_t{});runtime.EndUse(*capture,true,true,Config{},1);
            bool depleted=false;
            while(auto e=runtime.TakeDiagnostic())if(e->kind==DiagnosticKind::ToolTrace && e->tool.stage==ToolStage::Use){
                CHECK(e->tool.invoker==9 && e->tool.depletedByUse && e->tool.depletions==1);depleted=true;
            }
            CHECK(depleted && x.sends==0 && x.requests==0);
        }
        { // Both the45 owning-function and51/52 function-ref diagnostic reads.
            OrdinaryFixture x;PrepareCompat(x);
            Put(&x.actualSlots[2],34,std::uint8_t{1});Put(&x.actualSlots[2],64,std::uint8_t{1});
            Runtime runtime{Native(true),WorldBinding(),base};CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,100));
            CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
            Use(runtime,x,0);bool operation=false,depletion=false;
            while(auto e=runtime.TakeDiagnostic())if(e->kind==DiagnosticKind::ToolTrace){
                if(e->tool.stage==ToolStage::Operation){
                    CHECK(e->tool.operationRva==c.use && e->tool.captured && e->tool.operations==1);operation=true;
                }
                if(e->tool.stage==ToolStage::Use){CHECK(e->tool.depletedByUse && e->tool.depletions==1);depletion=true;}
            }
            CHECK(operation && depletion && x.sends==0 && x.requests==0);
        }
        {
            Fixture x;Adapt(x);auto api=Native(false);auto source=x.source,destination=x.destination;ResponseTracker tracker;
            CHECK(ExecuteTransfer(api,base,x.manager,x.player,{9,2,20,17},&source,&destination,tracker)==TransferResult::Queued);
            CHECK(x.order==std::vector<int>({1,2,3,4,5,6}) && x.requests==1 && x.sets==2 && x.owned.empty());
            CHECK(Read<std::uint8_t>(&x.source,34)==0 && Read<std::uint8_t>(&x.destination,34)==25);
            CHECK(tracker.Pending() && !Read<void*>(x.manager,96) && Read<void*>(x.manager,104)==x.queue);
        }
        {
            OrdinaryFixture x;PrepareCompat(x);Put(&x.actualSlots[9],34,std::uint8_t{5});
            x.actualSlots[10]=x.source;Put(&x.actualSlots[10],34,std::uint8_t{20});
            Runtime runtime{Native(true),WorldBinding(),base};CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,100));
            CHECK(runtime.Tick(x.manager,true,true,Config{true,17},100,1)==TransferResult::Queued && x.sends==1);
            Use(runtime,x,0);Incoming(runtime,x,9,209);Incoming(runtime,x,2,210);CHECK(runtime.OrdinaryPaused());
            CHECK(!runtime.Tick(x.manager,true,true,Config{true,17},1599,1));
            CHECK(!runtime.Tick(x.manager,true,true,Config{true,17},1600,1));
            CHECK(runtime.Tick(x.manager,true,true,Config{true,17},1650,1)==TransferResult::Queued && x.sends==2);
            CHECK(Read<std::uint8_t>(&x.actualSlots[2],34)==20 && !Read<std::uint8_t>(&x.actualSlots[10],34));
            runtime.Reset();CHECK(x.owned.empty() && !x.requests && !x.allocated);
        }
        {
            OrdinaryFixture x;PrepareCompat(x);
            for(int slot=9;slot<36;++slot){x.actualSlots[slot]=x.source;Put(&x.actualSlots[slot],34,std::uint8_t{4});}
            Runtime runtime{Native(true),WorldBinding(),base};CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,100));
            int used=0;std::uint64_t now=100;
            for(int step=0;step<10000 && used<113;++step){
                auto result=runtime.Tick(x.manager,true,true,Config{true,17},now,1);if(result)CHECK(*result==TransferResult::Queued);
                int count=Read<std::uint8_t>(&x.actualSlots[2],34);
                if(count){Use(runtime,x,count-1);++used;}
                int total=Read<std::uint8_t>(&x.actualSlots[2],34);
                for(int slot=9;slot<36;++slot)total+=Read<std::uint8_t>(&x.actualSlots[slot],34);
                CHECK(total+used==113 && !runtime.Faulted() && !runtime.OrdinaryBlocked());
                while(runtime.TakeDiagnostic()){}now+=50;
            }
            CHECK(used==113 && x.sends>0 && x.setterCalls==2*x.sends && !x.requests && !x.allocated);
            runtime.Reset();CHECK(x.owned.empty());
        }
    }
    std::cout<<"PASS: all3 profiles modern scope ABI, ordinary 27-source depletion, live-use closure, exact incoming recovery and count conservation (offline only)\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
