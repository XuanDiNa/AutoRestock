// A tool held while integrated admission waits must still be observable.
// Removing pre-admission telemetry must fail this test without sending a refill.
#define AUTORESTOCK_RUNTIME_FIXTURE_ONLY
#include "RuntimeTests.cpp"
#include "ActorFixture.h"
std::optional<Diagnostic> Trace(Runtime& runtime,ToolStage stage){
    while(auto event=runtime.TakeDiagnostic())if(event->kind==DiagnosticKind::ToolTrace && event->tool.stage==stage)return event;
    return {};
}
void HoldTool(Fixture& x){
    Put(&x.destination,34,std::uint8_t{1});Put(&x.destination,64,std::uint8_t{1});Put(&x.destination,60,int{31});
}
Runtime IntegratedRuntime(IntegratedBridge& bridge,bool classify=true){
    auto api=Api();api.getItem=AllSlots;api.stack.merge=FullMetadata;api.stack.equal=FullMetadata;api.stack.setDamage=ToolDamage;
    WorldApi worldApi{Local,World,Input,Top,Name,RealItem,Damageable,FindNativeProfile(GameVersion::V52)};
    if(!classify)worldApi.damageable=[](void const*){return false;};
    return {api,worldApi,base,bridge};
}
void BreakHeld(Fixture& x){Put(&x.destination,34,std::uint8_t{});Put(&x.destination,144,std::uint8_t{255});}
struct DestroyOperation {
    alignas(8) std::byte operation[16]{},closure[8]{},gameMode[16]{};
    explicit DestroyOperation(Fixture& x){
        Put(operation,0,base+0x25B6380);Put(operation,8,static_cast<void*>(closure));
        Put(closure,0,static_cast<void*>(gameMode));Put(gameMode,8,static_cast<void*>(x.player));
    }
};
int main(){try{
    {
    Fixture x;Prepare(x);ActorFixture actor;actor.Attach(x.player);HoldTool(x);
    IntegratedBridge bridge;auto runtime=IntegratedRuntime(bridge);
    CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));
    auto event=runtime.TakeDiagnostic();
    CHECK(event.has_value());
    CHECK(event->destination==2 && event->destinationCount==1 && event->amount==0);
    CHECK(event->tool.after.damageable && event->tool.after.capacity==1);
    auto gate=Trace(runtime,ToolStage::Gate);
    CHECK(gate && gate->tool.reason==ToolReason::PairingNotReady);
    for(unsigned now=101;now<1100;++now)CHECK(!runtime.Tick(x.manager,true,true,Config{},now,1));
    CHECK(!runtime.TakeDiagnostic()); // bounded records, not one per tick
    BreakHeld(x);CHECK(!runtime.Tick(x.manager,true,true,Config{},1100,1));
    event=Trace(runtime,ToolStage::Sample);
    CHECK(event && event->tool.before.damageable && event->tool.before.count==1 && event->tool.after.empty);
    CHECK(!event->tool.depletedByUse && event->tool.completedUses==0);
    CHECK(x.requests==0 && !bridge.Pending());
    }
    { // A failed damageability classification must remain visible for capacity1.
        Fixture x;Prepare(x);ActorFixture actor;actor.Attach(x.player);HoldTool(x);
        IntegratedBridge bridge;auto runtime=IntegratedRuntime(bridge,false);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));
        auto event=Trace(runtime,ToolStage::Sample);
        CHECK(event && !event->tool.after.damageable && event->tool.after.capacity==1);
    }
    { // Actual observation timing is distinct from a manufactured synchronous break.
        Fixture x;Prepare(x);ActorFixture actor;actor.Attach(x.player);HoldTool(x);
        IntegratedBridge bridge;auto runtime=IntegratedRuntime(bridge);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
        DestroyOperation op(x);auto capture=runtime.BeginUse(x.inventory,op.operation,true,true,Config{},1);CHECK(capture);
        Put(&x.destination,60,int{32});runtime.EndUse(*capture,true,true,Config{},1);
        auto event=Trace(runtime,ToolStage::Use);
        CHECK(event && event->tool.identityChanged && event->sourceCount==1 && event->destinationCount==1);
        CHECK(!event->tool.depletedByUse && event->tool.completedUses==1);
        capture=runtime.BeginUse(x.inventory,op.operation,true,true,Config{},1);CHECK(capture);
        BreakHeld(x);runtime.EndUse(*capture,true,true,Config{},1);
        event=Trace(runtime,ToolStage::Use);
        CHECK(event && event->tool.depletedByUse && event->tool.depletions==1 && event->destinationCount==0);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},101,1));CHECK(x.requests==0);
        runtime.Reset(ResetCause::ConfigDisabled,102);
        event=Trace(runtime,ToolStage::Reset);
        CHECK(event && event->resetCause==ResetCause::ConfigDisabled);
        CHECK(!runtime.Tick(x.manager,true,true,Config{false,17},103,1) && !runtime.TakeDiagnostic());
    }
    { // Counts mismatch must be distinguished from missing native-use evidence.
        Fixture x;Prepare(x);ActorFixture actor;actor.Attach(x.player);HoldTool(x);
        Put(&x.source,34,std::uint8_t{});IntegratedBridge bridge;auto runtime=IntegratedRuntime(bridge);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
        auto server=ReadActorIdentity(x.player);server.registry=0x500;server.role=ActorRole::Server;
        InventoryCounts counts{};counts[2]=1;bridge.EndServer(server,{},1,101,counts);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},102,1));while(runtime.TakeDiagnostic()){}
        DestroyOperation op(x);auto capture=runtime.BeginUse(x.inventory,op.operation,true,true,Config{},1);CHECK(capture);
        BreakHeld(x);runtime.EndUse(*capture,true,true,Config{},1);while(runtime.TakeDiagnostic()){}
        CHECK(!runtime.Tick(x.manager,true,true,Config{},103,1));
        auto gate=Trace(runtime,ToolStage::Gate);
        CHECK(gate && gate->tool.reason==ToolReason::CountMismatch && gate->tool.matchingSources==-1);
        counts[2]=0;bridge.EndServer(server,{},1,104,counts);
        CHECK(!runtime.Tick(x.manager,true,true,Config{},105,1));
        auto event=Trace(runtime,ToolStage::Policy);
        CHECK(event && event->tool.depletedByUse && event->tool.matchingSources==0);
        CHECK(event->tool.reason==ToolReason::NoMatchingSource && x.requests==0);
    }
    { // Read-only telemetry must not turn manual clearing into tool replacement.
        Fixture x;Prepare(x);HoldTool(x);auto runtime=New();
        CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
        BreakHeld(x);CHECK(!runtime.Tick(x.manager,true,true,Config{},101,1));
        auto event=Trace(runtime,ToolStage::Policy);
        CHECK(event && event->tool.reason==ToolReason::IntentionalRemoval && !event->tool.depletedByUse);
        CHECK(x.requests==0);
    }
    { // A synthetic proven break still queues the same metadata-matched tool.
        Fixture x;Prepare(x);HoldTool(x);Put(&x.source,34,std::uint8_t{1});Put(&x.source,64,std::uint8_t{1});
        Put(&x.source,60,int{2});x.expectedAmount=1;auto runtime=New();
        CHECK(!runtime.Tick(x.manager,true,true,Config{},100,1));while(runtime.TakeDiagnostic()){}
        DestroyOperation op(x);auto capture=runtime.BeginUse(x.inventory,op.operation,true,true,Config{},1);CHECK(capture);
        BreakHeld(x);runtime.EndUse(*capture,true,true,Config{},1);while(runtime.TakeDiagnostic()){}
        auto result=runtime.Tick(x.manager,true,true,Config{},101,1);CHECK(result && *result==TransferResult::Queued);
        auto event=Trace(runtime,ToolStage::Policy);
        CHECK(event && event->tool.reason==ToolReason::TransferQueued && event->tool.matchingSources==1);
        CHECK(x.requests==1 && Read<int>(&x.destination,60)==2);
    }
    std::cout<<"Tool diagnostic sampling,classification,timing,gates,matching,reset and unchanged policy passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
