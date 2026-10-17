#include "AutoRestock/NativeStack.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
template<class T> T Read(void const* p,int offset) { T v;std::memcpy(&v,static_cast<std::byte const*>(p)+offset,sizeof v);return v; }
template<class T> void Put(void* p,int offset,T v) {std::memcpy(static_cast<std::byte*>(p)+offset,&v,sizeof v);}
// Doubles model only the external native ownership boundary, not the game.
// Catch missing flags0 destruction, shallow copies, writes to borrowed memory,
// wildcard acceptance, count truncation, and partial normalization.
int copies{},destructions{},migrations{},damageWrites{},nativeComparisons{};
std::vector<void*> live;
void* Copy(void* out,void const* in) {
    CHECK(out!=in); CHECK(reinterpret_cast<std::uintptr_t>(out)%8==0);
    std::memcpy(out,in,152); live.push_back(out); ++copies; return out;
}
void* Destroy(void* p,int flags) {
    CHECK(flags==0);
    auto found=std::find(live.begin(),live.end(),p);CHECK(found!=live.end());
    live.erase(found);++destructions;return p;
}
bool Empty(void const* p) {return Read<std::uint8_t>(p,34)==0;}
std::uint8_t Capacity(void const* p) {return Read<std::uint8_t>(p,64);}
bool Equal(void const* a,void const* b) {
    ++nativeComparisons;
    // Intentionally wildcard aux/block: production must enforce exact fields.
    if(Read<std::uint8_t>(a,65)==1) return true;
    if(Read<std::uint8_t>(a,65)==2) return false;
    return Read<std::uint64_t>(a,8)==Read<std::uint64_t>(b,8) &&
        Read<int>(a,56)==Read<int>(b,56) && Read<int>(a,60)==Read<int>(b,60);
}
void SetCount(void* p,std::uint8_t value) {CHECK(std::find(live.begin(),live.end(),p)!=live.end());Put(p,34,value);}
void Migrate(void*,void* stack) {CHECK(std::find(live.begin(),live.end(),stack)!=live.end());++migrations;}
void SetDamage(void*,void* stack,std::int16_t damage) {
    CHECK(std::find(live.begin(),live.end(),stack)!=live.end());CHECK(damage==0);
    Put(stack,60,int{0});++damageWrites;
}
StackApi Api() {return {Copy,Destroy,Empty,Capacity,Equal,Equal,SetCount,Migrate,SetDamage};}
StackStorage Item(int count=5,int damage=0) {
    StackStorage v{};
    static std::uintptr_t nativeItem=0x12345678;
    Put(&v,8,reinterpret_cast<std::uintptr_t>(&nativeItem));
    Put(&v,34,static_cast<std::uint8_t>(count));Put(&v,35,std::uint8_t{1});
    Put(&v,56,int{123});Put(&v,60,damage);Put(&v,64,std::uint8_t{64});return v;
}
int main() {
    try {
        auto api=Api();auto borrowed=Item();auto before=borrowed.bytes;
        CHECK(api.Complete());
        {
            OwnedStack a;CHECK(!a.SetCount(1));CHECK(!a.NormalizeDamage());
            CHECK(a.Copy(api,&borrowed));CHECK(copies==1 && live.size()==1);
            CHECK(a.Get()!=&borrowed && a.Count()==5);
            CHECK(!a.SetCount(-1));CHECK(!a.SetCount(65));CHECK(!a.SetCount(256));
            CHECK(a.Count()==5);CHECK(a.SetCount(32));CHECK(a.Count()==32);
            CHECK(borrowed.bytes==before);
            auto stable=a.Get();OwnedStack b(std::move(a));
            CHECK(!a.Get() && b.Get()==stable && b.Count()==32);
            OwnedStack c;CHECK(c.Copy(api,&borrowed));
            c=std::move(b);CHECK(destructions==1 && !b.Get() && c.Get()==stable);
            c=std::move(c);CHECK(c.Get()==stable && destructions==1);
            auto bad=api;bad.destroy=nullptr;
            CHECK(!c.Copy(bad,&borrowed));CHECK(c.Get()==stable);
            CHECK(!c.Copy(api,nullptr));CHECK(c.Get()==stable);
            CHECK(c.Copy(api,c.Get()));CHECK(c.Get()!=stable);
            CHECK(destructions==2 && c.Count()==32);
            CHECK(c.SetCount(0));CHECK(c.Count()==0);
            c.Reset();c.Reset();CHECK(!c.Get() && live.empty());
        }
        CHECK(copies==destructions);
        auto other=Item(31);CHECK(SameNativeItem(api,&borrowed,&other));
        Put(&other,32,std::uint16_t{32767});
        int calls=nativeComparisons;CHECK(!SameNativeItem(api,&borrowed,&other));
        CHECK(nativeComparisons==calls);
        other=Item();Put(&other,24,std::uintptr_t{42});
        CHECK(!SameNativeItem(api,&borrowed,&other));
        other=Item();Put(&other,65,std::uint8_t{1});Put(&borrowed,65,std::uint8_t{2});
        CHECK(!SameNativeItem(api,&other,&borrowed));
        borrowed=Item(1,49);other=Item(1,2);before=borrowed.bytes;auto otherBefore=other.bytes;
        CHECK(!SameNativeItem(api,&borrowed,&other));
        CHECK(SameNativeToolIgnoringDamage(api,&borrowed,&other));
        CHECK(migrations==2 && damageWrites==2 && live.empty());
        CHECK(borrowed.bytes==before && other.bytes==otherBefore);
        Put(&other,56,int{456});
        CHECK(!SameNativeToolIgnoringDamage(api,&borrowed,&other));
        CHECK(live.empty() && copies==destructions);
        CHECK(!SameNativeItem(api,nullptr,&other));
        auto incomplete=api;incomplete.equal=nullptr;
        CHECK(!SameNativeItem(incomplete,&borrowed,&other));
        std::cout<<"PASS: stable native ownership, flags0 lifetime, exact/symmetric compatibility, owned-only durability normalization\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
