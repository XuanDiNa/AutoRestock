#pragma once
#include "NativeStack.h"
#include "Profile.h"
#include "NativeProfile.h"
#include <optional>
namespace autorestock {
struct LegacyApi {
    void (*beginScope)(void*,void*){};              // 25B05E0 manager -> scope72
    void (*endScope)(void*){};                      // 25BE780 scope
    void const* (*inventoryItem)(void const*,int){}; // 25A5100 actual Inventory::getItem
    void (*setInventory)(void*,int,void const*,bool){}; // 1D5170 actual Inventory::setItem
    bool Complete() const noexcept {return beginScope && endScope && inventoryItem && setInventory;}
};
struct NativeApi {
    StackApi stack;
    void* (*context)(void*){};                     // 25AC180
    int (*size)(void const*){};                    // 7C99260
    void const* (*getItem)(void const*,int){};      // 7C98E60
    void (*setItem)(void*,int,void const*){};       // 7C98F50
    bool (*canBegin)(void*){};                     // 25AF570
    void (*begin)(void*,bool){};                   // 25B0750, return unused
    void** (*place)(void**,std::uint8_t const*,void const*,void const*){}; // 33F6460
    void (*add)(void*,void**){};                   // 25B1EF0, consumes action
    void (*end)(void*){};                          // 25B0A90, return unused
    void* (*destroyPlace)(void*,int){};            // 7C9EC10, flags1 frees104
    LegacyApi legacy;
    NativeProfile const* profile{};
    bool Complete() const noexcept;
};
std::optional<NativeApi> BindNative(HMODULE module,std::string& error);
// Completes only an already-begun modern request, using the selected ABI.
void EndNativeRequest(NativeApi const&,void* manager);

// A borrowed view, valid only for the current native game-thread callback.
// Never keep this in Policy, a worker, or across UI/world/native context calls.
struct WorldInventoryView {
    void* manager{};
    void* player{};
    void* inventory{};
    void* wrapper{};
    void* component{};
};
// Fails closed on UI context depth, active/handling request, unsupported mode,
// wrong player, wrong native adapter/callbacks, and anything other than 36 slots.
// This does not replace the caller's HUD, focus and current-LocalPlayer gates.
std::optional<WorldInventoryView> ResolveWorldInventory(
    NativeApi const&,std::uintptr_t imageBase,void* manager,void* player);
// Explicit mode0 path. No modern registry, context or wrapper exists here.
std::optional<WorldInventoryView> ResolveOrdinaryInventory(
    NativeApi const&,std::uintptr_t imageBase,void* manager,void* player);
}
