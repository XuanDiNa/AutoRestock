#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "AutoRestock/ConfigReload.h"
#include "AutoRestock/Runtime.h"
#include "AutoRestock/ValidationDiagnostic.h"
#include "AutoRestock/RemoteSession.h"
#include "AutoRestock/RemoteAdmissionDiagnostic.h"
#include "AutoRestock/HookDiagnostic.h"
#include <intrin.h>
#include <exception>
#include <MinHook.h>
#include <atomic>
#include <fstream>
#include <memory>
#include <limits>
#include <string>
namespace {
using namespace autorestock;
using FlushFunction=std::intptr_t(*)(void*);
using ResponseFunction=std::intptr_t(*)(void*,void*);
using OperationFunction=std::intptr_t(*)(void*,void*,void*);
using WorldDestroyFunction=std::intptr_t(*)(void*,void*,std::uint8_t);
using ToolHurtFunction=std::intptr_t(*)(void*,int,void*);
using ServerPacketFunction=void(*)(void*,void*,void*);
using NormalFunction=std::uint32_t(*)(void*,void*,std::uint32_t);
using VerifySlotFunction=std::uint32_t(*)(void*,void*,void const*,std::uint8_t const*);
using NativeCompareFunction=std::uint32_t(*)(void const*,void const*);
using InventorySetFunction=void(*)(void*,std::uint32_t,void const*,std::uint32_t);
FlushFunction originalFlush{};
ResponseFunction originalResponse{};
OperationFunction originalOperation{};
WorldDestroyFunction originalWorldDestroy{};
ToolHurtFunction originalToolHurt{};
ServerPacketFunction originalServerPacket{};
NormalFunction originalNormal{};
VerifySlotFunction originalVerifySlot{};
NativeCompareFunction originalNativeCompare{};
InventorySetFunction originalInventorySet{};
ServerValidationQueue serverValidation;
IntegratedBridge integrated;
void HookServerPacket(void* packet,void* player,void* context);
std::uint32_t HookNormal(void* transaction,void* player,std::uint32_t authority);
std::atomic<int> status{}; // starting0, ready1, inactive/fault-1
std::atomic<bool> started{};
std::atomic<DWORD> ownerThread{};
std::atomic<std::uint64_t> settings{13}; // enabled1 | threshold6<<1; revision in upper bits
std::atomic<std::uint64_t> queued{},rejected{},faults{};
std::atomic<std::uint64_t> responseCalls{},responseOwnerCalls{},responseMatches{};
std::atomic<std::uint64_t> serverPackets{},pairedPackets{},normalResults{};
std::atomic<std::uint64_t> serverInventorySamples{};
HookDiagnostics hookDiagnostics;
HINSTANCE module{};
Runtime* runtime{}; // pinned for process lifetime; never destroy native copies during DLL detach
WorldApi worldApi{};
std::uintptr_t imageBase{};
std::uintptr_t GameAddress(std::uint32_t key) noexcept {
    auto rva=worldApi.profile?worldApi.profile->Rva(key):0;
    return imageBase && rva?imageBase+rva:0;
}
NativeLayout const& Layout() noexcept {return worldApi.profile->layout;}
char const* ProfileName(GameVersion version) noexcept {
    switch(version){
    case GameVersion::V45:return "1.26.45.01";
    case GameVersion::V51:return "1.26.51.01";
    case GameVersion::V52:return "1.26.52.03";
    }
    return "unknown";
}
bool EntriesMatch() noexcept {
    if(!worldApi.profile || FindNativeProfile(worldApi.profile->version)!=worldApi.profile || !imageBase)return false;
    __try {for(auto const& g:worldApi.profile->guards)if(!MatchGuard({reinterpret_cast<std::uint8_t const*>(imageBase+g.rva),32},g))return false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::filesystem::path directory;
thread_local unsigned nativeDepth{};
struct Depth {Depth(){++nativeDepth;}~Depth(){--nativeDepth;}};
struct Focus {bool foreground{},hidden{};};
Focus CurrentFocus() noexcept {
    auto window=GetForegroundWindow();DWORD process{};
    if(window) GetWindowThreadProcessId(window,&process);
    CURSORINFO cursor{sizeof cursor};
    return {window && process==GetCurrentProcessId() && !IsIconic(window),
        GetCursorInfo(&cursor) && !(cursor.flags&CURSOR_SHOWING)};
}
template<class T> T Read(void const* p,std::size_t offset) noexcept {
    if(!p) return {};
    __try{T v;std::memcpy(&v,static_cast<std::byte const*>(p)+offset,sizeof v);return v;}
    __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
#include "RemoteHooks.inl"
struct ServerFrame {
    void* transaction{};void* player{};int result=-1;bool handling{};
    ServerTicket ticket;
    unsigned validationEvents{};
};
thread_local ServerFrame* currentServerFrame{};
struct ValidationFrame {
    ServerFrame* server{};void const* expected{};ServerValidationEvent* event{};bool comparing{};
};
thread_local ValidationFrame* currentValidationFrame{};
StackObservation ObserveStack(void const* stack) noexcept {
    StackObservation result;
    // The complete stack152, weak item+8 -> item+170, block+24 -> runtime+276
    // and native variant layouts are proven in the exported original functions.
    // A failed read is explicit; zero must not masquerade as an empty stack.
    __try {
        if(!stack)return result;
        auto p=static_cast<std::byte const*>(stack);
        result.count=std::to_integer<std::uint8_t>(p[34]);
        result.valid=std::to_integer<std::uint8_t>(p[35]);
        result.aux=*reinterpret_cast<std::int16_t const*>(p+32);
        result.variant=std::to_integer<std::uint8_t>(p[144]);
        if(result.variant<=2)result.networkId=*reinterpret_cast<std::int32_t const*>(p+(result.variant?136:128));
        auto handle=*reinterpret_cast<void const* const*>(p+8);
        auto item=handle?*static_cast<std::byte const* const*>(handle):nullptr;
        result.hasItem=item!=nullptr;
        if(item)result.itemId=*reinterpret_cast<std::int16_t const*>(item+170);
        auto block=*reinterpret_cast<std::byte const* const*>(p+24);
        result.hasBlock=block!=nullptr;
        if(block)result.blockId=*reinterpret_cast<std::int32_t const*>(block+276);
        result.hasNbt=*reinterpret_cast<void const* const*>(p+16)!=nullptr;
        result.hasNested=*reinterpret_cast<void const* const*>(p+120)!=nullptr;
        result.readable=true;
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return result;
}
std::uint32_t HookNativeCompare(void const* actual,void const* expected){
    auto frame=currentValidationFrame;
    bool capture=frame && frame->server==currentServerFrame && currentServerFrame &&
        currentServerFrame->handling && !frame->comparing && frame->expected==expected;
    if(capture){
        frame->comparing=true;
        auto& event=*frame->event;event.actual=ObserveStack(actual);event.expected=ObserveStack(expected);
        // Equality-only bits for native comparison inputs. Addresses and tag
        // contents never leave this callback. The native return is authoritative.
        if(event.actual.readable && event.expected.readable){
            event.differences=(event.actual.count!=event.expected.count?1u:0u)|
                (event.actual.itemId!=event.expected.itemId?2u:0u)|
                (event.actual.aux!=event.expected.aux?4u:0u)|
                (Read<void const*>(actual,24)!=Read<void const*>(expected,24)?8u:0u)|
                (event.actual.hasNbt!=event.expected.hasNbt?16u:0u)|
                (Read<std::uint64_t>(actual,72)!=Read<std::uint64_t>(expected,72)?32u:0u)|
                (Read<std::uint64_t>(actual,104)!=Read<std::uint64_t>(expected,104)?64u:0u)|
                (Read<std::uint64_t>(actual,112)!=Read<std::uint64_t>(expected,112)?128u:0u)|
                (event.actual.hasNested!=event.expected.hasNested?256u:0u);
        }
    }
    auto result=originalNativeCompare(actual,expected); // exactly one original call; no extra comparison
    if(capture){frame->event->comparison=static_cast<int>(result);frame->comparing=false;}
    return result;
}
std::uint32_t HookVerifySlot(void* functor,void* player,void const* action,std::uint8_t const* authority){
    auto frame=currentServerFrame;
    bool capture=status.load(std::memory_order_acquire)==1 && runtime && frame && frame->ticket.token &&
        frame->handling && frame->player==player && authority && Read<std::uint8_t>(authority,0)==0 &&
        action && Read<std::uint32_t>(action,0)==0 && Read<std::uint8_t>(action,4)==0 && frame->validationEvents<16;
    if(!capture)return originalVerifySlot(functor,player,action,authority);
    ServerValidationEvent event;event.ticket=frame->ticket;event.time=GetTickCount64();
    event.sourceType=Read<std::uint32_t>(action,0);event.container=Read<std::uint8_t>(action,4);
    event.slot=Read<int>(action,12);event.beforeDescriptorCount=Read<std::uint16_t>(action,32);
    event.afterDescriptorCount=Read<std::uint16_t>(action,128);event.afterCount=Read<std::uint8_t>(action,394);
    event.descriptorBlockId=Read<std::int32_t>(action,72);event.userdataSize=Read<std::uint64_t>(action,96);
    ValidationFrame validation{frame,static_cast<std::byte const*>(action)+208,&event};
    auto previous=currentValidationFrame;currentValidationFrame=&validation;
    struct Restore {ValidationFrame* p;~Restore(){currentValidationFrame=p;}} restore{previous};
    auto result=originalVerifySlot(functor,player,action,authority); // original verifies/corrects normally
    event.status=result;++frame->validationEvents;serverValidation.Push(event);
    return result;
}
void HookServerPacket(void* packet,void* player,void* context){
    hookDiagnostics.Hit(HookCounter::ServerEntry);
    serverPackets.fetch_add(1,std::memory_order_relaxed);
    bool observe=status.load(std::memory_order_acquire)==1 && runtime && worldApi.profile;
    auto identity=observe?ReadActorIdentity(player):ActorIdentity{};
    if(observe){
        hookDiagnostics.Hit(HookCounter::ServerActive);
        if(identity.Valid())hookDiagnostics.Hit(HookCounter::ServerIdentityValid);
        if(identity.role==ActorRole::Local)hookDiagnostics.Hit(HookCounter::ServerRoleLocal);
        if(identity.role==ActorRole::Server)hookDiagnostics.Hit(HookCounter::ServerRoleServer);
    }
    auto transaction=worldApi.profile?Read<void*>(packet,Layout().packetComplex):nullptr;
    auto ticket=observe?integrated.BeginServer(identity,Read<std::int32_t>(packet,Layout().packetRequest),GetTickCount64()):ServerTicket{};
    ServerFrame frame{transaction,player,-1,false,ticket};auto previous=currentServerFrame;currentServerFrame=&frame;
    struct Restore {ServerFrame* p;~Restore(){currentServerFrame=p;}} restore{previous};
    originalServerPacket(packet,player,context); // native validation/apply/correction unchanged
    if(observe){
        // Read on the server thread AFTER native validation/apply/correction.
        // Ordinary packets can overtake queued world-use; pairing alone is
        // not evidence that the previous predicted count was applied.
        auto counts=ReadInventoryCounts(*worldApi.profile,player,imageBase);
        if(counts)hookDiagnostics.Hit(HookCounter::ServerCounts);
        auto evidence=integrated.Inspect(identity,GetTickCount64());
        if(evidence.localStored)hookDiagnostics.Hit(HookCounter::ServerLocalStored);
        if(evidence.localFresh)hookDiagnostics.Hit(HookCounter::ServerLocalFresh);
        if(evidence.candidateRegistryDistinct)hookDiagnostics.Hit(HookCounter::ServerRegistryDistinct);
        if(evidence.candidateUniqueMatches)hookDiagnostics.Hit(HookCounter::ServerUniqueMatch);
        if(evidence.candidateRuntimeMatches)hookDiagnostics.Hit(HookCounter::ServerRuntimeMatch);
        if(counts)serverInventorySamples.fetch_add(1,std::memory_order_relaxed);
        integrated.EndServer(identity,ticket,frame.result,GetTickCount64(),counts);
        if(ticket.token)pairedPackets.fetch_add(1,std::memory_order_relaxed);
    }
}
std::uint32_t HookNormal(void* transaction,void* player,std::uint32_t authority){
    auto frame=currentServerFrame;
    bool capture=status.load(std::memory_order_acquire)==1 && runtime && frame && !frame->handling &&
        transaction==frame->transaction && player==frame->player && authority==0 &&
        Read<std::uintptr_t>(transaction,0)==GameAddress(0xE6D6C00) && Read<int>(transaction,8)==0;
    if(capture)frame->handling=true;
    auto result=originalNormal(transaction,player,authority);
    if(capture){frame->result=static_cast<int>(result);frame->handling=false;normalResults.fetch_add(1,std::memory_order_relaxed);}
    return result;
}
Config DecodeSettings(std::uint64_t value){return {bool(value&1),int((value>>1)&127)};}
void Fail() noexcept {faults.fetch_add(1,std::memory_order_relaxed);status.store(-1,std::memory_order_release);}
bool OnOwner() noexcept {return ownerThread.load(std::memory_order_acquire)==GetCurrentThreadId();}
SessionKey integratedContext;
std::uint64_t integratedGeneration{};
std::atomic<int> sessionState{}; // 0 HUD,1 integrated,2 modern,3 unsupported,4 fault,5 fence,6 ordinary,7 ordinary stopped
std::atomic<std::uint64_t> sessionGeneration{};
bool SelectSession(WorldView const& view,std::uint64_t now){
    restockManager=reinterpret_cast<std::uintptr_t>(view.manager);
    restockInventory.store(reinterpret_cast<std::uintptr_t>(view.inventory),std::memory_order_release);
    auto selection=remote.Select(reinterpret_cast<std::uintptr_t>(view.manager),
        reinterpret_cast<std::uintptr_t>(view.player),reinterpret_cast<std::uintptr_t>(view.world));
    auto backend=selection.backend;auto generation=selection.generation;
    int state=backend==Backend::RemoteModern?2:backend==Backend::RemoteOrdinary?6:3;
    if(generation && Read<std::uint8_t>(view.manager,Layout().managerMode)!=(selection.mode==InventoryMode::Modern?1:0)){
        backend=Backend::Unsupported;state=3;
    }
    if(remote.Faulted()){backend=Backend::Unsupported;state=4;}
    else if(!generation && Read<std::uint8_t>(view.manager,Layout().managerMode)==1){
        // This candidate NEVER authorizes a remote transaction. Runtime still
        // requires its exact in-process server pairing and copied counts.
        SessionKey key{0,reinterpret_cast<std::uintptr_t>(view.world),
            reinterpret_cast<std::uintptr_t>(view.player),reinterpret_cast<std::uintptr_t>(view.manager)};
        if(key!=integratedContext){
            if(integratedGeneration==std::numeric_limits<std::uint64_t>::max()){Fail();return false;}
            ++integratedGeneration;integratedContext=key;
        }
        backend=Backend::Integrated;generation=integratedGeneration;state=1;
    }
    auto enabled=runtime->SelectBackend(backend,generation,now);
    sessionGeneration.store(generation,std::memory_order_relaxed);
    sessionState.store(enabled?state:backend==Backend::RemoteOrdinary && runtime->OrdinaryBlocked()?7:
        (backend==Backend::Unsupported?state:5),std::memory_order_release);
    return enabled;
}
bool SelectForManager(void* manager,Focus focus,std::uint64_t now){
    auto flag=Read<std::uint8_t>(manager,Layout().managerMode);
    auto mode=flag==1?InventoryMode::Modern:flag==0?InventoryMode::Ordinary:InventoryMode::Unknown;
    WorldFailure failure{};
    auto view=ResolveWorld(worldApi,imageBase,manager,Read<void*>(manager,Layout().managerOwner),focus.foreground,focus.hidden,mode,&failure);
    if(!view){runtime->Reset(ResetCause::SelectionWorldGate,now,failure);sessionState.store(0,std::memory_order_release);return false;}
    return SelectSession(*view,now);
}
void Tick(void* manager){
    // Acquire the worker's publication before reading its immutable runtime
    // pointer or native bindings; hooks can run while installation finishes.
    if(status.load(std::memory_order_acquire)!=1 || !runtime || nativeDepth) return;
    if(!RemoteOwner()) {remote.Foreign(RemoteSession::Retired::Manager,reinterpret_cast<std::uintptr_t>(manager));return;}
    auto focus=CurrentFocus();auto now=GetTickCount64();
    Depth depth;
    auto selected=SelectForManager(manager,focus,now);
    auto value=settings.load(std::memory_order_acquire);
    if(!selected){runtime->PollStoppedWriters(DecodeSettings(value),value>>8,now);return;}
    auto result=runtime->Tick(manager,focus.foreground,focus.hidden,DecodeSettings(value),now,value>>8);
    if(result){
        if(*result==TransferResult::Queued) queued.fetch_add(1,std::memory_order_relaxed);
        else if(*result==TransferResult::Fault) Fail();
        else rejected.fetch_add(1,std::memory_order_relaxed);
    }
}
std::intptr_t HookFlush(void* manager){
    try{Tick(manager);}catch(...){Fail();}
    return originalFlush(manager); // always preserve exactly one original call and its return
}
std::optional<RequestCompletion> DecodeResponse(void* manager,void* vector) noexcept {
    __try{
        auto first=Read<std::uintptr_t>(vector,0),last=Read<std::uintptr_t>(vector,8);
        if(!first || last<first || (last-first)%48 || last-first>4096*48) return {};
        return runtime->Decode(manager,{reinterpret_cast<std::byte const*>(first),last-first});
    }__except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::intptr_t HookResponse(void* manager,void* vector){
    responseCalls.fetch_add(1,std::memory_order_relaxed);
    std::optional<RequestCompletion> completion;
    auto frame=remoteResponseFrame;
    auto authorized=[&]{return frame && frame->valid && frame->manager==reinterpret_cast<std::uintptr_t>(manager) &&
        remote.Authorize(frame->route,frame->manager,frame->generation);};
    bool eligible=status.load(std::memory_order_acquire)==1 && runtime && OnOwner() && !nativeDepth && authorized();
    if(!OnOwner())remote.Foreign(RemoteSession::Retired::Manager,reinterpret_cast<std::uintptr_t>(manager));
    if(eligible){responseOwnerCalls.fetch_add(1,std::memory_order_relaxed);completion=DecodeResponse(manager,vector);}
    auto result=originalResponse(manager,vector); // original consumes/reconciles the response
    if(completion && status.load(std::memory_order_acquire)==1 && authorized()){
        responseMatches.fetch_add(1,std::memory_order_relaxed);runtime->Publish(*completion);
    } // never publish prediction as server acceptance
    return result;
}
std::intptr_t HookOperation(void* inventory,void* listener,void* operation){
    hookDiagnostics.Hit(HookCounter::OperationEntry);
    std::optional<UseCapture> capture;
    bool available=status.load(std::memory_order_acquire)==1 && runtime;
    bool owner=available && RemoteOwner();
    bool observe=owner && !nativeDepth;
    if(!available)hookDiagnostics.Hit(HookCounter::NotReady);
    else if(!owner)hookDiagnostics.Hit(HookCounter::ForeignOwner);
    else if(nativeDepth)hookDiagnostics.Hit(HookCounter::Nested);
    if(!OnOwner() && inventory && restockInventory.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(inventory))remote.Poison();
    auto lifetime=observe?nativeLifetimeEpoch:0;
    auto value=settings.load(std::memory_order_acquire);
    if(observe){
        try{
            auto player=worldApi.profile?UsePlayer(*worldApi.profile,imageBase,operation):nullptr;
            auto manager=Read<void*>(player,Layout().managerPlayer);
            if(player)hookDiagnostics.Hit(HookCounter::KnownPlayer);
            if(manager && reinterpret_cast<std::uintptr_t>(manager)==restockManager)hookDiagnostics.Hit(HookCounter::ManagerMatch);
            if(inventory && reinterpret_cast<std::uintptr_t>(inventory)==restockInventory.load(std::memory_order_acquire))
                hookDiagnostics.Hit(HookCounter::InventoryMatch);
            if(remote.Faulted())hookDiagnostics.Hit(HookCounter::RemoteFault);
            // This hook sees every Inventory transaction-context callback,
            // not just local world use. Unknown operations or other inventories
            // must never select/reset the shared restock session. BeginUse
            // independently verifies the current player/world/HUD and closure.
            // Actual lifetime changes are still handled by the lifecycle hooks
            // and Tick; unmatched item mutations still fail exact reconciliation.
            if(player && manager && reinterpret_cast<std::uintptr_t>(manager)==restockManager &&
                inventory && reinterpret_cast<std::uintptr_t>(inventory)==restockInventory.load(std::memory_order_acquire) &&
                !remote.Faulted()){
                auto focus=CurrentFocus();
                hookDiagnostics.Hit(HookCounter::BeginAttempt);
                if(!(value&1))hookDiagnostics.Hit(HookCounter::ConfigDisabled);
                if(!focus.foreground)hookDiagnostics.Hit(HookCounter::ForegroundFalse);
                if(!focus.hidden)hookDiagnostics.Hit(HookCounter::CursorVisible);
                capture=runtime->BeginUse(inventory,operation,focus.foreground,focus.hidden,DecodeSettings(value),value>>8);
                hookDiagnostics.Hit(capture?HookCounter::Captured:HookCounter::NotCaptured);
            }
            if(inventory && reinterpret_cast<std::uintptr_t>(inventory)==restockInventory.load(std::memory_order_acquire) && !remote.Faulted())
                runtime->ObserveToolOperation(inventory,operation,capture.has_value(),GetTickCount64());
        }catch(...){Fail();}
    }
    std::intptr_t result{};
    {
        Depth depth;
        result=originalOperation(inventory,listener,operation); // borrowed callback/listener ownership untouched
    }
    hookDiagnostics.Hit(HookCounter::OriginalReturned);
    if(capture && status.load(std::memory_order_acquire)==1 && lifetime==nativeLifetimeEpoch && !remote.Faulted()){
        try{
            auto focus=CurrentFocus();value=settings.load(std::memory_order_acquire);
            hookDiagnostics.Hit(HookCounter::EndCalled);
            runtime->EndUse(*capture,focus.foreground,focus.hidden,DecodeSettings(value),value>>8);
        }catch(...){Fail();}
    }
    return result;
}
std::intptr_t HookWorldDestroy(void* gameMode,void* position,std::uint8_t face){
    hookDiagnostics.Hit(HookCounter::DestroyEntry);
    bool available=status.load(std::memory_order_acquire)==1 && runtime && worldApi.profile;
    auto player=available?Read<void*>(gameMode,Layout().gameModePlayer):nullptr;
    auto identity=available?ReadActorIdentity(player):ActorIdentity{};
    bool server=identity.role==ActorRole::Server;
    if(server)hookDiagnostics.Hit(HookCounter::DestroyServer);
    std::optional<UseCapture> capture;
    std::uint64_t lifetime{}; // owner-thread state; never read from the server callback
    if(available && identity.role==ActorRole::Local && RemoteOwner() && !nativeDepth && !remote.Faulted() &&
        reinterpret_cast<std::uintptr_t>(Read<void*>(player,Layout().managerPlayer))==restockManager &&
        reinterpret_cast<std::uintptr_t>(Read<void*>(Read<void*>(player,Layout().playerSupplies),Layout().suppliesInventory))==
            restockInventory.load(std::memory_order_acquire)){
        hookDiagnostics.Hit(HookCounter::DestroyLocal);
        lifetime=nativeLifetimeEpoch;
        try{
            auto focus=CurrentFocus();auto value=settings.load(std::memory_order_acquire);
            capture=runtime->BeginDestroy(gameMode,focus.foreground,focus.hidden,DecodeSettings(value),value>>8);
            if(capture)hookDiagnostics.Hit(HookCounter::DestroyCaptured);
        }catch(...){Fail();}
    }
    std::intptr_t result{};
    {Depth depth;result=originalWorldDestroy(gameMode,position,face);}
    // No result or borrowed inventory escapes a throwing original.
    if(status.load(std::memory_order_acquire)==1 && available){
        if(server && ReadActorIdentity(player)==identity){
            // A post-original server snapshot refreshes the existing pairing
            // and count gate only. No packet/ticket means NO acceptance result.
            auto counts=ReadInventoryCounts(*worldApi.profile,player,imageBase);
            if(counts)hookDiagnostics.Hit(HookCounter::DestroyServerCounts);
            integrated.EndServer(identity,{},-1,GetTickCount64(),counts);
        }
        if(capture && static_cast<std::uint8_t>(result) && lifetime==nativeLifetimeEpoch && !remote.Faulted()){
            try{
                auto focus=CurrentFocus();auto value=settings.load(std::memory_order_acquire);
                runtime->EndUse(*capture,focus.foreground,focus.hidden,DecodeSettings(value),value>>8);
                hookDiagnostics.Hit(HookCounter::DestroyEnd);
            }catch(...){Fail();}
        }
    }
    return result;
}
std::intptr_t HookToolHurt(void* stack,int amount,void* player){
    hookDiagnostics.Hit(HookCounter::HurtEntry);
    bool available=status.load(std::memory_order_acquire)==1 && runtime && worldApi.profile;
    auto identity=available?ReadActorIdentity(player):ActorIdentity{};
    if(identity.role==ActorRole::Server)hookDiagnostics.Hit(HookCounter::HurtServer);
    std::optional<UseCapture> capture;
    std::uint64_t lifetime{};
    // May be nested inside native destroy/transaction work. No submission or
    // request/consumption ledger changes take place inside this observer.
    if(available && amount>0 && identity.role==ActorRole::Local && RemoteOwner() && !remote.Faulted() &&
        reinterpret_cast<std::uintptr_t>(Read<void*>(player,Layout().managerPlayer))==restockManager &&
        reinterpret_cast<std::uintptr_t>(Read<void*>(Read<void*>(player,Layout().playerSupplies),Layout().suppliesInventory))==
            restockInventory.load(std::memory_order_acquire)){
        hookDiagnostics.Hit(HookCounter::HurtLocal);lifetime=nativeLifetimeEpoch;
        try{auto focus=CurrentFocus();auto value=settings.load(std::memory_order_acquire);
            capture=runtime->BeginToolHurt(stack,player,focus.foreground,focus.hidden,DecodeSettings(value),value>>8);
            if(capture)hookDiagnostics.Hit(HookCounter::HurtCaptured);
        }catch(...){Fail();}
    }
    std::intptr_t result{};{Depth depth;result=originalToolHurt(stack,amount,player);}
    if(capture && static_cast<std::uint8_t>(result) && status.load(std::memory_order_acquire)==1 &&
        lifetime==nativeLifetimeEpoch && !remote.Faulted()){
        try{auto focus=CurrentFocus();auto value=settings.load(std::memory_order_acquire);
            if(runtime->EndToolHurt(*capture,stack,focus.foreground,focus.hidden,DecodeSettings(value),value>>8))
                hookDiagnostics.Hit(HookCounter::HurtBreak);
        }catch(...){Fail();}
    }
    return result;
}
WriteStackState ObserveWriteStack(void const* stack) noexcept{
    WriteStackState result;
    __try{
        if(!stack)return result;
        auto p=static_cast<std::byte const*>(stack);
        result.count=std::to_integer<std::uint8_t>(p[34]);
        result.native.variant=std::to_integer<std::uint8_t>(p[144]);
        result.native.idKnown=result.native.variant<=2;
        if(result.native.idKnown)result.native.id=*reinterpret_cast<std::int32_t const*>(p+(result.native.variant?136:128));
        result.readable=true;
    }__except(EXCEPTION_EXECUTE_HANDLER){result={};}
    return result;
}
NativeWrite WriterOrigin(std::uintptr_t caller) noexcept{
    NativeWrite result;
    // Both accepted exact image profiles share this original image range.
    // This labels an observation only; an RVA never grants native authority.
    if(worldApi.profile && imageBase && caller>=imageBase && caller-imageBase<worldApi.profile->imageExtent){
        result.callerKnown=true;result.callerRva=static_cast<std::uint32_t>(caller-imageBase);
    }
    return result;
}
void HookInventorySet(void* inventory,std::uint32_t slot,void const* stack,std::uint32_t network){
    auto value=settings.load(std::memory_order_acquire);
    bool observe=status.load(std::memory_order_acquire)==1 && runtime && OnOwner() && !remote.Faulted() &&
        (value&1) && slot<36 && inventory &&
        reinterpret_cast<std::uintptr_t>(inventory)==restockInventory.load(std::memory_order_acquire) && worldApi.inventoryItem;
    auto now=observe?GetTickCount64():0;
    auto request=observe?runtime->NativeWriterRequest(inventory,static_cast<int>(slot),value>>8,now):0;
    auto lifetime=request?nativeLifetimeEpoch:0;
    NativeWrite write;
    if(request){
        write=WriterOrigin(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        write.requestId=request;write.slot=static_cast<int>(slot);write.network=network;write.nativeDepth=nativeDepth;
        write.beginAt=now;
        write.before=ObserveWriteStack(worldApi.inventoryItem(inventory,write.slot));write.input=ObserveWriteStack(stack);
    }
    // Preserve every original argument and its one call, including unwinding.
    originalInventorySet(inventory,slot,stack,network);
    if(request && status.load(std::memory_order_acquire)==1 && lifetime==nativeLifetimeEpoch && !remote.Faulted() &&
        settings.load(std::memory_order_acquire)==value &&
        runtime->NativeWriterRequest(inventory,write.slot,value>>8,GetTickCount64())==request){
        write.after=ObserveWriteStack(worldApi.inventoryItem(inventory,write.slot));write.endAt=GetTickCount64();
        runtime->ObserveNativeWrite(inventory,value>>8,write);
    }
}
bool InstallHooks(){
    if(!EntriesMatch())return false;
    struct Hook {std::uintptr_t rva;void* replacement;void** original;};
#define RESTOCK_HOOK(rva,name,original) Hook{rva,reinterpret_cast<void*>(name),reinterpret_cast<void**>(&original)}
    Hook hooks[]={
        RESTOCK_HOOK(0x25B1D10,HookFlush,originalFlush),
        RESTOCK_HOOK(0x25B23F0,HookResponse,originalResponse),
        RESTOCK_HOOK(0x26AFFB0,HookOperation,originalOperation),
        RESTOCK_HOOK(0x259ABB0,HookWorldDestroy,originalWorldDestroy),
        RESTOCK_HOOK(0x2488FE0,HookToolHurt,originalToolHurt),
        RESTOCK_HOOK(0x1D5170,HookInventorySet,originalInventorySet),
        RESTOCK_HOOK(0x8B2D490,HookServerPacket,originalServerPacket),
        RESTOCK_HOOK(0x266C180,HookNormal,originalNormal),
        RESTOCK_HOOK(0x268F860,HookVerifySlot,originalVerifySlot),
        RESTOCK_HOOK(0x266FAD0,HookNativeCompare,originalNativeCompare),
        RESTOCK_HOOK(0x16CA370,HookRemoteReceive,originalRemoteReceive),
        RESTOCK_HOOK(0x16AF460,HookConnected,originalConnected),
        RESTOCK_HOOK(0xD92690,HookConnectionConstruct,originalConnectionConstruct),
        RESTOCK_HOOK(0x132E2B0,HookHandlerConnected,originalHandlerConnected),
        RESTOCK_HOOK(0x16AADF0,HookEventPump,originalEventPump),
        RESTOCK_HOOK(0x25AA010,HookManagerConstruct,originalManagerConstruct),
        RESTOCK_HOOK(0x1332440,HookStartGame,originalStartGame),
        RESTOCK_HOOK(0x1308CE0,HookPacketResponse,originalPacketResponse),
        RESTOCK_HOOK(0x1725AE0,HookConnectionDestroy,originalConnectionDestroy),
        RESTOCK_HOOK(0x132D2A0,HookHandlerDestroy,originalHandlerDestroy),
        RESTOCK_HOOK(0x25B4490,HookManagerDestroy,originalManagerDestroy),
        RESTOCK_HOOK(0x16A9B90,HookDisconnect,originalDisconnect),
        RESTOCK_HOOK(0x16AE270,HookClosing,originalClosing)};
#undef RESTOCK_HOOK
    static_assert(std::size(hooks)==23);
    void* targets[std::size(hooks)]{};
    for(unsigned i=0;i<std::size(hooks);++i)targets[i]=reinterpret_cast<void*>(GameAddress(static_cast<std::uint32_t>(hooks[i].rva)));
    auto initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED) return false;
    unsigned created{};
    for(;created<std::size(targets);++created) if(MH_CreateHook(targets[created],hooks[created].replacement,hooks[created].original)!=MH_OK) break;
    bool success=created==std::size(targets);
    for(unsigned i=0;i<created && success;++i) success=MH_QueueEnableHook(targets[i])==MH_OK;
    if(success) success=MH_ApplyQueued()==MH_OK;
    if(!success) for(unsigned i=0;i<created;++i){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}
    // Never uninitialize a MinHook instance potentially shared with other mods.
    return success;
}
void Log(std::string const& message){
    auto line="[AutoRestock] "+message+"\n";OutputDebugStringA(line.c_str());
    if(!directory.empty()){std::ofstream log(directory/"AutoRestock.log",std::ios::app|std::ios::binary);log<<GetTickCount64()<<' '<<line;}
}
std::string ObservationText(StackObservation const& s){
    return std::string("readable=")+(s.readable?"1":"0")+",count="+std::to_string(s.count)+
        ",item="+std::to_string(s.itemId)+",aux="+std::to_string(s.aux)+",block="+std::to_string(s.blockId)+
        ",valid="+std::to_string(s.valid)+",variant="+std::to_string(s.variant)+",id="+std::to_string(s.networkId)+
        ",nbt="+(s.hasNbt?"1":"0")+",nested="+(s.hasNested?"1":"0");
}
std::string ValidationText(ServerValidationEvent const& e){
    return "ServerVerify policy="+std::to_string(e.ticket.policyId)+" native="+std::to_string(e.ticket.nativeId)+
        " observedAt="+std::to_string(e.time)+" slot="+std::to_string(e.slot)+" sourceType="+std::to_string(e.sourceType)+
        " container="+std::to_string(e.container)+" status="+std::to_string(e.status)+" comparison="+std::to_string(e.comparison)+
        " beforeDescriptorCount="+std::to_string(e.beforeDescriptorCount)+" afterDescriptorCount="+std::to_string(e.afterDescriptorCount)+
        " afterCount="+std::to_string(e.afterCount)+" descriptorBlock="+std::to_string(e.descriptorBlockId)+
        " userdataSize="+std::to_string(e.userdataSize)+" differences="+std::to_string(e.differences)+
        " actual={"+ObservationText(e.actual)+"} expected={"+ObservationText(e.expected)+"}";
}
std::string AdmissionText(RemoteAdmissionEvent const& event){
    static constexpr std::array stages{"entry","foreign-owner","remote-fault","missing-event-pump","invalid-event-pump",
        "unknown-continuation","native-route","network-mismatch","invalid-mode","ledger-rejected","manager-not-captured",
        "missing-world-api","post-original-route","player-mismatch","installed-manager-mismatch","finish-rejected","accepted","original-unwind"};
    static constexpr std::array routes{"unobserved","none","handler","network-binding","registration","not-established",
        "supplied-type","saved-type","guid-mismatch","vector","entry","ambiguous","connection-state","removed","closing","missing-peer","allocation-mismatch"};
    auto stage=static_cast<std::size_t>(event.stage),route=static_cast<std::size_t>(event.route.failure);
    return "Admission ordinal="+std::to_string(event.ordinal)+" observedAt="+std::to_string(event.time)+
        " stage="+(stage<stages.size()?stages[stage]:"unknown")+" route="+(route<routes.size()?routes[route]:"unknown")+
        " mode="+std::to_string(event.mode)+" state="+std::to_string(event.route.state)+
        " constructorObserved="+(event.constructorObserved?"1":"0")+
        " typesObserved="+std::to_string(event.route.typesObserved)+
        " suppliedTypeRead="+std::to_string(event.route.suppliedTypeRead)+" suppliedType="+std::to_string(event.route.suppliedType)+
        " savedTypeRead="+std::to_string(event.route.savedTypeRead)+" savedType="+std::to_string(event.route.savedType)+
        " typesMatch="+std::to_string(event.route.suppliedTypeRead && event.route.savedTypeRead?
            static_cast<int>(event.route.suppliedType==event.route.savedType):-1)+
        " suppliedVariant="+std::to_string(event.route.suppliedVariant)+" savedVariant="+std::to_string(event.route.savedVariant);
}
std::string HookDiagnosticText(HookSnapshot const& snapshot){
    constexpr char const* names[]={"operations","notReady","foreignOwner","nested","inventoryMatch","knownPlayer","managerMatch","remoteFault",
        "beginAttempt","configDisabled","foregroundFalse","cursorVisible","captured","notCaptured","originalReturned","endCalled",
        "serverEntries","serverActive","serverIdentityValid","serverRoleLocal","serverRoleServer","serverCounts",
        "serverLocalStored","serverLocalFresh","serverRegistryDistinct","serverUniqueMatch","serverRuntimeMatch",
        "destroyEntries","destroyLocal","destroyCaptured","destroyEnd","destroyServer","destroyServerCounts",
        "hurtEntries","hurtLocal","hurtCaptured","hurtBreak","hurtServer",
        "remoteReceiveEntry","remoteReceiveValid","remoteConnectedEntry","remoteConnectedValid",
        "remoteConstructEntry","remoteConstructObserved","remoteNotifyEntry","remoteNotifyObserved"};
    static_assert(std::size(names)==static_cast<unsigned>(HookCounter::Count));
    std::string line="HookTrace";
    for(unsigned n=0;n<std::size(names);++n)line+=" "+std::string(names[n])+"="+std::to_string(snapshot.values[n]);
    return line+" scope=process-lifetime independentlySampled=1 diagnosticOnly=1";
}
std::string DiagnosticText(Diagnostic const& e){
    if(e.kind==DiagnosticKind::ToolTrace){
        constexpr char const* stages[]={"sample","operation","use","gate","policy","reset","break-armed","break-applied","break-cancelled"};
        constexpr char const* reasons[]={"none","inventory-busy","pairing-not-ready","authoritative-unavailable","count-mismatch",
            "intentional-removal","no-depletion","no-matching-source","policy-no-transfer","transfer-queued","transfer-deferred","transfer-other"};
        auto stage=static_cast<std::size_t>(e.tool.stage),reason=static_cast<std::size_t>(e.tool.reason);
        auto sample=[](ToolObservation const& s){return std::string("readable=")+std::to_string(s.readable)+
            ",empty="+std::to_string(s.empty)+",damageable="+std::to_string(s.damageable)+",count="+std::to_string(s.count)+
            ",capacity="+std::to_string(s.capacity)+",variant="+std::to_string(s.native.variant)+
            ",idKnown="+std::to_string(s.native.idKnown)+",id="+std::to_string(s.native.id);};
        auto const& t=e.tool;
        std::string text="ToolTrace observedAt="+std::to_string(e.time)+" backend="+(e.integrated?"integrated":e.ordinary?"remote-ordinary":"modern")+
            " slot="+std::to_string(e.destination)+" stage="+(stage<std::size(stages)?stages[stage]:"unknown")+
            " reason="+(reason<std::size(reasons)?reasons[reason]:"unknown")+
            " before={"+sample(t.before)+"} after={"+sample(t.after)+"}"+
            " operationRva="+std::to_string(t.operationRva)+" invokerOrdinal="+std::to_string(t.invoker)+
            " captured="+std::to_string(t.captured)+" operations="+std::to_string(t.operations)+" captures="+std::to_string(t.captures)+
            " completedUses="+std::to_string(t.completedUses)+" depletions="+std::to_string(t.depletions)+
            " operationSamplesOmitted="+std::to_string(t.operationSamplesOmitted)+
            " identityChanged="+std::to_string(t.identityChanged)+" depletedByUse="+std::to_string(t.depletedByUse)+
            " intentionalRemoval="+std::to_string(t.intentionalRemoval)+" matchingSources="+std::to_string(t.matchingSources)+
            " pending="+std::to_string(t.pending)+" transferReady="+std::to_string(t.transferReady)+
            " mismatchSlot="+std::to_string(t.mismatchSlot)+" clientCount="+std::to_string(t.clientCount)+" serverCount="+std::to_string(t.serverCount);
        if(e.integrated){auto const& p=t.pairing;
            text+=" localValid="+std::to_string(p.localValid)+" localRole="+std::to_string(p.localRole)+
                " localStored="+std::to_string(p.localStored)+" localMatches="+std::to_string(p.localMatches)+
                " localFresh="+std::to_string(p.localFresh)+" serverSeen="+std::to_string(p.serverSeen)+
                " serverFresh="+std::to_string(p.serverFresh)+" registryDistinct="+std::to_string(p.registryDistinct)+
                " uniqueMatches="+std::to_string(p.uniqueMatches)+" runtimeMatches="+std::to_string(p.runtimeMatches)+
                " epochMatches="+std::to_string(p.epochMatches)+" countsPresent="+std::to_string(p.countsPresent)+
                " countsFresh="+std::to_string(p.countsFresh)+" countsMatch="+std::to_string(p.countsMatch);
        }
        if(t.stage==ToolStage::Reset)text+=" resetCause="+std::to_string(static_cast<int>(e.resetCause))+
            " worldGate="+WorldFailureName(static_cast<WorldFailure>(e.worldFailure));
        return text+" diagnosticOnly=1";
    }
    char const* kind=e.kind==DiagnosticKind::Request?"Request":e.kind==DiagnosticKind::Response?"Response":
        e.kind==DiagnosticKind::Reconciled?"Reconciled":e.kind==DiagnosticKind::Timeout?"Timeout":
        e.kind==DiagnosticKind::NativeResult?"NativeResult":e.kind==DiagnosticKind::PredictionSettled?"PredictionSettled":
        e.kind==DiagnosticKind::PredictionStopped?"PredictionStopped":e.kind==DiagnosticKind::NativeWrite?"NativeWrite":
        e.kind==DiagnosticKind::PostStopSummary?"PostStopSummary":e.kind==DiagnosticKind::PredictionPaused?"PredictionPaused":
        e.kind==DiagnosticKind::ResampleBaseline?"ResampleBaseline":e.kind==DiagnosticKind::ResampleReady?"ResampleReady":
        e.kind==DiagnosticKind::PredictionRearmed?"PredictionRearmed":"ContextReset";
    auto text=std::string(kind)+" policy="+std::to_string(e.requestId)+" native="+std::to_string(e.nativeId);
    text+=" observedAt="+std::to_string(e.time);
    if(e.kind==DiagnosticKind::Response || e.kind==DiagnosticKind::Reconciled)
        text+=" status="+std::to_string(e.nativeStatus)+(e.nativeStatus==(e.integrated?1:0)?" accepted":" rejected");
    if(e.integrated) text+=" backend=integrated";
    if(e.ordinary)text+=" backend=remote-ordinary serverConfirmed=0";
    if(e.kind==DiagnosticKind::NativeWrite){
        auto const& w=e.nativeWrite;
        text+=" ordinal="+std::to_string(e.nativeWriteOrdinal)+" capturedCount="+std::to_string(e.nativeWriteCount);
        text+=" omitted="+std::to_string(e.nativeWritesOmitted)+" beginAt="+std::to_string(w.beginAt)+" endAt="+std::to_string(w.endAt);
        text+=" slot="+std::to_string(w.slot)+" network="+std::to_string(w.network)+" nativeDepth="+std::to_string(w.nativeDepth);
        text+=" callerKnown="+std::to_string(w.callerKnown)+" callerRva="+std::to_string(w.callerRva);
        auto append=[&](char const* name,WriteStackState const& s){
            text+=std::string(" ")+name+"={readable="+std::to_string(s.readable)+",count="+std::to_string(s.count)+
                ",variant="+std::to_string(s.native.variant)+",id="+std::to_string(s.native.id)+",idKnown="+std::to_string(s.native.idKnown)+"}";
        };
        append("before",w.before);append("input",w.input);append("after",w.after);
        text+=w.postStop?" phase=post-stop":" phase=pre-stop";return text;
    }
    if(e.kind==DiagnosticKind::PostStopSummary){
        char const* reasons[]={"unknown","deadline","context-reset","settings-changed","clock-reversal","automatic-replan"};
        auto reason=e.postStopEndReason;
        text+=" reason="+std::string(reason>=1 && reason<=5?reasons[reason]:reasons[0]);
        text+=" stoppedAt="+std::to_string(e.submittedAt)+" elapsedMs="+std::to_string(e.elapsedMs);
        text+=" capturedCount="+std::to_string(e.nativeWriteCount)+" omitted="+std::to_string(e.nativeWritesOmitted);
        text+=" retention=first-eight-and-last-eight reconnectRequired=0 diagnosticOnly=1";
        text+=" resetCauseCode="+std::to_string(static_cast<int>(e.resetCause))+" worldGate="+
            std::string(WorldFailureName(static_cast<WorldFailure>(e.worldFailure)));return text;
    }
    if(e.kind==DiagnosticKind::PredictionSettled)text+=" fenceMs=1500 localPredictionOnly=1";
    if(e.kind==DiagnosticKind::PredictionRearmed)
        text+=" automaticRecovery=1 reconnectRequired=0 freshSnapshotRequired=1 localPlanningBoundaryOnly=1 oldRequestCompleted=0 oldRequestReplayed=0";
    if(e.kind==DiagnosticKind::PredictionPaused || e.kind==DiagnosticKind::ResampleBaseline || e.kind==DiagnosticKind::ResampleReady || e.resampleStartedAt){
        text+=" retiredPolicy="+std::to_string(e.requestId)+" resampleStartedAt="+std::to_string(e.resampleStartedAt);
        text+=" syncCoverage="+std::to_string(e.syncCoverage)+" syncApplications="+std::to_string(e.syncApplications);
        text+=" emptyFallback="+std::to_string(e.emptyFallback);
        text+=" localPlanningBoundaryOnly=1 oldRequestCompleted=0 oldRequestReplayed=0";
        if(e.kind==DiagnosticKind::PredictionPaused)text+=" reason=incoming-inventory-slot callerRva="+
            std::to_string(e.nativeWrite.callerRva)+" slot="+std::to_string(e.nativeWrite.slot);
    }
    if(e.kind==DiagnosticKind::PredictionStopped){
        char const* reasons[]={"unknown","observed-correction-or-use-proof-mismatch","unresolved-context-cancellation","clock-reversal",
            "resample-window-expired","resample-chain-or-snapshot-mismatch"};
        text+=" reason="+std::string(e.predictionStopReason>=0 && e.predictionStopReason<6?reasons[e.predictionStopReason]:reasons[0]);
        text+=" sameGenerationBlocked=1 reconnectRequired=0 automaticRecovery=1 retryIntervalMs=1500";
        text+=" nativeWriteCount="+std::to_string(e.nativeWriteCount)+" nativeWritesOmitted="+std::to_string(e.nativeWritesOmitted);
    }
    if(e.kind==DiagnosticKind::ContextReset || e.kind==DiagnosticKind::PredictionStopped){
        if(e.settledWatchCleared)text+=" settledWatchCleared=1 sameGenerationBlocked=0";
        if(e.kind==DiagnosticKind::ContextReset || e.predictionStopReason==2){
            constexpr char const* causes[]={"explicit","selection-world-gate","tick-world-gate","config-disabled","backend-disabled",
                "context-changed","config-changed","stack-copy-failed","identity-failed","end-use-config-changed","end-use-world-gate",
                "end-use-context-changed","end-use-sample-failed","backend-changed","start-game","remote-retired"};
            auto index=static_cast<std::size_t>(e.resetCause);
            text+=" resetCause="+std::string(index<std::size(causes)?causes[index]:"unknown");
            text+=" worldGate="+std::string(WorldFailureName(static_cast<WorldFailure>(e.worldFailure)));
        }
        text+=" submittedAt="+std::to_string(e.submittedAt)+" elapsedMs="+std::to_string(e.elapsedMs);
    }
    if(e.kind==DiagnosticKind::Request){
        text+=" emptyFallback="+std::to_string(e.emptyFallback);
        char const* result=e.result==int(TransferResult::Queued)?"queued":e.result==int(TransferResult::Fault)?"fault":
            e.result==int(TransferResult::Rejected)?"rejected":"deferred";
        text+=std::string(" result=")+result+" amount="+std::to_string(e.amount);
        text+=" srcVariant="+std::to_string(e.sourceVariant)+" srcId="+std::to_string(e.sourceId);
        text+=" dstVariant="+std::to_string(e.destinationVariant)+" dstId="+std::to_string(e.destinationId);
    }
    text+=" src="+std::to_string(e.source)+" dst="+std::to_string(e.destination);
    text+=" sourceCount="+std::to_string(e.sourceCount)+" destinationCount="+std::to_string(e.destinationCount);
    if(e.kind==DiagnosticKind::Response || e.kind==DiagnosticKind::Reconciled || e.kind==DiagnosticKind::PredictionSettled)
        text+=" provenConsumption="+std::to_string(e.provenConsumption);
    if(e.ordinary && e.kind==DiagnosticKind::Timeout)return text+" stage=original-prediction-fence-expired serverAcknowledgementExpected=0";
    if(e.kind==DiagnosticKind::NativeResult || e.kind==DiagnosticKind::Timeout){
        text+=" status="+std::to_string(e.nativeStatus);
        if(e.kind==DiagnosticKind::NativeResult)text+=std::string(" nativeOutcome=")+(e.nativeStatus==(e.integrated?1:0)?"accepted":"rejected");
        else text+=std::string(" stage=")+(e.nativeStatus==(e.integrated?1:0)?"awaiting-exact-progress":"awaiting-native-result");
        text+=" expectedSourceCount="+std::to_string(e.sourceCount-e.amount)+" expectedDestinationCount="+std::to_string(e.destinationCount+e.amount);
        text+=" provenConsumption="+std::to_string(e.provenConsumption)+" accountedDestinationCount="+std::to_string(e.destinationCount+e.amount-e.provenConsumption);
        text+=std::string(" consumptionChainValid=")+(e.consumptionChainValid?"1":"0")+" accountedProgress="+(e.accountedProgress?"1":"0");
        text+=" wrapperSourceCount="+std::to_string(e.wrapperSourceCount)+" wrapperDestinationCount="+std::to_string(e.wrapperDestinationCount);
        text+=" actualSourceCount="+std::to_string(e.actualSourceCount)+" actualDestinationCount="+std::to_string(e.actualDestinationCount);
        text+=std::string(" inventorySampled=")+(e.inventorySampled?"1":"0")+" realMatches="+(e.realMatches?"1":"0")+" exactProgress="+(e.exactProgress?"1":"0");
    }
    if(e.kind==DiagnosticKind::PredictionStopped){
        text+=" wrapperSourceCount="+std::to_string(e.wrapperSourceCount)+" wrapperDestinationCount="+std::to_string(e.wrapperDestinationCount);
        text+=" actualSourceCount="+std::to_string(e.actualSourceCount)+" actualDestinationCount="+std::to_string(e.actualDestinationCount);
        text+=std::string(" inventorySampled=")+(e.inventorySampled?"1":"0")+" realMatches="+(e.realMatches?"1":"0")+" exactProgress="+(e.exactProgress?"1":"0");
        text+=std::string(" accountedProgress=")+(e.accountedProgress?"1":"0")+" intentionalRemoval="+(e.intentionalRemoval?"1":"0");
        auto const& proof=e.useEvidence;
        text+=std::string(" proofSampled=")+(proof.armed?"1":"0");
        if(proof.armed){
            text+=" sampleSourceVariant="+std::to_string(e.sampleSourceNative.variant)+" sampleSourceId="+std::to_string(e.sampleSourceNative.id);
            text+=" sampleSourceIdKnown="+std::to_string(e.sampleSourceNative.idKnown);
            text+=" sampleDestinationVariant="+std::to_string(e.sampleDestinationNative.variant)+" sampleDestinationId="+std::to_string(e.sampleDestinationNative.id);
            text+=" sampleDestinationIdKnown="+std::to_string(e.sampleDestinationNative.idKnown);
            text+=std::string(" proofChainValid=")+(proof.chainValid?"1":"0")+" proofConsumed="+std::to_string(proof.consumed);
            text+=" proofExpectedSourceCount="+std::to_string(proof.expectedSourceCount)+" proofExpectedDestinationCount="+std::to_string(proof.expectedDestinationCount);
            text+=std::string(" sourceCountMatches=")+(proof.sourceCountMatches?"1":"0")+" sourceItemMatches="+(proof.sourceItemMatches?"1":"0");
            text+=std::string(" destinationCountMatches=")+(proof.destinationCountMatches?"1":"0")+" destinationItemMatches="+(proof.destinationItemMatches?"1":"0");
            text+=" observedUses="+std::to_string(proof.observedUses)+" lastBeforeCount="+std::to_string(proof.lastBeforeCount)+" lastAfterCount="+std::to_string(proof.lastAfterCount);
            text+=" lastExpectedBeforeCount="+std::to_string(proof.lastExpectedBeforeCount);
            text+=std::string(" lastRequestMatches=")+(proof.lastRequestMatches?"1":"0")+" lastBeforeMatches="+(proof.lastBeforeMatches?"1":"0")+" lastResultValid="+(proof.lastResultValid?"1":"0");
            text+=" recentUseCount="+std::to_string(proof.recentUseCount);
            for(unsigned n=0;n<proof.recentUseCount && n<proof.recentUses.size();++n){
                auto const& use=proof.recentUses[n];
                text+=" recentUse"+std::to_string(n)+"={beginAt="+std::to_string(use.beginAt)+" endAt="+std::to_string(use.endAt);
                text+=" request="+std::to_string(use.requestId)+" invoker="+UseInvokerName(use.invoker);
                text+=" before="+std::to_string(use.beforeCount)+" after="+std::to_string(use.afterCount);
                text+=" beforeVariant="+std::to_string(use.beforeNative.variant)+" beforeId="+std::to_string(use.beforeNative.id);
                text+=" afterVariant="+std::to_string(use.afterNative.variant)+" afterId="+std::to_string(use.afterNative.id);
                text+=" beforeIdKnown="+std::to_string(use.beforeNative.idKnown)+" afterIdKnown="+std::to_string(use.afterNative.idKnown);
                text+=" requestMatches="+std::to_string(use.requestMatches)+" beforeMatches="+std::to_string(use.beforeMatches)+" resultValid="+std::to_string(use.resultValid)+"}";
            }
        }
    }
    return text;
}
DWORD WINAPI Worker(void*){
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Worker),&pinned)){
        status.store(-1,std::memory_order_release);return 0;
    }
    try{
        std::wstring path(32768,L'\0');auto used=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(!used || used>=path.size()){status.store(-1,std::memory_order_release);return 0;}
        path.resize(used);directory=std::filesystem::path(path).parent_path();
        Log("0.3.2-test2 kind3 session candidate starting; Bedrock 1.26.45.01,1.26.51.01,1.26.52.03; same 23 hooks,67 loaded guards per admitted image; tagged type0 GUID and kind3 variants0/1/2; generic kind3 registration without RakNet receive scope; exact constructor,notification,StartGame and retirement provenance retained; restock/transaction policy unchanged; manual server validation pending");
        auto game=GetModuleHandleW(nullptr);std::string error;
        auto native=BindNative(game,error);
        if(!native){Log("Inactive: "+error);status.store(-1,std::memory_order_release);return 0;}
        auto world=BindWorld(game,error);
        if(!world){Log("Inactive: "+error);status.store(-1,std::memory_order_release);return 0;}
        if(native->profile!=world->profile){Log("Inactive: mixed native profiles");status.store(-1,std::memory_order_release);return 0;}
        imageBase=reinterpret_cast<std::uintptr_t>(game);worldApi=*world;
        Log(std::string("Selected profile: ")+ProfileName(native->profile->version));
        nativeThreadId=reinterpret_cast<decltype(nativeThreadId)>(GameAddress(0xE5A4F20));
        // ConfigReload never writes a missing/invalid user file. Package contains
        // the defaults. Keep file I/O, JSON and English logs off the game thread.
        ConfigReload reload(directory/"config.json");
        auto poll=reload.Poll(GetTickCount64());
        if(poll==ReloadResult::Rejected) Log("Config rejected; retaining defaults: "+reload.Error());
        auto config=reload.Value();std::uint64_t revision=1;
        auto publish=[&]{settings.store((revision<<8)|(std::uint64_t(config.preRestockThreshold)<<1)|(config.enabled?1:0),std::memory_order_release);};
        publish();
        if(!native->legacy.Complete()){Log("Inactive: ordinary transaction bindings incomplete");status.store(-1,std::memory_order_release);return 0;}
        runtime=new Runtime(*native,*world,imageBase,integrated);
        if(!InstallHooks()){Log("Inactive: guarded hook installation failed");status.store(-1,std::memory_order_release);return 0;}
        status.store(1,std::memory_order_release);
        Log("Ready: 0.3.2-test2; 23 guarded hooks; 67 loaded guards; kind3-session=1; Admission includes type read/value/match and copied active variants only; HookTrace connection-flow counters retained; worker-only formatting and log I/O; JSON-only,main hand from slots9-35; restock/transaction policy unchanged; manual server validation pending");
        HookDiagnosticReporter hookReporter;
        std::string previousError=reload.Error();
        int previousSession=-1,previousMode=-2;std::uint64_t previousGeneration{},previousBirths{},previousBindings{},previousStarts{},previousStartCalls{};
        bool previousRemoteFault{};
        std::uint64_t previousQueued{},previousRejected{},previousServer{},lastReport=GetTickCount64(),previousDropped{},previousValidationDropped{},previousAdmissionDropped{};
        for(;;){
            auto hookSample=hookDiagnostics.Snapshot();
            if(hookReporter.Poll(hookSample,GetTickCount64()))Log(HookDiagnosticText(hookSample));
            auto selected=sessionState.load(std::memory_order_acquire),mode=observedRemoteMode.load(std::memory_order_acquire);
            auto generation=sessionGeneration.load(std::memory_order_relaxed);
            auto births=remoteBirths.load(std::memory_order_relaxed),bindings=remoteBindings.load(std::memory_order_relaxed),starts=remoteStarts.load(std::memory_order_relaxed);
            auto startCalls=remoteStartCalls.load(std::memory_order_relaxed);
            bool remoteFault=remote.Faulted();
            if(selected!=previousSession || generation!=previousGeneration || mode!=previousMode || remoteFault!=previousRemoteFault ||
               births!=previousBirths || bindings!=previousBindings || starts!=previousStarts || startCalls!=previousStartCalls){
                char const* states[]={"waiting-for-world-hud","integrated-candidate-requires-local-server-pairing","remote-modern",
                    "unsupported-contract","remote-fault-disabled","awaiting-original-request-fence","remote-ordinary-predicted","remote-ordinary-replan-quarantine"};
                Log(std::string("Session backend=")+states[selected]+" generation="+std::to_string(generation)+
                    " nativeMode="+(mode==1?"modern":mode==0?"ordinary":"unavailable")+
                    " acceptedGeneration="+std::to_string(observedRemoteGeneration.load(std::memory_order_relaxed))+
                    " births="+std::to_string(births)+" bindings="+std::to_string(bindings)+" starts="+std::to_string(starts)+
                    " startCalls="+std::to_string(startCalls)+" pumpCalls="+std::to_string(remotePumpCalls.load(std::memory_order_relaxed))+
                    " validPumps="+std::to_string(remoteValidPumps.load(std::memory_order_relaxed))+
                    " remoteFault="+(remoteFault?"1":"0"));
                previousSession=selected;previousGeneration=generation;previousMode=mode;previousRemoteFault=remoteFault;
                previousBirths=births;previousBindings=bindings;previousStarts=starts;previousStartCalls=startCalls;
            }
            while(auto event=remoteAdmissions.Pop())Log(AdmissionText(*event));
            auto admissionDropped=remoteAdmissions.Dropped();
            if(admissionDropped!=previousAdmissionDropped){Log("Admission events dropped="+std::to_string(admissionDropped));previousAdmissionDropped=admissionDropped;}
            while(auto event=runtime->TakeDiagnostic()) Log(DiagnosticText(*event));
            while(auto event=serverValidation.Pop()) Log(ValidationText(*event));
            auto validationDropped=serverValidation.Dropped();
            if(validationDropped!=previousValidationDropped){Log("Server validation events dropped="+std::to_string(validationDropped));previousValidationDropped=validationDropped;}
            auto dropped=runtime->DroppedDiagnostics();
            if(dropped!=previousDropped){Log("Diagnostic events dropped="+std::to_string(dropped));previousDropped=dropped;}
            if(status.load(std::memory_order_acquire)<0){Log("Native invariant failed; restock disabled for this process. Native reconciliation remains untouched.");break;}
            auto now=GetTickCount64();auto result=reload.Poll(now);
            if(result==ReloadResult::Applied){config=reload.Value();++revision;publish();Log(std::string("Config loaded: enabled=")+(config.enabled?"true":"false")+", preRestockThreshold="+std::to_string(config.preRestockThreshold));previousError.clear();}
            else if(result==ReloadResult::Rejected && reload.Error()!=previousError){previousError=reload.Error();Log("Config rejected; retaining last valid settings: "+previousError);}
            else if(result==ReloadResult::Unchanged) previousError.clear();
            if(now-lastReport>=10000){
                auto q=queued.load(std::memory_order_relaxed),r=rejected.load(std::memory_order_relaxed);
                auto sp=serverPackets.load(std::memory_order_relaxed);
                if(q!=previousQueued || r!=previousRejected || sp!=previousServer){
                    Log("Requests queued="+std::to_string(q)+", suppressed="+std::to_string(r)+
                        ", responseCalls="+std::to_string(responseCalls.load(std::memory_order_relaxed))+
                        ", ownerResponses="+std::to_string(responseOwnerCalls.load(std::memory_order_relaxed))+
                        ", matchedResponses="+std::to_string(responseMatches.load(std::memory_order_relaxed))+
                        ", serverPackets="+std::to_string(sp)+", pairedPackets="+std::to_string(pairedPackets.load(std::memory_order_relaxed))+
                        ", normalResults="+std::to_string(normalResults.load(std::memory_order_relaxed))+
                        ", serverInventorySamples="+std::to_string(serverInventorySamples.load(std::memory_order_relaxed)));
                    previousQueued=q;previousRejected=r;previousServer=sp;
                }
                lastReport=now;
            }
            Sleep(100);
        }
    }catch(std::exception const& e){status.store(-1,std::memory_order_release);Log(std::string("Inactive: ")+e.what());}
    return 0;
}
}
extern "C" __declspec(dllexport) int AutoRestockStatus(){return status.load(std::memory_order_acquire);}
extern "C" __declspec(dllexport) int AutoRestockInit(HINSTANCE instance){
    if(started.exchange(true)) return status.load(std::memory_order_acquire)>=0;
    module=instance;auto thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(!thread){status.store(-1,std::memory_order_release);return 0;}CloseHandle(thread);return 1;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);AutoRestockInit(instance);}return TRUE;
}
