#include "OrdinaryFixture.h"
int main(){try{
    // Break caught: substituting modern/scope/wrapper handling, skipping native
    // setters, or duplicating the balanced pair cannot produce this outcome.
    {
        OrdinaryFixture x;PrepareOrdinary(x);auto s=x.actualSlots[9],d=x.actualSlots[2];
        CHECK(ExecuteOrdinaryTransfer(OrdinaryApi(),base,x.manager,x.player,{9,2,20,1},&s,&d)==TransferResult::Queued);
        CHECK(x.sends==1 && x.setterCalls==2 && !x.balance && x.owned.empty());
        CHECK(Read<std::uint8_t>(&x.actualSlots[9],34)==0 && Read<std::uint8_t>(&x.actualSlots[2],34)==25);
        CHECK(Read<int>(x.manager,80)==0 && Read<std::uint8_t>(&s,34)==20 && Read<std::uint8_t>(&d,34)==5);
        CHECK(x.requests==0 && x.allocated==0); // no modern request/action
    }
    for(int issue=0;issue<8;++issue){
        OrdinaryFixture x;PrepareOrdinary(x);auto s=x.actualSlots[9],d=x.actualSlots[2];
        if(issue==0)Put(x.manager,8,std::uint8_t{1});
        if(issue==1)Put(x.manager,8,std::uint8_t{2});
        if(issue==2)Put(x.manager,9,std::uint8_t{});
        if(issue==3)Put(x.player,2680,static_cast<void*>(x.nativeMap.data()));
        if(issue==4)Put(x.inventory,432,std::uintptr_t{});
        if(issue==5)Put(x.manager,80,int{-4});
        if(issue==6)x.playerTable[1808/8]=0;
        if(issue==7)Put(x.player,2744,std::uintptr_t{});
        CHECK(ExecuteOrdinaryTransfer(OrdinaryApi(),base,x.manager,x.player,{9,2,20,1},&s,&d)!=TransferResult::Queued);
        CHECK(x.sends==0 && x.setterCalls==0 && x.owned.empty());
    }
    for(auto transfer:{Transfer{8,2,20,1},Transfer{36,2,20,1},Transfer{9,9,20,1},
        Transfer{9,2,0,1},Transfer{9,2,21,1},Transfer{9,2,20,0}}){
        OrdinaryFixture x;PrepareOrdinary(x);auto s=x.actualSlots[9],d=x.actualSlots[2];
        CHECK(ExecuteOrdinaryTransfer(OrdinaryApi(),base,x.manager,x.player,transfer,&s,&d)==TransferResult::Rejected);
        CHECK(!x.sends && !x.setterCalls && x.owned.empty());
    }
    for(int issue=0;issue<4;++issue){
        OrdinaryFixture x;PrepareOrdinary(x);auto s=x.actualSlots[9],d=x.actualSlots[2];auto api=OrdinaryApi();
        if(issue==0)Put(&s,34,std::uint8_t{19});
        if(issue==1)Put(x.supplies,16,int{3});
        if(issue==2)api.stack.copy=OrdinaryCopyBusy;
        if(issue==3)api.stack.copy=OrdinaryCopyStale;
        CHECK(ExecuteOrdinaryTransfer(api,base,x.manager,x.player,{9,2,20,1},&s,&d)!=TransferResult::Queued);
        CHECK(!x.sends && !x.setterCalls && x.owned.empty());
    }
    {
        OrdinaryFixture x;PrepareOrdinary(x);x.keepPending=true;auto s=x.actualSlots[9],d=x.actualSlots[2];
        CHECK(ExecuteOrdinaryTransfer(OrdinaryApi(),base,x.manager,x.player,{9,2,20,1},&s,&d)==TransferResult::Fault);
        CHECK(!x.sends && x.setterCalls==2 && Read<void*>(x.player,2680)==x.nativeMap.data());
        CHECK(x.owned.empty()); // no forced send, queue clearing or rollback
    }
    {
        OrdinaryFixture x;x.useSecond=true;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued && x.sends==1);
        for(auto now:{101,500,1599})CHECK(!r.Tick(x.manager,true,true,Config{},now,1) && x.sends==1);
        CHECK(r.Tick(x.manager,true,true,Config{},1600,1)==TransferResult::Queued && x.sends==2);
        CHECK(Read<std::uint8_t>(&x.actualSlots[10],34)==1 && Read<std::uint8_t>(&x.actualSlots[2],34)==64);
        CHECK(!r.Tick(x.manager,true,true,Config{},3100,1) && x.sends==2);
    }
    for(int when:{101,1601}){
        OrdinaryFixture x;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued);
        if(when==1601)CHECK(!r.Tick(x.manager,true,true,Config{},1600,1)); // latest watch survives settlement
        x.actualSlots[9]=x.source;x.actualSlots[2]=x.destination; // correction back to server state
        CHECK(!r.Tick(x.manager,true,true,Config{},when,1) && r.OrdinaryBlocked());
        r.Reset(ResetCause::Explicit,when+1);
        CHECK(!r.SelectBackend(Backend::RemoteOrdinary,10,when+1499));
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,when+1500));
        CHECK(!r.Tick(x.manager,true,true,Config{},when+1500,2) && x.sends==1);
        CHECK(r.Tick(x.manager,true,true,Config{},when+1550,2)==TransferResult::Queued && x.sends==2);
    }
    for(int reason=0;reason<5;++reason){
        OrdinaryFixture x;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued);
        Config config;if(reason==0)config.enabled=false;
        if(reason==3)hud=false;if(reason==4)Put(x.supplies,16,int{3});
        CHECK(!r.Tick(x.manager,reason!=1,reason!=2,config,101,1));
        hud=true;Put(x.supplies,16,int{2});
        CHECK(!r.SelectBackend(Backend::RemoteOrdinary,10,1600)); // quarantine cannot shorten the old fence
        CHECK(!r.SelectBackend(Backend::RemoteOrdinary,11,1599)); // reconnect cannot shorten original fence
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,11,1600));
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,2) && x.sends==1);
    }
    {
        OrdinaryFixture x;x.useSecond=true;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued);
        void* player=x.player;alignas(8) std::uintptr_t operation[]={base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        for(int n=0;n<3;++n){
            auto use=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(use && use->requestId==1);
            Put(&x.actualSlots[2],34,static_cast<std::uint8_t>(24-n));r.EndUse(*use,true,true,Config{},1);
            CHECK(!r.Tick(x.manager,true,true,Config{},150+n,1) && !r.OrdinaryBlocked());
        }
        CHECK(r.Tick(x.manager,true,true,Config{},1600,1)==TransferResult::Queued && x.sends==2);
        CHECK(Read<std::uint8_t>(&x.actualSlots[10],34)==0 && Read<std::uint8_t>(&x.actualSlots[2],34)==62);
        int settled=0;while(auto e=r.TakeDiagnostic()){
            CHECK(e->ordinary && e->nativeId==0 && e->nativeStatus==-1);
            CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled && e->kind!=DiagnosticKind::NativeResult);
            if(e->kind==DiagnosticKind::PredictionSettled){++settled;CHECK(e->provenConsumption==3 && e->accountedProgress);}
        }CHECK(settled==1);
        auto reply=Reply();CHECK(!r.Decode(x.manager,reply));
        r.Publish({2,Completion::Accepted,0,0});CHECK(!r.TakeDiagnostic());
    }
    for(int mismatch=0;mismatch<4;++mismatch){
        OrdinaryFixture x;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        if(mismatch==0)Put(&x.actualSlots[2],34,std::uint8_t{24}); // unobserved consumption
        if(mismatch==1)Put(&x.actualSlots[9],34,std::uint8_t{1});
        if(mismatch==2)Put(&x.actualSlots[2],60,int{99}); // full metadata change
        CHECK(!r.Tick(x.manager,true,true,Config{},mismatch==3?99:101,1));
        CHECK(r.OrdinaryBlocked() && !r.Faulted() && x.sends==1);
    }
    {
        OrdinaryFixture x;x.useSecond=true;PrepareOrdinary(x);auto api=OrdinaryApi();api.stack.copy=OrdinaryCopyTransient;
        Runtime r(api,OrdinaryWorld(),base);CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued);
        ordinaryCopies=0;ordinaryBusyAt=29; //28 sample copies, first transaction clone becomes busy
        CHECK(r.Tick(x.manager,true,true,Config{},1600,1)==TransferResult::Deferred && x.sends==1);
        Put(x.player,2680,std::uintptr_t{});ordinaryBusyAt=0;
        CHECK(r.Tick(x.manager,true,true,Config{},1601,1)==TransferResult::Queued && x.sends==2 && !r.OrdinaryBlocked());
    }
    for(bool tool:{false,true}){
        OrdinaryFixture x;PrepareOrdinary(x);
        Put(&x.actualSlots[2],34,std::uint8_t{1});
        if(tool){
            Put(&x.actualSlots[2],64,std::uint8_t{1});Put(&x.actualSlots[2],60,int{90});
            Put(&x.actualSlots[9],64,std::uint8_t{1});Put(&x.actualSlots[9],34,std::uint8_t{1});
            Put(&x.actualSlots[9],60,int{5});
        }
        Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Config c;c.preRestockThreshold=1;
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));CHECK(!r.Tick(x.manager,true,true,c,100,1));
        void* player=x.player;std::uintptr_t operation[]={base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        auto use=r.BeginUse(x.inventory,operation,true,true,c,1);CHECK(use);
        Put(&x.actualSlots[2],34,std::uint8_t{});r.EndUse(*use,true,true,c,1);
        CHECK(r.Tick(x.manager,true,true,c,101,1)==TransferResult::Queued && x.sends==1);
        CHECK(Read<std::uint8_t>(&x.actualSlots[2],34)==(tool?1:20));
        if(tool)CHECK(Read<int>(&x.actualSlots[2],60)==5); // never normalize borrowed durability
    }
    {
        OrdinaryFixture x;PrepareOrdinary(x);Put(&x.actualSlots[2],34,std::uint8_t{});
        Runtime r(OrdinaryApi(),OrdinaryWorld(),base);CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1) && !x.sends); // manual empty hand is not native-use proof
    }
    {
        OrdinaryFixture x;PrepareOrdinary(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        CHECK(!r.SelectBackend(Backend::RemoteModern,19,99)); // no early release on reversed clock
        CHECK(!r.SelectBackend(Backend::RemoteModern,20,101));CHECK(!r.SelectBackend(Backend::Unsupported,21,1500));
        CHECK(!r.SelectBackend(Backend::RemoteModern,22,1599));CHECK(r.SelectBackend(Backend::RemoteModern,22,1600));
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,2000)); // local replan allowed, no stale native transaction
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,23,2001));
    }
    std::cout<<"PASS ordinary actual pair, owned clones, no scopes/modern calls, native preflight,1500ms prediction fence, callback consumption, bounded quarantine and same-generation fresh replan\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
