#pragma once
#include "NativeSession.h"
#include <array>
#include <atomic>
namespace autorestock {
enum class RemoteAdmissionStage {
    Entry, Foreign, Faulted, MissingPump, InvalidPump, Continuation, Route,
    Network, Mode, Ledger, ManagerCapture, Api, PostRoute, Player, Manager,
    Finish, Accepted, OriginalUnwind
};
struct RemoteAdmissionEvent {
    std::uint64_t ordinal{},time{};
    RemoteAdmissionStage stage=RemoteAdmissionStage::Entry;
    NativeRouteDiagnostic route;
    int mode=-1;
    bool constructorObserved{};
};
// A bounded, nonblocking copied-value channel. No native identity is stored.
class RemoteAdmissionQueue {
public:
    bool Push(RemoteAdmissionEvent const& event) noexcept {
        if(producer_.test_and_set(std::memory_order_acquire)){
            dropped_.fetch_add(1,std::memory_order_relaxed);return false;
        }
        auto written=written_.load(std::memory_order_relaxed);
        bool room=written-read_.load(std::memory_order_acquire)<values_.size();
        if(room){values_[written%values_.size()]=event;written_.store(written+1,std::memory_order_release);}
        else dropped_.fetch_add(1,std::memory_order_relaxed);
        producer_.clear(std::memory_order_release);return room;
    }
    std::optional<RemoteAdmissionEvent> Pop() noexcept {
        auto read=read_.load(std::memory_order_relaxed);
        if(read==written_.load(std::memory_order_acquire))return {};
        auto event=values_[read%values_.size()];read_.store(read+1,std::memory_order_release);return event;
    }
    std::uint64_t Dropped() const noexcept {return dropped_.load(std::memory_order_relaxed);}
private:
    std::array<RemoteAdmissionEvent,32> values_{};
    std::atomic<std::uint64_t> written_{},read_{},dropped_{};
    std::atomic_flag producer_=ATOMIC_FLAG_INIT;
};
}
