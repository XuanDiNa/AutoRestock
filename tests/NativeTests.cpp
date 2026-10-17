#include "AutoRestock/Native.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
template<class T> void Put(void* p,std::size_t offset,T value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof value);}
template<class T> T Read(void const* p,std::size_t offset){T value;std::memcpy(&value,static_cast<std::byte const*>(p)+offset,sizeof value);return value;}
struct Fixture {
    static constexpr std::uintptr_t base=0x140000000;
    alignas(8) std::byte manager[168]{},player[1480]{},supplies[232]{},inventory[408]{};
    alignas(8) std::byte deque[40]{},component[328]{},adapter[152]{},wrapper[968]{};
    alignas(8) std::byte requestCallback[16]{},playerCallback[16]{};
    void* adapters[4]{};
    int slotCount=36;
    int resolves{},sizes{};
    Fixture() {
        Put(manager,0,base+0xE827EC0);Put(manager,8,std::uint8_t{1});Put(manager,9,std::uint8_t{1});
        Put(manager,16,static_cast<void*>(player));Put(manager,56,static_cast<void*>(deque));
        Put(player,1464,static_cast<void*>(supplies));
        Put(supplies,184,static_cast<void*>(inventory));
        Put(deque,32,std::size_t{1});
        adapters[0]=adapter;Put(component,152,adapters);Put(component,160,adapters+1);
        Put(adapter,0,base+0xE827EA8);Put(adapter,8,static_cast<void*>(wrapper));
        Put(wrapper,0,base+0xEA05D70);Put(wrapper,400,int{123});Put(inventory,400,int{123});
        Put(wrapper,416,int{1});Put(wrapper,424,static_cast<void*>(inventory));
        Put(wrapper,496,static_cast<void*>(requestCallback));Put(wrapper,504,static_cast<void*>(playerCallback));
        Put(wrapper,960,std::uint8_t{1});Put(wrapper,961,std::uint8_t{1});
        Put(requestCallback,0,base+0xEA05EE0);Put(requestCallback,8,static_cast<void*>(manager));
        Put(playerCallback,0,base+0xEA05CF0);Put(playerCallback,8,static_cast<void*>(player));
    }
};
Fixture* f;
void* Context(void* manager) {CHECK(manager==f->manager);++f->resolves;return f->component;}
int Size(void const* wrapper) {CHECK(wrapper==f->wrapper);++f->sizes;return f->slotCount;}
void* Copy(void* a,void const*){return a;}
void* Destroy(void* a,int){return a;}
bool Empty(void const*){return false;}
std::uint8_t Capacity(void const*){return 64;}
bool Equal(void const*,void const*){return true;}
void Count(void*,std::uint8_t){}
void Migrate(void*,void*){}
void Damage(void*,void*,std::int16_t){}
void const* Get(void const*,int){return nullptr;}
void Set(void*,int,void const*){}
bool CanBegin(void*){return true;}
void Begin(void*,bool){}
void** Place(void** p,std::uint8_t const*,void const*,void const*){return p;}
void Add(void*,void**){}
void End(void*){}
void EndScope45(void* scope){
    CHECK(scope!=f->manager && Read<void*>(scope,0)==f->manager && Read<std::uint8_t>(scope,8)==1);
    Put(scope,0,std::uintptr_t{});Put(scope,8,std::uint8_t{});++f->resolves;
}
NativeApi Api(){return {{Copy,Destroy,Empty,Capacity,Equal,Equal,Count,Migrate,Damage},Context,Size,Get,Set,CanBegin,Begin,Place,Add,End,Destroy,{},FindNativeProfile(GameVersion::V52)};}
int main(){try{
    {
        Fixture x;f=&x;auto api=Api();
        api.profile=nullptr;CHECK(!ResolveWorldInventory(api,Fixture::base,x.manager,x.player));
        api.profile=FindNativeProfile(GameVersion::V45);
        api.end=EndScope45;EndNativeRequest(api,x.manager);CHECK(x.resolves==1);
        Put(x.manager,0,Fixture::base+0xE8201E0);Put(x.adapter,0,Fixture::base+0xE8201C8);
        Put(x.wrapper,0,Fixture::base+0xE8204A0);Put(x.requestCallback,0,Fixture::base+0xE820610);
        Put(x.playerCallback,0,Fixture::base+0xE820420);
        CHECK(ResolveWorldInventory(api,Fixture::base,x.manager,x.player));
        api.profile=FindNativeProfile(GameVersion::V51);CHECK(!ResolveWorldInventory(api,Fixture::base,x.manager,x.player));
    }
    Fixture fixture;f=&fixture;auto api=Api();CHECK(api.Complete());
    auto resolve=[&]{return ResolveWorldInventory(api,Fixture::base,f->manager,f->player);};
    auto v=resolve();CHECK(v && v->inventory==f->inventory && v->wrapper==f->wrapper && v->component==f->component);
    CHECK(f->resolves==1 && f->sizes==1);
    for(std::size_t offset:{8,9,88}) {
        auto previous=Read<std::uint8_t>(f->manager,offset);
        Put(f->manager,offset,static_cast<std::uint8_t>(offset==88?1:0));
        int before=f->resolves;CHECK(!resolve());CHECK(f->resolves==before);Put(f->manager,offset,previous);
    }
    Put(f->manager,96,std::uintptr_t{1});CHECK(!resolve());Put(f->manager,96,std::uintptr_t{0});
    Put(f->deque,32,std::size_t{2});CHECK(!resolve());Put(f->deque,32,std::size_t{1});
    Put(f->manager,16,std::uintptr_t{1});CHECK(!resolve());Put(f->manager,16,static_cast<void*>(f->player));
    Put(f->adapter,0,Fixture::base+0xE827EB0);CHECK(!resolve());Put(f->adapter,0,Fixture::base+0xE827EA8);
    Put(f->wrapper,416,int{4});CHECK(!resolve());Put(f->wrapper,416,int{1});
    Put(f->wrapper,424,std::uintptr_t{1});CHECK(!resolve());Put(f->wrapper,424,static_cast<void*>(f->inventory));
    Put(f->requestCallback,8,std::uintptr_t{1});CHECK(!resolve());Put(f->requestCallback,8,static_cast<void*>(f->manager));
    Put(f->playerCallback,8,std::uintptr_t{1});CHECK(!resolve());Put(f->playerCallback,8,static_cast<void*>(f->player));
    Put(f->wrapper,961,std::uint8_t{0});CHECK(!resolve());Put(f->wrapper,961,std::uint8_t{1});
    Put(f->inventory,400,int{124});CHECK(!resolve());Put(f->inventory,400,int{123});
    for(int size:{0,9,27,35,37,1000}){f->slotCount=size;CHECK(!resolve());}f->slotCount=36;
    Put(f->component,160,f->adapters);CHECK(!resolve());Put(f->component,160,f->adapters+1);
    auto broken=api;broken.context=nullptr;CHECK(!ResolveWorldInventory(broken,Fixture::base,f->manager,f->player));
    CHECK(!ResolveWorldInventory(api,Fixture::base,reinterpret_cast<void*>(1),f->player));
    CHECK(!ResolveWorldInventory(api,0,f->manager,f->player));
    std::string error;CHECK(!BindNative(GetModuleHandleW(nullptr),error) && !error.empty());
    CHECK(!BindNative(nullptr,error));
    std::cout<<"PASS: default-world adapter identity/lifetime gates, native context depth, request exclusion and unsupported binding\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
