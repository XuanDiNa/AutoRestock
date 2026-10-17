#include "AutoRestock/Integrated.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include "ActorFixture.h"
#define CHECK(x) do {if(!(x)) throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);}while(false)
using namespace autorestock;
constexpr ActorIdentity local{0x100,42,77,ActorRole::Local},server{0x200,42,77,ActorRole::Server};
int main(){try{
    {
        ActorFixture f;auto id=ReadActorIdentity(f.actor.data());
        CHECK(id.Valid() && id.role==ActorRole::Local && id.unique==42 && id.runtime==77);
        CHECK(id.registry==reinterpret_cast<std::uintptr_t>(f.registry.data()));
        f.Attach(f.actor.data(),7|(1u<<18));CHECK(!ReadActorIdentity(f.actor.data()).Valid());
        f.Attach(f.actor.data());f.components[1][3]=UINT64_MAX;CHECK(!ReadActorIdentity(f.actor.data()).Valid());
        f.components[1][3]=42;f.components[2][3]=0;CHECK(!ReadActorIdentity(f.actor.data()).Valid());
        f.components[2][3]=77;f.componentPages[1]=0;CHECK(!ReadActorIdentity(f.actor.data()).Valid());
        ActorFixture s(false,9);id=ReadActorIdentity(s.actor.data());CHECK(id.Valid() && id.role==ActorRole::Server);
        s.sparse[0][9]=UINT32_MAX;CHECK(!ReadActorIdentity(s.actor.data()).Valid());
    }
    IntegratedBridge b;
    b.Local(local,100);
    CHECK(!b.Ready(local,100)); // remote: no in-process server, no send
    b.EndServer(server,{},1,101);
    CHECK(b.Ready(local,102)); // catches missing/wrong dual-ID pairing
    CHECK(!b.Ready(local,602));
    CHECK(!b.Arm(local,102,-3,1));
    CHECK(!b.Arm(local,102,0,1));
    CHECK(!b.Arm(local,102,-4,0));
    CHECK(b.Arm(local,102,-4,1));
    CHECK(b.Pending() && b.PendingNativeId()==-4 && !b.Poll());
    CHECK(!b.Arm(local,103,-6,2));
    CHECK(!b.BeginServer(server,-6,103).token);
    auto wrong=server;wrong.unique=43;
    CHECK(!b.BeginServer(wrong,-4,103).token);
    wrong=server;wrong.runtime=78;CHECK(!b.BeginServer(wrong,-4,103).token);
    wrong=server;wrong.registry=local.registry;CHECK(!b.BeginServer(wrong,-4,103).token);
    auto ticket=b.BeginServer(server,-4,104);CHECK(ticket.token && ticket.policyId==1);
    CHECK(!b.Poll()); // capture and client prediction are not acceptance
    b.EndServer(server,ticket,1,105);
    auto done=b.Poll();CHECK(done && done->requestId==1 && done->nativeId==-4 && done->result==Completion::Accepted && done->nativeStatus==1);
    b.Clear();CHECK(!b.Pending() && !b.Poll());
    b.Local(local,106);CHECK(b.Arm(local,107,-6,2));
    b.EndServer(server,ticket,1,108);CHECK(!b.Poll()); // late result cannot complete a different request
    ticket=b.BeginServer(server,-6,109);CHECK(ticket.token);
    b.Invalidate();CHECK(b.Pending()); // integer fence survives context cancellation
    CHECK(!b.Ready(local,110));
    b.EndServer(server,ticket,7,110);
    done=b.Poll();CHECK(done && done->result==Completion::Rejected && done->nativeStatus==7);
    b.Clear();b.Local(local,111);CHECK(!b.Ready(local,112)); // old seen sample cannot reauthorize reset
    b.EndServer(server,{},1,113);CHECK(b.Ready(local,114));
    auto next=local;next.runtime=88;b.Local(next,115);CHECK(!b.Ready(next,116));
    // Server writer/client reader cannot accept torn copied identifiers.
    IntegratedBridge race;std::atomic<bool> running=true,bad=false;
    std::thread writer([&]{for(int i=0;i<100000;++i){race.Local(local,100);race.Invalidate();}running=false;});
    while(running){race.EndServer(server,{},1,100);auto t=race.BeginServer(server,-4,100);if(t.token) bad=true;}
    writer.join();CHECK(!bad);
    {
        IntegratedBridge sync;InventoryCounts counts{};counts[2]=6;counts[9]=64;counts[35]=7;
        sync.Local(local,100);CHECK(!sync.AuthoritativeCounts(local,100));
        sync.EndServer(server,{},1,101,counts);
        auto got=sync.AuthoritativeCounts(local,102);CHECK(got && (*got)[2]==6 && (*got)[9]==64 && (*got)[35]==7);
        counts[2]=5; // copied ownership: editing caller storage cannot change published data
        CHECK((*sync.AuthoritativeCounts(local,102))[2]==6);
        sync.EndServer(server,{},1,103,counts);CHECK((*sync.AuthoritativeCounts(local,104))[2]==5);
        CHECK(!sync.AuthoritativeCounts(local,99) && !sync.AuthoritativeCounts(local,604));
        auto other=local;other.runtime=88;CHECK(!sync.AuthoritativeCounts(other,104));
        auto unrelated=server;unrelated.unique=99;counts[2]=9;sync.EndServer(unrelated,{},1,105,counts);
        CHECK((*sync.AuthoritativeCounts(local,106))[2]==5);
        sync.EndServer(server,{},1,107);CHECK(!sync.AuthoritativeCounts(local,108));
        counts[2]=65;sync.EndServer(server,{},1,109,counts);CHECK(!sync.AuthoritativeCounts(local,110));
        counts[2]=5;sync.EndServer(server,{},1,111,counts);CHECK(sync.AuthoritativeCounts(local,112));
        sync.Invalidate();sync.Local(local,113);CHECK(!sync.AuthoritativeCounts(local,114));
        sync.EndServer(server,{},1,115,counts);CHECK(sync.AuthoritativeCounts(local,116));
    }
    {
        // Original player -> supplies -> Inventory36 vector, independently
        // constructed; not the abbreviated transaction fixture's fake vector.
        alignas(8) std::byte player[1472]{},supplies[192]{},inventory[440]{},stacks[36*152]{};
        constexpr std::uintptr_t image=0x140000000;
        ActorFixture::Write(player,1464,static_cast<void*>(supplies));ActorFixture::Write(supplies,184,static_cast<void*>(inventory));
        ActorFixture::Write(inventory,0,image+0xE6AFE10);ActorFixture::Write(inventory,8,std::uint8_t{255});
        ActorFixture::Write(inventory,432,static_cast<void*>(player));ActorFixture::Write(inventory,408,static_cast<void*>(stacks));
        ActorFixture::Write(inventory,416,static_cast<void*>(stacks+36*152));
        for(unsigned i=0;i<36;++i)ActorFixture::Write(stacks,152*i+34,static_cast<std::uint8_t>(i));
        auto got=ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image);CHECK(got && (*got)[0]==0 && (*got)[9]==9 && (*got)[35]==35);
        ActorFixture::Write(inventory,0,image+0xE69DAB0);
        CHECK(ReadInventoryCounts(*FindNativeProfile(GameVersion::V45),player,image));
        CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image));
        ActorFixture::Write(inventory,0,image+0xE6AFE10);
        CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),nullptr,image) && !ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,0));
        ActorFixture::Write(inventory,416,static_cast<void*>(stacks+35*152));CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image));
        ActorFixture::Write(inventory,416,static_cast<void*>(stacks+36*152));
        ActorFixture::Write(inventory,0,image+0xEA05D70);CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image));
        ActorFixture::Write(inventory,0,image+0xE6AFE10);ActorFixture::Write(inventory,432,static_cast<void*>(nullptr));
        CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image));ActorFixture::Write(inventory,432,static_cast<void*>(player));
        ActorFixture::Write(stacks,35*152+34,std::uint8_t{65});CHECK(!ReadInventoryCounts(*FindNativeProfile(GameVersion::V52),player,image));
    }
    {
        IntegratedBridge sync;sync.Local(local,100);std::atomic<bool> finished=false,torn=false;
        std::thread publisher([&]{for(unsigned n=0;n<20000;++n){InventoryCounts c;c.fill(n&1?5:6);sync.EndServer(server,{},1,100,c);}finished=true;});
        while(!finished.load())if(auto c=sync.AuthoritativeCounts(local,100))for(auto count:*c)if(count!=(*c)[0])torn=true;
        publisher.join();CHECK(!torn);
    }
    CHECK(!ReadActorIdentity(nullptr).Valid());
    std::cout<<"Integrated bridge tests passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
