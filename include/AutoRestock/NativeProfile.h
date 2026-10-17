#pragma once
#include "Profile.h"
namespace autorestock {
struct NativeAddress {std::uint32_t reference,target;};
enum class OperationAbi { FunctionRef, OwningFunction };
enum class EndRequestAbi { Manager, Scope16 };
// Shared values remain explicit reviewed contracts; no implicit 52 default.
struct NativeLayout {
    std::uint32_t stackSize,count,variant,variantTable,variantValue;
    std::uint32_t managerOwner,managerPlayer,managerMode,managerClient;
    std::uint32_t playerClient,playerSupplies,suppliesInventory,selectedSlot;
    std::uint32_t inventoryFirst,inventoryLast,inventoryOwner;
    std::uint32_t sceneHasName,inventoryGet,itemDamageable,itemMaxDamage;
    std::uint32_t schedulerOwner,clientGame,gameSecondary,gameNetwork;
    std::uint32_t networkSecondary,networkRegistration,networkFirst,networkLast,networkCapacity;
    std::uint32_t handlerClient,handlerEstablished,handlerGuid,handlerType;
    std::uint32_t connectionType,connectionGuid,connectionState,connectionRemoved,connectionClosing,connectionPeer;
    std::uint32_t packetComplex,packetRequest,packetMode;
    std::uint32_t sendAuto,sendNormal,sendPacket;
    std::uint32_t operationImplementation,implementationInvoker,closureCapture,gameModePlayer;
    OperationAbi operationAbi;
    EndRequestAbi endRequestAbi;
};
struct NativeProfile {
    GameVersion version;
    bool runtimeReady;
    std::uint32_t imageExtent;
    NativeLayout const& layout;
    std::span<NativeAddress const> addresses;
    std::span<Guard const> guards;
    // A reference is a catalog key, NEVER an address fallback.
    std::uint32_t Rva(std::uint32_t reference) const noexcept;
};
NativeProfile const* FindNativeProfile(GameVersion) noexcept;
// Read-only loaded-image header/guard inspection; not disk/runtime admission.
NativeProfile const* InspectLoadedNativeProfile(HMODULE) noexcept;
NativeProfile const* ValidateNativeProfile(HMODULE,std::string& error);
}
