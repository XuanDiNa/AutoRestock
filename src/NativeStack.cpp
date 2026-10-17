#include "AutoRestock/NativeStack.h"
#include <cstring>
#include <utility>
namespace autorestock {
namespace {
template<class T> T Read(void const* p,std::size_t offset) noexcept {
    T v;std::memcpy(&v,static_cast<std::byte const*>(p)+offset,sizeof v);return v;
}
bool Nonempty(StackApi const& api,void const* p) {
    return p && Read<std::uint8_t>(p,35)==1 && Read<std::uint8_t>(p,34)>0 &&
        Read<void const*>(p,8) && !api.empty(p);
}
}
bool StackApi::Complete() const noexcept {
    return copy && destroy && empty && capacity && merge && equal && setCount && migrateDamage && setDamage;
}
OwnedStack::OwnedStack(OwnedStack&& other) noexcept : api_(other.api_),storage_(std::move(other.storage_)) {}
OwnedStack& OwnedStack::operator=(OwnedStack&& other) noexcept {
    if(this!=&other){Reset();api_=other.api_;storage_=std::move(other.storage_);}return *this;
}
OwnedStack::~OwnedStack() {Reset();}
bool OwnedStack::Copy(StackApi const& api,void const* borrowed) {
    if(!api.Complete() || !borrowed) return false;
    auto next=std::make_unique<StackStorage>();
    api.copy(next.get(),borrowed);
    // Construct before releasing current contents: borrowed can be Get().
    Reset();api_=api;storage_=std::move(next);return true;
}
bool OwnedStack::SetCount(int count) {
    if(!storage_ || count<0 || count>64) return false;
    auto capacity=api_.capacity(storage_.get());
    if(capacity<1 || capacity>64 || count>capacity) return false;
    api_.setCount(storage_.get(),static_cast<std::uint8_t>(count));
    return Count()==count;
}
bool OwnedStack::NormalizeDamage() {
    if(!storage_ || !Nonempty(api_,storage_.get())) return false;
    auto item=Read<void*>(storage_.get(),8);
    item=Read<void*>(item,0);
    if(!item) return false;
    api_.migrateDamage(item,storage_.get());
    api_.setDamage(item,storage_.get(),0);
    return true;
}
void OwnedStack::Reset() noexcept {
    if(storage_){api_.destroy(storage_.get(),0);storage_.reset();}api_={};
}
int OwnedStack::Count() const noexcept {return storage_ ? Read<std::uint8_t>(storage_.get(),34) : 0;}
bool SameNativeItem(StackApi const& api,void const* a,void const* b) {
    return api.Complete() && Nonempty(api,a) && Nonempty(api,b) &&
        Read<std::uint16_t>(a,32)==Read<std::uint16_t>(b,32) &&
        Read<void const*>(a,24)==Read<void const*>(b,24) &&
        api.merge(a,b) && api.merge(b,a) && api.equal(a,b) && api.equal(b,a);
}
bool SameNativeToolIgnoringDamage(StackApi const& api,void const* a,void const* b) {
    if(!api.Complete() || !Nonempty(api,a) || !Nonempty(api,b)) return false;
    OwnedStack first,second;
    return first.Copy(api,a) && second.Copy(api,b) && first.NormalizeDamage() && second.NormalizeDamage() &&
        SameNativeItem(api,first.Get(),second.Get());
}
}
