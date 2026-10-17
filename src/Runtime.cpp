#include "AutoRestock/Runtime.h"
#include <cstring>
#include <limits>
namespace autorestock {
namespace {
template<class T> T Read(void const* p,std::size_t offset) noexcept {
    auto a=reinterpret_cast<std::uintptr_t>(p);if(!a || a>std::numeric_limits<std::uintptr_t>::max()-offset-sizeof(T)) return {};
    __try{T v;std::memcpy(&v,reinterpret_cast<void const*>(a+offset),sizeof v);return v;}
    __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
UseContext Key(WorldView const& v){return {reinterpret_cast<std::uintptr_t>(v.player),reinterpret_cast<std::uintptr_t>(v.world),reinterpret_cast<std::uintptr_t>(v.inventory),v.selected};}
bool SameVisible(StackApi const& api,void const* a,void const* b){
    if(!a || !b || Read<std::uint8_t>(a,34)!=Read<std::uint8_t>(b,34)) return false;
    return api.empty(a)?api.empty(b):!api.empty(b) && SameNativeItem(api,a,b) && api.capacity(a)==api.capacity(b);
}
NativeUseStamp NativeStamp(void const* stack){
    if(!stack)return {};
    NativeUseStamp stamp;stamp.variant=Read<std::uint8_t>(stack,144);
    stamp.idKnown=stamp.variant>=0 && stamp.variant<=2;
    if(stamp.idKnown)stamp.id=Read<std::int32_t>(stack,stamp.variant==0?128:136);
    return stamp;
}
}
std::optional<TransferResult> Runtime::Tick(void* manager,bool foreground,bool hidden,Config config,std::uint64_t now,std::uint64_t epoch){
    PollStoppedWriters(config,epoch,now);
    if(faulted_ || !native_.profile || native_.profile!=world_.profile) return {};
    if(!backendEnabled_){Reset(ResetCause::BackendDisabled,now);return {};}
    auto player=Read<void*>(manager,native_.profile->layout.managerOwner);
    if(!config.enabled){Reset(ResetCause::ConfigDisabled,now);return {};}
    WorldFailure failure{};
    auto view=ResolveWorld(world_,base_,manager,player,foreground,hidden,Mode(),&failure);
    if(!view){Reset(ResetCause::TickWorldGate,now,failure);return {};}
    Sync(*view,epoch,now);
    if(Ordinary() && OrdinaryBlocked()){Reset();return {};}
    if(Ordinary() && resample_.Active() && (!resample_.Healthy(now) || resample_.Expired(now))){
        StopOrdinary(now,resample_.Expired(now)?4:3);policy_.Reset();uses_.Reset();return {};
    }
    auto identity=integrated_?ReadActorIdentity(player):ActorIdentity{};
    if(integrated_) integrated_->Local(identity,now);
    if(abandoned_ && integrated_)if(auto done=integrated_->Poll()){
        // Cancellation keeps only the old integer fence. Observe its exact
        // result once, without dereferencing inventory from the old context.
        if(!observedNativeStatus_){
            auto event=pendingDiagnostic_;event.kind=DiagnosticKind::NativeResult;event.time=now;
            event.nativeStatus=done->nativeStatus;observedNativeStatus_=done->nativeStatus;diagnostics_.Push(event);
        }
        Publish(*done);
    }
    if(abandoned_ && Pending()){
        if(ordinaryPending_ && now<pendingSince_)return {};
        if(now>=pendingSince_ && now-pendingSince_<Policy::timeoutMs) return {};
        auto event=pendingDiagnostic_;event.kind=DiagnosticKind::Timeout;event.time=now;
        event.nativeStatus=observedNativeStatus_.value_or(-1);diagnostics_.Push(event);
        ClearPending();abandoned_=false;
    }
    // UI/disable/context reset discards policy references, not a native request
    // still in flight. Keep only its integer correlation as a fence until ACK
    // or timeout; do not overlap a new request with the abandoned old context.
    auto inventory=Ordinary()?ResolveOrdinaryInventory(native_,base_,manager,player):ResolveWorldInventory(native_,base_,manager,player);
    if(!inventory) return {}; // no native reads/transfers while the engine owns an active operation
    if(Ordinary() && ordinaryNeedsResample_){
        resample_.Start(pendingDiagnostic_.source,view->selected,now,false);
        ordinaryNeedsResample_=false;
    }
    auto get=[&](int slot){return Ordinary()?native_.legacy.inventoryItem(inventory->inventory,slot):native_.getItem(inventory->wrapper,slot);};
    if(integrated_ && !Pending() && !completion_ && !integrated_->Ready(identity,now))return {};
    auto progressObservation=[&](Snapshot const& sample){
        auto event=pendingDiagnostic_;
        auto src=get(event.source),dst=get(event.destination);
        auto realSrc=world_.inventoryItem(view->inventory,event.source),realDst=world_.inventoryItem(view->inventory,event.destination);
        event.time=now;event.nativeStatus=observedNativeStatus_.value_or(-1);
        event.inventorySampled=src && dst && realSrc && realDst;
        event.wrapperSourceCount=src?Read<std::uint8_t>(src,34):-1;event.wrapperDestinationCount=dst?Read<std::uint8_t>(dst,34):-1;
        event.actualSourceCount=realSrc?Read<std::uint8_t>(realSrc,34):-1;event.actualDestinationCount=realDst?Read<std::uint8_t>(realDst,34):-1;
        event.realMatches=SameVisible(native_.stack,src,realSrc) && SameVisible(native_.stack,dst,realDst);
        event.exactProgress=event.inventorySampled && event.wrapperSourceCount==event.sourceCount-event.amount &&
            event.wrapperDestinationCount==event.destinationCount+event.amount && event.realMatches;
        event.consumptionChainValid=sample.useConsumption.has_value();
        event.provenConsumption=sample.useConsumption?sample.useConsumption->count:0;
        event.accountedProgress=event.consumptionChainValid && event.inventorySampled && event.realMatches &&
            event.wrapperSourceCount==sample.items[event.source].count &&
            event.wrapperDestinationCount==sample.items[event.destination].count && !sample.intentionalRemoval;
        if(Ordinary()){
            event.sampleSourceNative=NativeStamp(realSrc);event.sampleDestinationNative=NativeStamp(realDst);
            event.useEvidence=useProgress_.Inspect(sample.items[event.source],sample.items[event.destination]);
            event.intentionalRemoval=sample.intentionalRemoval;
        }
        return event; // only copied scalars leave these calling-thread borrowed reads
    };
    Snapshot sample;sample.now=now;sample.player=context_.player;sample.world=context_.world;
    sample.selected=view->selected;sample.eligible=true;
    std::array<OwnedStack,36> owned;
    for(int slot=0;slot<36;++slot){
        if(slot!=view->selected && slot<9) continue;
        auto borrowed=get(slot);
        if(!borrowed || !owned[slot].Copy(native_.stack,borrowed)){Reset(ResetCause::StackCopyFailed,now);return {};}
        bool damageable=!native_.stack.empty(owned[slot].Get()) && world_.damageable(owned[slot].Get());
        auto item=identities_.Identify(native_.stack,owned[slot].Get(),damageable);
        if(!item){Reset(ResetCause::IdentityFailed,now);return {};}
        sample.items[slot]=std::move(*item);
    }
    if(integrated_){
        auto counts=integrated_->AuthoritativeCounts(identity,now);
        sample.transferReady=counts.has_value();
        if(counts)for(int slot=0;slot<36;++slot){
            if(slot!=view->selected && slot<9)continue;
            if((*counts)[slot]!=sample.items[slot].count)sample.transferReady=false;
        }
        // Before policy/UseTracker consumes a depletion/threshold trigger, wait
        // for a copied post-handler server count sample matching these slots.
        // No request exists yet: waiting must not become a rejected attempt.
        // Existing in-flight requests still need reconciliation/timeout below.
        if(!sample.transferReady && !Pending() && !completion_)return {};
    }
    uses_.Apply(context_,sample);
    if(Ordinary() && (!sample.items[view->selected].Empty() || sample.intentionalRemoval))emptyFallback_.reset();
    if(Ordinary() && sample.items[view->selected].Empty() && emptyFallback_ && !sample.intentionalRemoval)
        sample.emptyFallback=emptyFallback_;
    if((Pending() || ordinaryWatching_) && !abandoned_ && !resample_.Active())
        sample.useConsumption=useProgress_.Sample(sample.items[pendingDiagnostic_.source],sample.items[pendingDiagnostic_.destination]);
    if(Ordinary() && resample_.Active()){
        auto result=resample_.Sample(sample,pendingSince_);
        auto event=pendingDiagnostic_;event.time=now;event.syncCoverage=resample_.Coverage();
        event.emptyFallback=sample.emptyFallback.has_value();
        event.resampleStartedAt=resample_.StartedAt();event.syncApplications=resample_.SyncCount();
        if(result==ResampleResult::Invalid || result==ResampleResult::Expired){
            StopOrdinary(now,result==ResampleResult::Expired?4:5);policy_.Reset();uses_.Reset();return {};
        }
        if(now>=pendingSince_ && now-pendingSince_>=Policy::timeoutMs)ordinaryPending_=false;
        if(result==ResampleResult::Waiting)return {};
        if(result==ResampleResult::Baseline){event.kind=DiagnosticKind::ResampleBaseline;diagnostics_.Push(event);return {};}
        event.kind=DiagnosticKind::ResampleReady;event.sourceCount=sample.items[event.source].count;
        event.destinationCount=sample.items[event.destination].count;diagnostics_.Push(event);
        resample_.Reset(); // policy was reset at pause; only the current snapshot can start a new plan
    }
    if(Ordinary() && ordinaryWatching_ && !abandoned_){
        auto observation=progressObservation(sample);
        if(!observation.accountedProgress || sample.intentionalRemoval || now<pendingSince_){
            StopOrdinary(now,now<pendingSince_?3:1,ResetCause::Explicit,WorldFailure::None,&observation);
            policy_.Reset();uses_.Reset();return {};
        }
        if(ordinaryPending_ && now-pendingSince_>=Policy::timeoutMs){
            // This is local prediction settlement, NOT a native ACK/acceptance.
            observation.kind=DiagnosticKind::PredictionSettled;diagnostics_.Push(observation);
            sample.completion=Completion::Predicted;sample.completionId=pendingDiagnostic_.requestId;
            ordinaryPending_=false; // keep latest watch/proof until replaced or explicit context reset
        }
    }
    if(integrated_){
        if(auto done=integrated_->Poll()){
            auto observation=progressObservation(sample);
            if(!observedNativeStatus_){
                observedNativeStatus_=done->nativeStatus;observation.kind=DiagnosticKind::NativeResult;
                observation.nativeStatus=done->nativeStatus;diagnostics_.Push(observation);
            }
            // Both inventories must agree with the exact callback-accounted
            // progress. Neither native acceptance nor a lower count suffices.
            bool withinDeadline=now>=pendingSince_ && now-pendingSince_<Policy::timeoutMs;
            if(abandoned_ || done->result==Completion::Rejected || (observation.accountedProgress && withinDeadline)){
                pendingDiagnostic_.provenConsumption=observation.provenConsumption;
                pendingDiagnostic_.consumptionChainValid=observation.consumptionChainValid;
                pendingDiagnostic_.accountedProgress=observation.accountedProgress;
                Publish(*done);
            }
        }
        if(!Pending() && !completion_ && !integrated_->Ready(identity,now)) return {};
    }
    if(!Ordinary() && !integrated_ && nativeCompletion_){
        auto observation=progressObservation(sample);
        bool withinDeadline=now>=pendingSince_ && now-pendingSince_<Policy::timeoutMs;
        if(nativeCompletion_->result==Completion::Rejected || (observation.accountedProgress && withinDeadline)){
            pendingDiagnostic_.provenConsumption=observation.provenConsumption;
            pendingDiagnostic_.consumptionChainValid=observation.consumptionChainValid;
            pendingDiagnostic_.accountedProgress=observation.accountedProgress;
            completion_=*nativeCompletion_;
            ClearPending();abandoned_=false;
        }
    }
    if(!Ordinary() && Pending() && (now<pendingSince_ || now-pendingSince_>=Policy::timeoutMs)){
        auto event=progressObservation(sample);
        event.kind=DiagnosticKind::Timeout;event.time=now;diagnostics_.Push(event);ClearPending();
    }
    if(completion_){
        sample.completion=completion_->result;sample.completionId=completion_->requestId;
        if(!Ordinary()){
            auto event=pendingDiagnostic_;event.kind=DiagnosticKind::Reconciled;event.time=now;event.nativeStatus=completion_->nativeStatus;
            event.provenConsumption=sample.useConsumption?sample.useConsumption->count:0;
            event.sourceCount=sample.items[event.source].count;event.destinationCount=sample.items[event.destination].count;diagnostics_.Push(event);
        }
    }
    auto transfer=policy_.Tick(sample,config);completion_.reset();
    if(!transfer){
        if(Ordinary() && sample.emptyFallback && !Pending())emptyFallback_.reset();
        return {};
    }
    Diagnostic event;event.requestId=transfer->requestId;event.time=now;event.source=transfer->source;
    event.emptyFallback=Ordinary() && sample.emptyFallback.has_value() && sample.items[view->selected].Empty();
    event.destination=transfer->destination;event.amount=transfer->amount;
    event.sourceCount=sample.items[event.source].count;event.destinationCount=sample.items[event.destination].count;
    auto source=owned[event.source].Get(),destination=owned[event.destination].Get();
    event.sourceVariant=Read<std::uint8_t>(source,144);event.destinationVariant=Read<std::uint8_t>(destination,144);
    event.sourceId=Read<std::int32_t>(source,event.sourceVariant==0?128:136);
    event.destinationId=Read<std::int32_t>(destination,event.destinationVariant==0?128:136);
    auto result=Ordinary()?ExecuteOrdinaryTransfer(native_,base_,manager,player,*transfer,owned[transfer->source].Get(),owned[transfer->destination].Get()):integrated_?
        ExecuteIntegratedTransfer(native_,base_,manager,player,*transfer,owned[transfer->source].Get(),owned[transfer->destination].Get(),*integrated_,identity,now):
        ExecuteTransfer(native_,base_,manager,player,*transfer,owned[transfer->source].Get(),owned[transfer->destination].Get(),responses_);
    event.result=static_cast<int>(result);event.nativeId=Ordinary()?0:PendingId();event.integrated=integrated_!=nullptr;event.ordinary=Ordinary();diagnostics_.Push(event);
    // An unsent ordinary attempt cannot replace the latest prediction watch.
    if(!Ordinary() || result==TransferResult::Queued || result==TransferResult::Fault)pendingDiagnostic_=event;
    if(result==TransferResult::Queued){
        emptyFallback_.reset(); // one native submission consumes this intent; never an old-request retry
        nativeWriteCount_=0;nativeWritesOmitted_=0;
        if(Ordinary()){ordinaryPending_=ordinaryWatching_=true;ordinaryGeneration_=selectionGeneration_;}
        pendingSince_=now;completion_.reset();observedNativeStatus_.reset();
        useProgress_.Arm(*transfer,sample.items[transfer->source],sample.items[transfer->destination]);
    }
    else if(result==TransferResult::Fault) faulted_=true;
    else if(result==TransferResult::Deferred){
        // No native transaction was submitted. Preserve fill/depletion intent
        // through a busy manager or a newer server sample arriving during copy.
        policy_.Defer(transfer->requestId);completion_.reset();
    }
    else completion_=RequestCompletion{transfer->requestId,Completion::Rejected};
    return result;
}
std::uint64_t Runtime::NativeWriterRequest(void* inventory,int slot,std::uint64_t epoch,std::uint64_t now) const noexcept{
    if(!Ordinary() || faulted_ || epoch!=epoch_ || ordinaryGeneration_!=selectionGeneration_ ||
        !inventory || reinterpret_cast<std::uintptr_t>(inventory)!=context_.inventory ||
        slot<0 || slot>=36)return 0;
    if(resample_.Active())return backendEnabled_ && !abandoned_ && !OrdinaryBlocked() &&
        (slot==context_.selected || slot>=9) && resample_.Healthy(now) && !resample_.Expired(now)?pendingDiagnostic_.requestId:0;
    if(slot!=pendingDiagnostic_.source && slot!=pendingDiagnostic_.destination)return 0;
    if(backendEnabled_ && !abandoned_ && ordinaryWatching_ && !OrdinaryBlocked())return pendingDiagnostic_.requestId;
    return stoppedWritersActive_ && OrdinaryBlocked() && now>=stoppedAt_ && now-stoppedAt_<6000?
        pendingDiagnostic_.requestId:0;
}
void Runtime::ObserveNativeWrite(void* inventory,std::uint64_t epoch,NativeWrite const& write) noexcept{
    if(!write.requestId || write.endAt<write.beginAt || NativeWriterRequest(inventory,write.slot,epoch,write.beginAt)!=write.requestId ||
        NativeWriterRequest(inventory,write.slot,epoch,write.endAt)!=write.requestId)return;
    if(!stoppedWritersActive_ && write.beginAt>=pendingSince_ && native_.profile && IncomingInventorySlot(write,native_.profile->Rva(0x134348C)+20)){
        if(write.slot==context_.selected && write.after.count>0)emptyFallback_.reset();
        if(!resample_.Active())PauseOrdinary(write);
        resample_.Incoming(write.slot,write.endAt);
        uses_.Reset(); // no pre-sync use/depletion can authorize the fresh plan
        return;
    }
    if(stoppedWritersActive_){
        if(stoppedWriteCount_==stoppedWrites_->size()){
            // Preserve the first eight AND the latest eight. The omission
            // total explicitly exposes the missing middle history.
            for(unsigned n=9;n<stoppedWriteCount_;++n)(*stoppedWrites_)[n-1]=(*stoppedWrites_)[n];
            if(stoppedWritesOmitted_!=std::numeric_limits<std::uint64_t>::max())++stoppedWritesOmitted_;
        }else ++stoppedWriteCount_;
        auto copied=write;copied.postStop=true;(*stoppedWrites_)[stoppedWriteCount_-1]=copied;
        return;
    }
    if(nativeWriteCount_==nativeWrites_->size()){
        for(unsigned n=1;n<nativeWriteCount_;++n)(*nativeWrites_)[n-1]=(*nativeWrites_)[n];
        if(nativeWritesOmitted_!=std::numeric_limits<std::uint64_t>::max())++nativeWritesOmitted_;
    }else ++nativeWriteCount_;
    (*nativeWrites_)[nativeWriteCount_-1]=write;
}
void Runtime::PollStoppedWriters(Config config,std::uint64_t epoch,std::uint64_t now) noexcept{
    if(!stoppedWritersActive_)return;
    if(!config.enabled || epoch!=epoch_)FinishStoppedWriters(now,3);
    else if(now<stoppedAt_)FinishStoppedWriters(now,4);
    else if(now-stoppedAt_>=6000)FinishStoppedWriters(now,1);
}
void Runtime::FinishStoppedWriters(std::uint64_t now,int reason,ResetCause cause,WorldFailure failure) noexcept{
    if(!stoppedWritersActive_)return;
    stoppedWritersActive_=false;
    auto event=pendingDiagnostic_;event.kind=DiagnosticKind::PostStopSummary;event.time=now;
    event.submittedAt=stoppedAt_;event.elapsedMs=now>=stoppedAt_?now-stoppedAt_:0;
    event.postStopEndReason=reason;event.resetCause=cause;event.worldFailure=static_cast<int>(failure);
    event.nativeWriteCount=stoppedWriteCount_;event.nativeWritesOmitted=stoppedWritesOmitted_;
    diagnostics_.Push(event);
    for(unsigned n=0;n<stoppedWriteCount_;++n){
        event.kind=DiagnosticKind::NativeWrite;event.nativeWrite=(*stoppedWrites_)[n];
        event.time=event.nativeWrite.endAt;event.nativeWriteOrdinal=n;diagnostics_.Push(event);
    }
    stoppedWriteCount_=0;stoppedWritesOmitted_=0;
}
std::optional<UseCapture> Runtime::BeginUse(void* inventory,void const* operation,bool foreground,bool hidden,Config config,std::uint64_t epoch){
    if(faulted_ || !backendEnabled_ || !config.enabled) return {};
    if(!native_.profile || native_.profile!=world_.profile)return {};
    int invoker{};auto player=UsePlayer(*native_.profile,base_,operation,&invoker);
    auto manager=Read<void*>(player,native_.profile->layout.managerPlayer);
    auto view=ResolveWorld(world_,base_,manager,player,foreground,hidden,Mode());
    if(!view || view->inventory!=inventory) return {};
    Sync(*view,epoch);
    RecentUse trace;trace.recorded=Ordinary() && ordinaryWatching_ && !abandoned_;
    trace.invoker=invoker;if(trace.recorded)trace.beginAt=GetTickCount64();
    auto before=Main(*view,trace.recorded?&trace.beforeNative:nullptr);if(!before || before->Empty()) return {};
    return UseCapture{context_,std::move(*before),epoch,useProgress_.RequestId(),trace,resample_.Revision()};
}
void Runtime::EndUse(UseCapture const& capture,bool foreground,bool hidden,Config config,std::uint64_t epoch){
    if(faulted_ || !native_.profile || native_.profile!=world_.profile) return;
    if(!config.enabled || capture.epoch!=epoch){Reset(ResetCause::EndUseConfigChanged);return;}
    auto player=reinterpret_cast<void*>(capture.context.player);
    auto manager=Read<void*>(player,native_.profile->layout.managerPlayer);
    WorldFailure failure{};
    auto view=ResolveWorld(world_,base_,manager,player,foreground,hidden,Mode(),&failure);
    if(!view){Reset(ResetCause::EndUseWorldGate,0,failure);return;}
    if(Key(*view)!=capture.context || context_!=capture.context){Reset(ResetCause::EndUseContextChanged);return;}
    auto trace=capture.trace;
    auto after=Main(*view,trace.recorded?&trace.afterNative:nullptr);if(!after){Reset(ResetCause::EndUseSampleFailed);return;}
    if(trace.recorded)trace.endAt=GetTickCount64();
    if(resample_.Active() || capture.resampleRevision){
        if(!capture.resampleRevision || capture.resampleRevision!=resample_.Revision())return;
        resample_.Use(capture.resampleRevision,capture.before,*after);
    }
    if(Ordinary()){
        if(after->Empty() && !capture.before.Empty() && !capture.before.damageable)
            emptyFallback_=capture.before;
        else emptyFallback_.reset();
    }
    uses_.Observe(capture.context,capture.before,context_,*after);
    useProgress_.Observe(capture.requestId,capture.before,*after,trace);
}
std::optional<RequestCompletion> Runtime::Decode(void* manager,std::span<std::byte const> response) const noexcept{
    return faulted_ || integrated_ || Ordinary() ? std::nullopt : responses_.Decode(reinterpret_cast<std::uintptr_t>(manager),response);
}
void Runtime::Publish(RequestCompletion const& value) noexcept{
    if(Ordinary() || ordinaryPending_ || value.result==Completion::Predicted)return;
    if(!integrated_){
        if(!responses_.Pending() || value.requestId!=pendingDiagnostic_.requestId ||
            value.nativeId!=responses_.PendingNativeId() || value.result==Completion::Waiting)return;
        // Only the original handler's copied matching result is latched here.
        // Keep the request/proof/fence until a later calling-thread inventory
        // sample also proves exact progress. Rejection wins across callbacks.
        if(nativeCompletion_ && (nativeCompletion_->result==Completion::Rejected || value.result==Completion::Accepted))return;
        nativeCompletion_=value;observedNativeStatus_=value.nativeStatus;
    }
    auto event=pendingDiagnostic_;event.kind=DiagnosticKind::Response;event.nativeId=value.nativeId;
    event.requestId=value.requestId;event.nativeStatus=value.nativeStatus;diagnostics_.Push(event);
    if(!integrated_ && !abandoned_)return;
    if(!abandoned_) completion_=value;
    ClearPending();abandoned_=false;
}
void Runtime::Reset(ResetCause cause,std::uint64_t now,WorldFailure failure) noexcept{
    if(stoppedWritersActive_)FinishStoppedWriters(now?now:GetTickCount64(),2,cause,failure);
    // Only active-watch diagnostics need a timestamp; idle resets do not query
    // the clock. Never report the request's start as its cancellation time.
    if((Pending() || ordinaryWatching_ || resample_.Active()) && !abandoned_ && !now)now=GetTickCount64();
    if(ordinaryWatching_ && !ordinaryPending_ && !abandoned_){
        auto event=pendingDiagnostic_;event.kind=DiagnosticKind::ContextReset;event.time=now;
        event.resetCause=cause;event.worldFailure=static_cast<int>(failure);event.submittedAt=pendingSince_;
        event.elapsedMs=now>=pendingSince_?now-pendingSince_:0;event.settledWatchCleared=true;
        diagnostics_.Push(event); // once, before clearing the settled watch; no native reads
    }
    if((ordinaryPending_ && !abandoned_) || resample_.Active())StopOrdinary(now,2,cause,failure);
    if(Pending()){
        if(!abandoned_){auto event=pendingDiagnostic_;event.kind=DiagnosticKind::ContextReset;event.time=now;
            event.resetCause=cause;event.worldFailure=static_cast<int>(failure);event.submittedAt=pendingSince_;
            event.elapsedMs=now>=pendingSince_?now-pendingSince_:0;diagnostics_.Push(event);}
        abandoned_=true;
        // A matching original modern result already arrived. Cancellation
        // needs no old-context reconciliation; retain no gameplay references.
        if(!integrated_ && nativeCompletion_){ClearPending();abandoned_=false;}
    }
    if(integrated_)integrated_->Invalidate();
    ordinaryWatching_=false;nativeWriteCount_=0;nativeWritesOmitted_=0;
    emptyFallback_.reset();
    policy_.Reset();uses_.Reset();useProgress_.Reset();identities_.Reset();completion_.reset();context_={};
}
void Runtime::PauseOrdinary(NativeWrite const& write) noexcept{
    resample_.Start(pendingDiagnostic_.source,pendingDiagnostic_.destination,write.endAt);
    auto event=pendingDiagnostic_;event.kind=DiagnosticKind::PredictionPaused;event.time=write.endAt;
    event.submittedAt=pendingSince_;event.elapsedMs=write.endAt>=pendingSince_?write.endAt-pendingSince_:0;
    event.resampleStartedAt=write.endAt;event.nativeWrite=write;diagnostics_.Push(event);
    ordinaryWatching_=false;policy_.Reset();uses_.Reset();useProgress_.Reset();completion_.reset();
    // No ACK, settlement, replay, rollback or early release of ordinaryPending_.
    nativeWriteCount_=0;nativeWritesOmitted_=0;
}
bool Runtime::SelectBackend(Backend backend,std::uint64_t generation,std::uint64_t now) noexcept {
    if(faulted_)return false;
    if(!selectionConfigured_ || backend!=requestedBackend_ || generation!=selectionGeneration_){
        Reset(ResetCause::BackendChanged,now);backendEnabled_=false;requestedBackend_=backend;
        selectionGeneration_=generation;selectionConfigured_=true;
    }
    if(backend==Backend::RemoteOrdinary && !generation){backendEnabled_=false;return false;}
    if(backend==Backend::RemoteOrdinary && OrdinaryBlocked()){
        // The deadline releases only a LOCAL quarantine, never a transaction.
        // A new full two-Tick snapshot is still mandatory on the owner thread.
        if(now<ordinaryRetrySince_ || now-ordinaryRetrySince_<Policy::timeoutMs ||
            now<pendingSince_ || now-pendingSince_<Policy::timeoutMs){backendEnabled_=false;return false;}
        FinishStoppedWriters(now,5);
        ordinaryBlockedGeneration_.reset();ordinaryNeedsResample_=true;
        auto event=pendingDiagnostic_;event.kind=DiagnosticKind::PredictionRearmed;event.time=now;
        event.submittedAt=ordinaryRetrySince_;event.elapsedMs=now-ordinaryRetrySince_;diagnostics_.Push(event);
    }
    if(backendEnabled_)return true;
    if(Pending() && integrated_)if(auto done=integrated_->Poll())Publish(*done);
    if(Pending()){
        if(ordinaryPending_ && now<pendingSince_)return false;
        if(now>=pendingSince_ && now-pendingSince_<Policy::timeoutMs)return false;
        auto event=pendingDiagnostic_;event.kind=DiagnosticKind::Timeout;event.time=now;
        event.nativeStatus=observedNativeStatus_.value_or(-1);diagnostics_.Push(event);
        ClearPending();abandoned_=false;
    }
    if(backend==Backend::Unsupported || (backend==Backend::Integrated && !bridge_))return false;
    integrated_=backend==Backend::Integrated?bridge_:nullptr;
    backendEnabled_=true;return true;
}
void Runtime::StopOrdinary(std::uint64_t now,int reason,ResetCause cause,WorldFailure failure,Diagnostic const* observation) noexcept{
    if(ordinaryBlockedGeneration_ && *ordinaryBlockedGeneration_==ordinaryGeneration_)return;
    ordinaryBlockedGeneration_=ordinaryGeneration_;ordinaryWatching_=false;
    ordinaryRetrySince_=now;
    if(reason!=4)emptyFallback_.reset(); // expiry may retain a real depletion; unexplained state may not
    if(ordinaryPending_)abandoned_=true; // retain original deadline, no native rollback/replay
    auto event=observation?*observation:pendingDiagnostic_;event.kind=DiagnosticKind::PredictionStopped;event.time=now;
    if(resample_.Active()){
        event.syncCoverage=resample_.Coverage();event.resampleStartedAt=resample_.StartedAt();event.syncApplications=resample_.SyncCount();
        resample_.Reset();
    }
    event.predictionStopReason=reason;event.resetCause=cause;event.worldFailure=static_cast<int>(failure);
    event.submittedAt=pendingSince_;event.elapsedMs=now>=pendingSince_?now-pendingSince_:0;
    event.nativeWriteCount=nativeWriteCount_;event.nativeWritesOmitted=nativeWritesOmitted_;
    diagnostics_.Push(event);
    for(unsigned n=0;n<nativeWriteCount_;++n){
        auto trace=pendingDiagnostic_;trace.kind=DiagnosticKind::NativeWrite;
        trace.nativeWrite=(*nativeWrites_)[n];trace.time=trace.nativeWrite.endAt;
        trace.nativeWriteCount=nativeWriteCount_;trace.nativeWriteOrdinal=n;trace.nativeWritesOmitted=nativeWritesOmitted_;
        diagnostics_.Push(trace);
    }
    nativeWriteCount_=0;nativeWritesOmitted_=0;useProgress_.Reset();
    // An observation-only tail is armed solely for an inventory/proof mismatch
    // in the current context. Cancellation/clock failures never retain a watch.
    if(reason==1 && context_.inventory && ordinaryGeneration_==selectionGeneration_){
        stoppedWritersActive_=true;stoppedAt_=now;stoppedWriteCount_=0;stoppedWritesOmitted_=0;
    }
}
void Runtime::Sync(WorldView const& view,std::uint64_t epoch,std::uint64_t now){
    auto key=Key(view);
    if(context_!=key || epoch_!=epoch){Reset(epoch_!=epoch?ResetCause::ConfigChanged:ResetCause::ContextChanged,now);context_=key;epoch_=epoch;}
}
std::optional<Item> Runtime::Main(WorldView const& view,NativeUseStamp* stamp){
    auto borrowed=world_.inventoryItem(view.inventory,view.selected);if(!borrowed) return {};
    OwnedStack stack;if(!stack.Copy(native_.stack,borrowed)) return {};
    if(stamp)*stamp=NativeStamp(stack.Get());
    bool damageable=!native_.stack.empty(stack.Get()) && world_.damageable(stack.Get());
    return identities_.Identify(native_.stack,stack.Get(),damageable);
}
}
