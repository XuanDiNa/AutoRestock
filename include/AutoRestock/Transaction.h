#pragma once
#include "Native.h"
#include "ResponseTracker.h"
#include "Integrated.h"
namespace autorestock {
enum class TransferResult { Deferred, Rejected, Queued, Fault };
TransferResult ExecuteOrdinaryTransfer(NativeApi const&,std::uintptr_t imageBase,
    void* manager,void* player,Transfer const&,void const* expectedSource,
    void const* expectedDestination);
// Expected stacks are full owned snapshots from this same game-thread callback.
// Fault means native mutation started but could not be verified: disable the
// plugin, leave native reconciliation intact, and never retry it automatically.
TransferResult ExecuteTransfer(NativeApi const&,std::uintptr_t imageBase,
    void* manager,void* player,Transfer const&,void const* expectedSource,
    void const* expectedDestination,ResponseTracker&);
TransferResult ExecuteIntegratedTransfer(NativeApi const&,std::uintptr_t imageBase,
    void* manager,void* player,Transfer const&,void const* expectedSource,
    void const* expectedDestination,IntegratedBridge&,ActorIdentity const&,std::uint64_t now);
}
