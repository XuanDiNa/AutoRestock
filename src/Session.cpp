#include "AutoRestock/Session.h"
#include <limits>
namespace autorestock {
bool SessionSelector::Advance() noexcept {
    if(exhausted_ || generation_==std::numeric_limits<std::uint64_t>::max()){
        exhausted_=true;Clear();return false;
    }
    ++generation_;return true;
}
void SessionSelector::Clear() noexcept {
    ticket_=connection_=0;kind_=ConnectionKind::Unknown;
    mode_=InventoryMode::Unknown;key_={};started_=false;
}
std::uint64_t SessionSelector::Connect(std::uint64_t connection,ConnectionKind kind) noexcept {
    Clear();if(!Advance() || !connection)return 0;
    connection_=connection;kind_=kind;ticket_=generation_;return ticket_;
}
bool SessionSelector::StartGame(std::uint64_t ticket,SessionKey key,InventoryMode mode) noexcept {
    if(exhausted_ || !ticket || ticket!=ticket_)return false;
    if(started_ || key.connection!=connection_ || !key.connection || !key.world || !key.player || !key.manager){
        Invalidate();return false;
    }
    if(!Advance())return false;
    key_=key;mode_=mode;started_=true;return true;
}
void SessionSelector::Retire(std::uint64_t manager) noexcept {
    if(started_ && manager && key_.manager==manager)Invalidate();
}
void SessionSelector::Invalidate() noexcept {Advance();Clear();}
SessionSelection SessionSelector::Select(SessionKey const& key,InventoryMode mode,
    bool paired,bool modernReady) const noexcept {
    SessionSelection result{generation_,key_,mode_,Backend::Unsupported};
    if(exhausted_ || !started_ || key!=key_ || mode!=mode_ ||
        (mode_!=InventoryMode::Modern && mode_!=InventoryMode::Ordinary))return result;
    if(kind_==ConnectionKind::Integrated && paired)result.backend=Backend::Integrated;
    else if(kind_==ConnectionKind::Remote && !paired && modernReady && mode_==InventoryMode::Modern)
        result.backend=Backend::RemoteModern;
    return result;
}
}
