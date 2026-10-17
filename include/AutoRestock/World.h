#pragma once
#include "Native.h"
#include "Session.h"
namespace autorestock {
struct NativeStringView {char const* data;std::size_t size;};
struct WorldApi {
    void* (*localPlayer)(void*){};        // 5DAED30
    void* (*world)(void*){};              // 5DC2630
    bool (*worldInput)(void*){};          // 5DC0C80
    void* (*topScene)(void*){};           // 5DC6430
    bool (*hasName)(void*,NativeStringView const*){}; // scene virtual57
    void const* (*inventoryItem)(void*,int){};       // container virtual7
    bool (*damageable)(void const*){};              // item virtual14+36
    NativeProfile const* profile{};
    bool Complete() const noexcept;
};
std::optional<WorldApi> BindWorld(HMODULE,std::string& error);
struct WorldView {
    void *client{},*world{},*player{},*manager{},*supplies{},*inventory{};
    int selected{};
};
// First failing eligibility check only. Resolving a diagnostic must not call
// additional game getters after a failed safety gate.
enum class WorldFailure {
    None,Foreground,CursorVisible,Api,Image,Player,ManagerTable,ManagerOwner,
    PlayerManager,Mode,ManagerMode,ManagerClient,ClientTable,Inventory,SelectedSlot,
    LocalPlayer,WorldInput,World,Scene,NotHud,NativeException
};
char const* WorldFailureName(WorldFailure) noexcept;
std::optional<WorldView> ResolveWorld(WorldApi const&,std::uintptr_t imageBase,
    void* manager,void* player,bool foreground,bool hiddenCursor,InventoryMode mode=InventoryMode::Modern,
    WorldFailure* failure=nullptr);
// Resolves the actual player in the known Inventory::createTransactionContext
// native operation closures. Unknown operations are not world-use evidence.
void* UsePlayer(NativeProfile const&,std::uintptr_t imageBase,void const* operation,int* invoker=nullptr) noexcept;
char const* UseInvokerName(int invoker) noexcept;
}
