#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace autorestock {
// Exact IDA-supported x64 ABI. Only resolve these from a verified profile.
// Dependency injection permits ownership tests without loading the game.
struct StackApi {
    void* (*copy)(void*, void const*){};                 // 2482180
    void* (*destroy)(void*, int){};                     // 21B1C0, flags0
    bool (*empty)(void const*){};                       // 2486D00
    std::uint8_t (*capacity)(void const*){};             // 247DCE0
    bool (*merge)(void const*, void const*){};           // 2488D80
    bool (*equal)(void const*, void const*){};           // 248D860 (AL)
    void (*setCount)(void*, std::uint8_t){};             // 2486E40, return unused
    void (*migrateDamage)(void*, void*){};              // 24C7CD0
    void (*setDamage)(void*, void*, std::int16_t){};     // 24C7D80
    bool Complete() const noexcept;
};
struct alignas(8) StackStorage { std::array<std::byte,152> bytes; };
static_assert(sizeof(StackStorage)==152 && alignof(StackStorage)==8);

// Storage is plugin-owned and stable across moves. Its contents are exclusively
// constructed, changed, and destroyed by native operations; never C++ delete
// on a game allocation or memcpy of an owning native stack.
class OwnedStack {
public:
    OwnedStack() = default;
    OwnedStack(OwnedStack const&) = delete;
    OwnedStack& operator=(OwnedStack const&) = delete;
    OwnedStack(OwnedStack&& other) noexcept;
    OwnedStack& operator=(OwnedStack&& other) noexcept;
    ~OwnedStack();
    bool Copy(StackApi const& api, void const* borrowed);
    bool SetCount(int count);
    bool NormalizeDamage();
    void Reset() noexcept;
    void const* Get() const noexcept { return storage_.get(); }
    int Count() const noexcept;
private:
    StackApi api_{};
    std::unique_ptr<StackStorage> storage_;
};

// Excludes counts and network IDs. Requires exact aux/block plus symmetric
// native full comparisons to close native wildcard/asymmetric cases. Capacity1
// uses exact Item target/full equality, not native canMerge (always false).
bool SameNativeItem(StackApi const& api, void const* a, void const* b);
// The caller must establish native damageability before choosing this path.
// Normalization is on two full owned clones only; live stacks stay untouched.
bool SameNativeToolIgnoringDamage(StackApi const& api, void const* a, void const* b);
}
