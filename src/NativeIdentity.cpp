#include "AutoRestock/NativeIdentity.h"
#include <cstring>
namespace autorestock {
std::optional<Item> NativeIdentity::Identify(StackApi const& api,void const* stack,bool damageable){
    if(!api.Complete() || !stack) return {};
    auto bytes=static_cast<std::byte const*>(stack);
    auto count=static_cast<int>(bytes[34]);
    bool empty=api.empty(stack);
    if(!count) return empty ? std::optional<Item>{Item{}} : std::nullopt;
    if(empty || bytes[35]!=std::byte{1}) return {};
    auto capacity=api.capacity(stack);
    if(!capacity || capacity>64 || count>capacity) return {};
    auto identity=Intern(api,stack,capacity,false);
    if(!identity) return {};
    std::string normalized;
    if(damageable){
        OwnedStack copy;
        if(!copy.Copy(api,stack) || !copy.NormalizeDamage()) return {};
        auto token=Intern(api,copy.Get(),capacity,true);
        if(!token) return {};
        normalized=std::move(*token);
    }
    return Item{std::move(*identity),std::move(normalized),count,capacity,damageable};
}
std::optional<std::string> NativeIdentity::Intern(StackApi const& api,void const* stack,int capacity,bool normalized){
    for(auto const& entry:entries_)
        if(entry.capacity==capacity && entry.normalized==normalized && SameNativeItem(api,stack,entry.stack.Get())) return entry.token;
    // Refuse unbounded metadata churn. The caller resets the context instead
    // of evicting a class that a pending transfer or depletion still refers to.
    if(entries_.size()>=256 || next_==0) return {};
    OwnedStack copy;if(!copy.Copy(api,stack)) return {};
    auto token=std::to_string(next_++);
    entries_.push_back({std::move(copy),token,capacity,normalized});
    return token;
}
void NativeIdentity::Reset() noexcept {entries_.clear();}
}
