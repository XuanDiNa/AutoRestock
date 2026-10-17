#pragma once
#include "Integrated.h"
#include <array>
#include <atomic>
#include <optional>
namespace autorestock {
// Value-only observation: no borrowed stack, item, tag or actor address crosses
// the native callback boundary. This never becomes a transaction input.
struct StackObservation {
    bool readable{},hasItem{},hasNbt{},hasBlock{},hasNested{};
    int count=-1,itemId=-1,aux{},blockId{},valid=-1,variant=-1,networkId{};
};
struct ServerValidationEvent {
    ServerTicket ticket;
    std::uint64_t time{},userdataSize{};
    std::uint32_t sourceType{},status{},differences{};
    int slot=-1,container=-1,beforeDescriptorCount=-1,afterDescriptorCount=-1,afterCount=-1;
    int descriptorBlockId{},comparison=-1;
    StackObservation actual,expected;
};
class ServerValidationQueue {
public:
    bool Push(ServerValidationEvent const& event) noexcept {
        // Native observers normally share the integrated-server thread. Refuse
        // (never wait on) any overlapping producer instead of assuming that.
        if(producer_.test_and_set(std::memory_order_acquire)){
            dropped_.fetch_add(1,std::memory_order_relaxed);return false;
        }
        auto written=written_.load(std::memory_order_relaxed);
        bool room=written-read_.load(std::memory_order_acquire)<values_.size();
        if(room){values_[written%values_.size()]=event;written_.store(written+1,std::memory_order_release);}
        else dropped_.fetch_add(1,std::memory_order_relaxed);
        producer_.clear(std::memory_order_release);return room;
    }
    std::optional<ServerValidationEvent> Pop() noexcept {
        auto read=read_.load(std::memory_order_relaxed);
        if(read==written_.load(std::memory_order_acquire))return {};
        auto event=values_[read%values_.size()];read_.store(read+1,std::memory_order_release);return event;
    }
    std::uint64_t Dropped() const noexcept {return dropped_.load(std::memory_order_relaxed);}
private:
    std::array<ServerValidationEvent,64> values_{};
    std::atomic<std::uint64_t> written_{},read_{},dropped_{};
    std::atomic_flag producer_=ATOMIC_FLAG_INIT;
};
}
