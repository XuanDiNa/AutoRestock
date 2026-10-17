#include "AutoRestock/World.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace autorestock;
#define CHECK(x) do {if(!(x))throw std::runtime_error("FAIL at "+std::to_string(__LINE__)+": " #x);}while(false)
template<class T>void Put(void* p,std::size_t n,T value){std::memcpy(static_cast<std::byte*>(p)+n,&value,sizeof value);}
constexpr std::uintptr_t base=0x140000000;
alignas(8) std::byte player[3448]{},client[8]{},manager[168]{},supplies[192]{},inventory[440]{},scene[8]{},world[8]{};
void* Local(void*){return player;}void* World(void*){return world;}bool Yes(void*){return true;}
void* Top(void*){return scene;}bool Name(void*,NativeStringView const*){return true;}
void const* Item(void*,int){return nullptr;}bool Tool(void const*){return false;}
int main(){try{
    auto p45=FindNativeProfile(GameVersion::V45),p51=FindNativeProfile(GameVersion::V51),p52=FindNativeProfile(GameVersion::V52);
    CHECK(p45 && p51 && p52 && p45!=p51 && p51!=p52);
    CHECK(!FindNativeProfile(static_cast<GameVersion>(99)));
    // Independent constructor pointers, not values manufactured using Rva().
    Put(client,0,base+0xE774DB0);Put(manager,0,base+0xE8201E0);
    Put(manager,8,std::uint8_t{1});Put(manager,9,std::uint8_t{1});Put(manager,16,static_cast<void*>(player));
    Put(player,2744,static_cast<void*>(manager));Put(player,3440,static_cast<void*>(client));
    Put(player,1464,static_cast<void*>(supplies));Put(supplies,184,static_cast<void*>(inventory));Put(supplies,16,int{2});
    WorldApi api{Local,World,Yes,Top,Name,Item,Tool,p45};
    CHECK(ResolveWorld(api,base,manager,player,true,true));
    api.profile=p51;CHECK(!ResolveWorld(api,base,manager,player,true,true));
    api.profile=p52;CHECK(!ResolveWorld(api,base,manager,player,true,true));
    api.profile=nullptr;CHECK(!ResolveWorld(api,base,manager,player,true,true));
    CHECK(!p45->Rva(0xFFFFFF) && !p51->Rva(0xFFFFFF) && !p52->Rva(0xFFFFFF));
    auto incomplete=*p45;incomplete.addresses={};api.profile=&incomplete;
    CHECK(!ResolveWorld(api,base,manager,player,true,true));
    auto copied=*p45;api.profile=&copied;
    CHECK(!ResolveWorld(api,base,manager,player,true,true));
    // 45 owns std::function. +56 -> implementation vtable -> Do_call+16.
    alignas(8) std::byte operation[64]{},implementation[64]{},mode[16]{},table[24]{};
    Put(operation,56,static_cast<void*>(implementation));Put(implementation,0,static_cast<void*>(table));
    Put(implementation,8,static_cast<void*>(mode));Put(mode,8,static_cast<void*>(player));
    int invoker{};
    constexpr std::uint32_t calls[]={0x2D3DCD0,0x2D3DF20,0x2D3E1F0,0x2D3B700,0x2D3B000,0x2D3CA90};
    for(unsigned i=0;i<6;++i){Put(table,16,base+calls[i]);CHECK(UsePlayer(*p45,base,operation,&invoker)==player && invoker==int(i+1));}
    Put(implementation,8,static_cast<void*>(player));
    Put(table,16,base+0x2D3E830);CHECK(UsePlayer(*p45,base,operation,&invoker)==player && invoker==7);
    Put(table,16,base+0x2FF0610);CHECK(UsePlayer(*p45,base,operation,&invoker)==player && invoker==8);
    Put(table,16,base+0x25B6660);CHECK(!UsePlayer(*p45,base,operation,&invoker) && !invoker);
    Put(operation,0,base+0x25B6660);Put(operation,8,static_cast<void*>(implementation));
    CHECK(!UsePlayer(*p45,base,operation,&invoker) && !invoker);
    Put(operation,56,std::uintptr_t{});CHECK(!UsePlayer(*p45,base,operation,&invoker));
    std::cout<<"PASS: cross-version world refusal, explicit profile requirement and 45 operation ABI\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
