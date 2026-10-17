#pragma once
#include <cstdint>
#include "NativeProfile.h"
#include <optional>
namespace autorestock {
struct NativeNetworkBinding {
    std::uintptr_t client{},game{},network{};
    bool operator==(NativeNetworkBinding const&) const = default;
};
// Calling-thread read-only identity snapshot. These integers are not ownership
// handles and must never be turned back into pointers after this callback.
// Connection kind, negotiated mode and lifecycle generation require separate
// original observations; readable identities alone enable no backend.
std::optional<NativeNetworkBinding> ReadNetworkBinding(
    NativeProfile const&,std::uintptr_t imageBase,void const* client) noexcept;
struct NativePrimaryGuidRoute {
    NativeNetworkBinding binding;
    std::uintptr_t handler{},connection{},activePeer{};
    std::uint64_t guid{};
    bool operator==(NativePrimaryGuidRoute const&) const = default;
};
// Diagnostic values only: no addresses, GUIDs, endpoints or borrowed objects.
enum class NativeRouteFailure {
    Unobserved, None, Handler, NetworkBinding, Registration, Established,
    SuppliedType, SavedType, Guid, Vector, Entry, Ambiguous, State, Removed,
    Closing, Peer, Allocation
};
struct NativeRouteDiagnostic {
    NativeRouteFailure failure=NativeRouteFailure::Unobserved;
    std::int64_t state=-1;
};
// Original owning packet callback only, on its proven owner thread and within
// the native dispatch lifetime/lock. No cross-thread or post-callback reads.
// packetIdentifier must be the exact connection allocation passed by the
// owning typed dispatcher; detached identifier/connection copies are rejected.
// Only primary registration + type0 GUID is admitted. This is NOT a connect
// attempt ticket, a generation proof, a remote classifier or a backend selector.
std::optional<NativePrimaryGuidRoute> ReadPrimaryGuidRoute(
    NativeProfile const&,std::uintptr_t imageBase,void const* handler,void const* packetIdentifier,
    NativeRouteDiagnostic* diagnostic=nullptr) noexcept;
// After the original primary connection notification, within that owning
// notification scope. This checks registration/copy only, not insertion.
std::optional<NativeNetworkBinding> ReadPrimaryNotificationBinding(
    NativeProfile const&,std::uintptr_t imageBase,void const* handler,void const* identifier) noexcept;
// Read-only existing singleton; currentNativeId comes from the guarded
// original _Thrd_id thunk. This never calls the allocating scheduler getter.
bool OnNativeMainThread(NativeProfile const&,std::uintptr_t imageBase,std::uint32_t currentNativeId) noexcept;
}
