#include "AutoRestock/NativeSession.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
constexpr std::uintptr_t base=0x140000000;
template<class T> void Put(void* p,std::size_t offset,T value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof value);}
struct Fixture {
    alignas(8) std::array<std::byte,432> client{};
    alignas(8) std::array<std::byte,2088> game{};
    alignas(8) std::array<std::byte,536> network{};
    Fixture(){
        Put(client.data(),0,base+0xE9731B0);
        Put(client.data(),424,static_cast<void*>(game.data()));
        Put(game.data(),0,base+0xE76E2D0);
        Put(game.data(),48,base+0xE76EAD0);
        Put(game.data(),2080,static_cast<void*>(network.data()));
        Put(network.data(),0,base+0xE75E2D0);
    }
};
struct RouteFixture : Fixture {
    alignas(8) std::array<std::byte,968> handler{};
    alignas(8) std::array<std::byte,176> packet{};
    alignas(8) std::array<std::byte,432> connection{},otherConnection{};
    std::array<std::uintptr_t,2> entries{};
    void* registration{};
    RouteFixture(){
        Put(network.data(),32,base+0xE75E2F0);
        Put(handler.data(),0,base+0xE7AF110);
        Put(handler.data(),88,static_cast<void*>(client.data()));
        Put(handler.data(),696,std::uint64_t{0x1122334455667788});
        Put(handler.data(),840,std::uint32_t{0});
        Put(handler.data(),960,std::uint8_t{1});
        Put(packet.data(),24,std::uint64_t{0x1122334455667788});
        Put(packet.data(),168,std::uint32_t{0});
        Put(connection.data(),24,std::uint64_t{0x1122334455667788});
        Put(connection.data(),168,std::uint32_t{0});
        Put(connection.data(),176,std::uint32_t{1});
        // An opaque equality token; this is intentionally not dereferenceable.
        Put(connection.data(),280,std::uintptr_t{0x1234});
        registration=handler.data();
        Put(network.data(),472,static_cast<void*>(&registration));
        entries[0]=reinterpret_cast<std::uintptr_t>(connection.data());
        auto first=reinterpret_cast<std::uintptr_t>(entries.data());
        Put(network.data(),192,first);
        Put(network.data(),200,first+8);
        Put(network.data(),208,first+16);
    }
    // 16AADF0/1732B10/1733F40 preserve the actual vector allocation as RDX.
    auto Route(){return ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,handler.data(),connection.data());}
};
int main(){try{
    {
        RouteFixture f;
        Put(f.client.data(),0,base+0xE774DB0);Put(f.client.data(),424,std::uintptr_t{});
        Put(f.client.data(),416,static_cast<void*>(f.game.data()));
        Put(f.game.data(),0,base+0xE759890);Put(f.game.data(),48,base+0xE75A090);
        Put(f.game.data(),2080,std::uintptr_t{});Put(f.game.data(),2032,static_cast<void*>(f.network.data()));
        Put(f.network.data(),0,base+0xE749B70);Put(f.network.data(),32,base+0xE749B90);
        Put(f.handler.data(),0,base+0xE79FE50);Put(f.handler.data(),960,std::uint8_t{});
        Put(f.handler.data(),936,std::uint8_t{1});Put(f.handler.data(),696,std::uint64_t{});
        Put(f.handler.data(),672,std::uint64_t{0x1122334455667788});
        Put(f.handler.data(),816,std::uint32_t{0});
        CHECK(ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V45),base,f.handler.data(),f.connection.data()));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V51),base,f.handler.data(),f.connection.data()));
    }
    // IDA D92690 constructs both0 and1. The live server reports0. Rejecting
    // zero here incorrectly prevents an otherwise exact native route.
    {
        RouteFixture f;NativeRouteDiagnostic diagnostic;
        Put(f.connection.data(),176,std::uint32_t{});
        CHECK(ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.connection.data(),&diagnostic));
        CHECK(diagnostic.failure==NativeRouteFailure::None && diagnostic.state==0);
        Put(f.connection.data(),176,std::uint32_t{1});
        CHECK(ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.connection.data(),&diagnostic));
        CHECK(diagnostic.failure==NativeRouteFailure::None && diagnostic.state==1);
        Put(f.connection.data(),409,std::uint8_t{1});
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.connection.data(),&diagnostic));
        CHECK(diagnostic.failure==NativeRouteFailure::Closing && diagnostic.state==1);
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,reinterpret_cast<void*>(1),f.connection.data(),&diagnostic));
        CHECK(diagnostic.failure==NativeRouteFailure::Handler && diagnostic.state==-1);
    }
    {
        RouteFixture f;
        CHECK(ReadPrimaryNotificationBinding(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.packet.data()));
        Put(f.network.data(),192,std::uintptr_t{}); // no out-of-scope vector scan
        CHECK(ReadPrimaryNotificationBinding(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.packet.data()));
        Put(f.handler.data(),960,std::uint8_t{});
        CHECK(!ReadPrimaryNotificationBinding(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.packet.data()));
    }
    {
        auto image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x11D2F000,MEM_RESERVE,PAGE_NOACCESS));CHECK(image);
        CHECK(VirtualAlloc(image+0x11D2E000,4096,MEM_COMMIT,PAGE_READWRITE));
        auto mappedBase=reinterpret_cast<std::uintptr_t>(image);
        alignas(8) std::array<std::byte,104> scheduler{};
        CHECK(!OnNativeMainThread(*FindNativeProfile(GameVersion::V52),mappedBase,17));
        Put(image,0x11D2E3B8,static_cast<void*>(scheduler.data()));
        Put(scheduler.data(),0,mappedBase+0xE76B238);Put(scheduler.data(),96,std::uint32_t{17});
        CHECK(OnNativeMainThread(*FindNativeProfile(GameVersion::V52),mappedBase,17));CHECK(!OnNativeMainThread(*FindNativeProfile(GameVersion::V52),mappedBase,18));
        Put(scheduler.data(),0,mappedBase+0xE76B239);CHECK(!OnNativeMainThread(*FindNativeProfile(GameVersion::V52),mappedBase,17));
        CHECK(!OnNativeMainThread(*FindNativeProfile(GameVersion::V52),0,17) && !OnNativeMainThread(*FindNativeProfile(GameVersion::V52),~std::uintptr_t{}-8,17));
        CHECK(VirtualFree(image,0,MEM_RELEASE));
    }
    // Returning no route, client/game rather than the connection allocation,
    // or confusing endpoint1/2 with packet type0 breaks this real-reader case.
    {
        RouteFixture f;auto networkBefore=f.network;auto handlerBefore=f.handler;
        auto connectionBefore=f.connection;auto packetBefore=f.packet;
        auto route=f.Route();CHECK(route);
        CHECK(route->binding.client==reinterpret_cast<std::uintptr_t>(f.client.data()));
        CHECK(route->binding.game==reinterpret_cast<std::uintptr_t>(f.game.data()));
        CHECK(route->binding.network==reinterpret_cast<std::uintptr_t>(f.network.data()));
        CHECK(route->handler==reinterpret_cast<std::uintptr_t>(f.handler.data()));
        CHECK(route->connection==reinterpret_cast<std::uintptr_t>(f.connection.data()));
        CHECK(route->activePeer==0x1234);CHECK(route->guid==0x1122334455667788);
        CHECK(f.network==networkBefore && f.handler==handlerBefore);
        CHECK(f.connection==connectionBefore && f.packet==packetBefore);
    }
    // GUID equality must not substitute a detached identifier for the exact
    // allocation passed by the owning typed dispatcher. Dropping the final
    // address comparison must fail each of these three cases.
    {
        RouteFixture f;CHECK(f.Route());
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.packet.data()));
    }
    {
        RouteFixture f;f.otherConnection=f.connection;
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),f.otherConnection.data()));
    }
    {
        RouteFixture f;f.otherConnection=f.connection;
        f.entries[0]=reinterpret_cast<std::uintptr_t>(f.otherConnection.data());
        CHECK(!f.Route());
    }
    // Identical type0 value alone must never override native ownership checks.
    for(int changed=0;changed<9;++changed){
        RouteFixture f;
        switch(changed){
        case 0:Put(f.handler.data(),0,base+0xE7AE8D0);break;
        case 1:Put(f.handler.data(),88,reinterpret_cast<void*>(1));break;
        case 2:Put(f.client.data(),0,base+0x1234);break;
        case 3:Put(f.game.data(),48,base+0x1234);break;
        case 4:Put(f.network.data(),0,base+0x1234);break;
        case 5:Put(f.network.data(),32,base+0x1234);break;
        case 6:f.registration=f.otherConnection.data();break;
        case 7:Put(f.network.data(),472,reinterpret_cast<void*>(1));break;
        case 8:Put(f.handler.data(),960,std::uint8_t{0});break;
        }
        CHECK(!f.Route());
    }
    // The work bound must be applied before scanning, not accidentally fail
    // because an undersized fixture's 65th entry happens to be unreadable.
    {
        RouteFixture f;std::array<std::uintptr_t,65> many{};
        f.otherConnection=f.connection;
        Put(f.otherConnection.data(),24,std::uint64_t{42});
        many.fill(reinterpret_cast<std::uintptr_t>(f.otherConnection.data()));
        many[0]=reinterpret_cast<std::uintptr_t>(f.connection.data());
        auto first=reinterpret_cast<std::uintptr_t>(many.data());
        Put(f.network.data(),192,first);Put(f.network.data(),208,first+65*8);
        Put(f.network.data(),200,first+64*8);CHECK(f.Route());
        Put(f.network.data(),200,first+65*8);CHECK(!f.Route());
    }
    for(auto type:std::array<std::uint32_t,5>{1,2,3,4,0xFFFFFFFF}){
        RouteFixture f;Put(f.connection.data(),168,type);CHECK(!f.Route());
    }
    {
        RouteFixture f;
        Put(f.handler.data(),696,std::uint64_t{});
        Put(f.connection.data(),24,std::uint64_t{});
        CHECK(f.Route()); // zero is a copied scalar, not an invalid-read fallback
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),reinterpret_cast<void*>(1)));
    }
    for(int changed=0;changed<8;++changed){
        RouteFixture f;
        switch(changed){
        case 0:Put(f.connection.data(),168,std::uint32_t{1});break;
        case 1:Put(f.handler.data(),840,std::uint32_t{2});break;
        case 2:Put(f.connection.data(),168,std::uint32_t{3});break;
        case 3:Put(f.connection.data(),24,std::uint64_t{42});break;
        case 4:Put(f.handler.data(),696,std::uint64_t{42});break;
        case 5:Put(f.connection.data(),24,std::uint64_t{42});break;
        case 6:Put(f.handler.data(),960,std::uint8_t{2});break;
        case 7:
            Put(f.handler.data(),696,~std::uint64_t{});
            Put(f.connection.data(),24,~std::uint64_t{});break;
        }
        CHECK(!f.Route());
    }
    for(auto state:std::array<std::uint32_t,2>{0,1})for(int changed=0;changed<5;++changed){
        RouteFixture f;
        Put(f.connection.data(),176,state);
        switch(changed){
        case 0:Put(f.connection.data(),176,~std::uint32_t{});break;
        case 1:Put(f.connection.data(),176,std::uint32_t{2});break;
        case 2:Put(f.connection.data(),408,std::uint8_t{1});break;
        case 3:Put(f.connection.data(),409,std::uint8_t{1});break;
        case 4:Put(f.connection.data(),280,std::uintptr_t{});break;
        }
        CHECK(!f.Route());
    }
    // A second distinct nonmatching connection is harmless. Repeated pointers,
    // duplicated matching allocations, or unreadable entries are ambiguous.
    for(int changed=0;changed<5;++changed){
        RouteFixture f;
        Put(f.network.data(),200,reinterpret_cast<std::uintptr_t>(f.entries.data())+16);
        f.otherConnection=f.connection;
        f.entries[1]=reinterpret_cast<std::uintptr_t>(f.otherConnection.data());
        if(changed==0)Put(f.otherConnection.data(),24,std::uint64_t{42});
        if(changed==1)f.entries[1]=f.entries[0];
        if(changed==3)f.entries[1]=1;
        if(changed==4)f.entries[1]=0;
        if(changed==0)CHECK(f.Route());else CHECK(!f.Route());
    }
    for(int changed=0;changed<9;++changed){
        RouteFixture f;auto first=reinterpret_cast<std::uintptr_t>(f.entries.data());
        switch(changed){
        case 0:Put(f.network.data(),192,std::uintptr_t{});break;
        case 1:Put(f.network.data(),200,first);break;
        case 2:Put(f.network.data(),200,first-8);break;
        case 3:Put(f.network.data(),200,first+7);break;
        case 4:Put(f.network.data(),208,first);break;
        case 5:Put(f.network.data(),208,first+15);break;
        case 6:Put(f.network.data(),192,first+1);break;
        case 7:Put(f.network.data(),200,first+65*8);Put(f.network.data(),208,first+65*8);break;
        case 8:Put(f.network.data(),192,std::uintptr_t{8});
            Put(f.network.data(),200,std::uintptr_t{16});Put(f.network.data(),208,std::uintptr_t{16});break;
        }
        CHECK(!f.Route());
    }
    {
        RouteFixture f;CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),nullptr));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,f.handler.data(),reinterpret_cast<void*>(1)));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,nullptr,f.connection.data()));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),0,f.handler.data(),f.connection.data()));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),~std::uintptr_t{}-8,f.handler.data(),f.connection.data()));
        CHECK(!ReadPrimaryGuidRoute(*FindNativeProfile(GameVersion::V52),base,reinterpret_cast<void*>(~std::uintptr_t{}-4),f.connection.data()));
    }
    // The old forwarding candidate is client+424 (game), not the network.
    // Returning it or missing the receiver's +48 adjustment breaks this case.
    {
        Fixture f;auto binding=ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data());CHECK(binding);
        CHECK(binding->client==reinterpret_cast<std::uintptr_t>(f.client.data()));
        CHECK(binding->game==reinterpret_cast<std::uintptr_t>(f.game.data()));
        CHECK(binding->network==reinterpret_cast<std::uintptr_t>(f.network.data()));
        CHECK(binding->network!=binding->game);
    }
    // A wrong owner/subobject/table must not yield a plausible readable ID.
    for(int changed=0;changed<4;++changed){
        Fixture f;auto p=changed==0?f.client.data():changed==3?f.network.data():f.game.data();
        Put(p,changed==2?48:0,base+0x1234);
        CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data()));
    }
    {
        Fixture f;Put(f.client.data(),424,static_cast<void*>(nullptr));
        CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data()));
    }
    {
        Fixture f;Put(f.game.data(),2080,static_cast<void*>(nullptr));
        CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data()));
    }
    {
        Fixture f;Put(f.client.data(),424,reinterpret_cast<void*>(1));
        CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data()));
    }
    {
        Fixture f;Put(f.game.data(),2080,reinterpret_cast<void*>(1));
        CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,f.client.data()));
    }
    CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,nullptr));
    CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,reinterpret_cast<void*>(1)));
    CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),0,reinterpret_cast<void*>(1)));
    CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),std::numeric_limits<std::uintptr_t>::max()-8,reinterpret_cast<void*>(1)));
    CHECK(!ReadNetworkBinding(*FindNativeProfile(GameVersion::V52),base,reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()-4)));
    std::cout<<"PASS: original client/game/network ownership, primary registration, GUID route, unique live connection, malformed/unreadable/retired fail closed\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
