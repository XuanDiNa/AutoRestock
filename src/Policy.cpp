#include "AutoRestock/Policy.h"
#include <algorithm>
namespace autorestock {
namespace {
bool Valid(Item const& i) {
    return i.count == 0 || (i.count > 0 && i.capacity > 0 && i.capacity <= 64 &&
        i.count <= i.capacity && !i.identity.empty() && (!i.damageable || !i.withoutDurability.empty()));
}
bool Same(Item const& a, Item const& b) { return a.identity == b.identity && a.capacity == b.capacity; }
bool Matches(Item const& a, Item const& reference) {
    return a.damageable == reference.damageable && a.capacity == reference.capacity &&
        (reference.damageable ? a.withoutDurability == reference.withoutDurability : Same(a,reference));
}
}
bool ExactTransferProgress(Transfer const& transfer,Item const& beforeSource,Item const& beforeDestination,
    Item const& source,Item const& destination,std::optional<UseConsumption> const& consumption) noexcept {
    int used=0;
    if(consumption){
        if(consumption->requestId!=transfer.requestId || consumption->count<0 ||
            consumption->count>beforeDestination.count+transfer.amount)return false;
        used=consumption->count;
    }
    int remaining=beforeDestination.count+transfer.amount-used;
    return Valid(source) && Valid(destination) && source.count==beforeSource.count-transfer.amount &&
        (source.Empty() || (Same(source,beforeSource) && source.damageable==beforeSource.damageable)) &&
        destination.count==remaining && (destination.Empty() ||
            (Same(destination,beforeSource) && destination.damageable==beforeSource.damageable));
}
void Policy::Reset() noexcept {
    player_ = world_ = sequence_ = 0;
    selected_ = -1;
    filling_ = inhibited_ = false;
    reference_.reset(); pending_.reset();
}
void Policy::Defer(std::uint64_t id) noexcept {
    if(pending_ && !pending_->accepted && pending_->transfer.requestId==id)pending_.reset();
}
std::optional<Transfer> Policy::Tick(Snapshot const& s, Config const& config) {
    bool freshUse = s.useSequence > sequence_;
    if (!config.enabled || !s.eligible || !s.player || !s.world || s.selected < 0 || s.selected > 8 ||
        config.preRestockThreshold < 1 || config.preRestockThreshold > 64) {
        Reset(); sequence_ = s.useSequence; return {};
    }
    if (s.player != player_ || s.world != world_ || s.selected != selected_) {
        Reset(); player_ = s.player; world_ = s.world; selected_ = s.selected;
    }
    sequence_ = s.useSequence;
    auto cancel = [&] { reference_.reset(); pending_.reset(); filling_=false; inhibited_=true; };
    if (s.intentionalRemoval) { cancel(); return {}; }
    auto const& destination = s.items[s.selected];
    if (!Valid(destination)) { cancel(); return {}; }
    if (pending_) {
        auto& pending = *pending_;
        auto const& currentSource = s.items[pending.transfer.source];
        bool completionMatches = s.completionId == pending.transfer.requestId;
        bool predicted = completionMatches && s.completion==Completion::Predicted &&
            s.now>=pending.since && s.now-pending.since>=timeoutMs;
        if ((completionMatches && s.completion == Completion::Rejected) || s.now < pending.since ||
            (s.now - pending.since >= timeoutMs && !predicted) || !Valid(currentSource)) { cancel(); return {}; }
        if (completionMatches && s.completion == Completion::Accepted) pending.accepted = true;
        bool progress = ExactTransferProgress(pending.transfer,pending.source,pending.destination,
            currentSource,destination,s.useConsumption);
        bool unchanged = destination.count == pending.destination.count &&
            (destination.Empty() || Same(destination,pending.destination)) &&
            currentSource.count == pending.source.count && Same(currentSource,pending.source);
        if (!progress && !unchanged) { cancel(); return {}; }
        if (!progress || (!pending.accepted && !predicted)) return {};
        pending_.reset();
    }
    if(!s.transferReady)return {};
    if (freshUse) inhibited_ = false;
    if (inhibited_) return {};
    if (destination.Empty()) {
        auto const& depletion=freshUse && s.depletedByUse?s.depletedByUse:s.emptyFallback;
        if (depletion && Valid(*depletion) && !depletion->Empty()) {
            reference_ = *depletion; filling_ = true;
        }
        if (!filling_ || !reference_) return {};
    } else {
        if (filling_ && reference_ && !Matches(destination,*reference_)) { cancel(); return {}; }
        if (!filling_) {
            if (destination.damageable || destination.count >= std::min(config.preRestockThreshold,destination.capacity))
                return {};
            reference_ = destination; filling_ = true;
        }
    }
    if (destination.count >= reference_->capacity) { filling_=false; reference_.reset(); return {}; }
    for (int source=9; source<=35; ++source) {
        auto const& item = s.items[source];
        if (!Valid(item) || item.Empty() || !Matches(item,*reference_)) continue;
        if (!destination.Empty() && !Same(item,destination)) continue;
        if (nextRequestId_ == 0) { cancel(); return {}; }
        Transfer transfer{source,s.selected,std::min(item.count,reference_->capacity-destination.count),nextRequestId_++};
        pending_ = Pending{transfer,item,destination,s.now,false};
        return transfer;
    }
    // An empty-hand world-use reference is one-shot, not an indefinite promise
    // to pull a subsequently picked-up or manually stored item into this slot.
    filling_=false; reference_.reset();
    return {};
}
}
