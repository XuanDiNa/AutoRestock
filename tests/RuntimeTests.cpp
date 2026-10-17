// Share only the independently hand-constructed external native ABI fixture.
// Runtime and Policy/Identity/Transaction/Use/Response production code is real.
#define main NativeTransactionCases
#include "TransactionTests.cpp"
#undef main
#include "AutoRestock/Runtime.h"
alignas(8) std::byte client[8]{},world[8]{},scene[8]{};
bool hud=true,input=true;
StackStorage emptySlot{};
bool separateActual=false;
std::array<StackStorage,36> actualSlots{};
void* Local(void* p){CHECK(p==client);return f->player;}
void* World(void* p){CHECK(p==client);return world;}
bool Input(void* p){CHECK(p==client);return input;}
void* Top(void* p){CHECK(p==client);return scene;}
bool Name(void*,NativeStringView const*){return hud;}
bool Damageable(void const* p){return Capacity(p)==1;}
bool FullMetadata(void const* a,void const* b){return Equal(a,b) && Read<int>(a,60)==Read<int>(b,60);}
void ToolDamage(void*,void* p,std::int16_t value){
    CHECK(value==0 && std::find(f->owned.begin(),f->owned.end(),p)!=f->owned.end());Put(p,60,int{});
}
void const* AllSlots(void const* p,int n){CHECK(p==f->wrapper);return n==9?&f->source:n==2?&f->destination:n==10 && f->useSecond?&f->secondSource:&emptySlot;}
void const* RealItem(void* p,int n){CHECK(p==f->inventory);return separateActual?&actualSlots[n]:AllSlots(f->wrapper,n);}
void Prepare(Fixture& x){
    f=&x;Put(client,0,base+0xE9731B0);Put(f->player,3440,static_cast<void*>(client));Put(f->player,2744,static_cast<void*>(f->manager));
    hud=input=true;separateActual=false;actualSlots={};
}
std::array<std::byte,48> DeliveredReply(){
    // The external native flush takes ownership of manager+104 BEFORE a
    // remote reply can arrive. This fixture does not reconcile inventory:
    // actual/wrapper progress remains independently controlled by each case.
    Put(f->manager,104,std::uintptr_t{});return Reply();
}
Runtime New(){auto api=Api();api.getItem=AllSlots;api.stack.merge=FullMetadata;api.stack.equal=FullMetadata;api.stack.setDamage=ToolDamage;
    api.legacy.inventoryItem=[](void const* inventory,int n){return RealItem(const_cast<void*>(inventory),n);};
    return {api,{Local,World,Input,Top,Name,RealItem,Damageable,FindNativeProfile(GameVersion::V52)},base};}
#ifndef AUTORESTOCK_RUNTIME_FIXTURE_ONLY
int main(){try{
    {
        Fixture x;Prepare(x);auto native=Api();native.profile=FindNativeProfile(GameVersion::V51);
        native.getItem=AllSlots;
        Runtime r{native,{Local,World,Input,Top,Name,RealItem,Damageable,FindNativeProfile(GameVersion::V52)},base};
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1) && x.requests==0);
    }
    {
        Fixture x;Prepare(x);auto r=New();
        CHECK(r.SelectBackend(Backend::RemoteModern,10,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1) && x.requests==1);
        CHECK(!r.SelectBackend(Backend::Unsupported,11,101));
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1) && x.requests==1);
        // Switching again must not restart the first request's1500ms fence.
        CHECK(!r.SelectBackend(Backend::RemoteModern,12,103));
        CHECK(!r.SelectBackend(Backend::RemoteModern,13,1599));
        CHECK(r.SelectBackend(Backend::RemoteModern,13,1600));
        Put(x.manager,104,std::uintptr_t{});
        Put(&x.source,34,std::uint8_t{20});Put(&x.destination,34,std::uint8_t{5});
        CHECK(r.Tick(x.manager,true,true,Config{},1600,1) && x.requests==2);r.Reset();
    }
    {
        Fixture x;Prepare(x);auto r=New();CHECK(r.SelectBackend(Backend::RemoteModern,20,100));
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        CHECK(!r.SelectBackend(Backend::Unsupported,21,101));
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        CHECK(r.SelectBackend(Backend::RemoteModern,22,102));
        CHECK(!r.Decode(x.manager,DeliveredReply()));r.Reset();
    }
    {
        Fixture x;Prepare(x);auto r=New();
        CHECK(!r.SelectBackend(Backend::Integrated,1,100)); // no bridge supplied
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1) && x.requests==0);
    }
    {
        Fixture x;Prepare(x);auto r=New();auto result=r.Tick(x.manager,true,true,Config{},100,1);
        CHECK(result && *result==TransferResult::Queued && x.requests==1);
        auto trace=r.TakeDiagnostic();CHECK(trace && trace->kind==DiagnosticKind::Request);
        CHECK(trace->nativeId==-3 && trace->source==9 && trace->destination==2 && trace->amount==20);
        CHECK(trace->sourceCount==20 && trace->destinationCount==5 && trace->sourceId==501 && trace->destinationId==502);
        CHECK(!r.TakeDiagnostic());
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1));CHECK(x.requests==1);
        auto completion=r.Decode(x.manager,DeliveredReply());CHECK(completion && completion->requestId!=0);r.Publish(*completion);
        trace=r.TakeDiagnostic();CHECK(trace && trace->kind==DiagnosticKind::Response && trace->nativeStatus==0 && trace->nativeId==-3);
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1));CHECK(x.requests==1 && !r.Faulted());
        trace=r.TakeDiagnostic();CHECK(trace && trace->kind==DiagnosticKind::Reconciled && trace->sourceCount==0 && trace->destinationCount==25);
        r.Reset();CHECK(x.owned.empty());
    }
    {
        Fixture x;Prepare(x);auto r=New();Put(&x.destination,34,std::uint8_t{8});
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));
        Put(&x.destination,34,std::uint8_t{5}); // intentional partial drop, no native use provenance
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1));CHECK(x.requests==0);
    }
    {
        Fixture x;Prepare(x);auto r=New();CHECK(r.Tick(x.manager,true,true,Config{},100,1));CHECK(r.TakeDiagnostic());
        auto reply=DeliveredReply();reply[0]=std::byte{27};auto completion=r.Decode(x.manager,reply);CHECK(completion);r.Publish(*completion);
        auto event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::Response && event->nativeStatus==27 && event->nativeId==-3);
        Put(&x.destination,34,std::uint8_t{5});Put(&x.source,34,std::uint8_t{20}); // original engine reconciles refusal
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1));CHECK(x.requests==1);
        event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::Reconciled && event->nativeStatus==27);
        CHECK(event->sourceCount==20 && event->destinationCount==5);CHECK(!r.Tick(x.manager,true,true,Config{},102,1));CHECK(x.requests==1);
    }
    {
        Fixture x;Prepare(x);auto r=New();Put(&x.destination,34,std::uint8_t{8});
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));
        alignas(8) std::byte operation[16]{},closure[48]{},gameMode[16]{};
        Put(operation,0,base+0x25B5EF0);Put(operation,8,static_cast<void*>(closure));
        Put(closure,0,static_cast<void*>(gameMode));Put(gameMode,8,static_cast<void*>(x.player));
        auto capture=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(capture);
        Put(&x.destination,34,std::uint8_t{5});
        r.EndUse(*capture,true,true,Config{},1);
        auto result=r.Tick(x.manager,true,true,Config{},101,1);CHECK(result && *result==TransferResult::Queued);
    }
    {
        Fixture x;Prepare(x);auto r=New();hud=false;
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));CHECK(x.requests==0);
        hud=true;Config disabled;disabled.enabled=false;
        CHECK(!r.Tick(x.manager,true,true,disabled,101,2));CHECK(x.requests==0);
        CHECK(!r.Tick(x.manager,false,true,Config{},102,3));CHECK(x.requests==0);
    }
    {
        Fixture x;Prepare(x);auto r=New();auto result=r.Tick(x.manager,true,true,Config{},100,1);CHECK(result);
        CHECK(r.TakeDiagnostic());
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        auto trace=r.TakeDiagnostic();CHECK(trace && trace->kind==DiagnosticKind::Timeout && trace->nativeId==-3);
        CHECK(!r.Tick(x.manager,true,true,Config{},1700,1));CHECK(x.requests==1); // timeout never floods retries
    }
    {
        Fixture x;Prepare(x);auto r=New();Put(&x.destination,34,std::uint8_t{1});
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));CHECK(x.requests==1);
        hud=false;CHECK(!r.Tick(x.manager,true,true,Config{},101,1));
        hud=true;Put(&x.destination,34,std::uint8_t{2});Put(&x.source,34,std::uint8_t{20});
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1));CHECK(x.requests==1); // canceled policy must retain in-flight fence
        auto completion=r.Decode(x.manager,DeliveredReply());CHECK(completion);r.Publish(*completion);
    }
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        auto completion=r.Decode(x.manager,DeliveredReply());CHECK(completion);r.Publish(*completion);
        x.expectedAmount=39;
        auto result=r.Tick(x.manager,true,true,Config{},101,1);
        CHECK(result && *result==TransferResult::Queued && x.requests==2);
        CHECK(Read<std::uint8_t>(&x.destination,34)==64 && Read<std::uint8_t>(&x.secondSource,34)==1);
        auto reply=DeliveredReply();Put(reply.data(),16,int{-5});completion=r.Decode(x.manager,reply);CHECK(completion);r.Publish(*completion);
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1));CHECK(x.requests==2);
    }
    // Removing BeginUse/EndUse provenance publication must break this case:
    // a verified delayed native completion consumes the last held item.
    {
        Fixture x;Prepare(x);auto r=New();Config config{true,1};Put(&x.destination,34,std::uint8_t{1});
        CHECK(!r.Tick(x.manager,true,true,config,100,1));
        alignas(8) std::byte operation[16]{},closure[8]{};
        Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(x.player));
        auto capture=r.BeginUse(x.inventory,operation,true,true,config,1);CHECK(capture);
        Put(&x.destination,34,std::uint8_t{});Put(&x.destination,144,std::uint8_t{255});
        r.EndUse(*capture,true,true,config,1);
        auto result=r.Tick(x.manager,true,true,config,101,1);
        CHECK(result && *result==TransferResult::Queued && Read<std::uint8_t>(&x.destination,34)==20);
        CHECK(x.requests==1 && Read<std::uint8_t>(&x.source,34)==0);
    }
    // Empty hand without a native use operation is intentional removal, not
    // depletion evidence; treating every empty hand as use must fail here.
    {
        Fixture x;Prepare(x);auto r=New();Config config{true,1};Put(&x.destination,34,std::uint8_t{1});
        CHECK(!r.Tick(x.manager,true,true,config,100,1));Put(&x.destination,34,std::uint8_t{});
        CHECK(!r.Tick(x.manager,true,true,config,101,1));CHECK(x.requests==0);
    }
    // Broken tools may differ ONLY in damage. The renamed earlier source
    // must be skipped and normalization must never touch the borrowed items.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;x.expectedAmount=1;
        for(auto p:{&x.source,&x.secondSource,&x.destination}){Put(p,34,std::uint8_t{1});Put(p,64,std::uint8_t{1});}
        Put(&x.source,56,int{124});Put(&x.source,60,int{7});
        Put(&x.secondSource,60,int{2});Put(&x.destination,60,int{99});
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));
        alignas(8) std::byte operation[16]{},closure[8]{},gameMode[16]{};
        Put(operation,0,base+0x25B6380);Put(operation,8,static_cast<void*>(closure));
        Put(closure,0,static_cast<void*>(gameMode));Put(gameMode,8,static_cast<void*>(x.player));
        auto capture=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(capture);
        CHECK(Read<int>(&x.destination,60)==99 && Read<int>(&x.secondSource,60)==2);
        Put(&x.destination,34,std::uint8_t{});Put(&x.destination,144,std::uint8_t{255});
        r.EndUse(*capture,true,true,Config{},1);
        auto result=r.Tick(x.manager,true,true,Config{},101,1);
        CHECK(result && *result==TransferResult::Queued && x.requests==1);
        CHECK(Read<std::uint8_t>(x.sourceInfo.data(),12)==10 && Read<std::uint8_t>(&x.source,34)==1);
        CHECK(Read<std::uint8_t>(&x.destination,34)==1 && Read<int>(&x.destination,60)==2);
        CHECK(Read<int>(&x.source,60)==7);
    }
    // Bug caught: modern Publish previously cleared the fence immediately,
    // allowing the next source before the real container matched prediction.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;separateActual=true;
        actualSlots[9]=x.source;actualSlots[10]=x.secondSource;actualSlots[2]=x.destination;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        x.expectedAmount=39;
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1));
        CHECK(x.requests==1); // wrapper25 versus actual5 is NOT reconciliation
        actualSlots[9]=x.source;actualSlots[2]=x.destination;
        auto next=r.Tick(x.manager,true,true,Config{},102,1);
        CHECK(next && *next==TransferResult::Queued && x.requests==2);
        r.Reset();
    }
    // Delayed success must account for request-bound native use25->24, so the
    // next partial source contributes40, not39. Counts alone cannot prove use.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        alignas(8) std::byte operation[16]{},closure[8]{};
        Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(x.player));
        auto capture=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(capture);
        Put(&x.destination,34,std::uint8_t{24});r.EndUse(*capture,true,true,Config{},1);
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1) && x.requests==1);
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        x.expectedAmount=40;auto next=r.Tick(x.manager,true,true,Config{},102,1);
        CHECK(next && *next==TransferResult::Queued && x.requests==2);
        CHECK(Read<std::uint8_t>(&x.destination,34)==64 && Read<std::uint8_t>(&x.secondSource,34)==0);
        r.Reset();
    }
    // An unexplained gap, metadata change or increase poisons the continuous
    // proof permanently; later good-looking counts never repair that request.
    for(int corruption=0;corruption<3;++corruption){
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        if(corruption==0)Put(&x.destination,34,std::uint8_t{24});
        if(corruption==1)Put(&x.destination,56,int{124});
        if(corruption==2)Put(&x.destination,34,std::uint8_t{26});
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1) && x.requests==1);
        Put(&x.destination,34,std::uint8_t{25});Put(&x.destination,56,int{123});
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1) && x.requests==1);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1) && x.requests==1);
        CHECK(!r.Decode(x.manager,DeliveredReply()));r.Reset();
    }
    // Rejection before reconciliation dominates a previously latched success
    // across separate original-handler callbacks, as well as within a batch.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;separateActual=true;
        actualSlots[9]=x.source;actualSlots[10]=x.secondSource;actualSlots[2]=x.destination;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        auto accepted=r.Decode(x.manager,DeliveredReply());CHECK(accepted);r.Publish(*accepted);
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1));
        auto reply=DeliveredReply();reply[0]=std::byte{49};auto rejected=r.Decode(x.manager,reply);CHECK(rejected);r.Publish(*rejected);
        r.Publish(*accepted); // cannot upgrade a matching refusal
        actualSlots[9]=x.source;actualSlots[2]=x.destination;
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1) && x.requests==1);
        bool refusal=false;
        while(auto trace=r.TakeDiagnostic())if(trace->kind==DiagnosticKind::Reconciled){CHECK(trace->nativeStatus==49);refusal=true;}
        CHECK(refusal && !r.Decode(x.manager,DeliveredReply()));r.Reset();
    }
    // Neither prediction alone nor a late success allows a next-source move;
    // the response must match the exact manager/id and original deadline.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        auto wrong=DeliveredReply();Put(wrong.data(),16,int{-5});CHECK(!r.Decode(x.manager,wrong));
        CHECK(!r.Decode(reinterpret_cast<void*>(0x1234),DeliveredReply()));
        CHECK(!r.Tick(x.manager,true,true,Config{},1599,1) && x.requests==1);
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1) && x.requests==1);
        CHECK(!r.Tick(x.manager,true,true,Config{},1601,1) && x.requests==1);r.Reset();
    }
    // Cancellation discards proof/references but keeps only the integer fence.
    // The matching original result releases it without completing old policy.
    {
        Fixture x;Prepare(x);auto r=New();x.useSecond=true;
        CHECK(r.Tick(x.manager,true,true,Config{},100,1));
        hud=false;CHECK(!r.Tick(x.manager,true,true,Config{},101,1));hud=true;
        CHECK(!r.Tick(x.manager,true,true,Config{},102,1) && x.requests==1);
        auto done=r.Decode(x.manager,DeliveredReply());CHECK(done);r.Publish(*done);
        CHECK(!r.Tick(x.manager,true,true,Config{},103,1) && x.requests==1);
        CHECK(!r.Decode(x.manager,DeliveredReply()));r.Reset();
    }
    // A busy native manager is not a rejected depletion/threshold attempt;
    // preserve policy intent while no request has crossed the native boundary.
    {
        Fixture x;Prepare(x);auto r=New();x.loseAvailability=true;
        auto deferred=r.Tick(x.manager,true,true,Config{},100,1);
        CHECK(deferred && *deferred==TransferResult::Deferred && x.requests==0);
        x.loseAvailability=false;x.available=true;
        auto queued=r.Tick(x.manager,true,true,Config{},101,1);
        CHECK(queued && *queued==TransferResult::Queued && x.requests==1);r.Reset();
    }
    // A real/wrapper preflight mismatch must also preserve a world-use
    // depletion trigger. Rejecting it would silently lose the empty refill.
    {
        Fixture x;Prepare(x);auto r=New();Config config{true,1};Put(&x.destination,34,std::uint8_t{1});
        CHECK(!r.Tick(x.manager,true,true,config,100,1));
        alignas(8) std::byte operation[16]{},closure[8]{};
        Put(operation,0,base+0x21E6E0);Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(x.player));
        auto capture=r.BeginUse(x.inventory,operation,true,true,config,1);CHECK(capture);
        Put(&x.destination,34,std::uint8_t{});Put(&x.destination,144,std::uint8_t{255});r.EndUse(*capture,true,true,config,1);
        separateActual=true;actualSlots[9]=x.source;actualSlots[2]=x.destination;Put(&actualSlots[9],34,std::uint8_t{19});
        auto deferred=r.Tick(x.manager,true,true,config,101,1);
        CHECK(deferred && *deferred==TransferResult::Deferred && x.requests==0 && x.sets==0);
        actualSlots[9]=x.source;
        auto queued=r.Tick(x.manager,true,true,config,102,1);
        CHECK(queued && *queued==TransferResult::Queued && x.requests==1);
        CHECK(Read<std::uint8_t>(&x.destination,34)==20);r.Reset();
    }
    // Diagnostic handoff is bounded and never overwrites unconsumed events.
    {
        DiagnosticQueue queue;Diagnostic event;event.nativeId=-7;
        for(unsigned i=0;i<64;++i){event.requestId=i+1;CHECK(queue.Push(event));}
        CHECK(!queue.Push(event) && queue.Dropped()==1);
        for(unsigned i=0;i<64;++i){auto e=queue.Pop();CHECK(e && e->requestId==i+1 && e->nativeId==-7);}
        CHECK(!queue.Pop() && queue.Push(event) && queue.Pop());
    }
    std::cout<<"PASS: production runtime threshold/use/empty/broken-tool/drop/UI/disable/pending/ACK/timeout integration and bounded diagnostics\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
