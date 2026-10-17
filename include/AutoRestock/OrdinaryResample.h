#pragma once
#include "Diagnostic.h"
#include <limits>
namespace autorestock {
// Exact IDA-proven incoming container0 application, NOT a transaction result.
// Profile guards cover both the handler entry and this indirect-call site.
inline bool IncomingInventorySlot(NativeWrite const& w,std::uint32_t expectedCaller) noexcept {
    auto readable=[](WriteStackState const& s){return s.readable && s.count>=0 && s.count<=64 && s.native.idKnown;};
    return w.callerKnown && expectedCaller && w.callerRva==expectedCaller && !w.nativeDepth && !w.network &&
        readable(w.before) && readable(w.input) && readable(w.after);
}
enum class ResampleResult { Waiting,Baseline,Ready,Invalid,Expired };
// An independent LOCAL planning boundary. It never accepts/completes the old
// request. No native objects, network IDs, retries or elapsed-time acceptance.
class OrdinaryResample {
public:
    static constexpr std::uint64_t windowMs=10000;
    bool Active() const noexcept{return active_;}
    std::uint64_t Revision() const noexcept{return active_?revision_:0;}
    unsigned Coverage() const noexcept{return coverage_;}
    std::uint64_t StartedAt() const noexcept{return started_;}
    std::uint64_t SyncCount() const noexcept{return syncCount_;}
    bool Healthy(std::uint64_t now) const noexcept{return !invalid_ && now>=lastSync_ && now>=lastObserved_ && now>=started_;}
    bool Expired(std::uint64_t now) const noexcept{return now>=started_ && now-started_>=windowMs;}
    void Start(int source,int destination,std::uint64_t now,bool requireFreshUse=true) noexcept {
        Reset();active_=true;requireFreshUse_=requireFreshUse;
        source_=source;destination_=destination;started_=lastSync_=now;
        if(revision_==std::numeric_limits<std::uint64_t>::max())invalid_=true;else ++revision_;
    }
    void Incoming(int slot,std::uint64_t now) noexcept {
        if(!active_)return;
        if(now<lastSync_ || now<lastObserved_ || revision_==std::numeric_limits<std::uint64_t>::max()){invalid_=true;return;}
        ++revision_;if(syncCount_!=std::numeric_limits<std::uint64_t>::max())++syncCount_;
        lastSync_=now;coverage_|=slot==source_?1u:slot==destination_?2u:0u;
        baseline_=false;freshUse_=false;expectedMain_={};depleted_.reset();
        // A proven new incoming application starts a new local baseline; it
        // does not repair an already invalid resampling chain.
    }
    void Use(std::uint64_t revision,Item const& before,Item const& after) {
        if(!active_ || !revision || revision!=revision_ || invalid_)return;
        if((freshUse_ || baseline_) && !Same(before,expectedMain_)){invalid_=true;return;}
        if(after.count<0 || after.count>before.count || (!after.Empty() && !SameItem(before,after))){invalid_=true;return;}
        if(before.count>after.count && !before.Empty()){
            freshUse_=true;expectedMain_=after;
            if(after.Empty())depleted_=before;else depleted_.reset();
        }
    }
    ResampleResult Sample(Snapshot& s,std::uint64_t submitted) {
        if(!active_ || !Healthy(s.now) || s.now<submitted)return ResampleResult::Invalid;
        lastObserved_=s.now;
        if(Expired(s.now))return ResampleResult::Expired;
        if(s.intentionalRemoval)return ResampleResult::Invalid;
        bool retainedEmpty=s.items[destination_].Empty() && s.emptyFallback.has_value();
        if(s.now-submitted<Policy::timeoutMs ||
            (requireFreshUse_ && (coverage_!=3 || (!freshUse_ && !retainedEmpty))))return ResampleResult::Waiting;
        if((freshUse_ || baseline_) && !Same(s.items[destination_],expectedMain_))return ResampleResult::Invalid;
        if(!baseline_){items_=s.items;expectedMain_=s.items[destination_];baseline_=true;sampledAt_=s.now;return ResampleResult::Baseline;}
        for(int slot=9;slot<=35;++slot)if(!Same(items_[slot],s.items[slot]))return ResampleResult::Invalid;
        if(s.now<=sampledAt_)return ResampleResult::Waiting; // not two samples in one Tick
        if(s.items[destination_].Empty()){
            if(depleted_)s.depletedByUse=depleted_;
            else if(!retainedEmpty)return ResampleResult::Waiting; // never guess an empty-hand item
        }
        return ResampleResult::Ready;
    }
    void Reset() noexcept {
        active_=baseline_=freshUse_=invalid_=false;coverage_=0;started_=lastSync_=sampledAt_=lastObserved_=syncCount_=0;
        source_=destination_=-1;expectedMain_={};depleted_.reset();items_={};
        // revision_ is never reused: old callbacks cannot match a new pause.
    }
private:
    static bool SameItem(Item const& a,Item const& b) noexcept {
        return a.Empty() && b.Empty()?true:a.identity==b.identity && a.capacity==b.capacity && a.damageable==b.damageable;
    }
    static bool Same(Item const& a,Item const& b) noexcept{return a.count==b.count && SameItem(a,b);}
    bool active_{},baseline_{},freshUse_{},invalid_{};
    bool requireFreshUse_=true;
    unsigned coverage_{};
    int source_=-1,destination_=-1;
    std::uint64_t revision_{},started_{},lastSync_{},sampledAt_{},lastObserved_{},syncCount_{};
    Item expectedMain_;
    std::optional<Item> depleted_;
    std::array<Item,36> items_;
};
}
