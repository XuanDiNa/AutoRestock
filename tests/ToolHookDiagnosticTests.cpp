#include "../src/Plugin.cpp"
#include <iostream>
#include <stdexcept>
#include <thread>
namespace FixtureABI {
#define AUTORESTOCK_RUNTIME_FIXTURE_ONLY
#include "RuntimeTests.cpp"
#include "ActorFixture.h"
}
#undef CHECK
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);} while(false)
unsigned originals{};
void* lastInventory{};void* lastListener{};void* lastOperation{};
std::intptr_t Pass(void* inventory,void* listener,void* operation){
    ++originals;lastInventory=inventory;lastListener=listener;lastOperation=operation;
    CHECK(nativeDepth>=1);return 0x76543210;
}
unsigned serverOriginals{};
void PassServer(void* packet,void* player,void* context){CHECK(!packet && !player && context==reinterpret_cast<void*>(7));++serverOriginals;}
std::intptr_t ThrowOperation(void*,void*,void*){CHECK(nativeDepth>=1);throw std::runtime_error("original-unwind");}
int main(){try{
    { // Missing reporting throttle must not turn 1000 changes into log spam.
        HookDiagnosticReporter reporter;HookSnapshot sample;
        CHECK(reporter.Poll(sample,100));CHECK(!reporter.Poll(sample,2100));
        sample.values[0]=1;CHECK(!reporter.Poll(sample,101));CHECK(!reporter.Poll(sample,2099));
        CHECK(reporter.Poll(sample,2100));CHECK(!reporter.Poll(sample,4100));
        sample.values[0]=2;CHECK(reporter.Poll(sample,2000)); // reversal resets only diagnostic clock
        CHECK(!reporter.Poll(sample,8000));
        HookDiagnostics counters;
        std::thread writer1([&]{for(int n=0;n<10000;++n)counters.Hit(HookCounter::OperationEntry);});
        std::thread writer2([&]{for(int n=0;n<10000;++n)counters.Hit(HookCounter::OperationEntry);});
        writer1.join();writer2.join();CHECK(counters.Snapshot()[HookCounter::OperationEntry]==20000);
    }
    { // A fresh bridge must distinguish unseen/stale/mismatched scalar evidence.
        IntegratedBridge b;ActorIdentity local{0x100,42,77,ActorRole::Local};
        ActorIdentity server{0x200,42,77,ActorRole::Server};InventoryCounts counts{};counts[9]=64;
        auto e=b.Inspect(local,100);CHECK(e.localValid && !e.localStored && !e.serverSeen);
        b.Local(local,100);e=b.Inspect(server,101);
        CHECK(e.localStored && e.localFresh && e.candidateRegistryDistinct && e.candidateUniqueMatches && e.candidateRuntimeMatches);
        auto wrong=server;wrong.runtime=99;e=b.Inspect(wrong,101);CHECK(!e.candidateRuntimeMatches && e.candidateUniqueMatches);
        b.EndServer(wrong,{},1,101,counts);e=b.Inspect(local,102);CHECK(!e.serverSeen && !b.Ready(local,102));
        b.EndServer(server,{},1,103,counts);e=b.Inspect(local,104);
        CHECK(e.serverSeen && e.serverFresh && e.registryDistinct && e.uniqueMatches && e.runtimeMatches && e.epochMatches);
        CHECK(e.countsPresent && e.countsFresh && e.countsMatch && b.Ready(local,104));
        e=b.Inspect(local,604);CHECK(!e.localFresh && !e.serverFresh && !e.countsFresh && !b.Ready(local,604));
        b.EndServer(server,{},1,104);e=b.Inspect(local,105);CHECK(e.serverSeen && !e.countsPresent);
        b.Invalidate();e=b.Inspect(local,105);CHECK(!e.localStored && !e.serverSeen && !e.countsPresent);
    }
    { // Missing pair detail must be visible without authorizing a transfer.
        FixtureABI::Fixture x;FixtureABI::Prepare(x);FixtureABI::ActorFixture actor;actor.Attach(x.player);
        FixtureABI::Put(&x.destination,34,std::uint8_t{1});FixtureABI::Put(&x.destination,64,std::uint8_t{1});
        auto native=FixtureABI::Api();native.getItem=FixtureABI::AllSlots;
        IntegratedBridge bridge;
        WorldApi world{FixtureABI::Local,FixtureABI::World,FixtureABI::Input,FixtureABI::Top,
            FixtureABI::Name,FixtureABI::RealItem,FixtureABI::Damageable,FindNativeProfile(GameVersion::V52)};
        Runtime candidate{native,world,FixtureABI::base,bridge};
        CHECK(!candidate.Tick(x.manager,true,true,Config{},100,1));
        bool gate=false;
        while(auto e=candidate.TakeDiagnostic())if(e->kind==DiagnosticKind::ToolTrace && e->tool.stage==ToolStage::Gate){
            auto line=DiagnosticText(*e);
            CHECK(line.find("localValid=1 localRole=1 localStored=1 localMatches=1 localFresh=1")!=std::string::npos);
            CHECK(line.find("serverSeen=0 serverFresh=0")!=std::string::npos);
            CHECK(line.find("countsPresent=0 countsFresh=0")!=std::string::npos);gate=true;
        }
        CHECK(gate && x.requests==0 && !bridge.Pending());
    }
    FixtureABI::Fixture x;FixtureABI::Prepare(x);
    FixtureABI::Put(&x.destination,34,std::uint8_t{1});FixtureABI::Put(&x.destination,64,std::uint8_t{1});
    auto r=FixtureABI::New();runtime=&r;status.store(1);ownerThread.store(GetCurrentThreadId());
    imageBase=FixtureABI::base;worldApi.profile=FindNativeProfile(GameVersion::V52);originalOperation=Pass;
    restockManager=reinterpret_cast<std::uintptr_t>(x.manager);restockInventory.store(reinterpret_cast<std::uintptr_t>(x.inventory));
    CHECK(!r.Tick(x.manager,true,true,Config{},100,1));while(r.TakeDiagnostic()){}
    alignas(8) std::uintptr_t unknown[]={FixtureABI::base+0x25B6661,0};
    CHECK(HookOperation(x.inventory,nullptr,unknown)==0x76543210 && originals==1 && nativeDepth==0);
    CHECK(lastInventory==x.inventory && !lastListener && lastOperation==unknown);
    auto event=r.TakeDiagnostic();
    CHECK(event && event->kind==DiagnosticKind::ToolTrace && event->tool.stage==ToolStage::Operation);
    CHECK(event->tool.operationRva==0x25B6661 && !event->tool.captured && event->tool.operations==1);
    auto text=DiagnosticText(*event);
    CHECK(text.starts_with("ToolTrace") && text.find("stage=operation")!=std::string::npos);
    CHECK(text.find("operationRva=39544417")!=std::string::npos && text.find("captured=0")!=std::string::npos);
    for(unsigned n=0;n<1000;++n)CHECK(HookOperation(x.inventory,nullptr,unknown)==0x76543210);
    CHECK(originals==1001 && !r.TakeDiagnostic() && x.requests==0);
    alignas(8) std::byte other[8]{};
    CHECK(HookOperation(other,nullptr,unknown)==0x76543210 && !r.TakeDiagnostic());
    CHECK(!r.Tick(x.manager,true,true,Config{},2100,1));
    event=r.TakeDiagnostic();CHECK(event && event->tool.operations==1001);
    r.Reset(ResetCause::ConfigDisabled,2101);while(r.TakeDiagnostic()){}
    CHECK(HookOperation(x.inventory,nullptr,unknown)==0x76543210 && !r.TakeDiagnostic());
    CHECK(originals==1003 && !r.Faulted() && x.requests==0);
    auto a=hookDiagnostics.Snapshot();
    CHECK(a[HookCounter::OperationEntry]==1003 && a[HookCounter::InventoryMatch]==1002);
    CHECK(a[HookCounter::KnownPlayer]==0 && a[HookCounter::BeginAttempt]==0 && a[HookCounter::OriginalReturned]==1003);
    void* player=x.player;
    alignas(8) std::uintptr_t known[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
    CHECK(HookOperation(other,reinterpret_cast<void*>(9),known)==0x76543210);
    CHECK(lastInventory==other && lastListener==reinterpret_cast<void*>(9) && lastOperation==known);
    a=hookDiagnostics.Snapshot();CHECK(a[HookCounter::KnownPlayer]==1 && a[HookCounter::ManagerMatch]==1 && a[HookCounter::BeginAttempt]==0);
    {Depth nested;CHECK(HookOperation(nullptr,nullptr,reinterpret_cast<void*>(1))==0x76543210);}
    a=hookDiagnostics.Snapshot();CHECK(a[HookCounter::Nested]==1 && a[HookCounter::KnownPlayer]==1 && nativeDepth==0);
    ownerThread.store(0);CHECK(HookOperation(nullptr,nullptr,reinterpret_cast<void*>(1))==0x76543210);
    ownerThread.store(GetCurrentThreadId());a=hookDiagnostics.Snapshot();CHECK(a[HookCounter::ForeignOwner]==1);
    originalServerPacket=PassServer;HookServerPacket(nullptr,nullptr,reinterpret_cast<void*>(7));
    a=hookDiagnostics.Snapshot();CHECK(serverOriginals==1 && a[HookCounter::ServerEntry]==1 && a[HookCounter::ServerActive]==1);
    CHECK(a[HookCounter::ServerIdentityValid]==0 && a[HookCounter::ServerCounts]==0 && a[HookCounter::ServerLocalStored]==0);
    runtime=nullptr;
    auto const before=hookDiagnostics.Snapshot();
    status.store(0);CHECK(HookOperation(nullptr,nullptr,nullptr)==0x76543210);
    auto const after=hookDiagnostics.Snapshot();
    CHECK(after[HookCounter::OperationEntry]==before[HookCounter::OperationEntry]+1);
    CHECK(after[HookCounter::NotReady]==before[HookCounter::NotReady]+1);
    auto summary=HookDiagnosticText(after);
    CHECK(summary.starts_with("HookTrace") && summary.find("diagnosticOnly=1")!=std::string::npos);
    originalOperation=ThrowOperation;bool unwound=false;
    try{HookOperation(nullptr,nullptr,nullptr);}catch(std::runtime_error const&){unwound=true;}
    CHECK(unwound && nativeDepth==0 && hookDiagnostics.Snapshot()[HookCounter::OriginalReturned]==after[HookCounter::OriginalReturned]);
    std::cout<<"Tool hook read-only unknown-operation,throttle,formatting,context isolation and exact forwarding passed\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
