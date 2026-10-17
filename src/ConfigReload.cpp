#include "AutoRestock/ConfigReload.h"
namespace autorestock {
ReloadResult ConfigReload::Poll(std::uint64_t now) {
    if (polled_ && now>=lastPoll_ && now-lastPoll_<500) return ReloadResult::NotDue;
    polled_=true; lastPoll_=now;
    Config next=value_;
    if (!LoadConfig(path_,next,error_)) return ReloadResult::Rejected;
    if (next==value_) return ReloadResult::Unchanged;
    value_=next;
    return ReloadResult::Applied;
}
}
