// Included by Plugin.cpp after its guarded Read helper, in the same namespace.
using Opaque4=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Opaque9=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,
    std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Opaque7=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,
    std::uintptr_t,std::uintptr_t,std::uintptr_t);
using StartGameFunction=void(*)(void*,void*,void*);
Opaque4 originalRemoteReceive{},originalConnected{},originalHandlerConnected{},originalEventPump{};
Opaque4 originalPacketResponse{},originalConnectionDestroy{},originalHandlerDestroy{},originalManagerDestroy{},originalDisconnect{};
Opaque4 originalManagerConstruct{};
Opaque9 originalConnectionConstruct{};
Opaque7 originalClosing{};
StartGameFunction originalStartGame{};
std::uint32_t(*nativeThreadId)(){};
RemoteSession remote;
std::atomic<int> observedRemoteMode{-1};
std::atomic<std::uint64_t> observedRemoteGeneration{};
std::atomic<std::uint64_t> remoteBirths{},remoteBindings{},remoteStarts{};
RemoteAdmissionQueue remoteAdmissions;
std::atomic<std::uint64_t> remoteStartCalls{};
std::atomic<std::uint64_t> remotePumpCalls{},remoteValidPumps{};
struct AdmissionReport {
    RemoteAdmissionEvent& event;
    int exceptions=std::uncaught_exceptions();
    ~AdmissionReport(){
        if(std::uncaught_exceptions()>exceptions)event.stage=RemoteAdmissionStage::OriginalUnwind;
        remoteAdmissions.Push(event);
    }
};
std::atomic<std::uintptr_t> restockInventory{};
std::uintptr_t restockManager{};
std::uint64_t nativeLifetimeEpoch{1};
void AdvanceNativeLifetime() noexcept {
    if(nativeLifetimeEpoch==std::numeric_limits<std::uint64_t>::max())remote.Poison();
    else ++nativeLifetimeEpoch;
}
struct RemoteReceiveFrame {std::uintptr_t connector{};bool valid{};};
struct RemoteConnectedFrame {std::uintptr_t network{},identifier{};bool valid{};std::optional<RemoteSession::Connection> born;};
struct RemotePumpFrame {std::uintptr_t network{};bool valid{};};
struct RemoteStartFrame {bool valid{};std::uint8_t mode{};std::uintptr_t manager{},player{};};
struct RemoteResponseFrame {NativePrimaryGuidRoute route;std::uintptr_t manager{};std::uint64_t generation{};bool valid{};};
thread_local RemoteReceiveFrame* remoteReceiveFrame{};
thread_local RemoteConnectedFrame* remoteConnectedFrame{};
thread_local RemotePumpFrame* remotePumpFrame{};
thread_local RemoteStartFrame* remoteStartFrame{};
thread_local RemoteResponseFrame* remoteResponseFrame{};
template<class Frame> struct RemoteScope {
    Frame*& slot;Frame* previous;int exceptions=std::uncaught_exceptions();
    RemoteScope(Frame*& current,Frame* frame):slot(current),previous(current){slot=frame;}
    ~RemoteScope(){if(std::uncaught_exceptions()>exceptions)remote.Poison();slot=previous;}
};
bool RemoteOwner() noexcept {
    if(status.load(std::memory_order_acquire)!=1 || !worldApi.profile)return false;
    auto owner=ownerThread.load(std::memory_order_acquire),current=GetCurrentThreadId();
    if(owner)return owner==current;
    if(!nativeThreadId || !OnNativeMainThread(*worldApi.profile,imageBase,nativeThreadId()))return false;
    DWORD empty{};ownerThread.compare_exchange_strong(empty,current,std::memory_order_acq_rel);
    return ownerThread.load(std::memory_order_acquire)==current;
}
std::optional<NativePrimaryGuidRoute> TypedRemoteRoute(void* handler,void* identifier,std::uintptr_t continuation,
    RemoteAdmissionEvent* event=nullptr) noexcept {
    auto fail=[&](RemoteAdmissionStage stage)->std::optional<NativePrimaryGuidRoute>{
        if(event)event->stage=stage;return {};
    };
    if(!RemoteOwner()){
        remote.Foreign(RemoteSession::Retired::Handler,reinterpret_cast<std::uintptr_t>(handler));return fail(RemoteAdmissionStage::Foreign);
    }
    if(remote.Faulted())return fail(RemoteAdmissionStage::Faulted);
    if(!remotePumpFrame)return fail(RemoteAdmissionStage::MissingPump);
    if(!remotePumpFrame->valid)return fail(RemoteAdmissionStage::InvalidPump);
    if(continuation!=GameAddress(0x16AC037))return fail(RemoteAdmissionStage::Continuation);
    auto route=ReadPrimaryGuidRoute(*worldApi.profile,imageBase,handler,identifier,event?&event->route:nullptr);
    if(!route)return fail(RemoteAdmissionStage::Route);
    if(route->binding.network!=remotePumpFrame->network)return fail(RemoteAdmissionStage::Network);
    return route;
}
std::uintptr_t HookRemoteReceive(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    hookDiagnostics.Hit(HookCounter::RemoteReceiveEntry);
    if(!RemoteOwner())return originalRemoteReceive(a,b,c,d);
    bool nested=remoteReceiveFrame!=nullptr;
    if(nested)remote.Poison();
    RemoteReceiveFrame frame{a,!nested && !remote.Faulted() && Read<std::uintptr_t>(reinterpret_cast<void*>(a),0)==GameAddress(0xE7BE750)};
    if(frame.valid)hookDiagnostics.Hit(HookCounter::RemoteReceiveValid);
    RemoteScope scope(remoteReceiveFrame,&frame);
    return originalRemoteReceive(a,b,c,d);
}
std::uintptr_t HookConnected(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    hookDiagnostics.Hit(HookCounter::RemoteConnectedEntry);
    if(!RemoteOwner()){remote.Foreign(RemoteSession::Retired::Network,a>=32?a-32:0);return originalConnected(a,b,c,d);}
    if(remoteConnectedFrame)remote.Poison();
    bool valid=remoteReceiveFrame && remoteReceiveFrame->valid && !remote.Faulted() &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())==GameAddress(0x16CAAC5) && a>=32 &&
        Read<std::uintptr_t>(reinterpret_cast<void*>(remoteReceiveFrame->connector),8)==a &&
        Read<std::uintptr_t>(reinterpret_cast<void*>(a),0)==GameAddress(0xE75E2F0) &&
        Read<std::uintptr_t>(reinterpret_cast<void*>(a-32),0)==GameAddress(0xE75E2D0);
    // Exact generic registration entry is also used by the native kind3
    // transport. It has no RakNet receive frame; require its independently
    // proven reciprocal registration and tagged address identity instead.
    if(!valid && !remoteConnectedFrame && !remoteReceiveFrame && !remote.Faulted())
        valid=ReadAddressConnectionBinding(*worldApi.profile,imageBase,reinterpret_cast<void*>(a),reinterpret_cast<void*>(b)).has_value();
    if(valid)hookDiagnostics.Hit(HookCounter::RemoteConnectedValid);
    RemoteConnectedFrame frame{a>=32?a-32:0,b,valid,{}};RemoteScope scope(remoteConnectedFrame,&frame);
    return originalConnected(a,b,c,d);
}
std::uintptr_t HookConnectionConstruct(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,
    std::uintptr_t e,std::uintptr_t f,std::uintptr_t g,std::uintptr_t h,std::uintptr_t i){
    hookDiagnostics.Hit(HookCounter::RemoteConstructEntry);
    auto frame=remoteConnectedFrame;
    bool observe=RemoteOwner() && frame && frame->valid && !remote.Faulted() && b==frame->identifier &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())==GameAddress(0x16AF709);
    if(observe)hookDiagnostics.Hit(HookCounter::RemoteConstructObserved);
    auto identity=observe?ReadConnectionIdentity(reinterpret_cast<void*>(b)):std::nullopt;
    auto result=originalConnectionConstruct(a,b,c,d,e,f,g,h,i);
    if(observe && !remote.Faulted()){
        auto copied=ReadConnectionIdentity(reinterpret_cast<void*>(a));
        if(frame->born || !identity || !copied || *identity!=*copied)remote.Poison();
        else {frame->born=remote.Born(a,frame->network,*identity);if(frame->born)remoteBirths.fetch_add(1,std::memory_order_relaxed);}
    }
    return result;
}
std::uintptr_t HookHandlerConnected(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    hookDiagnostics.Hit(HookCounter::RemoteNotifyEntry);
    if(!RemoteOwner()){remote.Foreign(RemoteSession::Retired::Handler,a);return originalHandlerConnected(a,b,c,d);}
    auto frame=remoteConnectedFrame;
    bool observe=RemoteOwner() && frame && frame->valid && frame->born && !remote.Faulted() && b==frame->identifier &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())==GameAddress(0x16AF7B3);
    if(observe)hookDiagnostics.Hit(HookCounter::RemoteNotifyObserved);
    auto result=originalHandlerConnected(a,b,c,d);
    if(observe){
        auto binding=ReadPrimaryNotificationBinding(*worldApi.profile,imageBase,reinterpret_cast<void*>(a),reinterpret_cast<void*>(b));
        if(binding && binding->network==frame->network){
            if(remote.Bind(*frame->born,*binding,a))remoteBindings.fetch_add(1,std::memory_order_relaxed);
        }
        else remote.Poison();
    }
    return result;
}
std::uintptr_t HookEventPump(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    remotePumpCalls.fetch_add(1,std::memory_order_relaxed);
    if(!RemoteOwner())return originalEventPump(a,b,c,d);
    if(remotePumpFrame)remote.Poison();
    auto adjusted=Read<std::uintptr_t>(reinterpret_cast<void*>(a),0);
    bool valid=adjusted>=32 && !remote.Faulted() &&
        Read<std::uintptr_t>(reinterpret_cast<void*>(adjusted),0)==GameAddress(0xE75E2F0);
    RemotePumpFrame frame{adjusted>=32?adjusted-32:0,valid};RemoteScope scope(remotePumpFrame,&frame);
    if(valid)remoteValidPumps.fetch_add(1,std::memory_order_relaxed);
    return originalEventPump(a,b,c,d);
}
std::uintptr_t HookManagerConstruct(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    auto frame=remoteStartFrame;
    bool observe=RemoteOwner() && frame && frame->valid && !remote.Faulted() &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())==GameAddress(0x1336D29);
    auto result=originalManagerConstruct(a,b,c,d);
    if(observe){
        if(frame->manager || std::uint8_t(c)!=1 || std::uint8_t(d)!=frame->mode)remote.Poison();
        else {frame->manager=a;frame->player=b;}
    }
    return result;
}
void HookStartGame(void* handler,void* identifier,void* packet){
    RemoteAdmissionEvent event{remoteStartCalls.fetch_add(1,std::memory_order_relaxed)+1,GetTickCount64()};
    AdmissionReport report{event};
    auto route=TypedRemoteRoute(handler,identifier,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),&event);
    if(!RemoteOwner()){originalStartGame(handler,identifier,packet);return;}
    if(remoteStartFrame)remote.Poison();
    AdvanceNativeLifetime();restockManager=0;
    remote.Cancel();if(runtime)runtime->Reset(ResetCause::StartGame);
    auto mode=route?Read<std::uint8_t>(packet,Layout().packetMode):std::uint8_t{255};
    auto ticket=route && mode<=1 ? remote.Begin(*route,mode?InventoryMode::Modern:InventoryMode::Ordinary):std::nullopt;
    if(route){event.mode=mode;event.stage=mode>1?RemoteAdmissionStage::Mode:RemoteAdmissionStage::Ledger;}
    RemoteStartFrame frame{ticket.has_value(),mode,0,0};RemoteScope scope(remoteStartFrame,&frame);
    originalStartGame(handler,identifier,packet);
    event.constructorObserved=frame.manager && frame.player;
    if(!ticket){remote.Cancel();return;}
    if(remote.Faulted()){event.stage=RemoteAdmissionStage::Faulted;remote.Cancel();return;}
    if(!frame.manager || !frame.player){event.stage=RemoteAdmissionStage::ManagerCapture;remote.Cancel();return;}
    if(!worldApi.localPlayer || !worldApi.world){event.stage=RemoteAdmissionStage::Api;remote.Cancel();return;}
    auto after=ReadPrimaryGuidRoute(*worldApi.profile,imageBase,handler,identifier,&event.route);
    if(!after){event.stage=RemoteAdmissionStage::PostRoute;remote.Cancel();return;} // never call getters through a retired route
    auto client=reinterpret_cast<void*>(route->binding.client);
    auto player=worldApi.localPlayer(client);auto world=worldApi.world(client);
    auto manager=Read<std::uintptr_t>(player,Layout().managerPlayer);
    if(reinterpret_cast<std::uintptr_t>(player)!=frame.player){event.stage=RemoteAdmissionStage::Player;remote.Cancel();return;}
    if(manager!=frame.manager || Read<std::uintptr_t>(reinterpret_cast<void*>(manager),0)!=GameAddress(0xE827EC0) ||
       Read<std::uintptr_t>(reinterpret_cast<void*>(manager),Layout().managerOwner)!=frame.player ||
       Read<std::uint8_t>(reinterpret_cast<void*>(manager),Layout().managerMode)!=mode ||
       Read<std::uint8_t>(reinterpret_cast<void*>(manager),Layout().managerClient)!=1){event.stage=RemoteAdmissionStage::Manager;remote.Cancel();return;}
    event.stage=RemoteAdmissionStage::Finish;
    if(remote.Finish(*ticket,*after,reinterpret_cast<std::uintptr_t>(world),frame.player,manager)){
        observedRemoteGeneration.store(ticket->serial,std::memory_order_relaxed);
        observedRemoteMode.store(mode,std::memory_order_release);remoteStarts.fetch_add(1,std::memory_order_relaxed);
        event.stage=RemoteAdmissionStage::Accepted;
    }
}
std::uintptr_t HookPacketResponse(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    auto route=TypedRemoteRoute(reinterpret_cast<void*>(a),reinterpret_cast<void*>(b),reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if(!RemoteOwner())return originalPacketResponse(a,b,c,d);
    if(remoteResponseFrame)remote.Poison();
    RemoteResponseFrame frame;
    if(route && !remote.Faulted() && worldApi.localPlayer && worldApi.world){
        auto client=reinterpret_cast<void*>(route->binding.client),player=worldApi.localPlayer(client);
        auto manager=Read<std::uintptr_t>(player,Layout().managerPlayer);auto world=worldApi.world(client);
        auto selection=remote.Select(manager,reinterpret_cast<std::uintptr_t>(player),reinterpret_cast<std::uintptr_t>(world));
        frame={*route,manager,selection.generation,remote.Authorize(*route,manager,selection.generation)};
    }
    RemoteScope scope(remoteResponseFrame,&frame);
    return originalPacketResponse(a,b,c,d);
}
void ObserveRemoteRetirement(RemoteSession::Retired kind,std::uintptr_t key) noexcept {
    if(!RemoteOwner()){remote.Foreign(kind,key);return;}
    auto generation=remote.Generation();
    switch(kind){
    case RemoteSession::Retired::Connection:remote.RetireConnection(key);break;
    case RemoteSession::Retired::Handler:remote.RetireHandler(key);break;
    case RemoteSession::Retired::Manager:remote.RetireManager(key);break;
    case RemoteSession::Retired::Network:remote.Disconnect(key);break;
    }
    bool currentManager=kind==RemoteSession::Retired::Manager && key && key==restockManager;
    if(generation!=remote.Generation() || currentManager){
        observedRemoteMode.store(-1,std::memory_order_release);observedRemoteGeneration.store(0,std::memory_order_relaxed);
        AdvanceNativeLifetime();restockManager=0;if(runtime)runtime->Reset(ResetCause::RemoteRetired);
    }
}
std::uintptr_t HookConnectionDestroy(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){ObserveRemoteRetirement(RemoteSession::Retired::Connection,a);return originalConnectionDestroy(a,b,c,d);}
std::uintptr_t HookHandlerDestroy(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){ObserveRemoteRetirement(RemoteSession::Retired::Handler,a);return originalHandlerDestroy(a,b,c,d);}
std::uintptr_t HookManagerDestroy(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){ObserveRemoteRetirement(RemoteSession::Retired::Manager,a);return originalManagerDestroy(a,b,c,d);}
std::uintptr_t HookDisconnect(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){ObserveRemoteRetirement(RemoteSession::Retired::Network,a>=32?a-32:0);return originalDisconnect(a,b,c,d);}
std::uintptr_t HookClosing(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,
    std::uintptr_t e,std::uintptr_t f,std::uintptr_t g){
    // This native notification marks the selected connection closing before
    // its eventual destructor. Retire network capability before native work.
    ObserveRemoteRetirement(RemoteSession::Retired::Network,a>=32?a-32:0);
    return originalClosing(a,b,c,d,e,f,g);
}
