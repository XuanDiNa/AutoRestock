// Break caught: a survival destroy which never enters26AFFB0 must still supply
// native-use proof; manual emptying and unrelated players must never do so.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
bool fixtureForeground=true,fixtureHidden=true;
HWND FixtureWindow(){return reinterpret_cast<HWND>(1);}
DWORD FixtureWindowProcess(HWND,LPDWORD process){*process=fixtureForeground?GetCurrentProcessId():0;return 1;}
BOOL FixtureIconic(HWND){return FALSE;}
BOOL FixtureCursor(PCURSORINFO p){p->flags=fixtureHidden?0:CURSOR_SHOWING;return TRUE;}
#define GetForegroundWindow FixtureWindow
#define GetWindowThreadProcessId FixtureWindowProcess
#define IsIconic FixtureIconic
#define GetCursorInfo FixtureCursor
#include "../src/Plugin.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef IsIconic
#undef GetCursorInfo
#include <iostream>
#include <stdexcept>
#include <thread>
namespace FixtureABI {
#define AUTORESTOCK_RUNTIME_FIXTURE_ONLY
#include "RuntimeTests.cpp"
#include "ActorFixture.h"
}
#undef CHECK
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);} while(false)
void* expectedMode{};void* expectedPosition{};
unsigned originalCalls{};
enum class Action {Break,Keep,Switch,Throw,Server,Refused,Damage,Settings};
Action action=Action::Break;
std::byte* serverStacks{};
std::intptr_t DestroyOriginal(void* mode,void* position,std::uint8_t face){
    CHECK(mode==expectedMode && position==expectedPosition && face==5);++originalCalls;
    if(action==Action::Server){CHECK(serverStacks);FixtureABI::Put(serverStacks,2*152+34,std::uint8_t{});return 0x1234567812345601;}
    CHECK(nativeDepth>=1);
    if(action==Action::Throw)throw std::runtime_error("native-destroy-unwind");
    if(action==Action::Refused)return 0;
    if(action==Action::Damage){FixtureABI::Put(&FixtureABI::f->destination,60,int{32});return 1;}
    if(action!=Action::Keep)FixtureABI::Put(&FixtureABI::f->destination,34,std::uint8_t{});
    if(action==Action::Switch)FixtureABI::Put(FixtureABI::f->supplies,16,int{3});
    if(action==Action::Settings)settings.store((std::uint64_t{2}<<8)|13);
    return 0x1234567812345601;
}
void HoldTool(FixtureABI::Fixture& x){
    FixtureABI::Put(&x.destination,34,std::uint8_t{1});FixtureABI::Put(&x.destination,64,std::uint8_t{1});
    FixtureABI::Put(&x.destination,60,int{31});
    FixtureABI::Put(&x.source,34,std::uint8_t{1});FixtureABI::Put(&x.source,64,std::uint8_t{1});
    FixtureABI::Put(&x.source,60,int{2});x.expectedAmount=1;
}
#ifndef AUTORESTOCK_WORLD_DESTROY_CASES_NAME
#define AUTORESTOCK_WORLD_DESTROY_CASES_NAME main
#endif
int AUTORESTOCK_WORLD_DESTROY_CASES_NAME(){try{
    // Hook tests isolate the three-argument ABI; all policy/identity/transaction
    // code is production. The external game body alone is a controlled fixture.
    for(int scenario=0;scenario<14;++scenario){
        FixtureABI::Fixture x;FixtureABI::Prepare(x);HoldTool(x);FixtureABI::ActorFixture actor;actor.Attach(x.player);
        auto r=FixtureABI::New();runtime=&r;status.store(1);ownerThread.store(GetCurrentThreadId());
        imageBase=FixtureABI::base;originalWorldDestroy=DestroyOriginal;
        worldApi={FixtureABI::Local,FixtureABI::World,FixtureABI::Input,FixtureABI::Top,FixtureABI::Name,
            FixtureABI::RealItem,FixtureABI::Damageable,FindNativeProfile(GameVersion::V52)};
        restockManager=reinterpret_cast<std::uintptr_t>(x.manager);restockInventory.store(reinterpret_cast<std::uintptr_t>(x.inventory));
        settings.store((std::uint64_t{1}<<8)|13);fixtureForeground=fixtureHidden=true;
        alignas(8) std::byte mode[16]{},position[12]{};
        FixtureABI::Put(mode,8,static_cast<void*>(x.player));expectedMode=mode;expectedPosition=position;
        action=scenario==1?Action::Keep:scenario==6?Action::Switch:scenario==7?Action::Throw:Action::Break;
        if(scenario==10)action=Action::Refused;
        if(scenario==11)action=Action::Damage;
        if(scenario==12)action=Action::Settings;
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));while(r.TakeDiagnostic()){}
        if(scenario==2)fixtureForeground=false;
        if(scenario==3)fixtureHidden=false;
        if(scenario==4)settings.store((std::uint64_t{1}<<8)|12);
        if(scenario==5)FixtureABI::Put(mode,8,static_cast<void*>(nullptr));
        if(scenario==9)restockInventory.store(1);
        if(scenario==13)FixtureABI::hud=false;
        auto calls=originalCalls;
        if(scenario==7){bool thrown=false;try{HookWorldDestroy(mode,position,5);}catch(std::runtime_error const&){thrown=true;}CHECK(thrown);}
        else if(scenario==8){Depth nested;CHECK(HookWorldDestroy(mode,position,5)==0x1234567812345601);}
        else CHECK(HookWorldDestroy(mode,position,5)==(scenario==10?0:scenario==11?1:0x1234567812345601));
        CHECK(originalCalls==calls+1 && nativeDepth==0);
        fixtureForeground=fixtureHidden=true;
        FixtureABI::hud=true;
        auto result=r.Tick(x.manager,true,true,Config{},101,1);
        if(scenario==0){CHECK(result==TransferResult::Queued && x.requests==1);CHECK(FixtureABI::Read<int>(&x.destination,60)==2);}
        else CHECK(!result && x.requests==0);
        r.Reset();runtime=nullptr;
    }
    { // Manual removal without a native destroy call is not a break.
        FixtureABI::Fixture x;FixtureABI::Prepare(x);HoldTool(x);auto r=FixtureABI::New();
        CHECK(!r.Tick(x.manager,true,true,Config{},100,1));FixtureABI::Put(&x.destination,34,std::uint8_t{});
        CHECK(!r.Tick(x.manager,true,true,Config{},101,1) && x.requests==0);
    }
    { // Server-side sampling is post-original and is NEVER an acceptance result.
        FixtureABI::Fixture x;FixtureABI::Prepare(x);auto r=FixtureABI::New();runtime=&r;
        FixtureABI::ActorFixture server(false),local(true);
        alignas(8) std::byte player[3504]{},supplies[192]{},inventory[440]{},stacks[36*152]{},mode[16]{},position[12]{};
        server.Attach(player);FixtureABI::Put(player,1464,static_cast<void*>(supplies));FixtureABI::Put(supplies,184,static_cast<void*>(inventory));
        FixtureABI::Put(inventory,0,imageBase+0xE6AFE10);FixtureABI::Put(inventory,8,std::uint8_t{255});
        FixtureABI::Put(inventory,432,static_cast<void*>(player));FixtureABI::Put(inventory,408,static_cast<void*>(stacks));
        FixtureABI::Put(inventory,416,static_cast<void*>(stacks+36*152));FixtureABI::Put(stacks,2*152+34,std::uint8_t{1});
        FixtureABI::Put(stacks,9*152+34,std::uint8_t{1});FixtureABI::Put(mode,8,static_cast<void*>(player));
        auto identity=ReadActorIdentity(local.actor.data());auto now=GetTickCount64();integrated.Local(identity,now);
        CHECK(!integrated.Ready(identity,now));action=Action::Server;serverStacks=stacks;expectedMode=mode;expectedPosition=position;
        std::intptr_t forwarded{};
        std::thread serverThread([&]{forwarded=HookWorldDestroy(mode,position,5);});serverThread.join();
        CHECK(forwarded==0x1234567812345601);
        auto counts=integrated.AuthoritativeCounts(identity,GetTickCount64());CHECK(counts && (*counts)[2]==0 && (*counts)[9]==1);
        CHECK(integrated.Arm(identity,GetTickCount64(),-4,55));
        CHECK(HookWorldDestroy(mode,position,5)==0x1234567812345601 && !integrated.Poll());
        integrated.Clear();integrated.Invalidate();runtime=nullptr;
    }
    std::cout<<"World-destroy native-use,tool replacement,main-hand guards,manual removal,unwind and post-server sample passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
