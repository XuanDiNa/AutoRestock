#include "AutoRestock/NativeIdentity.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <set>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
template<class T> void Put(void* p,int o,T x){std::memcpy(static_cast<std::byte*>(p)+o,&x,sizeof x);}
template<class T> T Read(void const* p,int o){T x;std::memcpy(&x,static_cast<std::byte const*>(p)+o,sizeof x);return x;}
std::set<void*> owned;
void* Copy(void* p,void const* s){std::memcpy(p,s,152);CHECK(owned.insert(p).second);return p;}
void* Destroy(void* p,int flags){CHECK(flags==0 && owned.erase(p)==1);return p;}
bool Empty(void const* p){return !Read<std::uint8_t>(p,34);}
std::uint8_t Capacity(void const* p){return Read<std::uint8_t>(p,64);}
bool Equal(void const* a,void const* b){return Read<int>(a,56)==Read<int>(b,56) && Read<int>(a,60)==Read<int>(b,60);}
void Count(void* p,std::uint8_t n){CHECK(owned.count(p));Put(p,34,n);}
void Migrate(void*,void*){}
void Damage(void*,void* p,std::int16_t n){CHECK(owned.count(p) && n==0);Put(p,60,int{});}
StackStorage Stack(int count=5,int damage=0,int identity=123,int capacity=64){
    StackStorage s{};static std::uintptr_t item=0x1234;Put(&s,8,&item);Put(&s,34,static_cast<std::uint8_t>(count));
    Put(&s,35,std::uint8_t{1});Put(&s,56,identity);Put(&s,60,damage);Put(&s,64,static_cast<std::uint8_t>(capacity));return s;
}
int main(){try{
    StackApi api{Copy,Destroy,Empty,Capacity,Equal,Equal,Count,Migrate,Damage};NativeIdentity map;
    auto first=Stack(),partial=Stack(19),different=Stack(19,0,456);
    auto a=map.Identify(api,&first,false);CHECK(a && a->count==5 && a->capacity==64 && !a->damageable && !a->identity.empty());
    auto b=map.Identify(api,&partial,false);CHECK(b && a->identity==b->identity && b->count==19);
    auto c=map.Identify(api,&different,false);CHECK(c && c->identity!=a->identity);
    auto cap=Stack(5,0,123,16);c=map.Identify(api,&cap,false);CHECK(c && c->identity!=a->identity);
    auto tool=Stack(1,45,789,1),replacement=Stack(1,2,789,1);auto before=tool.bytes;
    auto x=map.Identify(api,&tool,true),y=map.Identify(api,&replacement,true);
    CHECK(x && y && x->damageable && x->identity!=y->identity && x->withoutDurability==y->withoutDurability);
    CHECK(tool.bytes==before);
    auto renamed=Stack(1,2,790,1);auto z=map.Identify(api,&renamed,true);CHECK(z && z->withoutDurability!=y->withoutDurability);
    auto empty=Stack(0);c=map.Identify(api,&empty,false);CHECK(c && c->Empty() && c->identity.empty());
    auto bad=Stack(65);CHECK(!map.Identify(api,&bad,false));
    bad=Stack(5,0,123,0);CHECK(!map.Identify(api,&bad,false));
    CHECK(!map.Identify(api,nullptr,false));
    map.Reset();CHECK(owned.empty());
    std::cout<<"PASS: stable native identity classes, full metadata/capacity distinctions, owned-only durability and reset lifetime\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
