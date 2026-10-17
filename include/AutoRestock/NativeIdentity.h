#pragma once
#include "NativeStack.h"
#include "Policy.h"
#include <vector>
namespace autorestock {
// Tokens identify classes established by complete native comparisons. They are
// not item names, network IDs, hashes or numeric Minecraft item IDs.
class NativeIdentity {
public:
    std::optional<Item> Identify(StackApi const&,void const* stack,bool nativeDamageable);
    void Reset() noexcept;
private:
    struct Entry {OwnedStack stack;std::string token;int capacity{};bool normalized{};};
    std::optional<std::string> Intern(StackApi const&,void const*,int capacity,bool normalized);
    std::vector<Entry> entries_;
    std::uint64_t next_=1;
};
}
