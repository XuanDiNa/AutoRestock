#include "AutoRestock/World.h"
#include <cstring>
#include <limits>
namespace autorestock {
namespace {
template<class T> T Read(void const* p,std::size_t o) noexcept {
    auto a=reinterpret_cast<std::uintptr_t>(p);if(!a || a>std::numeric_limits<std::uintptr_t>::max()-o-sizeof(T)) return {};
    __try {T v;std::memcpy(&v,reinterpret_cast<void const*>(a+o),sizeof v);return v;}
    __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
template<GameVersion Version> bool HasName(void* scene,NativeStringView const* name){
    auto table=Read<void*>(scene,0);auto fn=Read<decltype(WorldApi::hasName)>(table,FindNativeProfile(Version)->layout.sceneHasName);
    return fn && fn(scene,name);
}
template<GameVersion Version> void const* InventoryItem(void* inventory,int slot){
    auto table=Read<void*>(inventory,0);auto fn=Read<decltype(WorldApi::inventoryItem)>(table,FindNativeProfile(Version)->layout.inventoryGet);
    return fn && slot>=0 && slot<36 ? fn(inventory,slot) : nullptr;
}
template<GameVersion Version> bool Damageable(void const* stack){
    auto item=Read<void*>(Read<void*>(stack,8),0);auto table=Read<void*>(item,0);
    auto test=Read<bool(*)(void*)>(table,FindNativeProfile(Version)->layout.itemDamageable);auto max=Read<int(*)(void*)>(table,FindNativeProfile(Version)->layout.itemMaxDamage);
    return item && test && max && test(item) && max(item)>0;
}
WorldFailure Check(WorldApi const& api,WorldView& view) noexcept {
    __try {
        if(api.localPlayer(view.client)!=view.player) return WorldFailure::LocalPlayer;
        if(!api.worldInput(view.client)) return WorldFailure::WorldInput;
        view.world=api.world(view.client);if(!view.world) return WorldFailure::World;
        auto scene=api.topScene(view.client);NativeStringView hud{"hud_screen",10};
        if(!scene)return WorldFailure::Scene;
        return api.hasName(scene,&hud)?WorldFailure::None:WorldFailure::NotHud;
    } __except(EXCEPTION_EXECUTE_HANDLER){return WorldFailure::NativeException;}
}
}
bool WorldApi::Complete() const noexcept {return profile && FindNativeProfile(profile->version)==profile && localPlayer && world && worldInput && topScene && hasName && inventoryItem && damageable;}
std::optional<WorldApi> BindWorld(HMODULE module,std::string& error){
    auto profile=ValidateNativeProfile(module,error);if(!profile)return {};
    auto base=reinterpret_cast<std::uintptr_t>(module);
    WorldApi api;
    api.profile=profile;
    api.localPlayer=reinterpret_cast<decltype(api.localPlayer)>(base+profile->Rva(0x5DAED30));
    api.world=reinterpret_cast<decltype(api.world)>(base+profile->Rva(0x5DC2630));
    api.worldInput=reinterpret_cast<decltype(api.worldInput)>(base+profile->Rva(0x5DC0C80));
    api.topScene=reinterpret_cast<decltype(api.topScene)>(base+profile->Rva(0x5DC6430));
    switch(profile->version){
#define BIND_WORLD_VERSION(v) case GameVersion::v: api.hasName=HasName<GameVersion::v>;api.inventoryItem=InventoryItem<GameVersion::v>;api.damageable=Damageable<GameVersion::v>;break
        BIND_WORLD_VERSION(V45);BIND_WORLD_VERSION(V51);BIND_WORLD_VERSION(V52);
#undef BIND_WORLD_VERSION
    }
    error.clear();return api;
}
char const* WorldFailureName(WorldFailure failure) noexcept {
    constexpr char const* names[]={"none","foreground","cursor-visible","world-api","image","player",
        "manager-table","manager-owner","player-manager","mode","manager-mode","manager-client",
        "client-table","inventory","selected-slot","local-player","world-input","world","scene","not-hud","native-exception"};
    auto index=static_cast<std::size_t>(failure);return index<std::size(names)?names[index]:"unknown";
}
std::optional<WorldView> ResolveWorld(WorldApi const& api,std::uintptr_t base,void* manager,void* player,bool foreground,bool hidden,InventoryMode mode,WorldFailure* failure){
    if(failure)*failure=WorldFailure::None;
    auto fail=[&](WorldFailure reason)->std::optional<WorldView>{if(failure)*failure=reason;return {};};
    if(!foreground)return fail(WorldFailure::Foreground);
    if(!hidden)return fail(WorldFailure::CursorVisible);
    if(!api.Complete())return fail(WorldFailure::Api);
    auto const& profile=*api.profile;auto const& layout=profile.layout;
    if(!base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent)return fail(WorldFailure::Image);
    if(!player)return fail(WorldFailure::Player);
    if(Read<std::uintptr_t>(manager,0)!=base+profile.Rva(0xE827EC0))return fail(WorldFailure::ManagerTable);
    if(Read<void*>(manager,layout.managerOwner)!=player)return fail(WorldFailure::ManagerOwner);
    if(Read<void*>(player,layout.managerPlayer)!=manager)return fail(WorldFailure::PlayerManager);
    if(mode!=InventoryMode::Modern && mode!=InventoryMode::Ordinary)return fail(WorldFailure::Mode);
    if(Read<std::uint8_t>(manager,layout.managerMode)!=(mode==InventoryMode::Modern?1:0))return fail(WorldFailure::ManagerMode);
    if(Read<std::uint8_t>(manager,layout.managerClient)!=1)return fail(WorldFailure::ManagerClient);
    WorldView view;view.player=player;view.manager=manager;view.client=Read<void*>(player,layout.playerClient);
    if(Read<std::uintptr_t>(view.client,0)!=base+profile.Rva(0xE9731B0))return fail(WorldFailure::ClientTable);
    view.supplies=Read<void*>(player,layout.playerSupplies);view.inventory=Read<void*>(view.supplies,layout.suppliesInventory);
    view.selected=Read<int>(view.supplies,layout.selectedSlot);
    if(!view.inventory)return fail(WorldFailure::Inventory);
    if(view.selected<0 || view.selected>8)return fail(WorldFailure::SelectedSlot);
    auto result=Check(api,view);if(result!=WorldFailure::None)return fail(result);
    return view;
}
char const* UseInvokerName(int invoker) noexcept{
    constexpr char const* names[]={"unknown","use","use-on","destroy","attack","interact","interaction","direct-player","delayed-use","world-destroy","tool-hurt"};
    return invoker>=0 && invoker<static_cast<int>(std::size(names))?names[invoker]:names[0];
}
void* UsePlayer(NativeProfile const& profile,std::uintptr_t base,void const* operation,int* invoker) noexcept{
    if(invoker)*invoker=0;
    if(FindNativeProfile(profile.version)!=&profile)return nullptr;
    if(!base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent)return nullptr;
    auto const& layout=profile.layout;
    void const* closure{};std::uintptr_t entry{};
    if(layout.operationAbi==OperationAbi::OwningFunction){
        closure=Read<void*>(operation,layout.operationImplementation);
        entry=Read<std::uintptr_t>(Read<void*>(closure,0),layout.implementationInvoker);
    }else{
        entry=Read<std::uintptr_t>(operation,0);closure=Read<void*>(operation,8);
    }
    if(entry<base)return nullptr;
    constexpr std::uint32_t references[]={0x25B5EF0,0x25B60F0,0x25B6380,0x25B4C50,0x25B4530,0x25B4E70,0x25B6660,0x21E6E0};
    for(unsigned n=0;n<std::size(references);++n){
        auto rva=profile.Rva(references[n]);if(!rva || entry-base!=rva)continue;
        auto captured=Read<void*>(closure,layout.closureCapture);
        if(invoker)*invoker=n+1;
        return n<6?Read<void*>(captured,layout.gameModePlayer):captured;
    }
    return nullptr;
}
}
