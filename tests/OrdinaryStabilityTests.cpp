#include "OrdinaryFixture.h"

// External ABI only is simulated. Runtime/Policy/UseTracker/Identity/native
// transfer orchestration are production code. Time advances explicitly: this
// is a long-sequence regression, not a wall-clock/network/game soak test.
struct Trial {
    OrdinaryFixture x;
    Runtime runtime{OrdinaryApi(),OrdinaryWorld(),base};
    std::array<StackStorage,9> protectedHotbar{};
    void* player{};
    alignas(8) std::uintptr_t operation[2]{};
    std::uint64_t now=100,pendingId{},submittedAt{},lastId{};
    int initial{},used{},usedAtSubmit{},queued{},settled{},resets{};
    bool pending{};

    Trial(int sourceSize,int mainSize,int sourceSlots=27){
        PrepareOrdinary(x);
        for(int slot=0;slot<36;++slot){
            x.actualSlots[slot]=x.source;
            Put(&x.actualSlots[slot],34,static_cast<std::uint8_t>(slot<9?64:slot<9+sourceSlots?sourceSize:0));
        }
        Put(&x.actualSlots[2],34,static_cast<std::uint8_t>(mainSize));
        for(int slot=0;slot<9;++slot)protectedHotbar[slot]=x.actualSlots[slot];
        initial=mainSize+sourceSlots*sourceSize;player=x.player;
        operation[0]=base+0x25B6660;operation[1]=reinterpret_cast<std::uintptr_t>(&player);
        CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,now));
    }
    int Count(int slot) const{return Read<std::uint8_t>(&x.actualSlots[slot],34);}
    void Drain(){
        while(auto e=runtime.TakeDiagnostic()){
            CHECK(e->ordinary && !e->integrated && e->nativeId==0 && e->nativeStatus==-1);
            if(e->kind==DiagnosticKind::Request){
                CHECK(!pending && e->result==int(TransferResult::Queued));
                CHECK(e->requestId==lastId+1 && e->source>=9 && e->source<=35 && e->destination==2);
                CHECK(e->amount>0 && e->amount<=64);
                if(lastId)CHECK(e->time>=submittedAt+1500);
                for(int slot=9;slot<e->source;++slot)CHECK(Count(slot)==0);
                pending=true;pendingId=e->requestId;lastId=e->requestId;submittedAt=e->time;
                usedAtSubmit=used;++queued;
            }else if(e->kind==DiagnosticKind::PredictionSettled){
                CHECK(pending && e->requestId==pendingId && e->time>=submittedAt+1500);
                CHECK(e->accountedProgress && e->consumptionChainValid && e->inventorySampled && e->realMatches);
                CHECK(e->provenConsumption==used-usedAtSubmit);
                pending=false;++settled;
            }else{
                CHECK(e->kind==DiagnosticKind::ContextReset && !pending && e->settledWatchCleared);
                ++resets;
            }
        }
        CHECK(runtime.DroppedDiagnostics()==0);
    }
    void Conserved() const{
        int total=Count(2);
        for(int slot=9;slot<36;++slot)total+=Count(slot);
        CHECK(total+used==initial);
        for(int slot=0;slot<9;++slot)if(slot!=2)
            CHECK(std::memcmp(&x.actualSlots[slot],&protectedHotbar[slot],sizeof(StackStorage))==0);
        CHECK(x.sends==queued && x.setterCalls==2*queued && !x.balance && !x.changedSlots);
        // One owned identity exemplar is intentionally retained for this one
        // metadata class until context reset. No transaction clones may leak.
        CHECK(!x.requests && !x.allocated && x.owned.size()<=1);
    }
    void Tick(bool foreground=true,bool hidden=true){
        auto result=runtime.Tick(x.manager,foreground,hidden,Config{},now,1);
        if(result)CHECK(*result==TransferResult::Queued);
        Drain();CHECK(!runtime.Faulted() && !runtime.OrdinaryBlocked());Conserved();
    }
    void UseOne(){
        int count=Count(2);CHECK(count>0);
        auto capture=runtime.BeginUse(x.inventory,operation,true,true,Config{},1);CHECK(capture);
        Put(&x.actualSlots[2],34,static_cast<std::uint8_t>(count-1));++used;
        runtime.EndUse(*capture,true,true,Config{},1);now+=50;Tick();
    }
    void Settle(bool depleteDuringFence=false){
        int iterations=0;
        while(pending){
            CHECK(++iterations<=60);
            if(depleteDuringFence){
                int count=Count(2);
                for(int n=0;n<count;++n)UseOne();
                CHECK(Count(2)==0);
            }
            int before=queued;auto deadline=submittedAt+1500;
            CHECK(now<deadline);
            now=deadline-1;Tick();CHECK(queued==before && pending);
            now=deadline;Tick();
        }
        CHECK(queued==settled);
    }
    void Idle(){
        int before=queued;
        for(int n=0;n<600;++n){now+=50;Tick();CHECK(queued==before);}
    }
    void InspectBoundary(int gate){
        CHECK(!pending);
        int before=resets;
        if(gate==2)hud=false;
        for(int n=0;n<25;++n){now+=50;Tick(gate!=0,gate!=1);}
        hud=true;
        CHECK(resets==before+1); // idle gates never emit a repeated reset
        CHECK(runtime.SelectBackend(Backend::RemoteOrdinary,10,now));
        ++now;Tick();
    }
};

void TwentyCycles(bool inspect,bool useDuringFence){
    // Break caught: a hidden request limit, stale proof across re-arm/reset,
    // broken multi-source continuation or serial fence cannot finish these
    // twenty fills with exact per-use conservation and unchanged hotbar slots.
    Trial t(64,6);t.Tick();CHECK(!t.queued);
    for(int cycle=0;cycle<20;++cycle){
        int before=t.queued;
        for(int n=0;n<(cycle==0?1:59);++n)t.UseOne();
        CHECK(t.pending && t.queued==before+1);
        if(useDuringFence)for(int n=0;n<3;++n)t.UseOne();
        t.Settle();CHECK(t.Count(2)==64);
        if(inspect)t.InspectBoundary(cycle%3);
        t.Idle();
    }
    // Hand-derived totals:1734 initial;1122 uses without fence consumption,
    //1182 with3 additional uses per cycle. Extra hotbar stacks are excluded.
    CHECK(t.used==(useDuringFence?1182:1122));
    CHECK(t.resets==(inspect?20:0));
    if(!useDuringFence){
        CHECK(t.queued==38 && t.settled==38);
        for(int slot=9;slot<=26;++slot)CHECK(t.Count(slot)==0);
        CHECK(t.Count(27)==36);
        for(int slot=28;slot<=35;++slot)CHECK(t.Count(slot)==64);
    }else{
        CHECK(t.queued>=40 && t.queued==t.settled);
        for(int slot=9;slot<=27;++slot)CHECK(t.Count(slot)==0);
        CHECK(t.Count(28)==40);
        for(int slot=29;slot<=35;++slot)CHECK(t.Count(slot)==64);
    }
    t.Conserved();
    std::cout<<"PASS20 cycles inspect="<<inspect<<" useDuringFence="<<useDuringFence
        <<" queued="<<t.queued<<" used="<<t.used<<" logicalMs="<<t.now-100<<'\n';

    // Break caught: lengthy success must not disable the latest correction
    // watch. UI must cancel that plan, and cannot authorize a native transfer.
    // For the inspect case, establish a new watch after the final UI reset.
    if(inspect){
        for(int n=0;n<59;++n)t.UseOne();
        t.Settle();CHECK(t.Count(2)==64);
    }
    int before=t.x.sends;
    Put(&t.x.actualSlots[2],34,std::uint8_t{63}); // unexplained late correction
    CHECK(!t.runtime.Tick(t.x.manager,true,true,Config{},++t.now,1));
    CHECK(t.runtime.OrdinaryBlocked() && !t.runtime.Faulted());
    auto event=t.runtime.TakeDiagnostic();CHECK(event && event->kind==DiagnosticKind::PredictionStopped);
    CHECK(event->inventorySampled && event->actualDestinationCount==63 && !event->useEvidence.destinationCountMatches);
    for(int n=0;n<100;++n){
        t.now+=50;
        CHECK(!t.runtime.Tick(t.x.manager,false,true,Config{},t.now,1));
        if(n==0){
            auto tail=t.runtime.TakeDiagnostic();
            CHECK(tail && tail->kind==DiagnosticKind::PostStopSummary && tail->postStopEndReason==2 && tail->nativeWriteCount==0);
        }
    }
    CHECK(t.x.sends==before && !t.runtime.TakeDiagnostic() && t.x.owned.empty());
    CHECK(t.runtime.SelectBackend(Backend::RemoteOrdinary,10,++t.now));
    CHECK(!t.runtime.Tick(t.x.manager,true,true,Config{},t.now,1));
    CHECK(!t.runtime.Tick(t.x.manager,true,true,Config{},t.now+50,1));
    CHECK(!t.runtime.OrdinaryBlocked() && t.x.sends==before);
    int rearmed=0;while(auto e=t.runtime.TakeDiagnostic()){
        CHECK(e->kind==DiagnosticKind::PredictionRearmed || e->kind==DiagnosticKind::ResampleBaseline || e->kind==DiagnosticKind::ResampleReady);
        if(e->kind==DiagnosticKind::PredictionRearmed)++rearmed;
    }CHECK(rearmed==1);
}

void ExhaustAllSources(bool useDuringFence){
    // Break caught: off-by-one scanning of the27 sources, an unbounded fill
    // retry, or taking an unselected hotbar stack after exhaustion.
    Trial t(1,5);t.Tick();t.Settle(useDuringFence);
    CHECK(t.queued==27 && t.settled==27 && t.Count(2)==(useDuringFence?0:32));
    for(int slot=9;slot<=35;++slot)CHECK(t.Count(slot)==0);
    if(!useDuringFence)for(int n=0;n<32;++n)t.UseOne();
    t.Idle();CHECK(t.Count(2)==0 && t.used==32 && t.queued==27);
    t.Conserved();
    std::cout<<"PASS all27 sources exhausted, useDuringFence="<<useDuringFence<<", native-use empty hand and no hotbar fallback/replay\n";
}

void ManualTenCycleReference(){
    // Literal checkpoints for the shipped manual trial. A skipped source,
    // source reuse or wrong refill amount must break the visible totals.
    Trial t(64,6,10);t.Tick();
    for(int cycle=1;cycle<=10;++cycle){
        for(int n=0;n<(cycle==1?1:59);++n)t.UseOne();
        t.Settle();CHECK(t.Count(2)==64);
        if(cycle==5){
            CHECK(t.used==237 && t.queued==9 && t.Count(13)==25);
            for(int slot=9;slot<=12;++slot)CHECK(t.Count(slot)==0);
            for(int slot=14;slot<=18;++slot)CHECK(t.Count(slot)==64);
        }
        if(cycle>=5)t.InspectBoundary(cycle%3);
    }
    CHECK(t.used==532 && t.queued==19 && t.settled==19 && t.Count(18)==50);
    for(int slot=9;slot<=35;++slot)if(slot!=18)CHECK(t.Count(slot)==0);
    t.Conserved();
    std::cout<<"PASS manual ten-cycle checkpoints:main64/source345 after5,main64/source50 after10,19 serialized submissions\n";
}

int main(){try{
    TwentyCycles(false,false);
    TwentyCycles(true,false);
    TwentyCycles(false,true);
    ExhaustAllSources(false);
    ExhaustAllSources(true);
    ManualTenCycleReference();
    std::cout<<"PASS ordinary long-sequence stability; virtual time only, not live server acceptance\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
