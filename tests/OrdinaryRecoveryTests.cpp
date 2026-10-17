#include "OrdinaryFixture.h"
namespace {
void Incoming(Runtime& r,OrdinaryFixture& x,int slot,StackStorage const& value,std::uint64_t now,
    std::uint32_t caller=0x13434A0){
    NativeWrite w;w.requestId=r.NativeWriterRequest(x.inventory,slot,1,now);
    CHECK(w.requestId);w.slot=slot;w.beginAt=w.endAt=now;w.callerKnown=true;w.callerRva=caller;
    w.before.readable=w.input.readable=w.after.readable=true;
    w.before.count=Read<std::uint8_t>(&x.actualSlots[slot],34);
    w.input.count=w.after.count=Read<std::uint8_t>(&value,34);
    w.before.native=w.input.native=w.after.native={0,1,true};
    x.actualSlots[slot]=value;r.ObserveNativeWrite(x.inventory,1,w);
}
void Use(Runtime& r,OrdinaryFixture& x,int count){
    void* player=x.player;std::uintptr_t operation[]={base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
    auto use=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(use);
    Put(&x.actualSlots[2],34,static_cast<std::uint8_t>(count));r.EndUse(*use,true,true,Config{},1);
}
void PrepareRecovery(OrdinaryFixture& x){
    PrepareOrdinary(x);Put(&x.actualSlots[9],34,std::uint8_t{5});
    x.actualSlots[10]=x.source;Put(&x.actualSlots[10],34,std::uint8_t{20});
}
void Arm(Runtime& r,OrdinaryFixture& x){
    CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
    CHECK(r.Tick(x.manager,true,true,Config{},100,1)==TransferResult::Queued && x.sends==1);
}
void PausePair(Runtime& r,OrdinaryFixture& x){
    Use(r,x,9);auto restored=x.actualSlots[2];Put(&restored,34,std::uint8_t{10});
    Incoming(r,x,9,x.actualSlots[9],209);Incoming(r,x,2,restored,210);
    CHECK(r.OrdinaryPaused() && !r.OrdinaryBlocked());
}
// test12: a late empty incoming write may retire the new-use proof, but must
// not erase the separately retained REAL world-use depletion in this context.
// The break caught is losing this one-shot identity, or bypassing the original
// serialization/native preflight while using it. No server ACK is simulated.
void RunEmptyFallback(){
    OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);
    Use(r,x,0);Incoming(r,x,2,x.actualSlots[2],220);
    CHECK(!r.Tick(x.manager,true,true,Config{},1599,1) && x.sends==1);
    CHECK(!r.Tick(x.manager,true,true,Config{},1600,1) && x.sends==1);
    CHECK(r.Tick(x.manager,true,true,Config{},1650,1)==TransferResult::Queued && x.sends==2);
    CHECK(Read<std::uint8_t>(&x.actualSlots[10],34)==0 && Read<std::uint8_t>(&x.actualSlots[2],34)==20);
    bool fallback=false;while(auto e=r.TakeDiagnostic())if(e->kind==DiagnosticKind::Request && e->requestId==2){
        CHECK(e->source==10 && e->amount==20);fallback=true;
    }CHECK(fallback);
}
// A canceled recovery is quarantined briefly, then a fresh complete two-Tick
// local plan is allowed in the SAME admitted generation. UI never authorizes a
// transfer or resurrects the old plan/depletion; it only cancels those values.
void RunSameGenerationRearm(){
    OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);
    Put(&x.actualSlots[2],34,std::uint8_t{5});
    CHECK(!r.Tick(x.manager,true,false,Config{},300,1) && r.OrdinaryBlocked());
    CHECK(!r.SelectBackend(Backend::RemoteOrdinary,10,1799) && x.sends==1);
    CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,1800));
    CHECK(!r.Tick(x.manager,true,true,Config{},1800,1) && x.sends==1);
    CHECK(r.Tick(x.manager,true,true,Config{},1850,1)==TransferResult::Queued && x.sends==2);
    CHECK(!r.OrdinaryBlocked() && Read<std::uint8_t>(&x.actualSlots[2],34)==25);
}
void RunSeventeenThreshold(){
    OrdinaryFixture x;PrepareOrdinary(x);Put(&x.actualSlots[2],34,std::uint8_t{17});
    Put(&x.actualSlots[9],34,std::uint8_t{64});Runtime r(OrdinaryApi(),OrdinaryWorld(),base);
    CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
    CHECK(!r.Tick(x.manager,true,true,Config{true,17},100,1) && !x.sends);
    Use(r,x,16);
    CHECK(r.Tick(x.manager,true,true,Config{true,17},150,1)==TransferResult::Queued && x.sends==1);
    CHECK(Read<std::uint8_t>(&x.actualSlots[9],34)==16 && Read<std::uint8_t>(&x.actualSlots[2],34)==64);
}
void RunFallbackBoundaries(){
    // Break caught: transferring a different full-metadata class, retaining
    // intent through UI/settings/selection, or counting a stale use capture.
    for(int gate=0;gate<7;++gate){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);
        if(gate==0)Put(&x.actualSlots[2],34,std::uint8_t{}); // manual empty, not native depletion
        else if(gate==6){
            void* player=x.player;std::uintptr_t op[]={base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
            auto stale=r.BeginUse(x.inventory,op,true,true,Config{},1);CHECK(stale);
            Incoming(r,x,2,x.actualSlots[2],220);Put(&x.actualSlots[2],34,std::uint8_t{});
            r.EndUse(*stale,true,true,Config{},1);
        }else Use(r,x,0);
        Incoming(r,x,2,x.actualSlots[2],230);
        if(gate==1)CHECK(!r.Tick(x.manager,true,false,Config{},300,1));
        if(gate==2)CHECK(!r.Tick(x.manager,true,true,Config{},300,2));
        if(gate==3){Put(x.supplies,16,int{3});CHECK(!r.Tick(x.manager,true,true,Config{},300,1));Put(x.supplies,16,int{2});}
        if(gate==4){
            auto nonempty=x.source;Put(&nonempty,34,std::uint8_t{3});Incoming(r,x,2,nonempty,240);
            Put(&x.actualSlots[2],34,std::uint8_t{});Incoming(r,x,2,x.actualSlots[2],250);
        }
        if(gate==5)Put(&x.actualSlots[10],60,int{999});
        auto epoch=gate==2?std::uint64_t{2}:std::uint64_t{1};
        for(auto now:{1600,1650,1800,1850,2000,2050}){
            if(r.SelectBackend(Backend::RemoteOrdinary,10,now))CHECK(!r.Tick(x.manager,true,true,Config{},now,epoch));
            CHECK(x.sends==1);
        }
        // No source at the successful fallback scan consumes the intent. A
        // later pickup must not inherit an indefinite empty-hand promise.
        if(gate==5){
            Put(&x.actualSlots[10],60,int{});
            CHECK(!r.Tick(x.manager,true,true,Config{},2100,epoch) && x.sends==1);
        }
    }
    // A successful native submission consumes fallback even if a later
    // unobserved empty hand looks convenient for another refill.
    {
        OrdinaryFixture x;PrepareRecovery(x);x.actualSlots[11]=x.source;
        Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);Use(r,x,0);
        Incoming(r,x,2,x.actualSlots[2],220);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        CHECK(r.Tick(x.manager,true,true,Config{},1650,1)==TransferResult::Queued && x.sends==2);
        Put(&x.actualSlots[2],34,std::uint8_t{});
        CHECK(!r.Tick(x.manager,true,true,Config{},1700,1) && r.OrdinaryBlocked());
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,3200));
        CHECK(!r.Tick(x.manager,true,true,Config{},3200,1));
        CHECK(!r.Tick(x.manager,true,true,Config{},3250,1) && x.sends==2);
    }
}
void RunTwentyEmptyFallbacks(){
    OrdinaryFixture x;PrepareRecovery(x);
    for(int slot=0;slot<36;++slot){x.actualSlots[slot]=x.source;Put(&x.actualSlots[slot],34,std::uint8_t{64});}
    Put(&x.actualSlots[2],34,std::uint8_t{5});
    Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);while(r.TakeDiagnostic()){}
    int source=9,used=0;std::uint64_t at=100;
    for(int cycle=1;cycle<=20;++cycle){
        used+=Read<std::uint8_t>(&x.actualSlots[2],34);Use(r,x,0);
        Incoming(r,x,source,x.actualSlots[source],at+100);Incoming(r,x,2,x.actualSlots[2],at+101);
        CHECK(!r.Tick(x.manager,true,true,Config{},at+1500,1));
        CHECK(r.Tick(x.manager,true,true,Config{},at+1550,1)==TransferResult::Queued && x.sends==cycle+1);
        int requests=0;while(auto e=r.TakeDiagnostic()){
            CHECK(e->kind!=DiagnosticKind::PredictionStopped && e->kind!=DiagnosticKind::PredictionSettled);
            if(e->kind==DiagnosticKind::Request){
                ++requests;CHECK(e->requestId==std::uint64_t(cycle+1) && e->emptyFallback);
                CHECK(e->amount==(cycle==1?5:64));source=e->source;
            }
        }CHECK(requests==1 && !r.DroppedDiagnostics());at+=1550;
        int remaining=Read<std::uint8_t>(&x.actualSlots[2],34);
        for(int slot=9;slot<36;++slot)remaining+=Read<std::uint8_t>(&x.actualSlots[slot],34);
        CHECK(used+remaining==1733);
        for(int slot=0;slot<9;++slot)if(slot!=2)CHECK(Read<std::uint8_t>(&x.actualSlots[slot],34)==64);
    }
    CHECK(used==1221 && source==28 && x.sends==21);
}
void RunGates(){
    // Twenty independent synchronization/replan cycles: every transfer has a
    // new ID; old predictions are neither completed nor replayed. Virtual time
    // and fixture writes are not live server or performance verification.
    {
        OrdinaryFixture x;PrepareRecovery(x);
        for(int slot=9;slot<=35;++slot){x.actualSlots[slot]=x.source;Put(&x.actualSlots[slot],34,std::uint8_t{64});}
        Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);
        int source=9;std::uint64_t at=100;
        while(r.TakeDiagnostic()){}
        for(int cycle=1;cycle<=20;++cycle){
            auto main=x.actualSlots[2];auto count=Read<std::uint8_t>(&main,34);CHECK(count>5);
            Use(r,x,count-1);
            Incoming(r,x,source,x.actualSlots[source],at+100);Incoming(r,x,2,main,at+101);
            for(int n=count-1;n>=5;--n)Use(r,x,n);
            CHECK(!r.Tick(x.manager,true,true,Config{},at+1500,1));
            CHECK(r.Tick(x.manager,true,true,Config{},at+1550,1)==TransferResult::Queued && x.sends==cycle+1);
            bool request=false;while(auto event=r.TakeDiagnostic()){
                CHECK(event->kind!=DiagnosticKind::PredictionSettled && event->kind!=DiagnosticKind::PredictionStopped);
                if(event->kind==DiagnosticKind::Request){request=true;CHECK(event->requestId==static_cast<std::uint64_t>(cycle+1));source=event->source;}
            }
            CHECK(request && !r.OrdinaryBlocked() && !r.OrdinaryPaused());at+=1550;
        }
    }
    // Neither silence, one slot, pre-sync use, nor repeated same-time samples
    // can authorize recovery. Expiration must stop, never trigger a retry.
    for(int gate=0;gate<5;++gate){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);
        if(gate<2){
            Incoming(r,x,gate?2:9,x.actualSlots[gate?2:9],210);Use(r,x,9);
        }else{
            PausePair(r,x);
            if(gate==3){Use(r,x,9);Incoming(r,x,2,x.actualSlots[2],211);} // old use invalidated
            if(gate==4)Use(r,x,9);
        }
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1) && x.sends==1);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1) && x.sends==1);
        if(gate!=4){
            CHECK(!r.Tick(x.manager,true,true,Config{},10000,1) && r.OrdinaryPaused());
            CHECK(!r.Tick(x.manager,true,true,Config{},10210,1) && r.OrdinaryBlocked());
        }
        CHECK(x.sends==1);
    }
    // Invalid provenance/readability/clock/token never becomes a pause/rebase.
    for(int bad=0;bad<12;++bad){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);
        NativeWrite w;w.requestId=1;w.slot=2;w.beginAt=w.endAt=210;w.callerKnown=true;w.callerRva=0x13434A0;
        w.before=w.input=w.after={true,11,{0,1,true}};
        if(bad==0)w.callerKnown=false;if(bad==1)++w.callerRva;
        if(bad==2)w.nativeDepth=1;if(bad==3)w.network=0x100;
        if(bad==4)w.before.readable=false;if(bad==5)w.after.native.idKnown=false;
        if(bad==6)w.requestId=2;if(bad==7)w.beginAt=w.endAt=99;
        if(bad==8)w.endAt=209;if(bad==9)w.slot=8;
        Put(&x.actualSlots[2],34,std::uint8_t{11});
        r.ObserveNativeWrite(bad==10?x.manager:x.inventory,bad==11?2:1,w);
        CHECK(!r.OrdinaryPaused());
        CHECK(!r.Tick(x.manager,true,true,Config{},211,1) && r.OrdinaryBlocked() && x.sends==1);
    }
    // Complete snapshot is not just the old pair: a third-source count or full
    // metadata change, unobserved hand decrement, or broken new use chain stops.
    for(int bad=0;bad<5;++bad){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);Use(r,x,9);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        if(bad==0)Put(&x.actualSlots[10],34,std::uint8_t{19});
        if(bad==1)Put(&x.actualSlots[10],60,int{99});
        if(bad==2)Put(&x.actualSlots[2],34,std::uint8_t{8});
        if(bad==3){Put(&x.actualSlots[2],34,std::uint8_t{10});Use(r,x,9);}
        if(bad==4)Put(&x.actualSlots[2],60,int{99});
        CHECK(!r.Tick(x.manager,true,true,Config{},1650,1) && r.OrdinaryBlocked() && x.sends==1);
    }
    // Resync, even a late update at~6s, invalidates the baseline and depletion.
    // A later NEW use and two NEW complete samples are required.
    {
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);Use(r,x,9);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        auto restored=x.actualSlots[2];Put(&restored,34,std::uint8_t{10});Incoming(r,x,2,restored,6200);
        CHECK(!r.Tick(x.manager,true,true,Config{},6201,1) && r.OrdinaryPaused());
        Use(r,x,5);CHECK(!r.Tick(x.manager,true,true,Config{},6250,1));
        CHECK(r.Tick(x.manager,true,true,Config{},6300,1)==TransferResult::Queued && x.sends==2);
        CHECK(Read<std::uint8_t>(&x.actualSlots[10],34)==0 && Read<std::uint8_t>(&x.actualSlots[2],34)==25);
    }
    // A genuine post-sync depletion may survive waiting across samples. A
    // manual empty hand without its callback may not inherit old depletion.
    for(bool genuine:{false,true}){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);
        if(genuine)Use(r,x,0);else Put(&x.actualSlots[2],34,std::uint8_t{});
        CHECK(!r.Tick(x.manager,true,true,Config{},300,1));
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        auto result=r.Tick(x.manager,true,true,Config{},1650,1);
        CHECK(genuine?result==TransferResult::Queued:!result);
        CHECK(x.sends==(genuine?2:1));
    }
    // Cancellation after the old fence is still unresolved recovery, not a
    // settled watch. UI, selected slot, settings, backend and clock cannot heal.
    for(int cause=0;cause<6;++cause){
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);Use(r,x,9);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        if(cause==0)r.Reset(ResetCause::SelectionWorldGate,1601);
        if(cause==1){Put(x.supplies,16,int{3});CHECK(!r.Tick(x.manager,true,true,Config{},1601,1));}
        if(cause==2)CHECK(!r.Tick(x.manager,true,true,Config{false,6},1601,1));
        if(cause==3)CHECK(!r.Tick(x.manager,true,true,Config{},1601,2));
        if(cause==4)CHECK(!r.SelectBackend(Backend::Unsupported,10,1601));
        if(cause==5)CHECK(!r.Tick(x.manager,true,true,Config{},209,1));
        // Reversed-clock quarantine started at209; other causes at1601.
        auto retryAt=cause==5?std::uint64_t{1709}:std::uint64_t{3101};
        CHECK(!r.SelectBackend(Backend::RemoteOrdinary,10,retryAt-1) && r.OrdinaryBlocked() && x.sends==1);
        CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,retryAt) && !r.OrdinaryBlocked() && x.sends==1);
    }
    // A world-use capture spanning an incoming update is stale even if its
    // before/after numbers appear reasonable. New revision is not reused.
    {
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);
        void* player=x.player;std::uintptr_t operation[]={base+0x25B6660,reinterpret_cast<std::uintptr_t>(&player)};
        auto stale=r.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(stale);
        Incoming(r,x,2,x.actualSlots[2],250);Put(&x.actualSlots[2],34,std::uint8_t{9});
        r.EndUse(*stale,true,true,Config{},1);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));CHECK(!r.Tick(x.manager,true,true,Config{},1650,1));
        CHECK(r.OrdinaryPaused() && x.sends==1);
    }
    {
        OrdinaryFixture x;PrepareRecovery(x);Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Arm(r,x);PausePair(r,x);Use(r,x,9);
        CHECK(!r.Tick(x.manager,true,true,Config{},1600,1));
        CHECK(!r.Tick(x.manager,true,true,Config{},1599,1) && r.OrdinaryBlocked());
        CHECK(x.sends==1); // reversal after baseline is not a same-time sample
    }
}
}
int main(){try{
    RunEmptyFallback();RunSameGenerationRearm();RunSeventeenThreshold();RunFallbackBoundaries();RunTwentyEmptyFallbacks();
    RunGates();
    // Real failure sequence: the incoming63 is not an ACK and cannot heal the
    // retired63->62 proof. Later native use plus complete resampling may plan
    // a different source/amount, under a NEW policy request ID.
    OrdinaryFixture x;PrepareOrdinary(x);
    Put(&x.actualSlots[9],34,std::uint8_t{6});Put(&x.actualSlots[2],34,std::uint8_t{57});
    x.actualSlots[10]=x.source;Put(&x.actualSlots[10],34,std::uint8_t{20});
    Runtime r(OrdinaryApi(),OrdinaryWorld(),base);Config c;c.preRestockThreshold=64;
    CHECK(r.SelectBackend(Backend::RemoteOrdinary,10,100));
    CHECK(r.Tick(x.manager,true,true,c,100,1)==TransferResult::Queued && x.sends==1);
    Use(r,x,62);auto restored=x.actualSlots[2];Put(&restored,34,std::uint8_t{63});
    Incoming(r,x,9,x.actualSlots[9],209);Incoming(r,x,2,restored,210);
    CHECK(!r.Tick(x.manager,true,true,c,211,1) && !r.OrdinaryBlocked() && x.sends==1);
    Use(r,x,62);
    CHECK(!r.Tick(x.manager,true,true,c,1599,1) && x.sends==1);
    CHECK(!r.Tick(x.manager,true,true,c,1600,1) && x.sends==1); // first full baseline
    Use(r,x,61);
    CHECK(r.Tick(x.manager,true,true,c,1650,1)==TransferResult::Queued && x.sends==2);
    CHECK(Read<std::uint8_t>(&x.actualSlots[10],34)==17 && Read<std::uint8_t>(&x.actualSlots[2],34)==64);
    int requests=0;while(auto e=r.TakeDiagnostic()){
        CHECK(e->kind!=DiagnosticKind::Response && e->kind!=DiagnosticKind::Reconciled && e->kind!=DiagnosticKind::NativeResult);
        if(e->kind==DiagnosticKind::Request){++requests;CHECK(e->requestId==static_cast<std::uint64_t>(requests));}
        CHECK(e->kind!=DiagnosticKind::PredictionSettled); // old request was retired, never settled
    }
    CHECK(requests==2);CHECK(!r.OrdinaryBlocked());
    std::cout<<"PASS incoming synchronization retires old proof; complete fresh resampling and native use create a new request\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
