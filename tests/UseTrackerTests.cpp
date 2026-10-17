#include "AutoRestock/UseTracker.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
Item Stack(int n){return {"native-class-A","",n,64,false};}
Snapshot Sample(int count){Snapshot s;s.eligible=true;s.selected=2;s.items[2]=Stack(count);return s;}
int main(){try{
    UseContext context{1,2,3,2};UseTracker t;
    auto s=Sample(8);t.Apply(context,s);CHECK(!s.intentionalRemoval && !s.depletedByUse && s.useSequence==0);
    t.Observe(context,Stack(8),context,Stack(7));s=Sample(7);t.Apply(context,s);
    CHECK(s.useSequence>0 && !s.intentionalRemoval && !s.depletedByUse);auto first=s.useSequence;
    s=Sample(0);t.Apply(context,s);CHECK(s.intentionalRemoval && !s.depletedByUse); // drop, not observed use
    t.Reset();s=Sample(1);t.Apply(context,s);
    t.Observe(context,Stack(1),context,Item{});s=Sample(0);t.Apply(context,s);
    CHECK(s.depletedByUse && s.depletedByUse->identity=="native-class-A" && !s.intentionalRemoval && s.useSequence>first);
    s=Sample(0);t.Apply(context,s);CHECK(!s.depletedByUse); // one-shot reference
    t.Reset();s=Sample(3);t.Apply(context,s);
    t.Observe(context,Stack(3),context,Stack(2));s=Sample(0);t.Apply(context,s);
    CHECK(s.intentionalRemoval && !s.depletedByUse); // use then drop before the next sample
    t.Reset();s=Sample(1);t.Apply(context,s);
    auto other=context;other.selected=3;
    t.Observe(context,Stack(1),other,Item{});s=Sample(0);t.Apply(context,s);
    CHECK(!s.depletedByUse && !s.intentionalRemoval); // selection boundary clears the entire old context
    t.Reset();s=Sample(1);t.Apply(context,s);
    t.Observe(context,Stack(1),context,Item{});other.world=4;s=Sample(0);t.Apply(other,s);
    CHECK(!s.depletedByUse && !s.intentionalRemoval); // old-world event cannot cross contexts
    t.Reset();t.Observe(context,Stack(0),context,Item{});s=Sample(0);t.Apply(context,s);CHECK(!s.depletedByUse);
    {
        PendingUseProgress progress;progress.Arm({9,2,20,7},Stack(20),Stack(5));
        progress.Observe(7,Stack(25),Stack(24));auto proof=progress.Sample(Stack(0),Stack(24));
        CHECK(proof && proof->requestId==7 && proof->count==1);
        progress.Observe(7,Stack(24),Stack(24)); // legitimate no-op cannot break or advance the chain
        progress.Observe(7,Stack(24),Stack(22));proof=progress.Sample(Stack(0),Stack(22));
        CHECK(proof && proof->count==3);
        progress.Observe(7,Stack(22),Item{});proof=progress.Sample(Stack(0),Item{});CHECK(proof && proof->count==25);
        progress.Reset();CHECK(!progress.Sample(Stack(0),Item{}));
    }
    // Break caught: diagnostics must never sample/repair/invalidate the proof,
    // even when asked to describe an inconsistent pair. The first rejected
    // callback must survive later ignored callbacks, and reset/re-arm must not
    // leak that old evidence into the next request.
    {
        PendingUseProgress progress;progress.Arm({9,2,20,7},Stack(20),Stack(5));
        auto waiting=progress.Inspect(Stack(20),Stack(5));
        CHECK(waiting.armed && waiting.chainValid && !waiting.observedUses);
        CHECK(waiting.expectedSourceCount==0 && waiting.expectedDestinationCount==25);
        CHECK(!waiting.sourceCountMatches && !waiting.destinationCountMatches);
        CHECK(!progress.Sample(Stack(20),Stack(5))); // unchanged is still waiting
        auto mismatch=progress.Inspect(Stack(1),Stack(24));
        CHECK(mismatch.chainValid && !mismatch.sourceCountMatches && !mismatch.destinationCountMatches);
        auto proof=progress.Sample(Stack(0),Stack(25));CHECK(proof && !proof->count);
        progress.Observe(7,Stack(25),Stack(25));
        progress.Observe(8,Stack(25),Stack(24));
        auto invalid=progress.Inspect(Stack(0),Stack(24));
        CHECK(!invalid.chainValid && invalid.observedUses==2 && !invalid.consumed);
        CHECK(!invalid.lastRequestMatches && invalid.lastBeforeMatches && invalid.lastResultValid);
        CHECK(invalid.lastBeforeCount==25 && invalid.lastAfterCount==24 && invalid.lastExpectedBeforeCount==25);
        progress.Observe(7,Stack(25),Stack(23));
        auto retained=progress.Inspect(Stack(0),Stack(25));
        CHECK(retained.observedUses==2 && retained.lastAfterCount==24 && !retained.lastRequestMatches);
        CHECK(!progress.Sample(Stack(0),Stack(25))); // inspecting correct counts cannot heal
        progress.Reset();auto cleared=progress.Inspect(Stack(0),Item{});
        CHECK(!cleared.armed && !cleared.chainValid && !cleared.observedUses);
        CHECK(cleared.expectedSourceCount==-1 && cleared.expectedDestinationCount==-1 && cleared.lastBeforeCount==-1);
        progress.Arm({9,2,20,9},Stack(20),Stack(5));
        auto fresh=progress.Inspect(Stack(0),Stack(25));
        CHECK(fresh.armed && fresh.chainValid && fresh.observedUses==0 && fresh.lastBeforeCount==-1);
        proof=progress.Sample(Stack(0),Stack(25));CHECK(proof && proof->requestId==9 && !proof->count);
    }
    // Break caught: trace collection must be bounded, ordered, copied, and
    // incapable of healing a rejected callback or changing consumption proof.
    {
        PendingUseProgress progress;progress.Arm({9,2,20,7},Stack(20),Stack(5));
        for(int n=0;n<6;++n){
            RecentUse trace;trace.recorded=true;trace.beginAt=100+n;trace.endAt=110+n;trace.invoker=2;
            trace.beforeNative={2,-10-n,true};trace.afterNative={2,-11-n,true};
            trace.beforeCount=999;trace.afterCount=888;trace.requestId=999; // caller text is not authoritative
            progress.Observe(7,Stack(25-n),Stack(24-n),trace);
        }
        auto proof=progress.Sample(Stack(0),Stack(19));CHECK(proof && proof->count==6);
        auto evidence=progress.Inspect(Stack(0),Stack(19));
        CHECK(evidence.chainValid && evidence.observedUses==6 && evidence.recentUseCount==4);
        CHECK(evidence.recentUses[0].beginAt==102 && evidence.recentUses[0].endAt==112);
        CHECK(evidence.recentUses[0].beforeCount==23 && evidence.recentUses[0].afterCount==22);
        CHECK(evidence.recentUses[3].beforeCount==20 && evidence.recentUses[3].afterCount==19);
        CHECK(evidence.recentUses[0].requestId==7 && evidence.recentUses[0].beforeNative.id==-12);
        CHECK(evidence.recentUses[3].afterNative.id==-16);
        CHECK(evidence.recentUses[3].requestMatches && evidence.recentUses[3].beforeMatches && evidence.recentUses[3].resultValid);
        RecentUse rejected;rejected.recorded=true;rejected.beginAt=200;rejected.endAt=201;
        progress.Observe(8,Stack(19),Stack(18),rejected);
        progress.Observe(7,Stack(19),Stack(18),rejected); // ignored after first rejection
        evidence=progress.Inspect(Stack(0),Stack(19));
        CHECK(!evidence.chainValid && evidence.observedUses==7 && evidence.recentUseCount==4);
        CHECK(evidence.recentUses[0].beginAt==103 && evidence.recentUses[3].requestId==8);
        CHECK(!evidence.recentUses[3].requestMatches && evidence.recentUses[3].beforeMatches);
        CHECK(!progress.Sample(Stack(0),Stack(19)));
        progress.Reset();evidence=progress.Inspect(Stack(0),Stack(19));
        CHECK(!evidence.recentUseCount && !evidence.recentUses[0].recorded);
        progress.Arm({9,2,20,9},Stack(20),Stack(5));evidence=progress.Inspect(Stack(0),Stack(25));
        CHECK(!evidence.recentUseCount && !evidence.recentUses[3].recorded);
        proof=progress.Sample(Stack(0),Stack(25));CHECK(proof && proof->requestId==9 && !proof->count);
    }
    for(int mode=0;mode<7;++mode){
        PendingUseProgress progress;progress.Arm({9,2,20,7},Stack(20),Stack(5));
        if(mode==0)progress.Observe(7,Stack(24),Stack(23)); // unobserved gap before use
        if(mode==1){progress.Observe(7,Stack(25),Stack(24));CHECK(!progress.Sample(Stack(0),Stack(23)));}
        if(mode==2)progress.Observe(8,Stack(25),Stack(24)); // capture bound to another request
        if(mode==3){auto renamed=Stack(24);renamed.identity="renamed";progress.Observe(7,Stack(25),renamed);}
        if(mode==4)progress.Observe(7,Stack(25),Stack(26)); // increase, not consumption
        if(mode==5)CHECK(!progress.Sample(Stack(1),Stack(25))); // wrong source
        if(mode==6)CHECK(!progress.Sample(Stack(0),Stack(24))); // unobserved loss then restored
        progress.Observe(7,Stack(25),Stack(24));CHECK(!progress.Sample(Stack(0),Stack(24)));
        progress.Arm({9,2,20,9},Stack(20),Stack(5));progress.Observe(7,Stack(25),Stack(24));
        CHECK(!progress.Sample(Stack(0),Stack(24))); // old callback cannot cross re-arm
    }
    std::cout<<"PASS: native-use provenance, depletion one-shot, drop/manual removal suppression, context cancellation and exact per-request consumption chains\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
