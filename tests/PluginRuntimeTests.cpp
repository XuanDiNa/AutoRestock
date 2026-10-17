#include "../src/Plugin.cpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>
static_assert(std::is_trivially_copyable_v<autorestock::Diagnostic>);
// Reuse external native ABI fixtures, not Runtime or Plugin behavior.
namespace FixtureABI {
#include "OrdinaryFixture.h"
}
#undef CHECK
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);} while(false)
using FixtureABI::f;
using FixtureABI::Put;
using FixtureABI::client;

NativePrimaryGuidRoute fixtureRoute{{31,32,33},34,35,36,77};
unsigned originals{};
bool retireDuringResponse{};
unsigned operationOriginals{};
std::intptr_t PassOperation(void*,void*,void*){++operationOriginals;return 0x76543210;}
std::intptr_t ApplyResponse(void* manager,void*){
    CHECK(manager==f->manager);++originals;
    // The plugin must publish only after the original consumes the vector.
    CHECK(!runtime->TakeDiagnostic());
    if(retireDuringResponse)ObserveRemoteRetirement(RemoteSession::Retired::Manager,reinterpret_cast<std::uintptr_t>(manager));
    return 0x12345678;
}
WorldView View(){return {client,FixtureABI::world,f->player,f->manager,f->supplies,f->inventory,2};}
std::uint64_t EstablishRemote(InventoryMode mode=InventoryMode::Modern){
    auto born=remote.Born(35,33,77);CHECK(born && remote.Bind(*born,fixtureRoute.binding,34));
    auto start=remote.Begin(fixtureRoute,mode);CHECK(start);
    CHECK(remote.Finish(*start,fixtureRoute,reinterpret_cast<std::uintptr_t>(FixtureABI::world),
        reinterpret_cast<std::uintptr_t>(f->player),reinterpret_cast<std::uintptr_t>(f->manager)));
    return start->serial;
}
#include "NativeWriterCases.h"
int main(){try{
    worldApi.profile=FindNativeProfile(GameVersion::V52);
    WriterCases::Run();
    PostStopWriterCases::Run();
    IncomingWriterCases::Run();
    FixtureABI::Fixture x;FixtureABI::Prepare(x);auto r=FixtureABI::New();runtime=&r;imageBase=FixtureABI::base;status.store(1);ownerThread.store(GetCurrentThreadId());
    originalResponse=ApplyResponse;
    CHECK(!SelectSession(View(),99)); // no remote authority and no integrated bridge
    auto generation=EstablishRemote();auto view=View();
    CHECK(SelectSession(view,100));
    CHECK(r.Tick(x.manager,true,true,Config{},100,1) && x.requests==1);
    while(r.TakeDiagnostic()){}
    auto reply=FixtureABI::DeliveredReply();std::uintptr_t vector[]={reinterpret_cast<std::uintptr_t>(reply.data()),reinterpret_cast<std::uintptr_t>(reply.data()+reply.size())};
    // Same matching native ID, but no typed-connection scope: never accepted.
    CHECK(HookResponse(x.manager,vector)==0x12345678);
    CHECK(responseMatches.load()==0 && !r.TakeDiagnostic());
    RemoteResponseFrame frame{fixtureRoute,reinterpret_cast<std::uintptr_t>(x.manager),generation,true};
    {
        RemoteScope scope(remoteResponseFrame,&frame);
        CHECK(HookResponse(x.manager,vector)==0x12345678);
    }
    CHECK(responseMatches.load()==1);
    auto event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::Response && event->nativeStatus==0);
    CHECK(!r.Tick(x.manager,true,true,Config{},101,1));
    event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::Reconciled);
    // A later retirement inside original reconciliation invalidates publication.
    Put(&x.source,34,std::uint8_t{20});Put(&x.destination,34,std::uint8_t{5});
    r.Reset();CHECK(SelectSession(view,102));CHECK(r.Tick(x.manager,true,true,Config{},102,1));
    while(r.TakeDiagnostic()){};reply=FixtureABI::DeliveredReply();Put(reply.data(),16,int{-5});
    retireDuringResponse=true;
    {
        RemoteScope scope(remoteResponseFrame,&frame);CHECK(HookResponse(x.manager,vector)==0x12345678);
    }
    CHECK(responseMatches.load()==1);
    CHECK(!SelectSession(view,103)); // canceled old fence blocks any fallback
    CHECK(!SelectSession(view,1601));
    CHECK(!r.Decode(x.manager,std::span<std::byte const>{}));
    remote.RetireConnection(35);
    // Break caught: an unrelated/unknown Inventory operation must not call
    // SelectForManager with a null/foreign player and cancel the active fence.
    {
        FixtureABI::OrdinaryFixture ordinary;ordinary.useSecond=true;FixtureABI::PrepareOrdinary(ordinary);
        ordinary.actualSlots[11]=ordinary.secondSource;
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        runtime=&ordinaryRuntime;worldApi=FixtureABI::OrdinaryWorld();originalOperation=PassOperation;
        settings.store((std::uint64_t{1}<<8)|13);
        EstablishRemote(InventoryMode::Ordinary);
        CHECK(SelectForManager(ordinary.manager,{true,true},100));
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},100,1)==TransferResult::Queued);
        alignas(8) std::uintptr_t unknown[]={FixtureABI::base+0x25B6661,0};
        void* player=ordinary.player;
        alignas(8) std::uintptr_t known[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        alignas(8) std::byte otherInventory[8]{};
        for(auto now:{101,500,1599}){
            auto before=operationOriginals;
            CHECK(HookOperation(ordinary.inventory,nullptr,unknown)==0x76543210);
            CHECK(HookOperation(ordinary.inventory,nullptr,nullptr)==0x76543210);
            CHECK(HookOperation(otherInventory,nullptr,known)==0x76543210);
            CHECK(operationOriginals==before+3 && nativeDepth==0);
            CHECK(!ordinaryRuntime.OrdinaryBlocked());
            CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},now,1) && ordinary.sends==1);
        }
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},1600,1)==TransferResult::Queued && ordinary.sends==2);
        CHECK(HookOperation(ordinary.inventory,nullptr,unknown)==0x76543210);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},3100,1) && !ordinaryRuntime.OrdinaryBlocked());
        // Repeated genuine world uses must still be tracked; no two-request cap.
        for(int count=63;count>=5;--count){
            auto capture=ordinaryRuntime.BeginUse(ordinary.inventory,known,true,true,Config{},1);CHECK(capture);
            Put(&ordinary.actualSlots[2],34,static_cast<std::uint8_t>(count));
            ordinaryRuntime.EndUse(*capture,true,true,Config{},1);
        }
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},3200,1)==TransferResult::Queued && ordinary.sends==3);
        CHECK(HookOperation(ordinary.inventory,nullptr,unknown)==0x76543210);
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},4700,1)==TransferResult::Queued && ordinary.sends==4);
        CHECK(Read<std::uint8_t>(&ordinary.actualSlots[2],34)==46);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},6200,1) && !ordinaryRuntime.OrdinaryBlocked());
        remote.RetireConnection(35);
    }
    // Characterization: a settled watch may be cleared by an inventory/UI
    // gate, then normal use in the same generation must restock again. This
    // catches accidentally blocking settled resets or retaining stale proof.
    for(int gate=0;gate<3;++gate){
        FixtureABI::OrdinaryFixture ordinary;FixtureABI::PrepareOrdinary(ordinary);
        Put(&ordinary.actualSlots[9],34,std::uint8_t{10});
        ordinary.actualSlots[10]=ordinary.source;Put(&ordinary.actualSlots[10],34,std::uint8_t{20});
        ordinary.actualSlots[11]=ordinary.source;Put(&ordinary.actualSlots[11],34,std::uint8_t{40});
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        runtime=&ordinaryRuntime;worldApi=FixtureABI::OrdinaryWorld();
        EstablishRemote(InventoryMode::Ordinary);CHECK(SelectForManager(ordinary.manager,{true,true},100));
        for(auto now:{100,1600,3100})CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},now,1)==TransferResult::Queued);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},4600,1));
        CHECK(Read<std::uint8_t>(&ordinary.actualSlots[2],34)==64 && Read<std::uint8_t>(&ordinary.actualSlots[11],34)==11);
        if(gate==2)FixtureABI::hud=false;
        for(auto now:{6100,6101,6102})CHECK(!SelectForManager(ordinary.manager,{gate!=0,gate!=1},now));
        FixtureABI::hud=true;
        CHECK(!ordinaryRuntime.OrdinaryBlocked() && SelectForManager(ordinary.manager,{true,true},6103));
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},6103,1));
        void* player=ordinary.player;
        alignas(8) std::uintptr_t operation[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        for(int count=63;count>=5;--count){
            auto capture=ordinaryRuntime.BeginUse(ordinary.inventory,operation,true,true,Config{},1);CHECK(capture);
            Put(&ordinary.actualSlots[2],34,static_cast<std::uint8_t>(count));
            ordinaryRuntime.EndUse(*capture,true,true,Config{},1);
            auto result=ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},6200+(63-count)*30,1);
            if(count==5)CHECK(result==TransferResult::Queued);else CHECK(!result);
            CHECK(!ordinaryRuntime.OrdinaryBlocked());
        }
        CHECK(ordinary.sends==4 && Read<std::uint8_t>(&ordinary.actualSlots[2],34)==16 && Read<std::uint8_t>(&ordinary.actualSlots[11],34)==0);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},9440,1));
        int requests=0,resets=0;while(auto e=ordinaryRuntime.TakeDiagnostic()){
            CHECK(e->kind!=DiagnosticKind::PredictionStopped);
            if(e->kind==DiagnosticKind::Request)++requests;
            if(e->kind==DiagnosticKind::ContextReset){
                ++resets;auto text=DiagnosticText(*e);
                CHECK(e->time==6100 && e->requestId==3 && e->submittedAt==3100 && e->elapsedMs==3000);
                CHECK(text.find("settledWatchCleared=1 sameGenerationBlocked=0")!=std::string::npos);
                CHECK(text.find("resetCause=selection-world-gate")!=std::string::npos);
                char const* names[]={"foreground","cursor-visible","not-hud"};
                CHECK(text.find(std::string("worldGate=")+names[gate])!=std::string::npos);
            }
        }CHECK(requests==4 && resets==1); // repeated idle gate failures must not spam
        remote.RetireConnection(35);
    }
    // Break caught: StopOrdinary must keep the sampled mismatch, rather than
    // replacing it with submission-time counts. Copied proof flags distinguish
    // missing callbacks, changed metadata, a broken callback chain and a clock
    // reversal without calling native getters after cancellation.
    for(int mismatch=0;mismatch<8;++mismatch){
        FixtureABI::OrdinaryFixture ordinary;FixtureABI::PrepareOrdinary(ordinary);
        if(mismatch==3)Put(&ordinary.actualSlots[9],34,std::uint8_t{64});
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        runtime=&ordinaryRuntime;worldApi=FixtureABI::OrdinaryWorld();
        EstablishRemote(InventoryMode::Ordinary);CHECK(SelectForManager(ordinary.manager,{true,true},100));
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},100,1)==TransferResult::Queued);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},1600,1));
        while(ordinaryRuntime.TakeDiagnostic()){}
        void* player=ordinary.player;
        alignas(8) std::uintptr_t operation[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        if(mismatch==0)Put(&ordinary.actualSlots[2],34,std::uint8_t{24});
        if(mismatch==1)Put(&ordinary.actualSlots[9],34,std::uint8_t{1});
        if(mismatch==2)Put(&ordinary.actualSlots[2],60,int{99});
        if(mismatch==3)Put(&ordinary.actualSlots[9],60,int{99});
        if(mismatch==4)Put(&ordinary.actualSlots[2],34,std::uint8_t{24}); // missed use before capture
        int uses=mismatch==6?2:(mismatch==4 || mismatch==5 || mismatch==7?1:0);
        for(int n=0;n<uses;++n){
            auto capture=ordinaryRuntime.BeginUse(ordinary.inventory,operation,true,true,Config{},1);CHECK(capture);
            Put(&ordinary.actualSlots[2],34,static_cast<std::uint8_t>(mismatch==4?23:24-n));
            if(mismatch==5)Put(&ordinary.actualSlots[2],60,int{99});
            ordinaryRuntime.EndUse(*capture,true,true,Config{},1);
        }
        if(mismatch==6)Put(&ordinary.actualSlots[2],34,std::uint8_t{22});
        auto now=mismatch==7?99:1601;
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},now,1));
        CHECK(ordinaryRuntime.OrdinaryBlocked() && ordinary.sends==1);
        auto e=ordinaryRuntime.TakeDiagnostic();CHECK(e && e->kind==DiagnosticKind::PredictionStopped);
        CHECK(e->inventorySampled && e->realMatches && e->time==now);
        int const sourceCounts[]={0,1,0,5,0,0,0,0},destinationCounts[]={24,25,25,64,23,24,22,24};
        CHECK(e->actualSourceCount==sourceCounts[mismatch] && e->wrapperSourceCount==sourceCounts[mismatch]);
        CHECK(e->actualDestinationCount==destinationCounts[mismatch] && e->wrapperDestinationCount==destinationCounts[mismatch]);
        auto text=DiagnosticText(*e);
        CHECK(text.find("inventorySampled=1 realMatches=1")!=std::string::npos);
        CHECK(text.find("proofSampled=1")!=std::string::npos);
        CHECK(text.find(std::string("proofChainValid=")+(mismatch==7?"1":"0"))!=std::string::npos);
        CHECK(text.find(std::string("proofExpectedSourceCount=")+(mismatch==3?"5":"0"))!=std::string::npos);
        int const expectedDest[]={25,25,25,64,25,25,23,24};
        CHECK(text.find("proofExpectedDestinationCount="+std::to_string(expectedDest[mismatch]))!=std::string::npos);
        CHECK(text.find(std::string("sourceCountMatches=")+(mismatch==1?"0":"1"))!=std::string::npos);
        CHECK(text.find(std::string("sourceItemMatches=")+(mismatch==3?"0":"1"))!=std::string::npos);
        CHECK(text.find(std::string("destinationItemMatches=")+(mismatch==2 || mismatch==5?"0":"1"))!=std::string::npos);
        CHECK(text.find(std::string("destinationCountMatches=")+(mismatch==0 || mismatch==4 || mismatch==5 || mismatch==6?"0":"1"))!=std::string::npos);
        CHECK(text.find("observedUses="+std::to_string(uses))!=std::string::npos);
        if(uses){
            CHECK(text.find(std::string("lastRequestMatches=1 lastBeforeMatches=")+(mismatch==4?"0":"1"))!=std::string::npos);
            CHECK(text.find(std::string("lastResultValid=")+(mismatch==5?"0":"1"))!=std::string::npos);
            CHECK(text.find("lastBeforeCount="+std::to_string(mismatch==6 || mismatch==4?24:25))!=std::string::npos);
            CHECK(text.find("lastAfterCount="+std::to_string(mismatch==6 || mismatch==4?23:24))!=std::string::npos);
            CHECK(text.find("lastExpectedBeforeCount="+std::to_string(mismatch==6?24:25))!=std::string::npos);
        }
        CHECK(text.find("proofConsumed="+std::to_string(mismatch==6?2:mismatch==7?1:0))!=std::string::npos);
        CHECK(!ordinaryRuntime.TakeDiagnostic());
        remote.RetireConnection(35);
    }
    // Break caught: a sampled return to the initial predicted count after two
    // genuine use captures must stop, retaining the observed native stamps and
    // both callback records. Counts restored here model an external write, not
    // proof that a live server rejected anything.
    {
        FixtureABI::OrdinaryFixture ordinary;FixtureABI::PrepareOrdinary(ordinary);
        Put(&ordinary.actualSlots[9],34,std::uint8_t{48});
        Put(&ordinary.actualSlots[2],34,std::uint8_t{54});
        Put(&ordinary.actualSlots[2],144,std::uint8_t{2});
        Put(&ordinary.actualSlots[2],128,FixtureABI::base+0xE827E30);Put(&ordinary.actualSlots[2],136,int{-208});
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        CHECK(ordinaryRuntime.SelectBackend(Backend::RemoteOrdinary,91,100));
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{true,55},100,1)==TransferResult::Queued);
        while(ordinaryRuntime.TakeDiagnostic()){}
        void* player=ordinary.player;
        alignas(8) std::uintptr_t operation[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        for(int count:{63,62}){
            auto capture=ordinaryRuntime.BeginUse(ordinary.inventory,operation,true,true,Config{true,55},1);CHECK(capture);
            Put(&ordinary.actualSlots[2],34,static_cast<std::uint8_t>(count));
            Put(&ordinary.actualSlots[2],136,count==63?int{-210}:int{-212});
            ordinaryRuntime.EndUse(*capture,true,true,Config{true,55},1);
        }
        Put(&ordinary.actualSlots[2],34,std::uint8_t{64});
        Put(&ordinary.actualSlots[2],144,std::uint8_t{0});Put(&ordinary.actualSlots[2],128,int{77});
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{true,55},256,1));
        CHECK(ordinaryRuntime.OrdinaryBlocked() && ordinary.sends==1);
        auto stopped=ordinaryRuntime.TakeDiagnostic();CHECK(stopped && stopped->kind==DiagnosticKind::PredictionStopped);
        CHECK(stopped->exactProgress && !stopped->accountedProgress && stopped->useEvidence.consumed==2);
        auto text=DiagnosticText(*stopped);
        CHECK(text.find("sampleSourceVariant=0 sampleSourceId=501 sampleSourceIdKnown=1")!=std::string::npos);
        CHECK(text.find("sampleDestinationVariant=0 sampleDestinationId=77")!=std::string::npos);
        CHECK(text.find("recentUseCount=2")!=std::string::npos);
        auto const& firstUse=stopped->useEvidence.recentUses[0];
        auto const& lastUse=stopped->useEvidence.recentUses[1];
        CHECK(firstUse.beginAt && firstUse.endAt>=firstUse.beginAt && lastUse.beginAt>=firstUse.endAt && lastUse.endAt>=lastUse.beginAt);
        CHECK(firstUse.beforeNative.idKnown && firstUse.afterNative.idKnown && lastUse.afterNative.idKnown);
        CHECK(firstUse.requestId==1 && lastUse.requestId==1);
        CHECK(text.find("invoker=direct-player before=64 after=63 beforeVariant=2 beforeId=-208 afterVariant=2 afterId=-210")!=std::string::npos);
        CHECK(text.find("invoker=direct-player before=63 after=62 beforeVariant=2 beforeId=-210 afterVariant=2 afterId=-212")!=std::string::npos);
        CHECK(text.find("serverConfirmed=0")!=std::string::npos && !ordinaryRuntime.TakeDiagnostic());
    }
    // Break caught: real eligibility loss must remain fail-closed, but the
    // diagnostic must name the first failed gate and the cancellation time,
    // not reuse the earlier request-submission timestamp.
    for(int gate=0;gate<6;++gate){
        FixtureABI::OrdinaryFixture ordinary;FixtureABI::PrepareOrdinary(ordinary);
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        runtime=&ordinaryRuntime;worldApi=FixtureABI::OrdinaryWorld();
        EstablishRemote(InventoryMode::Ordinary);CHECK(SelectForManager(ordinary.manager,{true,true},100));
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},100,1)==TransferResult::Queued);
        while(ordinaryRuntime.TakeDiagnostic()){}
        if(gate==2)FixtureABI::input=false;
        if(gate==3)FixtureABI::hud=false;
        if(gate==4)Put(client,0,std::uintptr_t{});
        if(gate==5)Put(ordinary.supplies,16,int{9});
        CHECK(!SelectForManager(ordinary.manager,{gate!=0,gate!=1},101));
        CHECK(ordinaryRuntime.OrdinaryBlocked() && ordinary.sends==1);
        auto gateEvent=ordinaryRuntime.TakeDiagnostic();CHECK(gateEvent && gateEvent->kind==DiagnosticKind::PredictionStopped);
        auto text=DiagnosticText(*gateEvent);
        CHECK(text.find("resetCause=selection-world-gate")!=std::string::npos);
        char const* names[]={"foreground","cursor-visible","world-input","not-hud","client-table","selected-slot"};
        CHECK(text.find(std::string("worldGate=")+names[gate])!=std::string::npos);
        CHECK(gateEvent->time==101 && text.find("submittedAt=100 elapsedMs=1")!=std::string::npos);
        CHECK(text.find("reconnectRequired=0 automaticRecovery=1")!=std::string::npos && text.find("serverConfirmed=0")!=std::string::npos);
        CHECK(!gateEvent->inventorySampled && gateEvent->actualSourceCount==-1 && gateEvent->actualDestinationCount==-1);
        CHECK(text.find("proofSampled=0")!=std::string::npos); // no fabricated sample after a failed world gate
        remote.RetireConnection(35);
    }
    {
        FixtureABI::OrdinaryFixture ordinary;FixtureABI::PrepareOrdinary(ordinary);
        Runtime ordinaryRuntime(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);
        runtime=&ordinaryRuntime;worldApi=FixtureABI::OrdinaryWorld();
        CHECK(!SelectForManager(ordinary.manager,{true,true},1700)); // mode0 alone grants no remote authority
        EstablishRemote(InventoryMode::Ordinary);
        CHECK(SelectForManager(ordinary.manager,{true,true},1701) && sessionState.load()==6);
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},1701,1)==TransferResult::Queued && ordinary.sends==1);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},3201,1));
        int settled=0;while(auto e=ordinaryRuntime.TakeDiagnostic()){
            auto text=DiagnosticText(*e);CHECK(text.find("backend=remote-ordinary")!=std::string::npos);
            CHECK(text.find("serverConfirmed=0")!=std::string::npos && text.find("accepted")==std::string::npos);
            if(e->kind==DiagnosticKind::PredictionSettled){++settled;CHECK(text.starts_with("PredictionSettled"));}
        }CHECK(settled==1);
        ordinary.actualSlots[9]=ordinary.source;ordinary.actualSlots[2]=ordinary.destination;
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},3202,1));
        CHECK(!SelectForManager(ordinary.manager,{true,true},3203) && sessionState.load()==7);
        auto stopped=ordinaryRuntime.TakeDiagnostic();CHECK(stopped && DiagnosticText(*stopped).starts_with("PredictionStopped"));
        CHECK(!SelectForManager(ordinary.manager,{true,true},4701) && ordinary.sends==1);
        CHECK(SelectForManager(ordinary.manager,{true,true},4702) && sessionState.load()==6);
        CHECK(!ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},4702,1) && ordinary.sends==1);
        CHECK(ordinaryRuntime.Tick(ordinary.manager,true,true,Config{},4752,1)==TransferResult::Queued && ordinary.sends==2);
        bool rearmed=false;while(auto next=ordinaryRuntime.TakeDiagnostic()){
            auto text=DiagnosticText(*next);CHECK(text.find("serverConfirmed=0")!=std::string::npos);
            if(next->kind==DiagnosticKind::PredictionRearmed){
                CHECK(text.starts_with("PredictionRearmed") && text.find("freshSnapshotRequired=1")!=std::string::npos);rearmed=true;
            }
        }CHECK(rearmed);
        remote.RetireManager(reinterpret_cast<std::uintptr_t>(ordinary.manager));
        CHECK(!SelectForManager(ordinary.manager,{true,true},4760) && ordinary.sends==2);
    }
    runtime=nullptr;
    std::cout<<"PASS Plugin backend/response authority, callback isolation, settled UI reset/reuse, preserved mismatch proofs, bounded native-use/writer provenance, writer lifetime/thread/config/request isolation, exact gate/time diagnostics and retirement fence\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
