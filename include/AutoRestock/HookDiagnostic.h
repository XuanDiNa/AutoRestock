#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace autorestock {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
enum class HookCounter : unsigned {
    OperationEntry,NotReady,ForeignOwner,Nested,InventoryMatch,KnownPlayer,ManagerMatch,RemoteFault,
    BeginAttempt,ConfigDisabled,ForegroundFalse,CursorVisible,Captured,NotCaptured,OriginalReturned,EndCalled,
    ServerEntry,ServerActive,ServerIdentityValid,ServerRoleLocal,ServerRoleServer,ServerCounts,
    ServerLocalStored,ServerLocalFresh,ServerRegistryDistinct,ServerUniqueMatch,ServerRuntimeMatch,
    DestroyEntry,DestroyLocal,DestroyCaptured,DestroyEnd,DestroyServer,DestroyServerCounts,
    HurtEntry,HurtLocal,HurtCaptured,HurtBreak,HurtServer,Count
};
struct HookSnapshot {
    std::array<std::uint64_t,static_cast<unsigned>(HookCounter::Count)> values{};
    std::uint64_t operator[](HookCounter c) const noexcept{return values[static_cast<unsigned>(c)];}
    bool operator==(HookSnapshot const&) const = default;
};
// Process-lifetime, multi-writer scalar counters only. Never clear on a world/UI
// reset: a callback excluded BEFORE Runtime must remain visible to the worker.
// Snapshots may straddle callbacks; totals are diagnostics, never authority.
class HookDiagnostics {
public:
    void Hit(HookCounter c) noexcept{values_[static_cast<unsigned>(c)].fetch_add(1,std::memory_order_relaxed);}
    HookSnapshot Snapshot() const noexcept {
        HookSnapshot result;
        for(unsigned n=0;n<result.values.size();++n)result.values[n]=values_[n].load(std::memory_order_relaxed);
        return result;
    }
private:
    std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(HookCounter::Count)> values_{};
};
class HookDiagnosticReporter {
public:
    bool Poll(HookSnapshot const& current,std::uint64_t now) noexcept {
        if(started_ && (current==previous_ || (now>=last_ && now-last_<2000)))return false;
        previous_=current;last_=now;started_=true;return true;
    }
private:
    HookSnapshot previous_;std::uint64_t last_{};bool started_{};
};
}
