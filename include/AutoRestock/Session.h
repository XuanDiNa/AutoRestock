#pragma once
#include <cstdint>
namespace autorestock {
enum class ConnectionKind { Unknown, Integrated, Remote };
enum class InventoryMode { Unknown, Modern, Ordinary };
enum class Backend { Unsupported, Integrated, RemoteModern, RemoteOrdinary };
struct SessionKey {
    std::uint64_t connection{},world{},player{},manager{};
    bool operator==(SessionKey const&) const = default;
};
struct SessionSelection {
    std::uint64_t generation{};
    SessionKey key;
    InventoryMode mode{};
    Backend backend{};
};
// Value-only, owner-thread state. Native observers must prove thread/lifetime
// contracts before calling this class; it performs no native reads or writes.
// This foundation is deliberately not wired into Plugin/Runtime yet.
class SessionSelector {
public:
    std::uint64_t Connect(std::uint64_t connection,ConnectionKind) noexcept;
    bool StartGame(std::uint64_t ticket,SessionKey,InventoryMode) noexcept;
    void Retire(std::uint64_t manager) noexcept;
    void Invalidate() noexcept;
    SessionSelection Select(SessionKey const&,InventoryMode currentMode,
        bool integratedReady,bool modernContractReady) const noexcept;
private:
    bool Advance() noexcept;
    void Clear() noexcept;
    std::uint64_t generation_{},ticket_{},connection_{};
    ConnectionKind kind_{};
    InventoryMode mode_{};
    SessionKey key_{};
    bool started_{},exhausted_{};
};
}
