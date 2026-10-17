#pragma once
#include "Policy.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace autorestock {
struct RequestCompletion {
    std::uint64_t requestId{};
    Completion result = Completion::Waiting;
    std::int32_t nativeId{};
    int nativeStatus=-1;
};
// Native response entries are borrowed only during 25B23F0. Decode before the
// original handler; deliver the value only after the original has returned.
class ResponseTracker {
public:
    bool Begin(std::uintptr_t manager, std::int32_t nativeId, std::uint64_t policyId) noexcept;
    std::optional<RequestCompletion> Decode(std::uintptr_t manager,
        std::span<std::byte const> entries) const noexcept;
    void Reset() noexcept;
    bool Pending() const noexcept { return policyId_ != 0; }
    std::int32_t PendingNativeId() const noexcept { return nativeId_; }
private:
    std::uintptr_t manager_{};
    std::int32_t nativeId_{};
    std::uint64_t policyId_{};
};
}
