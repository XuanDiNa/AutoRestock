#include "AutoRestock/UseTracker.h"
namespace autorestock {
namespace {
bool Valid(UseContext const& c){return c.player && c.world && c.inventory && c.selected>=0 && c.selected<=8;}
bool Same(Item const& a,Item const& b){
    return a.Empty() && b.Empty() ? true : a.identity==b.identity && a.capacity==b.capacity && a.damageable==b.damageable;
}
bool SameResult(Item const& a,Item const& b){return a.count==b.count && Same(a,b);}
}
void PendingUseProgress::Arm(Transfer const& transfer,Item const& source,Item const& destination){
    transfer_=transfer;source_=source;destination_=destination;
    last_=source;last_.count=destination.count+transfer.amount;consumed_=0;evidence_={};
    valid_=transfer.requestId && transfer.amount>0 && transfer.amount<=source.count &&
        last_.count>0 && last_.count<=source.capacity && source.capacity<=64 && !source.identity.empty() &&
        (destination.Empty() || Same(source,destination));
}
void PendingUseProgress::Observe(std::uint64_t id,Item const& before,Item const& after,RecentUse const& trace){
    if(!valid_ || !transfer_.requestId)return;
    if(evidence_.observedUses!=UINT64_MAX)++evidence_.observedUses;
    evidence_.lastBeforeCount=before.count;evidence_.lastAfterCount=after.count;
    evidence_.lastExpectedBeforeCount=last_.count;
    evidence_.lastRequestMatches=id==transfer_.requestId;
    evidence_.lastBeforeMatches=SameResult(before,last_);
    evidence_.lastResultValid=after.count>=0 && after.count<=before.count && (after.Empty() || Same(before,after));
    if(trace.recorded){
        if(evidence_.recentUseCount==evidence_.recentUses.size()){
            for(unsigned n=1;n<evidence_.recentUseCount;++n)evidence_.recentUses[n-1]=evidence_.recentUses[n];
            --evidence_.recentUseCount;
        }
        auto& entry=evidence_.recentUses[evidence_.recentUseCount++];entry=trace;
        entry.requestId=id;entry.beforeCount=before.count;entry.afterCount=after.count;
        entry.requestMatches=evidence_.lastRequestMatches;entry.beforeMatches=evidence_.lastBeforeMatches;
        entry.resultValid=evidence_.lastResultValid;
    }
    if(!evidence_.lastRequestMatches || !evidence_.lastBeforeMatches || !evidence_.lastResultValid){valid_=false;return;}
    consumed_+=before.count-after.count;last_=after;
}
std::optional<UseConsumption> PendingUseProgress::Sample(Item const& source,Item const& destination){
    if(!valid_ || !transfer_.requestId)return {};
    UseConsumption proof{transfer_.requestId,consumed_};
    if(SameResult(destination,last_) && ExactTransferProgress(transfer_,source_,destination_,source,destination,proof))return proof;
    // A completely unchanged prediction is still waiting, not consumption.
    if(!consumed_ && SameResult(source,source_) && SameResult(destination,destination_))return {};
    valid_=false;return {};
}
UseProgressEvidence PendingUseProgress::Inspect(Item const& source,Item const& destination) const{
    auto result=evidence_;result.armed=transfer_.requestId!=0;result.chainValid=valid_;result.consumed=consumed_;
    if(!result.armed)return result;
    result.expectedSourceCount=source_.count-transfer_.amount;result.expectedDestinationCount=last_.count;
    result.sourceCountMatches=source.count==result.expectedSourceCount;
    result.sourceItemMatches=source.Empty() || Same(source,source_);
    result.destinationCountMatches=destination.count==last_.count;
    result.destinationItemMatches=Same(destination,last_);
    return result;
}
void PendingUseProgress::Reset() noexcept{transfer_={};source_={};destination_={};last_={};consumed_=0;valid_=false;evidence_={};}
void UseTracker::Observe(UseContext const& beforeContext,Item const& before,UseContext const& afterContext,Item const& after){
    if(!Valid(beforeContext) || beforeContext!=afterContext){Reset();return;}
    if(context_!=beforeContext){Reset();context_=beforeContext;}
    if(before.Empty() || before.identity.empty() || before.count<=after.count || (!after.Empty() && !Same(before,after))) return;
    if(sequence_!=UINT64_MAX) ++sequence_;
    lastUseResult_=after;
    if(after.Empty()) depleted_=before;
    else depleted_.reset();
}
void UseTracker::Apply(UseContext const& context,Snapshot& sample){
    sample.depletedByUse.reset();sample.intentionalRemoval=false;
    if(!Valid(context) || !sample.eligible || sample.selected!=context.selected){Reset();sample.useSequence=sequence_;return;}
    if(context_!=context){Reset();context_=context;}
    auto const& current=sample.items[context.selected];
    bool observed=lastUseResult_ && SameResult(*lastUseResult_,current);
    bool removed=previous_ && !previous_->Empty() &&
        (current.count<previous_->count || !Same(current,*previous_));
    sample.intentionalRemoval=removed && !observed;
    if(observed && depleted_ && current.Empty()) sample.depletedByUse=depleted_;
    sample.useSequence=sequence_;
    previous_=current;lastUseResult_.reset();depleted_.reset();
}
void UseTracker::Reset() noexcept{context_={};previous_.reset();lastUseResult_.reset();depleted_.reset();}
}
