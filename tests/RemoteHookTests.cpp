#include "../src/Plugin.cpp"
#include "CompatibilityHookFixtures.h"
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <thread>
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);} while(false)
template<class T> void Put(void* p,std::size_t offset,T value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof value);}
std::uintptr_t Key(void const* p){return reinterpret_cast<std::uintptr_t>(p);}

// Private executable callsites reproduce the independently verified native
// continuation PCs. Only fixture functions run; no game code is mapped.
struct PrivateCalls {
    std::byte* image{};
    std::array<RUNTIME_FUNCTION,16> unwind{};
    unsigned used{};
    PrivateCalls(){image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x12C02000,MEM_RESERVE,PAGE_NOACCESS));CHECK(image);}
    void Commit(std::uint32_t rva){CHECK(VirtualAlloc(image+(rva&~4095u),4096,MEM_COMMIT,PAGE_READWRITE));}
    void Retarget(std::uint32_t continuation,void* target){
        DWORD old{};CHECK(VirtualProtect(image+continuation-10,8,PAGE_EXECUTE_READWRITE,&old));
        std::memcpy(image+continuation-10,&target,8);
        CHECK(VirtualProtect(image+continuation-10,8,old,&old));
        CHECK(FlushInstructionCache(GetCurrentProcess(),image+continuation-10,8));
    }
    void* Add(std::uint32_t continuation,void* target,bool nine=false){
        std::vector<unsigned char> bytes{0x48,0x83,0xEC,static_cast<unsigned char>(nine?0x58:0x28)};
        if(nine)for(unsigned i=0;i<5;++i){
            bytes.insert(bytes.end(),{0x4C,0x8B,0x94,0x24});
            auto from=std::uint32_t{0x80+i*8};for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<unsigned char>(from>>(8*n)));
            bytes.insert(bytes.end(),{0x4C,0x89,0x54,0x24,static_cast<unsigned char>(0x20+i*8)});
        }
        bytes.insert(bytes.end(),{0x48,0xB8});auto address=Key(target);
        for(unsigned i=0;i<8;++i)bytes.push_back(static_cast<unsigned char>(address>>(8*i)));
        bytes.insert(bytes.end(),{0xFF,0xD0});
        auto begin=continuation-static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(),{0x48,0x83,0xC4,static_cast<unsigned char>(nine?0x58:0x28),0xC3});
        Commit(begin);Commit(continuation);DWORD old{};
        CHECK(VirtualProtect(image+(begin&~4095u),4096,PAGE_READWRITE,&old));
        std::memcpy(image+begin,bytes.data(),bytes.size());
        CHECK(VirtualProtect(image+(begin&~4095u),4096,PAGE_EXECUTE_READ,&old));
        CHECK(FlushInstructionCache(GetCurrentProcess(),image+begin,bytes.size()));
        auto metadata=0x1000+used*8;Commit(metadata);
        unsigned char codes[]={1,4,1,0,4,static_cast<unsigned char>(nine?0xA2:0x42),0,0};
        std::memcpy(image+metadata,codes,8);
        auto& entry=unwind[used++];entry={begin,continuation+5,metadata};
        CHECK(RtlAddFunctionTable(&entry,1,Key(image)));return image+begin;
    }
    ~PrivateCalls(){for(unsigned i=0;i<used;++i)RtlDeleteFunctionTable(&unwind[i]);if(image)VirtualFree(image,0,MEM_RELEASE);}
};
struct Game {
    alignas(8) std::array<std::byte,432> client{};
    alignas(8) std::array<std::byte,2088> game{};
    alignas(8) std::array<std::byte,536> network{};
    alignas(8) std::array<std::byte,968> handler{};
    alignas(8) std::array<std::byte,432> connection{};
    alignas(8) std::array<std::byte,176> identifier{};
    alignas(8) std::array<std::byte,1608> packet{};
    alignas(8) std::array<std::byte,3504> player{};
    alignas(8) std::array<std::byte,136> manager{};
    alignas(8) std::array<std::byte,16> connector{};
    std::array<std::uintptr_t,1> entries{};
    std::uintptr_t world{111},registration{},pump{};
    Game(){
        Put(client.data(),0,imageBase+FixtureRva(0xE9731B0));Put(client.data(),fixtureVersion==45?416:424,static_cast<void*>(game.data()));
        Put(game.data(),0,imageBase+FixtureRva(0xE76E2D0));Put(game.data(),48,imageBase+FixtureRva(0xE76EAD0));
        Put(game.data(),fixtureVersion==45?2032:2080,static_cast<void*>(network.data()));
        Put(network.data(),0,imageBase+FixtureRva(0xE75E2D0));Put(network.data(),32,imageBase+FixtureRva(0xE75E2F0));
        registration=Key(handler.data());Put(network.data(),472,&registration);
        Put(handler.data(),0,imageBase+FixtureRva(0xE7AF110));Put(handler.data(),88,static_cast<void*>(client.data()));
        Put(identifier.data(),24,std::uint64_t{77});Put(identifier.data(),168,std::uint32_t{});
        std::memcpy(connection.data(),identifier.data(),176);
        Put(connection.data(),176,std::uint32_t{});Put(connection.data(),280,std::uintptr_t{555});
        entries[0]=Key(connection.data());Put(network.data(),192,Key(entries.data()));
        Put(network.data(),200,Key(entries.data())+8);Put(network.data(),208,Key(entries.data())+8);
        Put(connector.data(),0,imageBase+FixtureRva(0xE7BE750));Put(connector.data(),8,Key(network.data())+32);
        Put(packet.data(),1600,std::uint8_t{1});pump=Key(network.data())+32;
        Put(player.data(),2744,static_cast<void*>(manager.data()));
        Put(manager.data(),0,imageBase+FixtureRva(0xE827EC0));Put(manager.data(),16,static_cast<void*>(player.data()));
        Put(manager.data(),8,std::uint8_t{1});Put(manager.data(),9,std::uint8_t{1});
    }
};
Game* fixture{};
Opaque4 connectCall{},notifyCall{},managerCall{},packetCall{};
Opaque9 constructCall{};
unsigned receiveCalls{},connectedCalls{},constructCalls{},notifyCalls{},startCalls{},managerCalls{},packetCalls{},destroyCalls{};
bool omitManager{},throwReceive{},expectRetired{},expectResponseScope{},loseStartRoute{};
std::uintptr_t OriginalReceive(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(a==Key(fixture->connector.data()) && b==12 && c==13 && d==14);++receiveCalls;
    if(throwReceive)throw std::runtime_error("native fixture unwind");
    CHECK(connectCall(Key(fixture->network.data())+32,Key(fixture->identifier.data()),88,99)==0xABCD1234);
    return 0x123456789ABCDEF0;
}
std::uintptr_t OriginalConnected(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(a==Key(fixture->network.data())+32 && b==Key(fixture->identifier.data()) && c==88 && d==99);++connectedCalls;
    CHECK(constructCall(Key(fixture->connection.data()),b,33,
        Read<std::uint32_t>(fixture->connection.data(),176),55,66,77,88,99)==0x1122334455667788);
    CHECK(notifyCall(Key(fixture->handler.data()),b,111,222)==0x445566778899AABB);
    return 0xABCD1234;
}
std::uintptr_t OriginalConstruct(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,
    std::uintptr_t e,std::uintptr_t f,std::uintptr_t g,std::uintptr_t h,std::uintptr_t i){
    CHECK(a==Key(fixture->connection.data()) && b==Key(fixture->identifier.data()));
    CHECK(c==33 && d==Read<std::uint32_t>(fixture->connection.data(),176) &&
        e==55 && f==66 && g==77 && h==88 && i==99);++constructCalls;
    return 0x1122334455667788;
}
std::uintptr_t OriginalNotify(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(a==Key(fixture->handler.data()) && b==Key(fixture->identifier.data()) && c==111 && d==222);++notifyCalls;
    Put(fixture->handler.data(),fixtureVersion==45?936:960,std::uint8_t{1});
    std::memcpy(fixture->handler.data()+(fixtureVersion==45?648:672),reinterpret_cast<void const*>(b),176);
    return 0x445566778899AABB;
}
std::uintptr_t OriginalManager(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(a==Key(fixture->manager.data()) && b==Key(fixture->player.data()) && c==1 &&
        d==Read<std::uint8_t>(fixture->packet.data(),1600));++managerCalls;
    return 0x9988776655443322;
}
void OriginalStart(void* a,void* b,void* c){
    CHECK(a==fixture->handler.data() && b==fixture->connection.data() && c==fixture->packet.data());++startCalls;
    if(!omitManager){
        // Match1336D24: base ctor first, derived table/player installation
        // only after its normal return, not preinstalled by the fixture.
        Put(fixture->manager.data(),0,imageBase+0xBADCAFE);
        Put(fixture->player.data(),2744,std::uintptr_t{});
        CHECK(managerCall(Key(fixture->manager.data()),Key(fixture->player.data()),1,
            Read<std::uint8_t>(fixture->packet.data(),1600))==0x9988776655443322);
        Put(fixture->manager.data(),0,imageBase+FixtureRva(0xE827EC0));
        Put(fixture->player.data(),2744,static_cast<void*>(fixture->manager.data()));
    }
    if(loseStartRoute)Put(fixture->connection.data(),409,std::uint8_t{1});
}
std::uintptr_t OriginalPump(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(a==Key(&fixture->pump) && b==22 && c==23 && d==24);
    packetCall(Key(fixture->handler.data()),Key(fixture->connection.data()),Key(fixture->packet.data()),0);
    return 0xABCDEF0123456789;
}
std::uintptr_t OriginalPacket(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    ++packetCalls;
    if(expectResponseScope){
        CHECK(a==Key(fixture->handler.data()) && b==Key(fixture->connection.data()) && c==Key(fixture->packet.data()) && d==0);
        CHECK(remoteResponseFrame && remoteResponseFrame->valid && remoteResponseFrame->manager==Key(fixture->manager.data()));
        CHECK(remote.Authorize(remoteResponseFrame->route,remoteResponseFrame->manager,remoteResponseFrame->generation));
    }
    return 0x7F12345678901234;
}
std::uintptr_t OriginalDestroy(std::uintptr_t,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d){
    CHECK(b==2 && c==3 && d==4);++destroyCalls;
    if(expectRetired)CHECK(remote.Select(Key(fixture->manager.data()),Key(fixture->player.data()),fixture->world).backend==Backend::Unsupported);
    return 0xEF1234567890ABCD;
}
std::uintptr_t OriginalClosing(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,
    std::uintptr_t e,std::uintptr_t f,std::uintptr_t g){
    CHECK(a==Key(fixture->network.data())+32 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7);
    CHECK(remote.Select(Key(fixture->manager.data()),Key(fixture->player.data()),fixture->world).backend==Backend::Unsupported);
    return 0xFEDCBA9876543210;
}
void* LocalPlayer(void* client){CHECK(!loseStartRoute && client==fixture->client.data());return fixture->player.data();}
void* Level(void* client){CHECK(client==fixture->client.data());return reinterpret_cast<void*>(fixture->world);}
void Establish(){
    CHECK(HookRemoteReceive(Key(fixture->connector.data()),12,13,14)==0x123456789ABCDEF0);
    CHECK(HookEventPump(Key(&fixture->pump),22,23,24)==0xABCDEF0123456789);
}
int main(int argc,char** argv){try{
    fixtureVersion=argc>2?std::stoi(argv[2]):52;
    CHECK(fixtureVersion==45 || fixtureVersion==51 || fixtureVersion==52);
    worldApi.profile=FindNativeProfile(fixtureVersion==45?GameVersion::V45:fixtureVersion==51?GameVersion::V51:GameVersion::V52);
    {
        RemoteAdmissionQueue queue;
        for(unsigned i=0;i<32;++i)CHECK(queue.Push({i,100+i,RemoteAdmissionStage::Route,{NativeRouteFailure::State,0},-1,false}));
        CHECK(!queue.Push({}));CHECK(queue.Dropped()==1);
        for(unsigned i=0;i<32;++i){auto event=queue.Pop();CHECK(event && event->ordinal==i && event->time==100+i);}
        CHECK(!queue.Pop());CHECK(queue.Push({99}));CHECK(queue.Pop()->ordinal==99);
    }
    PrivateCalls calls;imageBase=Key(calls.image);
    if(argc>1 && std::string(argv[1])=="--install"){
        for(auto const& guard:worldApi.profile->guards){
            calls.Commit(guard.rva);calls.Commit(guard.rva+31);DWORD old{};
            CHECK(VirtualProtect(calls.image+guard.rva,32,PAGE_EXECUTE_READWRITE,&old));
            std::memcpy(calls.image+guard.rva,guard.bytes.data(),32);
            CHECK(VirtualProtect(calls.image+guard.rva,32,PAGE_EXECUTE_READ,&old));
        }
        CHECK(InstallHooks());CHECK(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);
        for(auto const& guard:worldApi.profile->guards)CHECK(std::memcmp(calls.image+guard.rva,guard.bytes.data(),32)==0);
        for(auto rva:{0x25B1D10,0x25B23F0,0x26AFFB0,0x259ABB0,0x2488FE0,0x1D5170,0x8B2D490,0x266C180,0x268F860,0x266FAD0,
            0x16CA370,0x16AF460,0xD92690,0x132E2B0,0x16AADF0,0x25AA010,0x1332440,0x1308CE0,
            0x1725AE0,0x132D2A0,0x25B4490,0x16A9B90,0x16AE270})
            CHECK(MH_RemoveHook(calls.image+FixtureRva(rva))==MH_OK);
        std::cout<<"PASS actual 23-hook MinHook install/disable/restore on exact guarded original entries; no game code executed\n";return 0;
    }
    Game game;fixture=&game;
    if(argc>1 && std::string(argv[1])=="--state-one")Put(game.connection.data(),176,std::uint32_t{1});
    connectCall=reinterpret_cast<Opaque4>(calls.Add(FixtureRva(0x16CAAC5),reinterpret_cast<void*>(&HookConnected)));
    constructCall=reinterpret_cast<Opaque9>(calls.Add(FixtureRva(0x16AF709),reinterpret_cast<void*>(&HookConnectionConstruct),true));
    notifyCall=reinterpret_cast<Opaque4>(calls.Add(FixtureRva(0x16AF7B3),reinterpret_cast<void*>(&HookHandlerConnected)));
    managerCall=reinterpret_cast<Opaque4>(calls.Add(FixtureRva(0x1336D29),reinterpret_cast<void*>(&HookManagerConstruct)));
    packetCall=reinterpret_cast<Opaque4>(calls.Add(FixtureRva(0x16AC037),reinterpret_cast<void*>(&HookStartGame)));
    originalRemoteReceive=OriginalReceive;originalConnected=OriginalConnected;originalConnectionConstruct=OriginalConstruct;
    originalHandlerConnected=OriginalNotify;originalEventPump=OriginalPump;originalStartGame=OriginalStart;
    originalManagerConstruct=OriginalManager;originalPacketResponse=OriginalPacket;
    originalConnectionDestroy=originalHandlerDestroy=originalManagerDestroy=originalDisconnect=OriginalDestroy;
    originalClosing=OriginalClosing;
    alignas(8) std::array<std::byte,104> scheduler{};
    calls.Commit(FixtureRva(0x11D2E3B8));Put(calls.image,FixtureRva(0x11D2E3B8),static_cast<void*>(scheduler.data()));
    Put(scheduler.data(),0,imageBase+FixtureRva(0xE76B238));Put(scheduler.data(),96,std::uint32_t{12345});
    nativeThreadId=[]()->std::uint32_t{return 12345;};
    ownerThread.store(0);status.store(1);runtime=nullptr;
    worldApi.localPlayer=LocalPlayer;worldApi.world=Level;
    if(argc>1 && std::string(argv[1]).starts_with("--type3")){
        // This generic native registration is reached without RakNet receive.
        // The real constructor, notification and StartGame hooks must still
        // establish a fresh generation, and retirement must revoke it.
        auto variant=std::string(argv[1])=="--type3-zero"?0:std::string(argv[1])=="--type3-one"?1:2;
        // Deliberately not the RakNet continuation: generic entry authority
        // comes from reciprocal objects plus subsequent exact inner sites.
        connectCall=reinterpret_cast<Opaque4>(calls.Add(0x100A105,reinterpret_cast<void*>(&HookConnected)));
        Put(game.identifier.data(),168,std::uint32_t{3});Put(game.identifier.data(),16,static_cast<std::uint8_t>(variant));
        Put(game.identifier.data(),0,std::uint64_t{0x12345678});Put(game.identifier.data(),8,std::uint64_t{0xABCDEF12});
        std::memcpy(game.connection.data(),game.identifier.data(),176);
        auto establish=[&]{
            CHECK(connectCall(Key(game.network.data())+32,Key(game.identifier.data()),88,99)==0xABCD1234);
            CHECK(HookEventPump(Key(&game.pump),22,23,24)==0xABCDEF0123456789);
            auto event=remoteAdmissions.Pop();CHECK(event && event->stage==RemoteAdmissionStage::Accepted);
        };
        establish();CHECK(receiveCalls==0 && !remote.Faulted());
        auto selection=remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world);
        CHECK(selection.backend==Backend::RemoteModern && selection.generation);
        auto route=ReadPrimaryGuidRoute(*worldApi.profile,imageBase,game.handler.data(),game.connection.data());CHECK(route);
        CHECK(remote.Authorize(*route,Key(game.manager.data()),selection.generation));
        calls.Retarget(FixtureRva(0x16AC037),reinterpret_cast<void*>(&HookPacketResponse));expectResponseScope=true;
        CHECK(HookEventPump(Key(&game.pump),22,23,24)==0xABCDEF0123456789);
        calls.Retarget(FixtureRva(0x16AC037),reinterpret_cast<void*>(&HookStartGame));expectResponseScope=false;
        expectRetired=true;HookConnectionDestroy(Key(game.connection.data()),2,3,4);
        CHECK(!remote.Authorize(*route,Key(game.manager.data()),selection.generation));
        establish();CHECK(remote.Generation()!=selection.generation);
        HookClosing(Key(game.network.data())+32,2,3,4,5,6,7);CHECK(!remote.Generation());
        Put(game.packet.data(),1600,std::uint8_t{});Put(game.manager.data(),8,std::uint8_t{});
        establish();CHECK(remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::RemoteOrdinary);
        HookConnectionDestroy(Key(game.connection.data()),2,3,4);
        // Still no late bootstrap from StartGame or a detached identifier.
        HookEventPump(Key(&game.pump),22,23,24);auto missing=remoteAdmissions.Pop();CHECK(missing && missing->stage==RemoteAdmissionStage::Ledger);
        auto birthsBefore=remoteBirths.load();
        Put(game.network.data(),216,std::uintptr_t{1});
        connectCall(Key(game.network.data())+32,Key(game.identifier.data()),88,99);
        CHECK(remoteBirths.load()==birthsBefore && !remote.Generation());Put(game.network.data(),216,std::uintptr_t{});
        Put(game.identifier.data(),16,std::uint8_t{3});
        connectCall(Key(game.network.data())+32,Key(game.identifier.data()),88,99);
        CHECK(remoteBirths.load()==birthsBefore && !remote.Generation() && !remote.Faulted());
        Put(game.identifier.data(),16,static_cast<std::uint8_t>(variant));
        // A constructor entered from any other inner site cannot mint birth.
        constructCall=reinterpret_cast<Opaque9>(calls.Add(0x100B205,reinterpret_cast<void*>(&HookConnectionConstruct),true));
        connectCall(Key(game.network.data())+32,Key(game.identifier.data()),88,99);
        CHECK(remoteBirths.load()==birthsBefore && !remote.Generation());
        // Foreign forwarding uses its own ABI fixture; no native reads.
        originalConnected=+[](std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d)->std::uintptr_t{
            CHECK(a==1 && b==1 && c==88 && d==99);return 0;
        };
        std::thread foreign([&]{HookConnected(1,1,88,99);});
        foreign.join();
        std::cout<<"PASS kind3 native generic registration without receive; full identity,modern/ordinary StartGame,response authority,retirement/reuse\n";return 0;
    }
    if(argc>1 && std::string(argv[1])=="--lost-route"){
        loseStartRoute=true;Establish();
        CHECK(!remote.Generation() && !remote.Faulted());
        auto event=remoteAdmissions.Pop();CHECK(event && event->stage==RemoteAdmissionStage::PostRoute);
        CHECK(event->route.failure==NativeRouteFailure::Closing);
        std::cout<<"PASS invalid post-StartGame route never calls native player/world getters\n";return 0;
    }
    Establish();
    auto admission=remoteAdmissions.Pop();CHECK(admission);
    CHECK(admission->stage==RemoteAdmissionStage::Accepted && admission->mode==1 && admission->constructorObserved);
    CHECK(admission->route.failure==NativeRouteFailure::None &&
        admission->route.state==(argc>1 && std::string(argv[1])=="--state-one"?1:0));
    auto admissionText=AdmissionText(*admission);
    CHECK(admissionText.find("stage=accepted")!=std::string::npos);
    CHECK(admissionText.find("route=none")!=std::string::npos);
    CHECK(admissionText.find("mode=1")!=std::string::npos);
    CHECK(admissionText.find("state="+std::to_string(admission->route.state))!=std::string::npos);
    CHECK(admissionText.find("constructorObserved=1")!=std::string::npos);
    if(argc>1 && std::string(argv[1])=="--route-diagnostic"){
        // Missing/corrupt scalar diagnostics must be observable through the
        // real reader + worker formatter without authorizing any new route.
        CHECK(admissionText.find("typesObserved=1 suppliedTypeRead=1 suppliedType=0 savedTypeRead=1 savedType=0 typesMatch=1")!=std::string::npos);
        auto summary=HookDiagnosticText(hookDiagnostics.Snapshot());
        CHECK(summary.find("remoteReceiveEntry=1 remoteReceiveValid=1 remoteConnectedEntry=1 remoteConnectedValid=1 remoteConstructEntry=1 remoteConstructObserved=1 remoteNotifyEntry=1 remoteNotifyObserved=1")!=std::string::npos);
        auto savedOffset=fixtureVersion==45?816:840;
        auto handlerBefore=game.handler;auto connectionBefore=game.connection;
        auto diagnose=[&](void const* identifier){
            auto handlerAtRead=game.handler;auto connectionAtRead=game.connection;
            RemoteAdmissionEvent event;
            event.stage=RemoteAdmissionStage::Route;
            CHECK(!ReadPrimaryGuidRoute(*worldApi.profile,imageBase,game.handler.data(),identifier,&event.route));
            CHECK(game.handler==handlerAtRead && game.connection==connectionAtRead);
            // Callers without diagnostics retain the same rejection result.
            CHECK(!ReadPrimaryGuidRoute(*worldApi.profile,imageBase,game.handler.data(),identifier));
            CHECK(game.handler==handlerAtRead && game.connection==connectionAtRead);
            return AdmissionText(event);
        };
        Put(game.connection.data(),168,std::uint32_t{3});Put(game.handler.data(),savedOffset,std::uint32_t{3});
        // Kind3 itself is now supported, but variant3 remains deliberately
        // unknown; diagnostics must still report both readable kind values.
        Put(game.connection.data(),16,std::uint8_t{3});
        auto text=diagnose(game.connection.data());
        CHECK(text.find("route=guid-mismatch")!=std::string::npos);
        CHECK(text.find("typesObserved=1 suppliedTypeRead=1 suppliedType=3 savedTypeRead=1 savedType=3 typesMatch=1")!=std::string::npos);
        Put(game.handler.data(),savedOffset,std::uint32_t{2});text=diagnose(game.connection.data());
        CHECK(text.find("suppliedType=3 savedTypeRead=1 savedType=2 typesMatch=0")!=std::string::npos);
        Put(game.connection.data(),168,~std::uint32_t{});text=diagnose(game.connection.data());
        CHECK(text.find("suppliedTypeRead=1 suppliedType=4294967295")!=std::string::npos);
        text=diagnose(reinterpret_cast<void*>(1));
        CHECK(text.find("route=supplied-type")!=std::string::npos);
        CHECK(text.find("typesObserved=1 suppliedTypeRead=0 suppliedType=-1 savedTypeRead=1 savedType=2 typesMatch=-1")!=std::string::npos);
        Put(game.connection.data(),168,std::uint32_t{});text=diagnose(game.connection.data());
        CHECK(text.find("route=saved-type")!=std::string::npos);
        CHECK(text.find("suppliedTypeRead=1 suppliedType=0 savedTypeRead=1 savedType=2 typesMatch=0")!=std::string::npos);
        game.connection=connectionBefore;game.handler=handlerBefore;
        // Exercise the real StartGame hook: type3 still forwards the original
        // exactly once and cannot create a generation, even with equal types.
        HookConnectionDestroy(Key(game.connection.data()),2,3,4);
        Put(game.connection.data(),168,std::uint32_t{3});Put(game.handler.data(),savedOffset,std::uint32_t{3});
        Put(game.connection.data(),16,std::uint8_t{3});
        auto before=startCalls;
        CHECK(HookEventPump(Key(&game.pump),22,23,24)==0xABCDEF0123456789);
        CHECK(startCalls==before+1 && !remote.Generation() && !remote.Faulted());
        auto rejectedEvent=remoteAdmissions.Pop();CHECK(rejectedEvent && rejectedEvent->stage==RemoteAdmissionStage::Route);
        CHECK(AdmissionText(*rejectedEvent).find("suppliedType=3 savedTypeRead=1 savedType=3 typesMatch=1")!=std::string::npos);
        CHECK(!remoteAdmissions.Pop());
        // Foreign callbacks must retain unobserved defaults, never read input.
        originalStartGame=+[](void*,void*,void*){};
        std::thread foreign([&]{HookStartGame(reinterpret_cast<void*>(1),reinterpret_cast<void*>(1),reinterpret_cast<void*>(1));});
        foreign.join();auto excluded=remoteAdmissions.Pop();CHECK(excluded);
        CHECK(excluded->stage==RemoteAdmissionStage::Foreign);
        CHECK(AdmissionText(*excluded).find("typesObserved=0 suppliedTypeRead=0 suppliedType=-1 savedTypeRead=0 savedType=-1 typesMatch=-1")!=std::string::npos);
        std::cout<<"PASS read-only route diagnostics distinguish type0/type3/mismatch/saved-type/unreadable/UINT32_MAX/foreign; native original forwarding and rejection unchanged\n";return 0;
    }
    CHECK(admissionText.find(std::to_string(Key(game.handler.data())))==std::string::npos);
    CHECK(!remoteAdmissions.Pop());
    auto selected=remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world);
    CHECK(selected.backend==Backend::RemoteModern);
    CHECK(receiveCalls==1 && connectedCalls==1 && constructCalls==1 && notifyCalls==1 && startCalls==1 && managerCalls==1);
    auto nativeRoute=ReadPrimaryGuidRoute(*worldApi.profile,imageBase,game.handler.data(),game.connection.data());CHECK(nativeRoute);
    CHECK(remote.Authorize(*nativeRoute,Key(game.manager.data()),selected.generation));
    CHECK(ownerThread.load()==GetCurrentThreadId());
    if(argc>1 && std::string(argv[1])=="--admission"){
        auto fresh=[&]{
            HookConnectionDestroy(Key(game.connection.data()),2,3,4);
            HookRemoteReceive(Key(game.connector.data()),12,13,14);
        };
        auto expect=[&](RemoteAdmissionStage stage){
            auto event=remoteAdmissions.Pop();CHECK(event && event->stage==stage);
            CHECK(!remoteAdmissions.Pop());CHECK(!remote.Generation() && !remote.Faulted());return *event;
        };
        fresh();HookStartGame(game.handler.data(),game.connection.data(),game.packet.data());
        expect(RemoteAdmissionStage::MissingPump);
        auto validPacketCall=packetCall;
        packetCall=reinterpret_cast<Opaque4>(calls.Add(0x16AD037,reinterpret_cast<void*>(&HookStartGame)));
        HookEventPump(Key(&game.pump),22,23,24);expect(RemoteAdmissionStage::Continuation);
        packetCall=validPacketCall;
        ++game.pump;HookEventPump(Key(&game.pump),22,23,24);expect(RemoteAdmissionStage::InvalidPump);--game.pump;
        Put(game.connection.data(),176,std::uint32_t{2});
        HookEventPump(Key(&game.pump),22,23,24);auto state=expect(RemoteAdmissionStage::Route);
        CHECK(state.route.failure==NativeRouteFailure::State && state.route.state==2);
        Put(game.connection.data(),176,std::uint32_t{});
        Put(game.packet.data(),1600,std::uint8_t{2});
        HookEventPump(Key(&game.pump),22,23,24);CHECK(expect(RemoteAdmissionStage::Mode).mode==2);
        Put(game.packet.data(),1600,std::uint8_t{1});
        omitManager=true;HookEventPump(Key(&game.pump),22,23,24);
        CHECK(!expect(RemoteAdmissionStage::ManagerCapture).constructorObserved);omitManager=false;
        fresh();worldApi.world=nullptr;HookEventPump(Key(&game.pump),22,23,24);
        CHECK(expect(RemoteAdmissionStage::Api).constructorObserved);worldApi.world=Level;
        fresh();Put(game.manager.data(),9,std::uint8_t{});HookEventPump(Key(&game.pump),22,23,24);
        expect(RemoteAdmissionStage::Manager);Put(game.manager.data(),9,std::uint8_t{1});
        fresh();game.world=0;HookEventPump(Key(&game.pump),22,23,24);expect(RemoteAdmissionStage::Finish);game.world=111;
        fresh();Put(game.packet.data(),1600,std::uint8_t{});Put(game.manager.data(),8,std::uint8_t{});
        HookEventPump(Key(&game.pump),22,23,24);auto ordinary=remoteAdmissions.Pop();
        CHECK(ordinary && ordinary->stage==RemoteAdmissionStage::Accepted && ordinary->mode==0);
        CHECK(remote.Generation() && remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::RemoteOrdinary);
        // A foreign callback must record only scalars, never read its inputs.
        originalStartGame=+[](void* a,void* b,void* c){
            CHECK(a==fixture->handler.data() && b==reinterpret_cast<void*>(1) && c==reinterpret_cast<void*>(1));
        };
        std::thread foreign([&]{HookStartGame(game.handler.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(1));});
        foreign.join();
        auto foreignEvent=remoteAdmissions.Pop();CHECK(foreignEvent && foreignEvent->stage==RemoteAdmissionStage::Foreign);
        CHECK(foreignEvent->route.failure==NativeRouteFailure::Unobserved && foreignEvent->mode==-1 && remote.Faulted());
        std::cout<<"PASS bounded admission diagnostics preserve route, caller, mode, manager and unsupported-contract gates\n";return 0;
    }
    calls.Retarget(FixtureRva(0x16AC037),reinterpret_cast<void*>(&HookPacketResponse));expectResponseScope=true;
    CHECK(HookEventPump(Key(&game.pump),22,23,24)==0xABCDEF0123456789);
    CHECK(packetCalls==1 && !remoteResponseFrame && !remotePumpFrame);
    calls.Retarget(FixtureRva(0x16AC037),reinterpret_cast<void*>(&HookStartGame));expectResponseScope=false;
    if(argc>1 && std::string(argv[1])=="--unwind"){
        throwReceive=true;bool escaped=false;
        try{HookRemoteReceive(Key(game.connector.data()),12,13,14);}catch(std::runtime_error const&){escaped=true;}
        CHECK(escaped && remote.Faulted());
        CHECK(!remoteReceiveFrame && !remoteConnectedFrame && !remotePumpFrame && !remoteStartFrame && !remoteResponseFrame);
        std::cout<<"PASS actual native unwind poisons live generation and restores all TLS scopes\n";return 0;
    }
    // The native manager destructor must invalidate BEFORE original work.
    expectRetired=true;
    CHECK(HookManagerDestroy(Key(game.manager.data()),2,3,4)==0xEF1234567890ABCD);
    CHECK(!remote.Authorize(*nativeRoute,Key(game.manager.data()),selected.generation));
    CHECK(HookConnectionDestroy(Key(game.connection.data()),2,3,4)==0xEF1234567890ABCD);
    Establish();auto second=remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world);
    CHECK(second.backend==Backend::RemoteModern && second.generation!=selected.generation);
    CHECK(HookHandlerDestroy(Key(game.handler.data()),2,3,4)==0xEF1234567890ABCD);
    CHECK(HookConnectionDestroy(Key(game.connection.data()),2,3,4)==0xEF1234567890ABCD);
    omitManager=true;Establish();
    CHECK(remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::Unsupported);
    CHECK(!remote.Begin(*nativeRoute,InventoryMode::Modern));
    CHECK(!remote.Faulted()); // failed post-StartGame verification must release its provisional ticket
    CHECK(HookConnectionDestroy(Key(game.connection.data()),2,3,4)==0xEF1234567890ABCD);
    omitManager=false;Establish();
    CHECK(remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::RemoteModern);
    CHECK(HookDisconnect(Key(game.network.data())+32,2,3,4)==0xEF1234567890ABCD);
    // Calls outside the exact remote/constructor/notification chain execute
    // normally but cannot recreate provenance from readable fixture pointers.
    HookConnected(Key(game.network.data())+32,Key(game.identifier.data()),88,99);
    CHECK(HookEventPump(Key(&game.pump),22,23,24)==0xABCDEF0123456789);
    CHECK(remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::Unsupported);
    Establish();CHECK(!remote.Faulted());
    CHECK(HookClosing(Key(game.network.data())+32,2,3,4,5,6,7)==0xFEDCBA9876543210);
    Establish();CHECK(!remote.Faulted());
    // A foreign callback receives unreadable packet inputs: only incoming
    // scalar handler identity may be checked before forwarding unchanged.
    std::thread foreign([&]{CHECK(HookPacketResponse(Key(game.handler.data()),1,1,1)==0x7F12345678901234);});foreign.join();
    CHECK(remote.Faulted());
    CHECK(remote.Select(Key(game.manager.data()),Key(game.player.data()),game.world).backend==Backend::Unsupported);
    std::cout<<"PASS actual Plugin native callsite/9-argument forwarding, constructor-notification-StartGame authority, before-original retirement, reuse, missing manager, unknown caller and foreign no-read\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
