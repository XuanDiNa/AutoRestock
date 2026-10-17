#include "AutoRestock/NativeSession.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <limits>
namespace autorestock {
namespace {
template<class T> bool Read(void const* object,std::size_t offset,T& value) noexcept {
    auto address=reinterpret_cast<std::uintptr_t>(object);
    if(!address || address>std::numeric_limits<std::uintptr_t>::max()-offset-sizeof(T))return false;
    __try{std::memcpy(&value,reinterpret_cast<void const*>(address+offset),sizeof value);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Table(void const* object,std::size_t offset,std::uintptr_t expected) noexcept;
}
bool OnNativeMainThread(NativeProfile const& profile,std::uintptr_t base,std::uint32_t current) noexcept {
    if(FindNativeProfile(profile.version)!=&profile || !current || !base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent)return false;
    auto slot=reinterpret_cast<void const*>(base+profile.Rva(0x11D2E3B8));
    void const *scheduler{},*again{};std::uint32_t owner{};
    return Read(slot,0,scheduler) && Table(scheduler,0,base+profile.Rva(0xE76B238)) &&
        Read(scheduler,profile.layout.schedulerOwner,owner) && owner==current && Read(slot,0,again) && scheduler==again;
}
namespace {
bool Table(void const* object,std::size_t offset,std::uintptr_t expected) noexcept {
    std::uintptr_t value{};return Read(object,offset,value) && value==expected;
}
}
std::optional<NativeNetworkBinding> ReadNetworkBinding(NativeProfile const& profile,std::uintptr_t base,void const* client) noexcept {
    if(FindNativeProfile(profile.version)!=&profile || !base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent ||
        !Table(client,0,base+profile.Rva(0xE9731B0)))return {};
    void const *game{},*network{};
    // 5D97580 stores the first constructor argument at client+424; 7C2190
    // supplies its game receiver. 768C70 installs the +48 interface table.
    // 5DCF790 adjusts the receiver by48; its virtual+80 target822AA0 reads
    // receiver+2032, hence game+2080, as independently written by78F430.
    if(!Read(client,profile.layout.clientGame,game) || !Table(game,0,base+profile.Rva(0xE76E2D0)) ||
        !Table(game,profile.layout.gameSecondary,base+profile.Rva(0xE76EAD0)) || !Read(game,profile.layout.gameNetwork,network) ||
        !Table(network,0,base+profile.Rva(0xE75E2D0)))return {};
    return NativeNetworkBinding{reinterpret_cast<std::uintptr_t>(client),
        reinterpret_cast<std::uintptr_t>(game),reinterpret_cast<std::uintptr_t>(network)};
}
std::optional<NativePrimaryGuidRoute> ReadPrimaryGuidRoute(
    NativeProfile const& profile,std::uintptr_t base,void const* handler,void const* packetIdentifier,
    NativeRouteDiagnostic* diagnostic) noexcept {
    if(diagnostic)*diagnostic={};
    auto fail=[&](NativeRouteFailure reason)->std::optional<NativePrimaryGuidRoute>{
        if(diagnostic)diagnostic->failure=reason;
        return {};
    };
    if(!base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent ||
        !Table(handler,0,base+profile.Rva(0xE7AF110)))return fail(NativeRouteFailure::Handler);
    void const* client{};
    if(!Read(handler,profile.layout.handlerClient,client))return fail(NativeRouteFailure::NetworkBinding);
    auto binding=ReadNetworkBinding(profile,base,client);
    if(!binding)return fail(NativeRouteFailure::NetworkBinding);
    auto network=reinterpret_cast<void const*>(binding->network);
    void const *registration{},*registered{};
    std::uint8_t established{};
    std::uint32_t suppliedType{},savedType{};
    std::uint64_t suppliedGuid{},savedGuid{};
    if(!Table(network,profile.layout.networkSecondary,base+profile.Rva(0xE75E2F0)) || !Read(network,profile.layout.networkRegistration,registration) ||
        !Read(registration,0,registered) || registered!=handler)return fail(NativeRouteFailure::Registration);
    if(!Read(handler,profile.layout.handlerEstablished,established) || established!=1)return fail(NativeRouteFailure::Established);
    if(!Read(packetIdentifier,profile.layout.connectionType,suppliedType) || suppliedType!=0)return fail(NativeRouteFailure::SuppliedType);
    if(!Read(handler,profile.layout.handlerType,savedType) || savedType!=0)return fail(NativeRouteFailure::SavedType);
    if(!Read(packetIdentifier,profile.layout.connectionGuid,suppliedGuid) || !Read(handler,profile.layout.handlerGuid,savedGuid) ||
        suppliedGuid!=savedGuid || suppliedGuid==std::numeric_limits<std::uint64_t>::max())return fail(NativeRouteFailure::Guid);

    // 16AF460/16AADF0 use the adjusted-network+160..168 vector. With the
    // proven raw+32 secondary receiver it is raw+192..200, capacity at208.
    // This function does not acquire a mutex or make an unsafe caller safe.
    std::uintptr_t first{},last{},capacity{};
    if(!Read(network,profile.layout.networkFirst,first) || !Read(network,profile.layout.networkLast,last) || !Read(network,profile.layout.networkCapacity,capacity) ||
        !first || first%8 || last<=first || last>capacity ||
        (last-first)%8 || (capacity-first)%8 || (last-first)/8>64)return fail(NativeRouteFailure::Vector);
    std::uintptr_t match{},activePeer{};
    for(auto position=first;position<last;position+=8){
        void const* connection{};std::uint32_t type{};std::uint64_t guid{};
        if(!Read(reinterpret_cast<void const*>(position),0,connection) ||
            !Read(connection,profile.layout.connectionType,type))return fail(NativeRouteFailure::Entry);
        if(type!=0)continue;
        if(!Read(connection,profile.layout.connectionGuid,guid))return fail(NativeRouteFailure::Entry);
        if(guid!=suppliedGuid)continue;
        // Reject ambiguity even if it repeats the same allocation, and never
        // treat a retiring/missing peer as a still-live route.
        std::uint32_t state{};std::uint8_t removed{},closing{};
        if(match)return fail(NativeRouteFailure::Ambiguous);
        if(!Read(connection,profile.layout.connectionState,state))return fail(NativeRouteFailure::State);
        if(diagnostic)diagnostic->state=state;
        // D92690 writes0 at D92909, or1 at D92921 when its R9B flag is set.
        // This is not a connected/alive boolean: D935A0 and16AADF0 receive
        // and dispatch valid packets in either branch. Keep the known domain
        // check; actual lifetime authority still requires the exact native
        // birth/binding/StartGame/manager chain and the flags below.
        if(state>1)return fail(NativeRouteFailure::State);
        if(!Read(connection,profile.layout.connectionRemoved,removed) || removed!=0)return fail(NativeRouteFailure::Removed);
        if(!Read(connection,profile.layout.connectionClosing,closing) || closing!=0)return fail(NativeRouteFailure::Closing);
        if(!Read(connection,profile.layout.connectionPeer,activePeer) || !activePeer)return fail(NativeRouteFailure::Peer);
        match=reinterpret_cast<std::uintptr_t>(connection);
    }
    // The owning 16AADF0 typed dispatch passes this vector allocation, not a
    // detached identifier copy. GUID equality cannot replace that boundary.
    if(!match || match!=reinterpret_cast<std::uintptr_t>(packetIdentifier))return fail(NativeRouteFailure::Allocation);
    if(diagnostic)diagnostic->failure=NativeRouteFailure::None;
    return NativePrimaryGuidRoute{*binding,reinterpret_cast<std::uintptr_t>(handler),
        match,activePeer,suppliedGuid};
}
std::optional<NativeNetworkBinding> ReadPrimaryNotificationBinding(
    NativeProfile const& profile,std::uintptr_t base,void const* handler,void const* identifier) noexcept {
    if(!base || base>std::numeric_limits<std::uintptr_t>::max()-profile.imageExtent ||
       !Table(handler,0,base+profile.Rva(0xE7AF110)))return {};
    void const* client{};if(!Read(handler,profile.layout.handlerClient,client))return {};
    auto binding=ReadNetworkBinding(profile,base,client);if(!binding)return {};
    auto network=reinterpret_cast<void const*>(binding->network);
    void const *registration{},*registered{};std::uint8_t established{};
    std::uint32_t suppliedType{},savedType{};std::uint64_t supplied{},saved{};
    if(!Table(network,profile.layout.networkSecondary,base+profile.Rva(0xE75E2F0)) || !Read(network,profile.layout.networkRegistration,registration) ||
       !Read(registration,0,registered) || registered!=handler ||
       !Read(handler,profile.layout.handlerEstablished,established) || established!=1 ||
       !Read(identifier,profile.layout.connectionType,suppliedType) || suppliedType!=0 ||
       !Read(handler,profile.layout.handlerType,savedType) || savedType!=0 ||
       !Read(identifier,profile.layout.connectionGuid,supplied) || !Read(handler,profile.layout.handlerGuid,saved) || supplied!=saved ||
       supplied==std::numeric_limits<std::uint64_t>::max())return {};
    return binding;
}
}
