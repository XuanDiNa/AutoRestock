#include "AutoRestock/ResponseTracker.h"
#include <cstring>
namespace autorestock {
bool ResponseTracker::Begin(std::uintptr_t manager, std::int32_t nativeId, std::uint64_t policyId) noexcept {
    if (policyId_ || !manager || !policyId || nativeId > -3 || (nativeId & 1)==0) return false;
    manager_=manager; nativeId_=nativeId; policyId_=policyId;
    return true;
}
std::optional<RequestCompletion> ResponseTracker::Decode(std::uintptr_t manager,
    std::span<std::byte const> entries) const noexcept {
    constexpr std::size_t stride=48;
    if (!policyId_ || manager!=manager_ || entries.empty() || entries.size()%stride ||
        entries.size()/stride>4096) return {};
    std::optional<RequestCompletion> result;
    for (std::size_t offset=0; offset<entries.size(); offset+=stride) {
        std::int32_t id{};
        std::memcpy(&id,entries.data()+offset+16,sizeof id);
        if (id!=nativeId_) continue;
        auto status=std::to_integer<int>(entries[offset]);bool accepted=status==0;
        if (!result) result=RequestCompletion{policyId_,accepted?Completion::Accepted:Completion::Rejected,nativeId_,status};
        else if (!accepted && result->result!=Completion::Rejected){result->result=Completion::Rejected;result->nativeStatus=status;}
    }
    return result;
}
void ResponseTracker::Reset() noexcept { manager_=0; nativeId_=0; policyId_=0; }
}
