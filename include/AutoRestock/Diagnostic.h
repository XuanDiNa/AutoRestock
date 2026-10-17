#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include "UseTracker.h"
namespace autorestock {
enum class DiagnosticKind { Request,Response,Reconciled,Timeout,ContextReset,NativeResult,PredictionSettled,PredictionStopped,NativeWrite,PostStopSummary,
    PredictionPaused,ResampleBaseline,ResampleReady,PredictionRearmed };
enum class ResetCause {
    Explicit,SelectionWorldGate,TickWorldGate,ConfigDisabled,BackendDisabled,ContextChanged,
    ConfigChanged,StackCopyFailed,IdentityFailed,EndUseConfigChanged,EndUseWorldGate,
    EndUseContextChanged,EndUseSampleFailed,BackendChanged,StartGame,RemoteRetired
};
// Values only: no absolute game addresses, borrowed objects, names or metadata survive
// the callback. The worker is the only consumer and performs all formatting/I/O.
struct WriteStackState {
    bool readable{};
    int count=-1;
    NativeUseStamp native;
};
struct NativeWrite {
    std::uint64_t beginAt{},endAt{},requestId{};
    std::uint32_t callerRva{}; // image-relative code location, never an absolute address
    int slot=-1;
    unsigned nativeDepth{};
    std::uint32_t network{}; // preserve full native R9D; low byte is the network flag
    bool callerKnown{};
    bool postStop{}; // diagnostic phase only, never acceptance or a policy input
    WriteStackState before,input,after;
};
struct Diagnostic {
    DiagnosticKind kind=DiagnosticKind::Request;
    std::uint64_t requestId{},time{};
    std::int32_t nativeId{},sourceId{},destinationId{};
    int source{},destination{},amount{},sourceCount{},destinationCount{};
    int result{},nativeStatus=-1,sourceVariant{},destinationVariant{};
    bool integrated{};
    bool ordinary{};
    int predictionStopReason{}; //1 observed correction/proof mismatch,2 unresolved cancellation,3 clock reversal
    ResetCause resetCause=ResetCause::Explicit;
    int worldFailure{};
    std::uint64_t submittedAt{},elapsedMs{};
    int wrapperSourceCount=-1,wrapperDestinationCount=-1,actualSourceCount=-1,actualDestinationCount=-1;
    bool inventorySampled{},realMatches{},exactProgress{};
    int provenConsumption{};
    bool consumptionChainValid{},accountedProgress{};
    bool settledWatchCleared{};
    bool intentionalRemoval{};
    NativeUseStamp sampleSourceNative,sampleDestinationNative;
    UseProgressEvidence useEvidence{};
    unsigned nativeWriteCount{},nativeWriteOrdinal{};
    std::uint64_t nativeWritesOmitted{};
    NativeWrite nativeWrite;
    int postStopEndReason{}; //1 deadline,2 context reset,3 settings changed,4 clock reversal,5 automatic replan
    unsigned syncCoverage{};
    std::uint64_t resampleStartedAt{},syncApplications{};
    bool emptyFallback{};
};
class DiagnosticQueue {
public:
    bool Push(Diagnostic const& value) noexcept{
        auto written=written_.load(std::memory_order_relaxed);
        if(written-read_.load(std::memory_order_acquire)>=values_->size()){
            dropped_.fetch_add(1,std::memory_order_relaxed);return false;
        }
        (*values_)[written%values_->size()]=value;
        written_.store(written+1,std::memory_order_release);return true;
    }
    std::optional<Diagnostic> Pop() noexcept{
        auto read=read_.load(std::memory_order_relaxed);
        if(read==written_.load(std::memory_order_acquire)) return {};
        auto value=(*values_)[read%values_->size()];read_.store(read+1,std::memory_order_release);return value;
    }
    std::uint64_t Dropped() const noexcept{return dropped_.load(std::memory_order_relaxed);}
private:
    // Allocate once during Runtime construction, outside gameplay callbacks.
    // Richer copied evidence must not multiply inline stack use in callers.
    std::unique_ptr<std::array<Diagnostic,64>> values_=std::make_unique<std::array<Diagnostic,64>>();
    std::atomic<std::uint64_t> written_{},read_{},dropped_{};
};
}
