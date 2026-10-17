#pragma once
#include "NativeProfile.h"
#include "ResponseTracker.h"
#include <array>
#include <atomic>
namespace autorestock {
enum class ActorRole : std::uint64_t { None, Local, Server };
struct ActorIdentity {
    std::uint64_t registry{},unique=UINT64_MAX,runtime{};
    ActorRole role{};
    bool Valid() const noexcept {return registry && unique!=UINT64_MAX && runtime && role!=ActorRole::None;}
    bool operator==(ActorIdentity const&) const = default;
};
// Reads only the calling thread's borrowed Actor and generation-sensitive ECS.
ActorIdentity ReadActorIdentity(void const* actor) noexcept;
template<std::size_t N> class AtomicWords {
public:
    bool Store(std::array<std::uint64_t,N> const&) noexcept;
    bool Load(std::array<std::uint64_t,N>&) const noexcept;
private:
    std::atomic<std::uint64_t> sequence_{};
    std::array<std::atomic<std::uint64_t>,N> data_{};
};
struct ServerTicket {std::uint64_t token{},policyId{};std::int32_t nativeId{};};
using InventoryCounts=std::array<std::uint8_t,36>;
// Calling-thread-only, read-only original Inventory layout. No native calls,
// borrowed pointers or game-owned stack objects leave this function.
std::optional<InventoryCounts> ReadInventoryCounts(NativeProfile const&,void const* player,std::uintptr_t imageBase) noexcept;
class IntegratedBridge {
public:
    void Local(ActorIdentity const&,std::uint64_t now) noexcept;
    void Invalidate() noexcept;
    bool Ready(ActorIdentity const&,std::uint64_t now) const noexcept;
    bool Arm(ActorIdentity const&,std::uint64_t now,std::int32_t nativeId,std::uint64_t policyId) noexcept;
    // Begin/finish surround the ORIGINAL packet handler; only copied values cross threads.
    ServerTicket BeginServer(ActorIdentity const&,std::int32_t nativeId,std::uint64_t now) const noexcept;
    void EndServer(ActorIdentity const&,ServerTicket,int nativeStatus,std::uint64_t now,
        std::optional<InventoryCounts> const& counts={}) noexcept;
    std::optional<InventoryCounts> AuthoritativeCounts(ActorIdentity const&,std::uint64_t now) const noexcept;
    std::optional<RequestCompletion> Poll() const noexcept;
    void Clear() noexcept;
    bool Pending() const noexcept;
    std::int32_t PendingNativeId() const noexcept;
private:
    AtomicWords<5> local_,seen_;
    AtomicWords<10> counts_; // paired identity/epoch/time plus 36 packed count bytes
    AtomicWords<7> armed_;
    std::atomic<std::uint64_t> result_{}; // token32 | native result32; all-ones result means waiting
    std::uint64_t nextToken_{},identityEpoch_{}; // owner thread only
};
}
