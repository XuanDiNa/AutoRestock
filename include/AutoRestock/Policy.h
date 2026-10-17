#pragma once
#include "Config.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace autorestock {
// Opaque identities are supplied by a native adapter after complete comparison;
// item numeric IDs, display names alone, and lossy hashes are not identities.
struct Item {
    std::string identity;
    std::string withoutDurability;
    int count{};
    int capacity{};
    bool damageable{};
    bool Empty() const noexcept { return count == 0; }
};
enum class Completion { Waiting, Accepted, Rejected, Predicted };
struct UseConsumption { std::uint64_t requestId{}; int count{}; };
struct Snapshot {
    std::uint64_t now{}, player{}, world{}, useSequence{};
    int selected{};
    bool eligible{};
    bool transferReady=true; // false allows pending reconciliation, never a new transfer
    bool intentionalRemoval{};
    std::array<Item, 36> items;
    // Set only by an observed native world-use depletion/break operation.
    // This is not inferred from an empty destination or a held mouse button.
    std::optional<Item> depletedByUse;
    // Ordinary-only, one-shot identity of an actually observed world-use
    // depletion retained through later empty incoming applications. This is
    // refill intent, NOT consumption accounting or server acceptance.
    std::optional<Item> emptyFallback;
    Completion completion = Completion::Waiting;
    std::uint64_t completionId{};
    // Supplied only by a continuous original world-use callback chain anchored
    // to this request's predicted post-transfer stack. Never a count tolerance.
    std::optional<UseConsumption> useConsumption;
};
struct Transfer {
    int source{}, destination{}, amount{};
    std::uint64_t requestId{};
    bool operator==(Transfer const&) const = default;
};
bool ExactTransferProgress(Transfer const&,Item const& beforeSource,Item const& beforeDestination,
    Item const& source,Item const& destination,std::optional<UseConsumption> const&) noexcept;
class Policy {
public:
    static constexpr std::uint64_t timeoutMs = 1500;
    std::optional<Transfer> Tick(Snapshot const& sample, Config const& config);
    void Defer(std::uint64_t requestId) noexcept;
    void Reset() noexcept;
private:
    struct Pending { Transfer transfer; Item source, destination; std::uint64_t since{}; bool accepted{}; };
    std::uint64_t player_{}, world_{}, sequence_{};
    std::uint64_t nextRequestId_ = 1;
    int selected_ = -1;
    bool filling_{}, inhibited_{};
    std::optional<Item> reference_;
    std::optional<Pending> pending_;
};
}
