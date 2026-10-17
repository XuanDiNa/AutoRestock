#include "AutoRestock/Session.h"
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
constexpr SessionKey key{11,22,33,44};

int main(){try{
    // Selecting modern merely because integratedReady=false is a bug. Neither
    // missing evidence nor a joining connection permits native submission.
    {
        SessionSelector selector;
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
        auto ticket=selector.Connect(11,ConnectionKind::Remote);CHECK(ticket!=0);
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
    }
    // All expected routes are independently specified. Enabling ordinary
    // remote or ignoring either evidence gate must break these assertions.
    struct Case {ConnectionKind kind;InventoryMode mode;bool paired,contract;Backend expected;};
    constexpr Case cases[]{
        {ConnectionKind::Unknown,InventoryMode::Modern,false,true,Backend::Unsupported},
        {ConnectionKind::Integrated,InventoryMode::Modern,true,false,Backend::Integrated},
        {ConnectionKind::Integrated,InventoryMode::Ordinary,true,false,Backend::Integrated},
        {ConnectionKind::Integrated,InventoryMode::Modern,false,true,Backend::Unsupported},
        {ConnectionKind::Remote,InventoryMode::Modern,false,true,Backend::RemoteModern},
        {ConnectionKind::Remote,InventoryMode::Modern,false,false,Backend::Unsupported},
        {ConnectionKind::Remote,InventoryMode::Modern,true,true,Backend::Unsupported},
        {ConnectionKind::Remote,InventoryMode::Ordinary,false,true,Backend::Unsupported},
        {ConnectionKind::Remote,InventoryMode::Unknown,false,true,Backend::Unsupported},
    };
    for(auto c:cases){
        SessionSelector selector;auto ticket=selector.Connect(11,c.kind);
        CHECK(selector.StartGame(ticket,key,c.mode));
        auto result=selector.Select(key,c.mode,c.paired,c.contract);
        CHECK(result.backend==c.expected && result.key==key && result.generation>ticket && result.mode==c.mode);
    }
    // Dropping any identity or current-mode check incorrectly binds a new
    // world/player/manager to a previous session's negotiated capability.
    {
        SessionSelector selector;auto ticket=selector.Connect(11,ConnectionKind::Remote);
        CHECK(selector.StartGame(ticket,key,InventoryMode::Modern));
        for(auto other:{SessionKey{12,22,33,44},SessionKey{11,23,33,44},SessionKey{11,22,34,44},SessionKey{11,22,33,45}})
            CHECK(selector.Select(other,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
        CHECK(selector.Select(key,InventoryMode::Ordinary,false,true).backend==Backend::Unsupported);
        CHECK(selector.Select(key,InventoryMode::Unknown,false,true).backend==Backend::Unsupported);
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::RemoteModern);
    }
    // Scalar addresses may be reused. Only a fresh connection ticket plus a
    // new StartGame observation can make the replacement session eligible.
    {
        SessionSelector selector;auto old=selector.Connect(11,ConnectionKind::Remote);
        CHECK(selector.StartGame(old,key,InventoryMode::Modern));
        auto generation=selector.Select(key,InventoryMode::Modern,false,true).generation;
        auto fresh=selector.Connect(11,ConnectionKind::Remote);CHECK(fresh>generation);
        CHECK(!selector.StartGame(old,key,InventoryMode::Modern));
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
        CHECK(selector.StartGame(fresh,key,InventoryMode::Ordinary));
        CHECK(selector.Select(key,InventoryMode::Ordinary,false,true).backend==Backend::Unsupported);
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
    }
    // Retiring an unrelated manager is irrelevant. Retiring the matching
    // manager permanently removes this generation, even if its address returns.
    {
        SessionSelector selector;auto ticket=selector.Connect(11,ConnectionKind::Remote);
        CHECK(selector.StartGame(ticket,key,InventoryMode::Modern));
        auto generation=selector.Select(key,InventoryMode::Modern,false,true).generation;
        selector.Retire(45);CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::RemoteModern);
        selector.Retire(44);auto result=selector.Select(key,InventoryMode::Modern,false,true);
        CHECK(result.backend==Backend::Unsupported && result.generation>generation);
        CHECK(!selector.StartGame(ticket,key,InventoryMode::Modern));
    }
    // A second StartGame in one connection is not allowed to retag pending
    // operations. Require fresh native lifecycle evidence after ambiguity.
    {
        SessionSelector selector;auto ticket=selector.Connect(11,ConnectionKind::Remote);
        CHECK(selector.StartGame(ticket,key,InventoryMode::Modern));
        auto generation=selector.Select(key,InventoryMode::Modern,false,true).generation;
        CHECK(!selector.StartGame(ticket,key,InventoryMode::Ordinary));
        auto result=selector.Select(key,InventoryMode::Modern,false,true);
        CHECK(result.backend==Backend::Unsupported && result.generation>generation);
        CHECK(!selector.StartGame(ticket,key,InventoryMode::Modern));
    }
    // Invalid observations cannot be repaired by opportunistically calling
    // StartGame again; no probe, fallback or implicit connection is created.
    for(auto invalid:{SessionKey{0,22,33,44},SessionKey{11,0,33,44},SessionKey{11,22,0,44},SessionKey{11,22,33,0},SessionKey{12,22,33,44}}){
        SessionSelector selector;auto ticket=selector.Connect(11,ConnectionKind::Remote);
        CHECK(!selector.StartGame(ticket,invalid,InventoryMode::Modern));
        CHECK(!selector.StartGame(ticket,key,InventoryMode::Modern));
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
    }
    {
        SessionSelector selector;CHECK(selector.Connect(0,ConnectionKind::Remote)==0);
        CHECK(!selector.StartGame(0,key,InventoryMode::Modern));
        auto ticket=selector.Connect(11,ConnectionKind::Remote);
        CHECK(selector.StartGame(ticket,key,InventoryMode::Modern));selector.Invalidate();
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::Unsupported);
        CHECK(!selector.StartGame(ticket,key,InventoryMode::Modern));
        auto fresh=selector.Connect(11,ConnectionKind::Remote);CHECK(fresh>ticket);
        CHECK(selector.StartGame(fresh,key,InventoryMode::Modern));
        CHECK(selector.Select(key,InventoryMode::Modern,false,true).backend==Backend::RemoteModern);
    }
    std::cout<<"PASS: explicit capability selection, no ordinary fallback, contradiction gates, lifecycle generations, stale/reused identities\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
