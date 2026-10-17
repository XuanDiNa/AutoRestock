#include "AutoRestock/World.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
template<class T> void Put(void* p,int o,T x){std::memcpy(static_cast<std::byte*>(p)+o,&x,sizeof x);}
constexpr std::uintptr_t base=0x140000000;
alignas(8) std::byte player[3448]{},client[8]{},manager[168]{},supplies[232]{},inventory[408]{},scene[8]{},world[8]{};
bool input=true,hud=true;int calls{};
void* Local(void* p){CHECK(p==client);++calls;return player;}
void* World(void* p){CHECK(p==client);return world;}
bool Input(void* p){CHECK(p==client);return input;}
void* Top(void* p){CHECK(p==client);return scene;}
bool Name(void* p,NativeStringView const* s){CHECK(p==scene && s->size==10 && std::memcmp(s->data,"hud_screen",10)==0);return hud;}
void const* Inventory(void*,int){return nullptr;}
bool Damageable(void const*){return false;}
int main(){try{
    auto profile=FindNativeProfile(GameVersion::V52);CHECK(profile);
    WorldApi api{Local,World,Input,Top,Name,Inventory,Damageable,profile};CHECK(api.Complete());
    Put(client,0,base+0xE9731B0);Put(manager,0,base+0xE827EC0);Put(manager,8,std::uint8_t{1});Put(manager,9,std::uint8_t{1});
    Put(manager,16,static_cast<void*>(player));Put(player,3440,static_cast<void*>(client));Put(player,2744,static_cast<void*>(manager));
    Put(player,1464,static_cast<void*>(supplies));Put(supplies,16,int{3});Put(supplies,184,static_cast<void*>(inventory));
    auto resolve=[&](bool foreground=true,bool hidden=true){return ResolveWorld(api,base,manager,player,foreground,hidden);};
    auto v=resolve();CHECK(v && v->client==client && v->world==world && v->selected==3 && v->inventory==inventory);
    CHECK(!ResolveWorld(api,base,manager,player,true,true,InventoryMode::Ordinary));
    Put(manager,8,std::uint8_t{});CHECK(!resolve());
    CHECK(ResolveWorld(api,base,manager,player,true,true,InventoryMode::Ordinary));
    CHECK(!ResolveWorld(api,base,manager,player,false,true,InventoryMode::Ordinary));
    CHECK(!ResolveWorld(api,base,manager,player,true,true,InventoryMode::Unknown));
    Put(manager,8,std::uint8_t{2});CHECK(!ResolveWorld(api,base,manager,player,true,true,InventoryMode::Ordinary));
    Put(manager,8,std::uint8_t{1});
    calls=0;CHECK(!resolve(false));CHECK(!resolve(true,false));CHECK(calls==0);
    input=false;CHECK(!resolve());input=true;hud=false;CHECK(!resolve());hud=true;
    Put(player,2744,std::uintptr_t{1});CHECK(!resolve());Put(player,2744,static_cast<void*>(manager));
    for(int n:{-1,9,35}){Put(supplies,16,n);CHECK(!resolve());}Put(supplies,16,int{3});
    Put(client,0,std::uintptr_t{1});CHECK(!resolve());Put(client,0,base+0xE9731B0);
    CHECK(!ResolveWorld(api,base,reinterpret_cast<void*>(1),player,true,true));
    alignas(8) std::byte operation[16]{},closure[48]{},gameMode[16]{};
    Put(operation,8,static_cast<void*>(closure));Put(closure,0,static_cast<void*>(gameMode));Put(gameMode,8,static_cast<void*>(player));
    int invoker{},expectedInvoker{};
    for(auto rva:{0x25B5EF0,0x25B60F0,0x25B6380,0x25B4C50,0x25B4530,0x25B4E70}){
        Put(operation,0,base+rva);CHECK(UsePlayer(*profile,base,operation,&invoker)==player && invoker==++expectedInvoker);
    }
    Put(closure,0,static_cast<void*>(player));Put(operation,0,base+0x25B6660);CHECK(UsePlayer(*profile,base,operation,&invoker)==player && invoker==7);
    CHECK(std::string(UseInvokerName(invoker))=="direct-player");
    Put(operation,0,base+0x21E6E0);CHECK(UsePlayer(*profile,base,operation,&invoker)==player && invoker==8); // delayed native use completion
    CHECK(std::string(UseInvokerName(invoker))=="delayed-use");
    Put(operation,0,base+0x25B6661);CHECK(!UsePlayer(*profile,base,operation,&invoker) && invoker==0);
    CHECK(std::string(UseInvokerName(-1))=="unknown" && std::string(UseInvokerName(99))=="unknown");
    CHECK(!UsePlayer(*profile,0,operation));CHECK(!UsePlayer(*profile,base,reinterpret_cast<void*>(1)));
    std::string error;CHECK(!BindWorld(GetModuleHandleW(nullptr),error) && !error.empty());
    std::cout<<"PASS: foreground/cursor/HUD/world/current-player gates and exact native use closure provenance\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
