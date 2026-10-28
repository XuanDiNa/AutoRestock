#pragma once
#include "NativeSession.h"
#include "Session.h"
#include <array>
#include <atomic>
#include <limits>
#include <type_traits>

namespace autorestock {
// All methods except Poison require the observed native owner thread. The
// native hooks establish ownership before passing copied scalar identities.
template<std::size_t Capacity=16,class Serial=std::uint64_t>
class RemoteSessionLedger {
    static_assert(Capacity>0 && std::is_unsigned_v<Serial>);
public:
    enum class Retired {Connection,Handler,Manager,Network};
    struct Connection {
        std::uintptr_t address{},network{};
        Serial serial{};
        bool operator==(Connection const&) const = default;
    };
    struct Start {
        Connection connection;
        NativePrimaryGuidRoute route;
        Serial serial{};
        InventoryMode mode{};
    };
    std::optional<Connection> Born(std::uintptr_t address,std::uintptr_t network,NativeConnectionIdentity identity) noexcept {
        if(Faulted())return {};
        if(!address || !network || !identity.Valid() || Find(address)){
            Poison();return {};
        }
        for(auto& record:records_)if(!record.connection.serial){
            auto serial=Issue();if(!serial)return {};
            record={{address,network,serial},{},0,identity,false};
            auto& watch=watches_[&record-records_.data()];
            watch.network.store(network,std::memory_order_release);
            watch.connection.store(address,std::memory_order_release);return record.connection;
        }
        Poison();return {};
    }
    bool Bind(Connection connection,NativeNetworkBinding binding,std::uintptr_t handler) noexcept {
        auto record=Find(connection.address);
        if(Faulted() || !record || record->connection!=connection || !handler ||
           !binding.client || !binding.game || binding.network!=connection.network){Cancel();return false;}
        if(record->handler){Poison();return false;}
        for(auto& other:records_)if(&other!=record && other.handler==handler){
            if(selectedRoute_.handler==handler || prepared_.route.handler==handler)Cancel();
            other.handler=0;
            watches_[&other-records_.data()].handler.store(0,std::memory_order_release);
        }
        record->binding=binding;record->handler=handler;
        watches_[record-records_.data()].handler.store(handler,std::memory_order_release);return true;
    }
    std::optional<Start> Begin(NativePrimaryGuidRoute route,InventoryMode mode) noexcept {
        if(prepared_.serial){Poison();return {};}
        Cancel();
        auto record=Find(route.connection);
        if(Faulted() || !Matches(record,route) || record->started ||
           (mode!=InventoryMode::Modern && mode!=InventoryMode::Ordinary))return {};
        auto serial=Issue();if(!serial)return {};
        record->started=true;prepared_={record->connection,route,serial,mode};return prepared_;
    }
    bool Finish(Start const& start,NativePrimaryGuidRoute route,
        std::uintptr_t world,std::uintptr_t player,std::uintptr_t manager) noexcept {
        auto record=Find(route.connection);
        if(Faulted() || !start.serial || prepared_.serial!=start.serial ||
           prepared_.connection!=start.connection || prepared_.route!=route || start.route!=route ||
           !Matches(record,route) || record->connection!=start.connection || !world || !player || !manager){
            Cancel();return false;
        }
        selected_={start.serial,{start.connection.serial,world,player,manager},start.mode,
            start.mode==InventoryMode::Modern?Backend::RemoteModern:Backend::RemoteOrdinary};
        selectedRoute_=route;prepared_={};watchedManager_.store(manager,std::memory_order_release);return true;
    }
    SessionSelection Select(std::uintptr_t manager,std::uintptr_t player,std::uintptr_t world) const noexcept {
        auto result=selected_;
        if(Faulted() || !result.generation || result.key.manager!=manager ||
           result.key.player!=player || result.key.world!=world || !Matches(Find(selectedRoute_.connection),selectedRoute_))
            result.backend=Backend::Unsupported;
        return result;
    }
    bool Authorize(NativePrimaryGuidRoute route,std::uintptr_t manager,std::uint64_t generation) const noexcept {
        return !Faulted() && generation && generation==selected_.generation &&
            selected_.backend==Backend::RemoteModern && selected_.key.manager==manager &&
            selectedRoute_==route && Matches(Find(route.connection),route);
    }
    void RetireConnection(std::uintptr_t address) noexcept {
        auto record=Find(address);if(!record)return;
        if(selectedRoute_.connection==address || prepared_.route.connection==address)Cancel();
        auto& watch=watches_[record-records_.data()];
        watch.connection.store(0,std::memory_order_release);watch.handler.store(0,std::memory_order_release);
        watch.network.store(0,std::memory_order_release);*record={};
    }
    void RetireHandler(std::uintptr_t handler) noexcept {
        if(!handler)return;
        if(selectedRoute_.handler==handler || prepared_.route.handler==handler)Cancel();
        for(auto& record:records_)if(record.handler==handler){
            record.handler=0;watches_[&record-records_.data()].handler.store(0,std::memory_order_release);
        }
    }
    void RetireManager(std::uintptr_t manager) noexcept {
        if(manager && selected_.key.manager==manager)Cancel();
    }
    void Disconnect(std::uintptr_t network) noexcept {
        for(auto& record:records_)if(record.connection.network==network)RetireConnection(record.connection.address);
    }
    void Cancel() noexcept {watchedManager_.store(0,std::memory_order_release);prepared_={};selected_={};selectedRoute_={};}
    // Foreign callbacks may compare only copied incoming address scalars and
    // these atomic watches. They never touch the owner ledger or native data.
    void Foreign(Retired kind,std::uintptr_t address) noexcept {
        if(!address)return;
        if(kind==Retired::Manager){
            if(watchedManager_.load(std::memory_order_acquire)==address)Poison();return;
        }
        for(auto const& watch:watches_){
            auto& slot=kind==Retired::Connection?watch.connection:kind==Retired::Handler?watch.handler:watch.network;
            if(slot.load(std::memory_order_acquire)==address){Poison();return;}
        }
    }
    void Poison() noexcept {poison_.store(true,std::memory_order_release);}
    bool Faulted() const noexcept {return poison_.load(std::memory_order_acquire);}
    std::uint64_t Generation() const noexcept {return Faulted()?0:selected_.generation;}
private:
    struct Record {
        Connection connection;
        NativeNetworkBinding binding;
        std::uintptr_t handler{};
        NativeConnectionIdentity identity;
        bool started{};
    };
    std::array<Record,Capacity> records_{};
    Serial issued_{1};
    Start prepared_{};
    SessionSelection selected_{};
    NativePrimaryGuidRoute selectedRoute_{};
    std::atomic<bool> poison_{};
    struct Watch {std::atomic<std::uintptr_t> connection{},handler{},network{};};
    std::array<Watch,Capacity> watches_{};
    std::atomic<std::uintptr_t> watchedManager_{};
    Serial Issue() noexcept {
        if(Faulted() || issued_==std::numeric_limits<Serial>::max()){Poison();return 0;}
        return ++issued_;
    }
    Record const* Find(std::uintptr_t address) const noexcept {
        if(!address)return nullptr;
        for(auto const& record:records_)if(record.connection.serial && record.connection.address==address)return &record;
        return nullptr;
    }
    Record* Find(std::uintptr_t address) noexcept {
        return const_cast<Record*>(static_cast<RemoteSessionLedger const*>(this)->Find(address));
    }
    static bool Matches(Record const* record,NativePrimaryGuidRoute const& route) noexcept {
        return record && record->handler && record->handler==route.handler &&
            record->binding==route.binding && record->identity==route.identity && route.activePeer;
    }
};
using RemoteSession=RemoteSessionLedger<>;
}
