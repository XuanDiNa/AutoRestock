#pragma once
#include "Policy.h"
namespace autorestock {
// Value-only native variant diagnostics. Never use these to authorize or repair
// inventory progress; unknown alternatives do not fabricate an ID.
struct NativeUseStamp {int variant=-1;std::int32_t id{};bool idKnown{};};
struct RecentUse {
    bool recorded{};
    std::uint64_t beginAt{},endAt{},requestId{};
    int invoker{},beforeCount=-1,afterCount=-1;
    NativeUseStamp beforeNative,afterNative;
    bool requestMatches{},beforeMatches{},resultValid{};
};
struct UseContext {
    std::uint64_t player{},world{},inventory{};
    int selected{};
    bool operator==(UseContext const&) const=default;
};
// Copied diagnostics only. Counts and comparison outcomes never retain native
// objects/identities, and inspecting them cannot advance or repair the proof.
struct UseProgressEvidence {
    bool armed{},chainValid{};
    int expectedSourceCount=-1,expectedDestinationCount=-1,consumed{};
    bool sourceCountMatches{},sourceItemMatches{},destinationCountMatches{},destinationItemMatches{};
    std::uint64_t observedUses{};
    int lastBeforeCount=-1,lastAfterCount=-1,lastExpectedBeforeCount=-1;
    bool lastRequestMatches{},lastBeforeMatches{},lastResultValid{};
    std::array<RecentUse,4> recentUses{};
    unsigned recentUseCount{};
};
// One owner-thread request, bounded to at most64 items. No native pointers or
// fabricated snapshots. Any unexplained chain break is irreversible until reset.
class PendingUseProgress {
public:
    void Arm(Transfer const&,Item const& source,Item const& destination);
    void Observe(std::uint64_t requestId,Item const& before,Item const& after,RecentUse const& trace={});
    std::optional<UseConsumption> Sample(Item const& source,Item const& destination);
    UseProgressEvidence Inspect(Item const& source,Item const& destination) const;
    std::uint64_t RequestId() const noexcept{return transfer_.requestId;}
    void Reset() noexcept;
private:
    Transfer transfer_{};
    Item source_,destination_,last_;
    int consumed_{};
    bool valid_{};
    UseProgressEvidence evidence_{}; // last callback only; sampled fields are derived by Inspect
};
// Game-thread values only. Capture before/after a proven native world-use
// transaction, never an arbitrary empty-hand observation or mouse state.
class UseTracker {
public:
    void Observe(UseContext const& beforeContext,Item const& before,
        UseContext const& afterContext,Item const& after);
    void Apply(UseContext const&,Snapshot& snapshot);
    void Reset() noexcept;
private:
    UseContext context_{};
    std::optional<Item> depleted_;
    std::optional<Item> lastUseResult_;
    std::optional<Item> previous_;
    std::uint64_t sequence_{};
};
}
