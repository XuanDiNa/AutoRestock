#include "AutoRestock/Native.h"
#include <cstring>
#include <limits>
namespace autorestock {
void EndNativeRequest(NativeApi const& api,void* manager){
    if(!api.profile || !api.end || !manager)return;
    if(api.profile->layout.endRequestAbi==EndRequestAbi::Scope16){
        struct alignas(8) Scope {void* manager;std::uint8_t active;std::uint8_t reserved[7];};
        static_assert(sizeof(Scope)==16);
        Scope scope{manager,1,{}};api.end(&scope);
    }else api.end(manager);
}
bool NativeApi::Complete() const noexcept {
    return profile && FindNativeProfile(profile->version)==profile && stack.Complete() && context && size && getItem && setItem &&
        canBegin && begin && place && add && end && destroyPlace;
}
std::optional<NativeApi> BindNative(HMODULE module,std::string& error) {
    auto profile=ValidateNativeProfile(module,error);if(!profile)return {};
    auto base=reinterpret_cast<std::uintptr_t>(module);
    NativeApi api;api.profile=profile;
    api.stack.copy=reinterpret_cast<decltype(api.stack.copy)>(base+profile->Rva(0x2482180));
    api.stack.destroy=reinterpret_cast<decltype(api.stack.destroy)>(base+profile->Rva(0x21B1C0));
    api.stack.empty=reinterpret_cast<decltype(api.stack.empty)>(base+profile->Rva(0x2486D00));
    api.stack.capacity=reinterpret_cast<decltype(api.stack.capacity)>(base+profile->Rva(0x247DCE0));
    api.stack.merge=reinterpret_cast<decltype(api.stack.merge)>(base+profile->Rva(0x2488D80));
    api.stack.equal=reinterpret_cast<decltype(api.stack.equal)>(base+profile->Rva(0x248D860));
    api.stack.setCount=reinterpret_cast<decltype(api.stack.setCount)>(base+profile->Rva(0x2486E40));
    api.stack.migrateDamage=reinterpret_cast<decltype(api.stack.migrateDamage)>(base+profile->Rva(0x24C7CD0));
    api.stack.setDamage=reinterpret_cast<decltype(api.stack.setDamage)>(base+profile->Rva(0x24C7D80));
    api.context=reinterpret_cast<decltype(api.context)>(base+profile->Rva(0x25AC180));
    api.size=reinterpret_cast<decltype(api.size)>(base+profile->Rva(0x7C99260));
    api.getItem=reinterpret_cast<decltype(api.getItem)>(base+profile->Rva(0x7C98E60));
    api.setItem=reinterpret_cast<decltype(api.setItem)>(base+profile->Rva(0x7C98F50));
    api.canBegin=reinterpret_cast<decltype(api.canBegin)>(base+profile->Rva(0x25AF570));
    api.begin=reinterpret_cast<decltype(api.begin)>(base+profile->Rva(0x25B0750));
    api.place=reinterpret_cast<decltype(api.place)>(base+profile->Rva(0x33F6460));
    api.add=reinterpret_cast<decltype(api.add)>(base+profile->Rva(0x25B1EF0));
    api.end=reinterpret_cast<decltype(api.end)>(base+profile->Rva(0x25B0A90));
    api.destroyPlace=reinterpret_cast<decltype(api.destroyPlace)>(base+profile->Rva(0x7C9EC10));
    api.legacy.beginScope=reinterpret_cast<decltype(api.legacy.beginScope)>(base+profile->Rva(0x25B05E0));
    api.legacy.endScope=reinterpret_cast<decltype(api.legacy.endScope)>(base+profile->Rva(0x25BE780));
    api.legacy.inventoryItem=reinterpret_cast<decltype(api.legacy.inventoryItem)>(base+profile->Rva(0x25A5100));
    api.legacy.setInventory=reinterpret_cast<decltype(api.legacy.setInventory)>(base+profile->Rva(0x1D5170));
    error.clear();
    return api;
}
namespace {
template<class T> bool Read(void const* p,std::size_t offset,T& out) noexcept {
    auto address=reinterpret_cast<std::uintptr_t>(p);
    if (!address || address>std::numeric_limits<std::uintptr_t>::max()-offset-sizeof(T)) return false;
    __try { std::memcpy(&out,reinterpret_cast<void const*>(address+offset),sizeof(T)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Is(void const* p,std::size_t offset,T expected) noexcept {
    T value{};return Read(p,offset,value) && value==expected;
}
void* Context(NativeApi const& api,void* manager) noexcept {
    __try { return api.context(manager); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
int Size(NativeApi const& api,void const* wrapper) noexcept {
    __try { return api.size(wrapper); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}
std::optional<WorldInventoryView> ResolveWorldInventory(NativeApi const& api,std::uintptr_t base,void* manager,void* player) {
    if(!api.profile)return {};
    auto profile=api.profile;auto const& layout=profile->layout;
    if (!api.Complete() || !base || base>std::numeric_limits<std::uintptr_t>::max()-profile->imageExtent || !player ||
        !Is(manager,0,base+profile->Rva(0xE827EC0)) || !Is(manager,layout.managerOwner,player) ||
        !Is(manager,layout.managerMode,std::uint8_t{1}) || !Is(manager,layout.managerClient,std::uint8_t{1}) ||
        !Is(manager,88,std::uint8_t{0}) || !Is(manager,96,std::uintptr_t{0})) return {};
    void *supplies{},*inventory{},*deque{};
    if (!Read(player,layout.playerSupplies,supplies) || !Read(supplies,layout.suppliesInventory,inventory) || !inventory ||
        !Read(manager,56,deque) || !Is(deque,32,std::size_t{1})) return {};
    void* component=Context(api,manager);
    std::uintptr_t first{},last{};
    if (!Read(component,152,first) || !Read(component,160,last) || !first ||
        first%8 || last<first || (last-first)%8 || last-first>16*8) return {};
    void* found{};
    for (auto entry=first;entry<last;entry+=8) {
        void *adapter{},*wrapper{},*requestCallback{},*playerCallback{};
        int containerID{};
        if (!Read(reinterpret_cast<void const*>(entry),0,adapter)) return {};
        if (!Is(adapter,0,base+profile->Rva(0xE827EA8)) || !Read(adapter,8,wrapper) ||
            !Is(wrapper,0,base+profile->Rva(0xEA05D70)) || !Is(wrapper,416,int{1}) ||
            !Is(wrapper,424,inventory)) continue;
        if (!Read(wrapper,400,containerID) || !containerID || !Is(inventory,400,containerID) ||
            !Read(wrapper,496,requestCallback) || !Read(wrapper,504,playerCallback) ||
            !Is(requestCallback,0,base+profile->Rva(0xEA05EE0)) || !Is(requestCallback,8,manager) ||
            !Is(playerCallback,0,base+profile->Rva(0xEA05CF0)) || !Is(playerCallback,8,player) ||
            !Is(wrapper,960,std::uint8_t{1}) || !Is(wrapper,961,std::uint8_t{1}) ||
            Size(api,wrapper)!=36 || found) return {};
        found=wrapper;
    }
    if (!found) return {};
    return WorldInventoryView{manager,player,inventory,found,component};
}
std::optional<WorldInventoryView> ResolveOrdinaryInventory(NativeApi const& api,std::uintptr_t base,void* manager,void* player) {
    if(!api.profile || FindNativeProfile(api.profile->version)!=api.profile)return {};
    auto profile=api.profile;auto const& layout=profile->layout;
    if(!api.stack.Complete() || !api.legacy.inventoryItem || !api.legacy.setInventory || !base ||
        base>std::numeric_limits<std::uintptr_t>::max()-profile->imageExtent || !player ||
        !Is(manager,0,base+profile->Rva(0xE827EC0)) || !Is(manager,layout.managerOwner,player) || !Is(player,layout.managerPlayer,manager) ||
        !Is(manager,layout.managerMode,std::uint8_t{0}) || !Is(manager,layout.managerClient,std::uint8_t{1}) ||
        !Is(manager,80,int{0}) || !Is(manager,88,std::uint8_t{0}) ||
        !Is(manager,96,std::uintptr_t{0}) || !Is(manager,104,std::uintptr_t{0}))return {};
    void *supplies{},*inventory{};std::uintptr_t first{},last{};
    if(!Read(player,layout.playerSupplies,supplies) || !Read(supplies,layout.suppliesInventory,inventory) || !inventory ||
        !Is(inventory,0,base+profile->Rva(0xE6AFE10)) || !Is(inventory,8,std::uint8_t{255}) ||
        !Is(inventory,layout.inventoryOwner,player) || !Read(inventory,layout.inventoryFirst,first) || !Read(inventory,layout.inventoryLast,last) ||
        !first || first%8 || last<first || last-first!=36*152)return {};
    return WorldInventoryView{manager,player,inventory,nullptr,nullptr};
}
}
