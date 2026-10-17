// Included after the real Plugin and the external ABI fixture. These tests
// catch missing writer provenance, observer-induced healing, and stale/foreign
// capture. Only the external native setter/getter are substituted.
#include <thread>
namespace WriterCases {
unsigned setters{},getters{};
std::uint32_t expectedNetwork{};
void const* expectedStack{};
void* expectedInventory{};
std::uint32_t expectedSlot{};
bool retire{},reset{},throwOriginal{},overrideAfter{},nullSample{};
void const* Get(void* inventory,int slot){++getters;return nullSample?nullptr:FixtureABI::OrdinaryGet(inventory,slot);}
void Set(void* inventory,std::uint32_t slot,void const* stack,std::uint32_t network){
    CHECK(inventory==expectedInventory && slot==expectedSlot && stack==expectedStack && network==expectedNetwork);
    ++setters;
    // This is the external writer, not any observer modifying a borrowed stack.
    if(slot<36)f->actualSlots[slot]=*static_cast<StackStorage const*>(stack);
    if(overrideAfter){Put(&f->actualSlots[slot],34,std::uint8_t{8});Put(&f->actualSlots[slot],144,std::uint8_t{1});Put(&f->actualSlots[slot],136,std::int32_t{123});}
    if(retire)++nativeLifetimeEpoch;
    if(reset)runtime->Reset(ResetCause::RemoteRetired,200);
    if(throwOriginal)throw std::runtime_error("native-original-unwind");
}
void Write(void* inventory,std::uint32_t slot,StackStorage const& stack,std::uint32_t network=0xA5){
    expectedInventory=inventory;expectedSlot=slot;expectedStack=&stack;expectedNetwork=network;
    HookInventorySet(inventory,slot,&stack,network);
}
void Prepare(FixtureABI::OrdinaryFixture& ordinary){
    FixtureABI::PrepareOrdinary(ordinary);
    Put(&ordinary.actualSlots[9],34,std::uint8_t{5});
    Put(&ordinary.actualSlots[2],128,FixtureABI::base+0xE827E30);
    Put(&ordinary.actualSlots[2],136,std::int32_t{-144});
    Put(&ordinary.actualSlots[2],144,std::uint8_t{2});
    worldApi=FixtureABI::OrdinaryWorld();worldApi.inventoryItem=Get;
    originalInventorySet=Set;imageBase=FixtureABI::base;status.store(1);setters=getters=0;
    retire=reset=throwOriginal=overrideAfter=nullSample=false;
    settings.store((std::uint64_t{1}<<8)|13);ownerThread.store(GetCurrentThreadId());
}
void Arm(Runtime& r,FixtureABI::OrdinaryFixture& ordinary){
    runtime=&r;EstablishRemote(InventoryMode::Ordinary);
    CHECK(SelectForManager(ordinary.manager,{true,true},100));
    CHECK(r.Tick(ordinary.manager,true,true,Config{},100,1)==TransferResult::Queued);
    CHECK(Read<std::uint8_t>(&ordinary.actualSlots[2],34)==10 && ordinary.sends==1);
    CHECK(r.TakeDiagnostic() && !r.TakeDiagnostic());
}
void Run(){
    {
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        void* player=ordinary.player;
        alignas(8) std::uintptr_t operation[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        auto capture=r.BeginUse(ordinary.inventory,operation,true,true,Config{},1);CHECK(capture);
        Put(&ordinary.actualSlots[2],34,std::uint8_t{9});Put(&ordinary.actualSlots[2],136,std::int32_t{-146});
        r.EndUse(*capture,true,true,Config{},1);
        auto replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{10});
        Put(&replacement,128,std::int32_t{1});Put(&replacement,144,std::uint8_t{});
        auto saved=replacement;
        Write(ordinary.inventory,2,replacement);
        CHECK(setters==1 && std::memcmp(&saved,&replacement,sizeof saved)==0);
        CHECK(!r.TakeDiagnostic()); // no callback I/O/event and no new transfer
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},194,1));
        CHECK(r.OrdinaryBlocked() && ordinary.sends==1);
        auto stopped=r.TakeDiagnostic();CHECK(stopped && stopped->kind==DiagnosticKind::PredictionStopped);
        CHECK(stopped->useEvidence.consumed==1 && !stopped->accountedProgress);
        auto text=DiagnosticText(*stopped);
        CHECK(text.find("nativeWriteCount=1 nativeWritesOmitted=0")!=std::string::npos);
        auto write=r.TakeDiagnostic();CHECK(write);
        text=DiagnosticText(*write);
        CHECK(text.starts_with("NativeWrite policy=1 native=0"));
        CHECK(text.find("slot=2 network=165 nativeDepth=0")!=std::string::npos);
        CHECK(text.find("before={readable=1,count=9,variant=2,id=-146,idKnown=1}")!=std::string::npos);
        CHECK(text.find("input={readable=1,count=10,variant=0,id=1,idKnown=1}")!=std::string::npos);
        CHECK(text.find("after={readable=1,count=10,variant=0,id=1,idKnown=1}")!=std::string::npos);
        CHECK(text.find("callerKnown=0 callerRva=0")!=std::string::npos); // host is outside guarded game image
        CHECK(text.find("serverConfirmed=0")!=std::string::npos);
        CHECK(getters==2 && !r.TakeDiagnostic());
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},6100,1) && ordinary.sends==1);
        remote.RetireConnection(35);
    }
    // Break caught: more than eight writer callbacks must retain the latest
    // eight in order, report discarded history, and stay silent until stop.
    {
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];Put(&replacement,144,std::uint8_t{});
        for(int n=0;n<10;++n){
            Put(&replacement,128,30+n);if(n==9)Put(&replacement,34,std::uint8_t{11});
            {Depth depth;Write(ordinary.inventory,2,replacement,1);}
            CHECK(!r.TakeDiagnostic());
        }
        CHECK(getters==20 && setters==10 && !r.Tick(ordinary.manager,true,true,Config{},200,1));
        auto stopped=r.TakeDiagnostic();CHECK(stopped && r.OrdinaryBlocked());
        CHECK(DiagnosticText(*stopped).find("nativeWriteCount=8 nativeWritesOmitted=2")!=std::string::npos);
        for(int n=0;n<8;++n){
            auto event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::NativeWrite);
            CHECK(event->nativeWriteOrdinal==static_cast<unsigned>(n) && event->nativeWrite.requestId==1);
            auto const& w=event->nativeWrite;
            CHECK(w.input.native.id==32+n && w.after.native.id==32+n && w.nativeDepth==1 && w.network==1);
            CHECK(w.beginAt && w.endAt>=w.beginAt && event->time==w.endAt);
            CHECK(w.input.count==(n==7?11:10) && w.before.count==10);
        }
        CHECK(!r.TakeDiagnostic() && ordinary.sends==1 && nativeDepth==0);remote.RetireConnection(35);
    }
    // Break caught: a passive observer must not touch Runtime or call native
    // getters outside its exact live owner/request/inventory/slot/config.
    for(int gate=0;gate<11;++gate){
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];unsigned slot=gate==2?10:gate==10?0xFFFFFFFFu:2;
        alignas(8) std::byte other[8]{};auto inventory=gate==3?static_cast<void*>(other):static_cast<void*>(ordinary.inventory);
        if(gate==0){status.store(0);runtime=reinterpret_cast<Runtime*>(1);}
        if(gate==1){status.store(-1);runtime=reinterpret_cast<Runtime*>(1);}
        if(gate==4)settings.store((std::uint64_t{1}<<8)|12);
        if(gate==5)settings.store((std::uint64_t{2}<<8)|13);
        if(gate==6)ownerThread.store(0); // observer must not adopt a thread
        if(gate==9)worldApi.inventoryItem=nullptr;
        if(gate==7){auto old=restockInventory.exchange(0);Write(inventory,slot,replacement);restockInventory.store(old);}
        else if(gate==8){std::thread otherThread([&]{Write(inventory,slot,replacement);});otherThread.join();}
        else Write(inventory,slot,replacement);
        CHECK(setters==1 && getters==0 && !remote.Faulted());
        runtime=&r;status.store(1);ownerThread.store(GetCurrentThreadId());settings.store((std::uint64_t{1}<<8)|13);
        Put(&ordinary.actualSlots[2],34,std::uint8_t{11});
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},200,1) && r.OrdinaryBlocked());
        auto stopped=r.TakeDiagnostic();CHECK(stopped && stopped->nativeWriteCount==0 && !r.TakeDiagnostic());
        CHECK(ordinary.sends==1);remote.RetireConnection(35);
    }
    // Break caught: lifetime changes, context reset, or an original exception
    // during the one native call must never publish stale after-samples.
    for(int mode=0;mode<3;++mode){
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{11});
        retire=mode==0;reset=mode==1;throwOriginal=mode==2;
        bool caught=false;try{Write(ordinary.inventory,2,replacement);}catch(std::runtime_error const& e){CHECK(std::string(e.what())=="native-original-unwind");caught=true;}
        CHECK(caught==(mode==2) && setters==1 && getters==1);
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},201,1));
        bool stop=false;while(auto event=r.TakeDiagnostic()){
            CHECK(event->kind!=DiagnosticKind::NativeWrite);
            if(event->kind==DiagnosticKind::PredictionStopped){CHECK(event->nativeWriteCount==0);stop=true;}
        }
        CHECK(stop && r.OrdinaryBlocked() && ordinary.sends==1);remote.RetireConnection(35);
    }
    // Break caught: the submitted input is not necessarily the result of the
    // native setter; unknown/unreadable alternatives must not become ID zero.
    for(int mode=0;mode<3;++mode){
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{11});
        if(mode==0)overrideAfter=true;
        if(mode==1)Put(&replacement,144,std::uint8_t{255});
        if(mode==2)nullSample=true;
        Write(ordinary.inventory,2,replacement);
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},200,1));CHECK(r.TakeDiagnostic());
        auto event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::NativeWrite && !r.TakeDiagnostic());
        auto const& w=event->nativeWrite;
        CHECK(w.input.readable && w.input.count==11 && w.input.native.idKnown==(mode!=1));
        if(mode==0)CHECK(w.after.readable && w.after.count==8 && w.after.native.variant==1 && w.after.native.id==123);
        if(mode==1)CHECK(w.after.readable && w.after.native.variant==255 && !w.after.native.idKnown && w.after.native.id==0);
        if(mode==2)CHECK(!w.before.readable && !w.after.readable && w.before.count==-1 && !w.after.native.idKnown);
        CHECK(r.OrdinaryBlocked() && ordinary.sends==1);remote.RetireConnection(35);
    }
    // Break caught: after replacement by a new request, the old writer ring
    // and a delayed old request token must not contaminate the new evidence.
    {
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        ordinary.actualSlots[10]=ordinary.source;Put(&ordinary.actualSlots[10],34,std::uint8_t{10});
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];Write(ordinary.inventory,2,replacement);
        CHECK(r.Tick(ordinary.manager,true,true,Config{},1600,1)==TransferResult::Queued && ordinary.sends==2);
        while(r.TakeDiagnostic()){}
        NativeWrite stale;stale.requestId=1;stale.slot=2;stale.input.count=99;
        r.ObserveNativeWrite(ordinary.inventory,1,stale);
        replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{21});Write(ordinary.inventory,2,replacement);
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},1601,1));auto stopped=r.TakeDiagnostic();
        CHECK(stopped && stopped->requestId==2 && stopped->nativeWriteCount==1);
        auto event=r.TakeDiagnostic();CHECK(event && event->nativeWrite.requestId==2 && event->nativeWrite.input.count==21);
        CHECK(!r.TakeDiagnostic() && ordinary.sends==2);remote.RetireConnection(35);
    }
    // Break caught: IDA assembly saves the complete R9D input. Even though
    // native bookkeeping later consumes its low-byte flag, the detour must
    // preserve the original four-argument ABI, not normalize its upper bits.
    {
        FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
        Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
        auto replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{11});
        Write(ordinary.inventory,2,replacement,0x123400A5);
        CHECK(!r.Tick(ordinary.manager,true,true,Config{},200,1) && r.TakeDiagnostic());
        auto event=r.TakeDiagnostic();CHECK(event && event->nativeWrite.network==0x123400A5);
        CHECK(!r.TakeDiagnostic() && setters==1 && getters==2);remote.RetireConnection(35);
    }
    CHECK(!ObserveWriteStack(nullptr).readable);
    CHECK(!ObserveWriteStack(reinterpret_cast<void const*>(1)).readable);
    auto origin=WriterOrigin(FixtureABI::base+0x123456);CHECK(origin.callerKnown && origin.callerRva==0x123456);
    for(auto caller:{std::uintptr_t{},FixtureABI::base-1,FixtureABI::base+0x12C01000}){
        auto unknown=WriterOrigin(caller);CHECK(!unknown.callerKnown && unknown.callerRva==0);
    }
    runtime=nullptr;
}
}

namespace PostStopWriterCases {
// Break caught: stopping prediction must not erase the immediately following
// native correction evidence, nor can that evidence restart/repair restock.
void Run(){
    using namespace WriterCases;
    FixtureABI::OrdinaryFixture ordinary;Prepare(ordinary);
    Runtime r(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(r,ordinary);
    auto replacement=ordinary.actualSlots[2];Put(&replacement,34,std::uint8_t{11});
    Write(ordinary.inventory,2,replacement);
    auto stoppedAt=GetTickCount64();
    CHECK(!r.Tick(ordinary.manager,true,true,Config{},stoppedAt,1));
    auto stopped=r.TakeDiagnostic();CHECK(stopped && stopped->kind==DiagnosticKind::PredictionStopped);
    CHECK(r.TakeDiagnostic() && !r.TakeDiagnostic() && r.OrdinaryBlocked());
    CHECK(!SelectForManager(ordinary.manager,{true,true},stoppedAt+1) && sessionState.load()==7);
    Put(&replacement,34,std::uint8_t{10});Write(ordinary.inventory,2,replacement);
    CHECK(getters==4 && setters==2 && ordinary.sends==1); // missing post-stop observation
    CHECK(!r.TakeDiagnostic()); // bounded history, no immediate per-write event
    r.Reset(ResetCause::RemoteRetired,stoppedAt+100);
    auto summary=r.TakeDiagnostic();CHECK(summary && DiagnosticText(*summary).starts_with("PostStopSummary"));
    CHECK(DiagnosticText(*summary).find("capturedCount=1 omitted=0")!=std::string::npos);
    auto event=r.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::NativeWrite);
    CHECK(event->nativeWrite.before.count==11 && event->nativeWrite.after.count==10);
    CHECK(DiagnosticText(*event).find("phase=post-stop")!=std::string::npos);
    CHECK(!r.TakeDiagnostic() && ordinary.sends==1 && r.OrdinaryBlocked());
    Write(ordinary.inventory,2,replacement);CHECK(getters==4 && setters==3);
    remote.RetireConnection(35);runtime=nullptr;

    // Break caught: the exact 6000ms boundary, config epoch, reset and
    // clock reversal must close the tail once, without a native read/send.
    for(int gate=0;gate<6;++gate){
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        auto changed=sample.actualSlots[2];Put(&changed,34,std::uint8_t{11});Write(sample.inventory,2,changed);
        auto at=GetTickCount64();CHECK(!active.Tick(sample.manager,true,true,Config{},at,1));
        while(active.TakeDiagnostic()){}
        CHECK(active.NativeWriterRequest(sample.inventory,2,1,at+5999)==1);
        CHECK(!active.NativeWriterRequest(sample.inventory,2,1,at+6000));
        CHECK(!active.NativeWriterRequest(sample.inventory,2,1,at-1));
        auto nativeReads=getters;
        if(gate==0)active.PollStoppedWriters(Config{},1,at+6000);
        if(gate==1)active.PollStoppedWriters(Config{false,6},1,at+1);
        if(gate==2)active.PollStoppedWriters(Config{},2,at+1);
        if(gate==3)active.PollStoppedWriters(Config{},1,at-1);
        if(gate==4)active.Reset(ResetCause::RemoteRetired,at+1);
        if(gate==5)active.SelectBackend(Backend::Unsupported,999,at+1);
        auto end=active.TakeDiagnostic();CHECK(end && end->kind==DiagnosticKind::PostStopSummary);
        CHECK(end->nativeWriteCount==0 && end->postStopEndReason==(gate==0?1:gate==3?4:gate>=4?2:3));
        CHECK(getters==nativeReads && sample.sends==1);
        while(auto extra=active.TakeDiagnostic())CHECK(gate>=4 && (extra->kind==DiagnosticKind::ContextReset || extra->kind==DiagnosticKind::Timeout));
        active.PollStoppedWriters(Config{},1,at+7000);
        CHECK(!active.TakeDiagnostic() && !active.NativeWriterRequest(sample.inventory,2,1,at+2));
        Write(sample.inventory,2,changed);CHECK(getters==nativeReads);
        remote.RetireConnection(35);runtime=nullptr;
    }
    // Break caught: bounded tail must retain the first 8 and last 8 writes,
    // expose the omitted middle, and reject stale/wrong-slot/expired tokens.
    {
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        auto changed=sample.actualSlots[2];Put(&changed,34,std::uint8_t{11});Write(sample.inventory,2,changed);
        auto at=GetTickCount64();CHECK(!active.Tick(sample.manager,true,true,Config{},at,1));
        while(active.TakeDiagnostic()){}
        for(int n=0;n<20;++n){
            NativeWrite w;w.requestId=1;w.slot=2;w.beginAt=w.endAt=at+1+n;w.input.count=n;
            active.ObserveNativeWrite(sample.inventory,1,w);
        }
        NativeWrite stale;stale.requestId=2;stale.slot=2;stale.beginAt=stale.endAt=at+30;
        active.ObserveNativeWrite(sample.inventory,1,stale);
        stale.requestId=1;stale.slot=10;active.ObserveNativeWrite(sample.inventory,1,stale);
        stale.slot=2;stale.beginAt=at+5999;stale.endAt=at+6000;active.ObserveNativeWrite(sample.inventory,1,stale);
        CHECK(!active.TakeDiagnostic());active.PollStoppedWriters(Config{},1,at+6000);
        auto end=active.TakeDiagnostic();CHECK(end && end->nativeWriteCount==16 && end->nativeWritesOmitted==4);
        for(int n=0;n<16;++n){
            auto e=active.TakeDiagnostic();CHECK(e && e->nativeWrite.postStop && e->nativeWrite.input.count==(n<8?n:n+4));
        }
        CHECK(!active.TakeDiagnostic() && sample.sends==1);remote.RetireConnection(35);runtime=nullptr;
    }
    // Break caught: stopped diagnostics are not an exception to owner,
    // settings or lifetime gates, and original unwinding remains untouched.
    for(int gate=0;gate<7;++gate){
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        auto changed=sample.actualSlots[2];Put(&changed,34,std::uint8_t{11});Write(sample.inventory,2,changed);
        auto at=GetTickCount64();CHECK(!active.Tick(sample.manager,true,true,Config{},at,1));
        while(active.TakeDiagnostic()){}
        auto reads=getters;
        if(gate==0)settings.store((std::uint64_t{2}<<8)|13);
        if(gate==1)settings.store((std::uint64_t{1}<<8)|12);
        if(gate==2)ownerThread.store(0);
        if(gate==3){std::thread foreign([&]{Write(sample.inventory,2,changed);});foreign.join();}
        else{
            retire=gate==4;reset=gate==5;throwOriginal=gate==6;
            bool caught=false;try{Write(sample.inventory,2,changed);}catch(std::runtime_error const&){caught=true;}
            CHECK(caught==(gate==6));
        }
        CHECK(setters==2 && getters==reads+(gate>=4?1:0));
        while(auto e=active.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::NativeWrite);
        active.PollStoppedWriters(Config{},1,at+6000);
        while(auto e=active.TakeDiagnostic())CHECK(e->kind!=DiagnosticKind::NativeWrite);
        CHECK(sample.sends==1);remote.RetireConnection(35);runtime=nullptr;
    }
    // Break caught: the real Plugin Tick route must close on timeout OR the
    // earlier OS focus reset. The test does not force desktop focus/cursor.
    {
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        auto changed=sample.actualSlots[2];Put(&changed,34,std::uint8_t{11});Write(sample.inventory,2,changed);
        auto at=GetTickCount64()-7000;CHECK(!active.Tick(sample.manager,true,true,Config{},at,1));
        while(active.TakeDiagnostic()){}
        Tick(sample.manager);
        auto end=active.TakeDiagnostic();CHECK(end && end->kind==DiagnosticKind::PostStopSummary &&
            (end->postStopEndReason==1 || end->postStopEndReason==2));
        CHECK(!active.TakeDiagnostic() && sample.sends==1);remote.RetireConnection(35);runtime=nullptr;
    }
}
}

namespace IncomingWriterCases {
// Private Win64 continuation thunk: executes ONLY the test detour and external
// fixture setter, never game code. Unlike passing a fabricated diagnostic to
// Runtime, this exercises the real _ReturnAddress capture and original ABI.
struct IncomingCall {
    std::byte* memory{};RUNTIME_FUNCTION unwind{};InventorySetFunction call{};
    IncomingCall(){
        memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x1345000,MEM_RESERVE,PAGE_NOACCESS));CHECK(memory);
        CHECK(VirtualAlloc(memory+0x1343000,4096,MEM_COMMIT,PAGE_READWRITE));
        CHECK(VirtualAlloc(memory+0x1000,4096,MEM_COMMIT,PAGE_READWRITE));
        auto target=reinterpret_cast<std::uintptr_t>(&HookInventorySet);
        unsigned char code[]={0x48,0x83,0xEC,0x28,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(code+6,&target,8);std::memcpy(memory+0x1343490,code,sizeof code);
        unsigned char info[]={1,4,1,0,4,0x42,0,0};std::memcpy(memory+0x1000,info,sizeof info);
        unwind={0x1343490,0x13434A5,0x1000};CHECK(RtlAddFunctionTable(&unwind,1,reinterpret_cast<std::uintptr_t>(memory)));
        DWORD old{};CHECK(VirtualProtect(memory+0x1343000,4096,PAGE_EXECUTE_READ,&old));
        CHECK(FlushInstructionCache(GetCurrentProcess(),memory+0x1343490,sizeof code));
        call=reinterpret_cast<InventorySetFunction>(memory+0x1343490);
    }
    ~IncomingCall(){RtlDeleteFunctionTable(&unwind);if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    void Write(void* inventory,unsigned slot,StackStorage const& stack,unsigned network=0){
        using namespace WriterCases;
        expectedInventory=inventory;expectedSlot=slot;expectedStack=&stack;expectedNetwork=network;
        auto savedBase=imageBase;imageBase=reinterpret_cast<std::uintptr_t>(memory);
        try{call(inventory,slot,&stack,network);}catch(...){imageBase=savedBase;throw;}
        imageBase=savedBase;
    }
};
void Run(){
    using namespace WriterCases;
    IncomingCall incoming;
    {
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        sample.actualSlots[10]=sample.source;
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        incoming.Write(sample.inventory,9,sample.actualSlots[9]);
        CHECK(active.OrdinaryPaused() && !active.OrdinaryBlocked());
        incoming.Write(sample.inventory,2,sample.actualSlots[2]);CHECK(setters==2 && getters==4);
        auto event=active.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::PredictionPaused);
        auto text=DiagnosticText(*event);CHECK(text.starts_with("PredictionPaused"));
        CHECK(text.find("serverConfirmed=0")!=std::string::npos && text.find("oldRequestCompleted=0")!=std::string::npos);
        CHECK(text.find("callerRva=20198560")!=std::string::npos && !active.TakeDiagnostic());
        void* player=sample.player;std::uintptr_t operation[]={FixtureABI::base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        auto use=active.BeginUse(sample.inventory,operation,true,true,Config{},1);CHECK(use && use->resampleRevision);
        Put(&sample.actualSlots[2],34,std::uint8_t{5});active.EndUse(*use,true,true,Config{},1);
        auto now=GetTickCount64();CHECK(SelectForManager(sample.manager,{true,true},now) && sessionState.load()==6);
        CHECK(!active.Tick(sample.manager,true,true,Config{},now,1));
        event=active.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::ResampleBaseline);
        CHECK(DiagnosticText(*event).find("syncCoverage=3 syncApplications=2")!=std::string::npos);
        CHECK(active.Tick(sample.manager,true,true,Config{},now+1,1)==TransferResult::Queued && sample.sends==2);
        event=active.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::ResampleReady);
        CHECK(DiagnosticText(*event).find("oldRequestReplayed=0")!=std::string::npos);
        event=active.TakeDiagnostic();CHECK(event && event->requestId==2 && event->source==10 && !active.TakeDiagnostic());
        remote.RetireConnection(35);runtime=nullptr;
    }
    // Correct return PC alone is insufficient: original unwind, lifetime,
    // revision, ownership and full-width R9D must still prevent recovery.
    for(int gate=0;gate<7;++gate){
        FixtureABI::OrdinaryFixture sample;Prepare(sample);
        Runtime active(FixtureABI::OrdinaryApi(),FixtureABI::OrdinaryWorld(),FixtureABI::base);Arm(active,sample);
        auto replacement=sample.actualSlots[2];Put(&replacement,34,std::uint8_t{11});
        retire=gate==0;reset=gate==1;throwOriginal=gate==2;
        if(gate==3)settings.store((std::uint64_t{2}<<8)|13);
        if(gate==4)ownerThread.store(0);
        bool caught=false;try{
            if(gate==5){std::thread foreign([&]{incoming.Write(sample.inventory,2,replacement);});foreign.join();}
            else incoming.Write(sample.inventory,2,replacement,gate==6?0x100:0);
        }catch(std::runtime_error const& e){CHECK(std::string(e.what())=="native-original-unwind");caught=true;}
        CHECK(caught==(gate==2) && setters==1 && !active.OrdinaryPaused());
        CHECK(!active.Tick(sample.manager,true,true,Config{},GetTickCount64(),1) && active.OrdinaryBlocked());
        CHECK(sample.sends==1);remote.RetireConnection(35);runtime=nullptr;
    }
}
}
