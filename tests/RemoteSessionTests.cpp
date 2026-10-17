#include "AutoRestock/RemoteSession.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <string>
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);} while(false)
using namespace autorestock;
NativePrimaryGuidRoute Route(std::uintptr_t connection=100) {
    return {{10,20,30},40,connection,50,60};
}
template<class Ledger> auto BornBound(Ledger& ledger,std::uintptr_t address=100) {
    auto token=ledger.Born(address,30,60);CHECK(token);
    CHECK(ledger.Bind(*token,{10,20,30},40));return *token;
}
template<class Ledger> auto Established(Ledger& ledger,std::uintptr_t address=100) {
    BornBound(ledger,address);
    auto start=ledger.Begin(Route(address),InventoryMode::Modern);CHECK(start);
    CHECK(ledger.Finish(*start,Route(address),70,80,90));return *start;
}
int main(){try{
    // Missing native constructor/notification provenance cannot be repaired by
    // a readable current route or a successful-looking StartGame alone.
    RemoteSession missing;
    CHECK(!missing.Begin(Route(),InventoryMode::Modern));
    RemoteSession ledger;auto token=ledger.Born(100,30,60);CHECK(token);
    CHECK(!ledger.Begin(Route(),InventoryMode::Modern));
    CHECK(ledger.Bind(*token,{10,20,30},40));
    auto start=ledger.Begin(Route(),InventoryMode::Modern);CHECK(start);
    CHECK(ledger.Select(90,80,70).backend==Backend::Unsupported);
    CHECK(ledger.Finish(*start,Route(),70,80,90));
    auto selected=ledger.Select(90,80,70);
    CHECK(selected.backend==Backend::RemoteModern && selected.generation!=0);
    CHECK(ledger.Authorize(Route(),90,selected.generation));
    CHECK(!ledger.Authorize(Route(),91,selected.generation));
    CHECK(!ledger.Authorize(Route(),90,selected.generation+1));
    for(auto field:{0,1,2,3,4,5,6}){
        auto wrong=Route();
        switch(field){case 0:wrong.binding.client++;break;case 1:wrong.binding.game++;break;
        case 2:wrong.binding.network++;break;case 3:wrong.handler++;break;
        case 4:wrong.connection++;break;case 5:wrong.activePeer++;break;case 6:wrong.guid++;break;}
        CHECK(!ledger.Authorize(wrong,90,selected.generation));
    }
    CHECK(ledger.Select(91,80,70).backend==Backend::Unsupported);
    CHECK(ledger.Select(90,81,70).backend==Backend::Unsupported);
    CHECK(ledger.Select(90,80,71).backend==Backend::Unsupported);
    // Lifetime reuse receives a fresh serial; an earlier captured StartGame
    // return cannot publish into the replacement even at identical addresses.
    RemoteSession reuse;auto old=Established(reuse);
    reuse.RetireConnection(100);CHECK(reuse.Select(90,80,70).backend==Backend::Unsupported);
    auto newer=BornBound(reuse);CHECK(newer.serial!=old.connection.serial);
    CHECK(!reuse.Finish(old,Route(),70,80,90));
    auto next=reuse.Begin(Route(),InventoryMode::Modern);CHECK(next);
    CHECK(reuse.Finish(*next,Route(),70,80,90));
    CHECK(!reuse.Authorize(Route(),90,old.serial));
    // Only destruction of a matching native identity cancels its capability.
    for(int kind=0;kind<4;++kind){
        RemoteSession retired;auto s=Established(retired);
        retired.RetireConnection(101);retired.RetireHandler(41);retired.RetireManager(91);retired.Disconnect(31);
        CHECK(retired.Authorize(Route(),90,s.serial));
        if(kind==0)retired.RetireConnection(100);
        if(kind==1)retired.RetireHandler(40);
        if(kind==2)retired.RetireManager(90);
        if(kind==3)retired.Disconnect(30);
        CHECK(!retired.Authorize(Route(),90,s.serial));
        CHECK(retired.Select(90,80,70).backend==Backend::Unsupported);
    }
    RemoteSession interrupted;BornBound(interrupted);
    auto pending=interrupted.Begin(Route(),InventoryMode::Modern);CHECK(pending);
    interrupted.RetireHandler(40);
    CHECK(!interrupted.Finish(*pending,Route(),70,80,90));
    RemoteSession changed;BornBound(changed);auto captured=changed.Begin(Route(),InventoryMode::Modern);CHECK(captured);
    auto wrong=Route();wrong.activePeer++;
    CHECK(!changed.Finish(*captured,wrong,70,80,90));
    CHECK(changed.Select(90,80,70).backend==Backend::Unsupported);
    // A native replay does not mint a second accepted session for one lifetime.
    RemoteSession duplicate;auto once=Established(duplicate);
    CHECK(!duplicate.Begin(Route(),InventoryMode::Modern));
    CHECK(!duplicate.Authorize(Route(),90,once.serial));
    RemoteSession ordinary;BornBound(ordinary);
    auto legacy=ordinary.Begin(Route(),InventoryMode::Ordinary);CHECK(legacy);
    CHECK(ordinary.Finish(*legacy,Route(),70,80,90));
    CHECK(ordinary.Select(90,80,70).backend==Backend::RemoteOrdinary);
    CHECK(!ordinary.Authorize(Route(),90,ordinary.Generation())); // typed modern reply authority stays separate
    RemoteSession wrongBinding;auto b=wrongBinding.Born(100,30,60);CHECK(b);
    CHECK(!wrongBinding.Bind(*b,{10,20,31},40));
    CHECK(!wrongBinding.Begin(Route(),InventoryMode::Modern));
    RemoteSession occupied;BornBound(occupied);
    CHECK(!occupied.Born(100,30,60));CHECK(occupied.Faulted());
    RemoteSession nested;BornBound(nested);CHECK(nested.Begin(Route(),InventoryMode::Modern));
    CHECK(!nested.Begin(Route(),InventoryMode::Modern));CHECK(nested.Faulted());
    RemoteSession foreign;auto alive=Established(foreign);
    std::thread unrelated([&]{foreign.Foreign(RemoteSession::Retired::Connection,101);});unrelated.join();
    CHECK(!foreign.Faulted());
    std::thread worker([&]{foreign.Foreign(RemoteSession::Retired::Connection,100);});worker.join();
    CHECK(!foreign.Authorize(Route(),90,alive.serial));
    foreign.Cancel();CHECK(foreign.Faulted() && !foreign.Born(101,30,60));
    RemoteSessionLedger<1> full;CHECK(full.Born(100,30,60));CHECK(!full.Born(101,30,60));CHECK(full.Faulted());
    RemoteSessionLedger<2,std::uint8_t> exhausted;
    for(unsigned i=0;i<300 && !exhausted.Faulted();++i){
        auto t=exhausted.Born(100,30,60);if(t)exhausted.RetireConnection(100);
    }
    CHECK(exhausted.Faulted() && !exhausted.Born(100,30,60));
    std::cout<<"PASS actual connection/handler/StartGame ordering, exact route authorization, retirement/reuse, permanent poison and bounded exhaustion\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
