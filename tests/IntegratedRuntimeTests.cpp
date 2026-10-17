#define AUTORESTOCK_LEGACY_FIXTURE_ONLY
#include "LegacyTests.cpp"
#include "ActorFixture.h"
#include "AutoRestock/Runtime.h"
alignas(8) std::byte client[8]{},world[8]{},scene[8]{};
StackStorage emptyStack{};
bool hud=true;
bool realLag{};StackStorage beforeSource{},beforeDestination{};
void* LocalPlayer(void*){return f->player;}void* World(void*){return world;}
bool Input(void*){return true;}void* Top(void*){return scene;}
bool Name(void*,NativeStringView const*){return hud;}bool Damageable(void const*){return false;}
void const* All(void const*,int n){return n==9?&f->source:n==2?&f->destination:&emptyStack;}
void const* Real(void*,int n){return realLag?(n==9?&beforeSource:n==2?&beforeDestination:&emptyStack):All(nullptr,n);}
Runtime NewRuntime(LegacyFixture& x,ActorFixture& actor){
    PrepareLegacy(x,false);actor.Attach(f->player);Put(client,0,base+0xE9731B0);
    Put(f->player,3440,static_cast<void*>(client));Put(f->player,2744,static_cast<void*>(f->manager));
    auto api=LegacyNative();api.getItem=All;
    return {api,{LocalPlayer,World,Input,Top,Name,Real,Damageable,FindNativeProfile(GameVersion::V52)},base,x.bridge};
}
void ObservedUse(Runtime& runtime,int afterCount,bool metadataChange=false,std::uint64_t epoch=1){
    alignas(8) std::byte operation[16]{},closure[8]{};
    Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(f->player));
    auto capture=runtime.BeginUse(f->inventory,operation,true,true,Config{},1);CHECK(capture);
    Put(&f->destination,34,static_cast<std::uint8_t>(afterCount));
    if(metadataChange)Put(&f->destination,56,int{124});
    lf->actualDestination=f->destination;runtime.EndUse(*capture,true,true,Config{},epoch);
}
int main(){try{
    LegacyFixture x;ActorFixture actor;auto r=NewRuntime(x,actor);
    beforeSource=f->source;beforeDestination=f->destination;
    // Missing server pairing must not enter Place or predict inventory changes.
    CHECK(!r.Tick(f->manager,true,true,Config{},100,1));CHECK(f->sets==0 && x.sends==0 && f->requests==0);
    auto local=ReadActorIdentity(f->player),server=local;server.registry=0x500;server.role=ActorRole::Server;
    x.bridge.EndServer(server,{},1,101,FixtureCounts());
    auto result=r.Tick(f->manager,true,true,Config{},102,1);CHECK(result && *result==TransferResult::Queued && x.sends==1);
    CHECK(f->requests==0); // old modern Place path must never be used by integrated runtime
    CHECK(x.bridge.Pending());auto t=x.bridge.BeginServer(server,-4,103);CHECK(t.token);
    CHECK(!r.Tick(f->manager,true,true,Config{},103,1));CHECK(x.sends==1 && x.bridge.Pending());
    x.bridge.EndServer(server,t,1,104,FixtureCounts());
    realLag=true;CHECK(!r.Tick(f->manager,true,true,Config{},105,1));CHECK(x.bridge.Pending());
    bool nativeObserved=false;
    while(auto event=r.TakeDiagnostic())if(event->kind==DiagnosticKind::NativeResult){
        CHECK(event->requestId==1 && event->nativeId==-4 && event->nativeStatus==1);
        CHECK(event->inventorySampled && !event->realMatches && !event->exactProgress);
        CHECK(event->wrapperSourceCount==0 && event->wrapperDestinationCount==25);
        CHECK(event->actualSourceCount==20 && event->actualDestinationCount==5);nativeObserved=true;
    }
    CHECK(nativeObserved); // original result must be observable even while real inventory lags
    CHECK(!r.Tick(f->manager,true,true,Config{},105,1) && !r.TakeDiagnostic()); // once, not each tick
    CHECK(!r.Faulted()); // server accepted but real inventory not synchronized: remain fenced
    realLag=false;
    CHECK(!r.Tick(f->manager,true,true,Config{},106,1));CHECK(!x.bridge.Pending() && !r.Faulted());
    bool accepted=false;while(auto event=r.TakeDiagnostic()) if(event->kind==DiagnosticKind::Response){CHECK(event->nativeStatus==1);accepted=true;}
    CHECK(accepted);
    r.Reset();CHECK(f->owned.empty());
    // No implicit remote/modern fallback after context invalidation.
    CHECK(!r.Tick(f->manager,true,true,Config{},107,2));CHECK(x.sends==1 && f->requests==0);
    {
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},200,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x600;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,201,FixtureCounts());CHECK(runtime.Tick(f->manager,true,true,Config{},202,1));
        auto ticket=y.bridge.BeginServer(s,-4,203);CHECK(ticket.token);
        hud=false;CHECK(!runtime.Tick(f->manager,true,true,Config{},204,1));CHECK(y.bridge.Pending());
        y.bridge.EndServer(s,ticket,1,205);hud=true;
        CHECK(!runtime.Tick(f->manager,true,true,Config{},206,1));CHECK(!y.bridge.Pending());
        CHECK(y.sends==1); // cancellation discards policy, not original server completion/fence
        bool canceledNativeResult=false,canceledResponse=false;
        while(auto event=runtime.TakeDiagnostic()){
            CHECK(event->kind!=DiagnosticKind::Reconciled);
            if(event->kind==DiagnosticKind::NativeResult){
                CHECK(event->requestId==1 && event->nativeStatus==1 && !event->inventorySampled);
                CHECK(event->actualSourceCount==-1 && event->actualDestinationCount==-1);canceledNativeResult=true;
            }
            if(event->kind==DiagnosticKind::Response)canceledResponse=true;
        }
        CHECK(canceledNativeResult && canceledResponse); // result survives UI cancellation without reading old context
    }
    {
        // Test4 runtime evidence: the client already used 6 -> 5, but the
        // authoritative packet handler still sees 6. A merely paired server
        // must not authorize an ordinary transaction that will be rejected3.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},300,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x700;s.role=ActorRole::Server;
        InventoryCounts counts{};counts[9]=20;counts[2]=6;
        y.bridge.EndServer(s,{},1,301,counts);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},302,1));
        CHECK(y.sends==0 && f->sets==0 && !y.bridge.Pending());
        // The user stops here. Many client ticks cannot consume the trigger,
        // fabricate a rejection, or send despite the unprocessed world use.
        CHECK(!runtime.Tick(f->manager,true,true,Config{},303,1));
        CHECK(!runtime.Tick(f->manager,true,true,Config{},350,1));
        counts[2]=5;y.bridge.EndServer(s,{},1,351,counts);
        auto queued=runtime.Tick(f->manager,true,true,Config{},352,1);
        CHECK(queued && *queued==TransferResult::Queued && y.sends==1 && f->sets==2);
        auto ticket=y.bridge.BeginServer(s,-4,353);CHECK(ticket.token);
        counts[9]=0;counts[2]=25;y.bridge.EndServer(s,ticket,1,354,counts);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},355,1));
        CHECK(!y.bridge.Pending() && !runtime.Faulted());
    }
    for(bool use:{false,true}){
        // Waiting must retain a real final-use depletion, but must never turn
        // manual clearing into an empty-hand refill after counts synchronize.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);Config config{true,1};
        Put(&f->destination,34,std::uint8_t{1});y.actualDestination=f->destination;
        CHECK(!runtime.Tick(f->manager,true,true,config,400,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x800;s.role=ActorRole::Server;
        InventoryCounts counts{};counts[9]=20;counts[2]=1;y.bridge.EndServer(s,{},1,401,counts);
        CHECK(!runtime.Tick(f->manager,true,true,config,402,1));
        alignas(8) std::byte operation[16]{},closure[8]{};
        Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(f->player));
        auto capture=use?runtime.BeginUse(f->inventory,operation,true,true,config,1):std::nullopt;
        CHECK(!use || capture);
        Put(&f->destination,34,std::uint8_t{});Put(&f->destination,144,std::uint8_t{255});y.actualDestination=f->destination;y.expectedTotal=20;
        if(capture)runtime.EndUse(*capture,true,true,config,1);
        CHECK(!runtime.Tick(f->manager,true,true,config,403,1) && y.sends==0);
        CHECK(!runtime.Tick(f->manager,true,true,config,450,1) && y.sends==0);
        counts[2]=0;y.bridge.EndServer(s,{},1,451,counts);
        auto refill=runtime.Tick(f->manager,true,true,config,452,1);
        CHECK(use?(refill && *refill==TransferResult::Queued && y.sends==1):(!refill && y.sends==0));
    }
    for(bool reply:{false,true}){
        // Removing request-bound native-use accounting must break this case.
        // Exact correlated acceptance plus the proven25->24 consumption must
        // reconcile actual24; the same counts without a result must not.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},500,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x900;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,501,FixtureCounts());
        CHECK(runtime.Tick(f->manager,true,true,Config{},502,1) && y.sends==1);
        auto ticket=y.bridge.BeginServer(s,-4,503);CHECK(ticket.token);
        alignas(8) std::byte operation[16]{},closure[8]{};
        Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(f->player));
        auto capture=runtime.BeginUse(f->inventory,operation,true,true,Config{},1);CHECK(capture);
        Put(&f->destination,34,std::uint8_t{24});y.actualDestination=f->destination;
        runtime.EndUse(*capture,true,true,Config{},1);
        y.bridge.EndServer(s,reply?ticket:ServerTicket{},1,504,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},505,1));
        if(reply){
            CHECK(!y.bridge.Pending());
            bool response=false,reconciled=false;
            while(auto event=runtime.TakeDiagnostic()){
                CHECK(event->kind!=DiagnosticKind::Timeout);
                if(event->kind==DiagnosticKind::Response){CHECK(event->nativeStatus==1);response=true;}
                if(event->kind==DiagnosticKind::Reconciled){
                    CHECK(event->nativeStatus==1 && event->sourceCount==0 && event->destinationCount==24);
                    CHECK(event->provenConsumption==1 && event->consumptionChainValid && event->accountedProgress);reconciled=true;
                }
            }
            CHECK(response && reconciled && y.sends==1);
            CHECK(!runtime.Tick(f->manager,true,true,Config{},2002,1) && !runtime.TakeDiagnostic());
            continue;
        }
        CHECK(y.bridge.Pending());
        bool received=false;
        while(auto event=runtime.TakeDiagnostic()){
            CHECK(event->kind!=DiagnosticKind::Response && event->kind!=DiagnosticKind::Reconciled);
            if(event->kind==DiagnosticKind::NativeResult){
                CHECK(event->nativeStatus==1 && !event->exactProgress && event->realMatches);
                CHECK(event->actualSourceCount==0 && event->actualDestinationCount==24);received=true;
            }
        }
        CHECK(received==reply);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},2002,1));
        auto timeout=runtime.TakeDiagnostic();CHECK(timeout && timeout->kind==DiagnosticKind::Timeout);
        CHECK(timeout->nativeStatus==(reply?1:-1) && timeout->inventorySampled && timeout->realMatches && !timeout->exactProgress);
        CHECK(timeout->sourceCount==20 && timeout->destinationCount==5 && timeout->amount==20);
        CHECK(timeout->wrapperSourceCount==0 && timeout->wrapperDestinationCount==24);
        CHECK(timeout->actualSourceCount==0 && timeout->actualDestinationCount==24);
        CHECK(timeout->provenConsumption==1 && timeout->consumptionChainValid && timeout->accountedProgress);
        CHECK(!runtime.TakeDiagnostic() && !y.bridge.Pending() && y.sends==1 && !runtime.Faulted());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},2100,1) && y.sends==1); // no retry flood
    }
    {
        // Exact local counts and a post-handler server count sample without
        // the armed transaction's result must never be promoted to success.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},3000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xA00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,3001,FixtureCounts());
        CHECK(runtime.Tick(f->manager,true,true,Config{},3002,1));CHECK(runtime.TakeDiagnostic());
        y.bridge.EndServer(s,{},1,3003,FixtureCounts()); // unrelated packet, not native acceptance
        CHECK(!runtime.Tick(f->manager,true,true,Config{},3004,1) && !runtime.TakeDiagnostic() && y.bridge.Pending());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},4502,1));
        auto timeout=runtime.TakeDiagnostic();CHECK(timeout && timeout->kind==DiagnosticKind::Timeout);
        CHECK(timeout->nativeStatus==-1 && timeout->inventorySampled && timeout->realMatches && timeout->exactProgress);
        CHECK(timeout->actualSourceCount==0 && timeout->actualDestinationCount==25);
        CHECK(!runtime.TakeDiagnostic() && !y.bridge.Pending() && y.sends==1);
    }
    for(int mode=0;mode<8;++mode){
        // A lower count, even followed by legitimate use, is not proof of a
        // complete consumption chain. Source/metadata/restoration cannot heal it.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},6000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xB00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,6001,FixtureCounts());
        CHECK(runtime.Tick(f->manager,true,true,Config{},6002,1));
        auto ticket=y.bridge.BeginServer(s,-4,6003);CHECK(ticket.token);
        if(mode==0){Put(&f->destination,34,std::uint8_t{24});} // manual drop
        if(mode==1){Put(&f->destination,34,std::uint8_t{24});ObservedUse(runtime,23);} // gap before use
        if(mode==2){ObservedUse(runtime,24);Put(&f->destination,34,std::uint8_t{23});} // gap after use
        if(mode==3)ObservedUse(runtime,24,true); // callback changes metadata as well
        if(mode==4){ObservedUse(runtime,24);Put(&f->source,34,std::uint8_t{1});} // source mismatch
        if(mode==5){Put(&f->destination,56,int{124});ObservedUse(runtime,24);} // pre-use metadata change
        if(mode==6){
            Put(&f->destination,34,std::uint8_t{24});ObservedUse(runtime,23);
            Put(&f->destination,34,std::uint8_t{25}); // restore original post-count after invalid callback
        }
        if(mode==7){
            Put(&f->destination,34,std::uint8_t{24});
            CHECK(!runtime.Tick(f->manager,true,true,Config{},6004,1)); // unexplained sample invalidates chain
            Put(&f->destination,34,std::uint8_t{25});ObservedUse(runtime,24);
        }
        y.actualDestination=f->destination;y.actualSource=f->source;
        y.bridge.EndServer(s,ticket,1,6005,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},6006,1) && y.bridge.Pending());
        while(auto e=runtime.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},7502,1) && !y.bridge.Pending());
        auto e=runtime.TakeDiagnostic();CHECK(e && e->kind==DiagnosticKind::Timeout && e->nativeStatus==1);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},7600,1) && y.sends==1 && f->sets==2);
    }
    for(bool empty:{false,true}){
        // Consumption proof survives intermediate owner ticks before a native
        // reply, without turning count samples into acceptance. Also covers0.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},8000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xC00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,8001,FixtureCounts());CHECK(runtime.Tick(f->manager,true,true,Config{},8002,1));
        auto ticket=y.bridge.BeginServer(s,-4,8003);CHECK(ticket.token);
        ObservedUse(runtime,24);CHECK(!runtime.Tick(f->manager,true,true,Config{},8004,1) && y.bridge.Pending());
        ObservedUse(runtime,empty?0:22);CHECK(!runtime.Tick(f->manager,true,true,Config{},8005,1) && y.bridge.Pending());
        while(auto e=runtime.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled);
        y.bridge.EndServer(s,ticket,1,8006,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},8007,1) && !y.bridge.Pending());
        bool reconciled=false;while(auto e=runtime.TakeDiagnostic())if(e->kind==DiagnosticKind::Reconciled){
            CHECK(e->sourceCount==0 && e->destinationCount==(empty?0:22));
            CHECK(e->provenConsumption==(empty?25:3));reconciled=true;
        }
        CHECK(reconciled && y.sends==1 && f->sets==2);
    }
    for(int boundary=0;boundary<4;++boundary){
        // UI/focus/config revision/selection invalidates proof but retains the
        // original integer native fence. Completion must not be reconciled.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},9000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xD00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,9001,FixtureCounts());CHECK(runtime.Tick(f->manager,true,true,Config{},9002,1));
        auto ticket=y.bridge.BeginServer(s,-4,9003);CHECK(ticket.token);ObservedUse(runtime,24);
        if(boundary==0)hud=false;
        if(boundary==3)Put(f->supplies,16,int{3});
        CHECK(!runtime.Tick(f->manager,boundary!=1,true,Config{},9004,boundary==2?2:1) && y.bridge.Pending());
        hud=true;Put(f->supplies,16,int{2});
        y.bridge.EndServer(s,ticket,1,9005,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},9006,boundary==2?2:1) && !y.bridge.Pending());
        while(auto e=runtime.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::Reconciled);
        CHECK(y.sends==1);
    }
    {
        // Metadata of a nonempty remainder must still match the original
        // source, even if wrapper/real agree and destination use is proven.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        Put(&f->source,34,std::uint8_t{64});y.actualSource=f->source;y.expectedTotal=69;
        CHECK(!runtime.Tick(f->manager,true,true,Config{},10000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xE00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,10001,FixtureCounts());CHECK(runtime.Tick(f->manager,true,true,Config{},10002,1));
        auto ticket=y.bridge.BeginServer(s,-4,10003);ObservedUse(runtime,63);
        Put(&f->source,56,int{124});y.actualSource=f->source;
        y.bridge.EndServer(s,ticket,1,10004,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},10005,1) && y.bridge.Pending());
        while(auto e=runtime.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},11502,1) && !y.bridge.Pending());
        CHECK(y.sends==1);
    }
    {
        // An acceptance first sampled at the original deadline must not
        // produce a successful Reconciled event that Policy will reject.
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(!runtime.Tick(f->manager,true,true,Config{},12000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0xF00;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,12001,FixtureCounts());CHECK(runtime.Tick(f->manager,true,true,Config{},12002,1));
        auto ticket=y.bridge.BeginServer(s,-4,12003);ObservedUse(runtime,24);
        y.bridge.EndServer(s,ticket,1,13502,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},13502,1) && !y.bridge.Pending());
        bool timeout=false;while(auto e=runtime.TakeDiagnostic()){
            CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled);
            if(e->kind==DiagnosticKind::Timeout){CHECK(e->nativeStatus==1 && e->accountedProgress);timeout=true;}
        }
        CHECK(timeout && y.sends==1);
    }
    {
        LegacyFixture y;ActorFixture a;auto runtime=NewRuntime(y,a);
        CHECK(runtime.SelectBackend(Backend::Integrated,1,14000));
        CHECK(!runtime.Tick(f->manager,true,true,Config{},14000,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x1100;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},1,14001,FixtureCounts());
        CHECK(runtime.Tick(f->manager,true,true,Config{},14002,1) && y.bridge.Pending());
        CHECK(!runtime.SelectBackend(Backend::RemoteModern,2,14003));
        CHECK(!runtime.SelectBackend(Backend::Integrated,3,14004));
        CHECK(!runtime.SelectBackend(Backend::RemoteModern,4,15501));
        CHECK(runtime.SelectBackend(Backend::RemoteModern,4,15502));
        CHECK(!y.bridge.Pending() && y.sends==1 && f->requests==0);
    }
    { // A copied native tool break waits for BOTH wrapper/real hand and the
      // existing authoritative server count sample. Never modern fallback.
        LegacyFixture y;ActorFixture a;auto setup=NewRuntime(y,a);
        Put(&f->source,34,std::uint8_t{1});Put(&f->destination,34,std::uint8_t{1});
        Put(&f->source,64,std::uint8_t{1});Put(&f->destination,64,std::uint8_t{1});
        y.actualSource=f->source;y.actualDestination=f->destination;y.expectedTotal=1;f->expectedAmount=1;
        auto api=LegacyNative();api.getItem=All;api.stack.merge=[](void const*,void const*){return false;};
        WorldApi worldApi{LocalPlayer,World,Input,Top,Name,Real,[](void const* p){return Capacity(p)==1;},FindNativeProfile(GameVersion::V52)};
        Runtime runtime{api,worldApi,base,y.bridge};auto now=GetTickCount64();
        CHECK(!runtime.Tick(f->manager,true,true,Config{},now,1));
        auto s=ReadActorIdentity(f->player);s.registry=0x1200;s.role=ActorRole::Server;
        y.bridge.EndServer(s,{},-1,now,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},now,1));
        OwnedStack working;CHECK(working.Copy(api.stack,&f->destination));
        auto capture=runtime.BeginToolHurt(working.Get(),f->player,true,true,Config{},1);CHECK(capture);
        Put(const_cast<void*>(working.Get()),34,std::uint8_t{});
        CHECK(runtime.EndToolHurt(*capture,working.Get(),true,true,Config{},1));
        CHECK(!runtime.Tick(f->manager,true,true,Config{},now+1,1) && y.sends==0);
        Put(&f->destination,34,std::uint8_t{});y.actualDestination=f->destination;
        CHECK(!runtime.Tick(f->manager,true,true,Config{},now+2,1) && y.sends==0); // stale server count1
        y.bridge.EndServer(s,{},-1,now+3,FixtureCounts());
        CHECK(runtime.Tick(f->manager,true,true,Config{},now+4,1)==TransferResult::Queued);
        CHECK(y.sends==1 && f->requests==0 && y.bridge.Pending() && !y.bridge.Poll());
        auto ticket=y.bridge.BeginServer(s,-4,now+5);CHECK(ticket.token);
        y.bridge.EndServer(s,ticket,1,now+6,FixtureCounts());
        CHECK(!runtime.Tick(f->manager,true,true,Config{},now+7,1) && !y.bridge.Pending());
        bool toolAccepted=false;while(auto e=runtime.TakeDiagnostic())if(e->kind==DiagnosticKind::Response){CHECK(e->nativeStatus==1);toolAccepted=true;}
        CHECK(toolAccepted && y.sends==1);runtime.Reset();
    }
    std::cout<<"Integrated runtime tests passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
