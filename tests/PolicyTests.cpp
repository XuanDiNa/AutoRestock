#include "AutoRestock/Policy.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace autorestock;
Item Stack(int count, std::string id = "stone", int max = 64) { return {id, id, count, max, false}; }
Snapshot World(int count = 5) {
    Snapshot s;
    s.now = 100; s.player = 1; s.world = 2; s.eligible = true; s.selected = 3;
    s.items[3] = Stack(count); s.items[9] = Stack(64);
    return s;
}
void Expect(std::optional<Transfer> got, int source, int target, int amount) {
    CHECK(got && got->source==source && got->destination==target && got->amount==amount && got->requestId>0);
}
int main() {
    try {
        Config c;
        { Policy p; auto s = World(6); CHECK(!p.Tick(s,c)); s.items[3].count = 5; Expect(p.Tick(s,c),9,3,59); }
        { Policy p; auto s = World(15); s.items[3].capacity = 16; s.items[9].capacity = 16;
          s.items[9].count = 16; c.preRestockThreshold = 64; Expect(p.Tick(s,c),9,3,1); c = {}; }
        { Policy p; auto s = World(1); s.items[3].capacity = 1; CHECK(!p.Tick(s,c)); }
        { Policy p; auto s = World(); s.items[0] = Stack(64); s.items[8] = Stack(64);
          s.items[9] = {}; s.items[35] = Stack(30); Expect(p.Tick(s,c),35,3,30); }
        { Policy p; auto s = World(); s.items[9].identity = "stone{custom_name:x}";
          s.items[10] = Stack(3); s.items[11] = Stack(64); Expect(p.Tick(s,c),10,3,3);
          s.now += 50; s.items[3].count = 8; s.items[10] = {};
          CHECK(!p.Tick(s,c)); // local prediction alone is not server acceptance
          s.completion = Completion::Accepted; s.completionId=1; Expect(p.Tick(s,c),11,3,56);
          s.items[3].count = 64; s.items[11].count = 8; CHECK(!p.Tick(s,c)); }
        { Policy p; auto s = World(); Expect(p.Tick(s,c),9,3,59);
          for (int i=0;i<10;++i) { s.now+=50; CHECK(!p.Tick(s,c)); }
          s.now += Policy::timeoutMs; CHECK(!p.Tick(s,c));
          s.now += 5000; CHECK(!p.Tick(s,c)); // no retry flood after timeout
          ++s.useSequence; Expect(p.Tick(s,c),9,3,59); }
        { Policy p; auto s = World(); Expect(p.Tick(s,c),9,3,59);
          s.completion = Completion::Rejected; s.completionId=1; CHECK(!p.Tick(s,c));
          s.completion = Completion::Waiting; s.now+=1000; CHECK(!p.Tick(s,c)); }
        { Policy p; auto s = World(0); CHECK(!p.Tick(s,c));
          s.depletedByUse = Stack(1); s.useSequence=1; Expect(p.Tick(s,c),9,3,64); }
        { Policy p; auto s = World(); s.intentionalRemoval=true; CHECK(!p.Tick(s,c));
          s.intentionalRemoval=false; CHECK(!p.Tick(s,c)); ++s.useSequence; Expect(p.Tick(s,c),9,3,59); }
        { Policy p; auto s = World(0); s.items[9] = {"pick{damage:3,ench:a}", "pick{ench:a}",1,1,true};
          s.items[10] = {"pick{damage:4,ench:b}", "pick{ench:b}",1,1,true};
          s.depletedByUse = Item{"pick{damage:100,ench:b}", "pick{ench:b}",1,1,true};
          s.useSequence=1; Expect(p.Tick(s,c),10,3,1); }
        for (int mode=0;mode<7;++mode) {
            Policy p; auto s = World(); Expect(p.Tick(s,c),9,3,59);
            s.items[3].count = 0; s.depletedByUse.reset(); s.now+=50;
            if (mode==0) { s.eligible=false; CHECK(!p.Tick(s,c)); s.eligible=true; }
            if (mode==1) { c.enabled=false; CHECK(!p.Tick(s,c)); c.enabled=true; }
            if (mode==2) s.selected=4;
            if (mode==3) ++s.player;
            if (mode==4) ++s.world;
            if (mode==5) s.items[9].identity="other";
            if (mode==6) { s.now=1; }
            CHECK(!p.Tick(s,c));
            s.now+=100; CHECK(!p.Tick(s,c));
        }
        { Policy p; auto s=World(); s.items[9].count=0; CHECK(!p.Tick(s,c));
          s.items[9]=Stack(12); Expect(p.Tick(s,c),9,3,12); }
        for (int invalid : {-1,9,100}) { Policy p; auto s=World(); s.selected=invalid; CHECK(!p.Tick(s,c)); }
        for (int invalid : {-1,65,255}) { Policy p; auto s=World(); s.items[9].count=invalid; CHECK(!p.Tick(s,c)); }
        { Policy p; auto s=World(); s.items[3].identity.clear(); CHECK(!p.Tick(s,c)); }
        { Policy p; auto s=World(0); s.depletedByUse=Stack(1); CHECK(!p.Tick(s,c)); } // no fresh native use
        { Policy p; auto s=World(); s.items[9]=Stack(2); s.items[10]=Stack(2); s.items[11]=Stack(64);
          Expect(p.Tick(s,c),9,3,2); s.items[3].count=7; s.items[9]={};
          s.completion=Completion::Accepted; s.completionId=999; CHECK(!p.Tick(s,c));
          s.completionId=1; Expect(p.Tick(s,c),10,3,2);
          s.items[3].count=9; s.items[10]={}; CHECK(!p.Tick(s,c)); // prior request's ack cannot accept request 2
          s.completionId=2; Expect(p.Tick(s,c),11,3,55); }
        { Policy p;auto s=World();s.items[9]=Stack(2);s.items[10]=Stack(64);
          Expect(p.Tick(s,c),9,3,2);s.items[9]={};s.items[3].count=7;
          s.completion=Completion::Accepted;s.completionId=1;s.transferReady=false;
          CHECK(!p.Tick(s,c));s.completion=Completion::Waiting;s.transferReady=true;
          Expect(p.Tick(s,c),10,3,57); } // reconcile while waiting, no new transfer until ready
        { Policy p;auto s=World();Expect(p.Tick(s,c),9,3,59);
          p.Defer(999);CHECK(!p.Tick(s,c)); // unrelated defer must not release the current fence
          p.Defer(1);Expect(p.Tick(s,c),9,3,59); } // no native send occurred, so do not inhibit
        { Policy p;auto s=World();s.items[9]=Stack(3);s.items[10]=Stack(64);
          Expect(p.Tick(s,c),9,3,3);s.items[9]={};s.items[3].count=7;s.useConsumption=UseConsumption{1,1};
          s.now=150;CHECK(!p.Tick(s,c)); //8 predicted,7 actual after one proven use, no acceptance yet
          s.items[3].count=6;s.useConsumption->count=2;s.useSequence=2;s.now=200;
          CHECK(!p.Tick(s,c)); // updated proof must not cancel pending policy
          s.completion=Completion::Accepted;s.completionId=1;Expect(p.Tick(s,c),10,3,58);
          s.items[3].count=64;s.items[10].count=6;s.completionId=2;s.useConsumption.reset();
          CHECK(!p.Tick(s,c)); } // continuation uses real6, not reconstructed8
        for(int mode=0;mode<5;++mode){
            Policy p;auto s=World();s.items[9]=Stack(3);s.items[10]=Stack(64);Expect(p.Tick(s,c),9,3,3);
            s.items[9]={};s.items[3].count=7;s.completion=Completion::Accepted;s.completionId=1;
            if(mode==0)s.useConsumption=UseConsumption{999,1};
            if(mode==1)s.useConsumption=UseConsumption{1,-1};
            if(mode==2)s.useConsumption=UseConsumption{1,9};
            if(mode==3)s.useConsumption=UseConsumption{1,2};
            CHECK(!p.Tick(s,c)); // no/wrong/unbounded/mismatched proof must not authorize continuation
            s.useConsumption.reset();s.items[3].count=8;CHECK(!p.Tick(s,c));
        }
        for(int mode=0;mode<6;++mode){
            Policy p;auto s=World();s.items[9]=Stack(3);s.items[10]=Stack(64);
            Expect(p.Tick(s,c),9,3,3);s.items[9]={};s.items[3].count=7;
            s.useConsumption=UseConsumption{1,1};s.completionId=1;s.completion=Completion::Predicted;
            if(mode==0){s.now=1599;CHECK(!p.Tick(s,c));s.now=1600;Expect(p.Tick(s,c),10,3,57);}
            if(mode==1){s.now=1700;Expect(p.Tick(s,c),10,3,57);} // delayed owner sample is allowed
            if(mode==2){s.now=1600;s.completion=Completion::Accepted;CHECK(!p.Tick(s,c));} // ACK timeout unchanged
            if(mode==3){s.now=1600;s.completionId=999;CHECK(!p.Tick(s,c));}
            if(mode==4){s.now=1600;s.useConsumption.reset();CHECK(!p.Tick(s,c));}
            if(mode==5){s.now=1600;s.items[9]=Stack(1);CHECK(!p.Tick(s,c));}
        }
        std::cout << "PASS: threshold/capacity, main-27 only, metadata, multi-source fill, ack/progress, timeout/refusal, world-use/tool-break, cancellation, request-bound consumed-count reconciliation, explicit ordinary prediction fence\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
