#pragma once
#include "NativeIdentity.h"
#include "Transaction.h"
#include "UseTracker.h"
#include "World.h"
#include "Diagnostic.h"
#include "Session.h"
#include "OrdinaryResample.h"
namespace autorestock {
struct UseCapture {UseContext context;Item before;std::uint64_t epoch{},requestId{};RecentUse trace;std::uint64_t resampleRevision{};};
class Runtime {
public:
    Runtime(NativeApi native,WorldApi world,std::uintptr_t imageBase):native_(native),world_(world),base_(imageBase){}
    Runtime(NativeApi native,WorldApi world,std::uintptr_t imageBase,IntegratedBridge& integrated):
        native_(native),world_(world),base_(imageBase),integrated_(&integrated),bridge_(&integrated){}
    std::optional<TransferResult> Tick(void* manager,bool foreground,bool hidden,Config,std::uint64_t now,std::uint64_t epoch);
    std::optional<UseCapture> BeginUse(void* inventory,void const* operation,bool foreground,bool hidden,Config,std::uint64_t epoch);
    // Called only around the exact-profile GameMode::destroyBlock original.
    // Tool-only: no threshold refill and no inference from an idle empty hand.
    std::optional<UseCapture> BeginDestroy(void* gameMode,bool foreground,bool hidden,Config,std::uint64_t epoch);
    // Native hurtAndBreak may operate on an exact-ID working copy. Only an
    // observed1->0 break can arm a short, same-context hand-application wait.
    std::optional<UseCapture> BeginToolHurt(void const* stack,void* player,bool foreground,bool hidden,Config,std::uint64_t epoch);
    bool EndToolHurt(UseCapture const&,void const* stack,bool foreground,bool hidden,Config,std::uint64_t epoch);
    void EndUse(UseCapture const&,bool foreground,bool hidden,Config,std::uint64_t epoch);
    // Owner-thread hook telemetry only; unknown callbacks cannot create use
    // proof. Reads only the existing version-profiled operation representation.
    void ObserveToolOperation(void* inventory,void const* operation,bool captured,std::uint64_t now) noexcept;
    // Owner-thread native writer observation. Exact incoming provenance may
    // retire prediction and PAUSE; it cannot accept, send or repair old proof.
    std::uint64_t NativeWriterRequest(void* inventory,int slot,std::uint64_t epoch,std::uint64_t now=0) const noexcept;
    void ObserveNativeWrite(void* inventory,std::uint64_t epoch,NativeWrite const&) noexcept;
    // Called even during ordinary quarantine. No native reads,
    // policy updates or transfer authorization; closes a copied diagnostic tail.
    void PollStoppedWriters(Config,std::uint64_t epoch,std::uint64_t now) noexcept;
    std::optional<RequestCompletion> Decode(void* manager,std::span<std::byte const> response) const noexcept;
    void Publish(RequestCompletion const&) noexcept;
    void Reset(ResetCause=ResetCause::Explicit,std::uint64_t now=0,WorldFailure=WorldFailure::None) noexcept;
    // Owner-only. A switch cancels intent, retaining the old tracker/deadline
    // until its exact result or timeout before changing transaction backends.
    bool SelectBackend(Backend,std::uint64_t generation,std::uint64_t now) noexcept;
    bool Faulted() const noexcept{return faulted_;}
    // A transient local quarantine, not a permanent connection-generation lock.
    bool OrdinaryBlocked() const noexcept{return ordinaryBlockedGeneration_ && *ordinaryBlockedGeneration_==selectionGeneration_;}
    bool OrdinaryPaused() const noexcept{return resample_.Active();}
    std::optional<Diagnostic> TakeDiagnostic() noexcept{return diagnostics_.Pop();}
    std::uint64_t DroppedDiagnostics() const noexcept{return diagnostics_.Dropped();}
private:
    bool Pending() const noexcept {return ordinaryPending_ || (integrated_?integrated_->Pending():responses_.Pending());}
    void ClearPending() noexcept {if(ordinaryPending_)ordinaryPending_=false;else if(integrated_)integrated_->Clear();else responses_.Reset();ordinaryWatching_=false;observedNativeStatus_.reset();nativeCompletion_.reset();useProgress_.Reset();}
    std::int32_t PendingId() const noexcept {return ordinaryPending_?0:integrated_?integrated_->PendingNativeId():responses_.PendingNativeId();}
    bool Ordinary() const noexcept{return requestedBackend_==Backend::RemoteOrdinary;}
    InventoryMode Mode() const noexcept{return Ordinary()?InventoryMode::Ordinary:InventoryMode::Modern;}
    void StopOrdinary(std::uint64_t now,int reason,ResetCause=ResetCause::Explicit,WorldFailure=WorldFailure::None,
        Diagnostic const* observation=nullptr) noexcept;
    void Sync(WorldView const&,std::uint64_t epoch,std::uint64_t now=0);
    void PauseOrdinary(NativeWrite const&) noexcept;
    void FinishStoppedWriters(std::uint64_t now,int reason,ResetCause=ResetCause::Explicit,WorldFailure=WorldFailure::None) noexcept;
    std::optional<Item> Main(WorldView const&,NativeUseStamp* stamp=nullptr);
    void SampleTool(WorldView const&,std::uint64_t now);
    void TraceTool(ToolStage,ToolReason,std::uint64_t now,ResetCause=ResetCause::Explicit,WorldFailure=WorldFailure::None) noexcept;
    void GateTool(ToolStage,ToolReason,std::uint64_t now) noexcept;
    void ApplyToolBreak(Item const& current,std::uint64_t now);
    struct ToolBreak {UseCapture capture;std::uint64_t observedAt{};};
    std::optional<ToolBreak> toolBreak_;
    ToolObservation toolLast_{};
    ToolEvidence toolEvidence_{};
    bool toolActive_{};
    std::uint64_t toolSampleAt_{};
    ToolStage toolGateStage_=ToolStage::Reset;
    ToolReason toolGateReason_=ToolReason::None;
    std::uint64_t toolGateAt_{},toolUseAt_{};
    std::array<std::uint64_t,8> toolOperations_{};
    unsigned toolOperationCount_{};
    NativeApi native_;WorldApi world_;std::uintptr_t base_;
    IntegratedBridge* integrated_{};
    IntegratedBridge* bridge_{};
    Backend requestedBackend_{};
    std::uint64_t selectionGeneration_{};
    bool selectionConfigured_{},backendEnabled_{true};
    bool ordinaryPending_{},ordinaryWatching_{};
    std::uint64_t ordinaryGeneration_{};
    std::optional<std::uint64_t> ordinaryBlockedGeneration_;
    std::uint64_t ordinaryRetrySince_{};
    bool ordinaryNeedsResample_{};
    std::optional<Item> emptyFallback_; // actual depletion; canceled by context/UI/config, consumed once
    NativeIdentity identities_;UseTracker uses_;Policy policy_;ResponseTracker responses_;
    PendingUseProgress useProgress_;
    OrdinaryResample resample_;
    std::optional<RequestCompletion> completion_;
    std::optional<RequestCompletion> nativeCompletion_; // modern result only; prediction/progress is a separate gate
    std::optional<int> observedNativeStatus_; // one copied-result diagnostic per armed request, never acceptance proof
    UseContext context_{};std::uint64_t epoch_{},pendingSince_{};
    bool faulted_{},abandoned_{};
    DiagnosticQueue diagnostics_;
    Diagnostic pendingDiagnostic_;
    std::unique_ptr<std::array<NativeWrite,8>> nativeWrites_=std::make_unique<std::array<NativeWrite,8>>();
    unsigned nativeWriteCount_{};
    std::uint64_t nativeWritesOmitted_{};
    std::unique_ptr<std::array<NativeWrite,16>> stoppedWrites_=std::make_unique<std::array<NativeWrite,16>>();
    bool stoppedWritersActive_{};
    std::uint64_t stoppedAt_{};
    unsigned stoppedWriteCount_{};
    std::uint64_t stoppedWritesOmitted_{};
};
}
