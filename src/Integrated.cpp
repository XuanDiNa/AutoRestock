#include "AutoRestock/Integrated.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstring>
namespace autorestock {
template<std::size_t N> bool AtomicWords<N>::Store(std::array<std::uint64_t,N> const& in) noexcept {
    auto s=sequence_.load();
    if((s&1) || !sequence_.compare_exchange_strong(s,s+1)) return false;
    for(std::size_t i=0;i<N;++i) data_[i].store(in[i]);
    sequence_.store(s+2);return true;
}
template<std::size_t N> bool AtomicWords<N>::Load(std::array<std::uint64_t,N>& out) const noexcept {
    auto s=sequence_.load();if(s&1) return false;
    for(std::size_t i=0;i<N;++i) out[i]=data_[i].load();
    return sequence_.load()==s;
}
namespace {
using IdentityWords=std::array<std::uint64_t,5>;
bool Fresh(std::uint64_t now,std::uint64_t stamp){return now>=stamp && now-stamp<=500;}
bool SameLocal(IdentityWords const& s,ActorIdentity const& id){
    return id.Valid() && id.role==ActorRole::Local && s[0]==id.registry && s[1]==id.unique && s[2]==id.runtime;
}
bool Paired(IdentityWords const& l,ActorIdentity const& s,std::uint64_t now){
    return l[0] && Fresh(now,l[3]) && s.Valid() && s.role==ActorRole::Server &&
        l[0]!=s.registry && l[1]==s.unique && l[2]==s.runtime;
}
bool ArmedServer(std::array<std::uint64_t,7> const& a,ActorIdentity const& s){
    return a[0] && s.Valid() && s.role==ActorRole::Server && a[3]==s.registry && a[4]==s.unique && a[5]==s.runtime;
}
}
void IntegratedBridge::Local(ActorIdentity const& id,std::uint64_t now) noexcept {
    if(!id.Valid() || id.role!=ActorRole::Local){Invalidate();return;}
    IdentityWords old{};
    if(!local_.Load(old) || !SameLocal(old,id)) ++identityEpoch_;
    local_.Store({id.registry,id.unique,id.runtime,now,identityEpoch_});
}
void IntegratedBridge::Invalidate() noexcept {++identityEpoch_;local_.Store({});seen_.Store({});counts_.Store({});}
bool IntegratedBridge::Ready(ActorIdentity const& id,std::uint64_t now) const noexcept {
    IdentityWords l{},s{};
    return local_.Load(l) && seen_.Load(s) && SameLocal(l,id) && Fresh(now,l[3]) &&
        s[0] && s[0]!=l[0] && s[1]==l[1] && s[2]==l[2] && s[4]==l[4] && Fresh(now,s[3]);
}
PairingEvidence IntegratedBridge::Inspect(ActorIdentity const& id,std::uint64_t now) const noexcept {
    PairingEvidence e;e.localValid=id.Valid();e.localRole=static_cast<int>(id.role);
    IdentityWords l{},s{};std::array<std::uint64_t,10> c{};
    if(!local_.Load(l))return e;
    e.localStored=l[0]!=0;e.localMatches=SameLocal(l,id);e.localFresh=e.localStored && Fresh(now,l[3]);
    if(e.localStored && id.Valid()){
        e.candidateRegistryDistinct=l[0]!=id.registry;
        e.candidateUniqueMatches=l[1]==id.unique;e.candidateRuntimeMatches=l[2]==id.runtime;
    }
    if(seen_.Load(s) && s[0]){
        e.serverSeen=true;e.serverFresh=Fresh(now,s[3]);e.registryDistinct=s[0]!=l[0];
        e.uniqueMatches=e.localStored && s[1]==l[1];e.runtimeMatches=e.localStored && s[2]==l[2];
        e.epochMatches=e.localStored && s[4]==l[4];
    }
    if(counts_.Load(c) && c[0]){
        e.countsPresent=true;e.countsFresh=Fresh(now,c[3]);
        e.countsMatch=e.localStored && e.serverSeen && c[0]==s[0] && c[1]==l[1] && c[2]==l[2] && c[4]==l[4];
    }
    return e; // independently copied atomics; not an admission/acceptance snapshot
}
bool IntegratedBridge::Arm(ActorIdentity const& id,std::uint64_t now,std::int32_t nativeId,std::uint64_t policyId) noexcept {
    if(Pending() || nativeId>=-1 || (nativeId&1) || !policyId || !Ready(id,now) || nextToken_>=UINT32_MAX) return false;
    IdentityWords s{},l{};if(!seen_.Load(s) || !local_.Load(l) || !SameLocal(l,id) || s[4]!=l[4]) return false;
    auto token=++nextToken_;result_.store((token<<32)|UINT32_MAX);
    return armed_.Store({token,static_cast<std::uint32_t>(nativeId),policyId,s[0],id.unique,id.runtime,l[4]});
}
ServerTicket IntegratedBridge::BeginServer(ActorIdentity const& id,std::int32_t nativeId,std::uint64_t) const noexcept {
    std::array<std::uint64_t,7> a{};
    if(!armed_.Load(a) || !ArmedServer(a,id) || static_cast<std::int32_t>(a[1])!=nativeId) return {};
    return {a[0],a[2],nativeId};
}
void IntegratedBridge::EndServer(ActorIdentity const& id,ServerTicket t,int status,std::uint64_t now,
    std::optional<InventoryCounts> const& counts) noexcept {
    IdentityWords l{};
    if(local_.Load(l) && Paired(l,id,now)){
        seen_.Store({id.registry,id.unique,id.runtime,now,l[4]});
        std::array<std::uint64_t,10> words{};
        bool valid=counts.has_value();
        if(counts)for(auto count:*counts)if(count>64)valid=false;
        if(valid){
            words[0]=id.registry;words[1]=id.unique;words[2]=id.runtime;words[3]=now;words[4]=l[4];
            for(unsigned i=0;i<36;++i)words[5+i/8]|=std::uint64_t((*counts)[i])<<(8*(i%8));
        }
        counts_.Store(words); // unreadable/missing inventory invalidates an older sample
    }
    std::array<std::uint64_t,7> a{};
    if(!t.token || status<0 || !armed_.Load(a) || !ArmedServer(a,id) || a[0]!=t.token || a[2]!=t.policyId ||
        static_cast<std::int32_t>(a[1])!=t.nativeId) return;
    auto waiting=(t.token<<32)|UINT32_MAX;
    result_.compare_exchange_strong(waiting,(t.token<<32)|static_cast<std::uint32_t>(status));
}
std::optional<InventoryCounts> IntegratedBridge::AuthoritativeCounts(ActorIdentity const& id,std::uint64_t now) const noexcept {
    IdentityWords l{},s{};std::array<std::uint64_t,10> words{};
    if(!local_.Load(l) || !seen_.Load(s) || !counts_.Load(words) || !SameLocal(l,id) || !Fresh(now,l[3]) ||
        !s[0] || s[0]==l[0] || s[0]!=words[0] || words[1]!=l[1] || words[2]!=l[2] ||
        s[1]!=l[1] || s[2]!=l[2] || s[4]!=l[4] || words[4]!=l[4] || !Fresh(now,words[3]))return {};
    InventoryCounts out{};
    for(unsigned i=0;i<36;++i)out[i]=static_cast<std::uint8_t>(words[5+i/8]>>(8*(i%8)));
    return out;
}
std::optional<InventoryCounts> ReadInventoryCounts(NativeProfile const& profile,void const* player,std::uintptr_t base) noexcept {
    __try {
        if(!player || !base || FindNativeProfile(profile.version)!=&profile)return {};
        auto actor=static_cast<std::byte const*>(player);
        auto supplies=*reinterpret_cast<std::byte const* const*>(actor+profile.layout.playerSupplies);
        if(!supplies)return {};
        auto inventory=*reinterpret_cast<std::byte const* const*>(supplies+profile.layout.suppliesInventory);
        if(!inventory || *reinterpret_cast<std::uintptr_t const*>(inventory)!=base+profile.Rva(0xE6AFE10) ||
            std::to_integer<std::uint8_t>(inventory[8])!=255 ||
            *reinterpret_cast<void const* const*>(inventory+profile.layout.inventoryOwner)!=player)return {};
        auto first=*reinterpret_cast<std::uintptr_t const*>(inventory+profile.layout.inventoryFirst),last=*reinterpret_cast<std::uintptr_t const*>(inventory+profile.layout.inventoryLast);
        if(!first || first%8 || last<first || last-first!=36*profile.layout.stackSize)return {};
        InventoryCounts out{};
        for(unsigned i=0;i<36;++i){out[i]=*reinterpret_cast<std::uint8_t const*>(first+profile.layout.stackSize*i+profile.layout.count);if(out[i]>64)return {};}
        return out;
    }__except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<RequestCompletion> IntegratedBridge::Poll() const noexcept {
    std::array<std::uint64_t,7> a{};if(!armed_.Load(a) || !a[0]) return {};
    auto result=result_.load();auto status=static_cast<std::uint32_t>(result);
    if((result>>32)!=a[0] || status==UINT32_MAX) return {};
    return RequestCompletion{a[2],status==1?Completion::Accepted:Completion::Rejected,static_cast<std::int32_t>(a[1]),static_cast<int>(status)};
}
void IntegratedBridge::Clear() noexcept {armed_.Store({});result_.store(0);}
bool IntegratedBridge::Pending() const noexcept {std::array<std::uint64_t,7>a{};return !armed_.Load(a) || a[0]!=0;}
std::int32_t IntegratedBridge::PendingNativeId() const noexcept {
    std::array<std::uint64_t,7>a{};return armed_.Load(a)?static_cast<std::int32_t>(a[1]):0;
}
// Actor+16 is its registry object (19F2DE0/19F2AE0), not FakeSneak's
// external dispatch registryBase+48. No native getter/lazy creation is called.
namespace {
std::uintptr_t Pool(std::uintptr_t registry,std::uint32_t hash){
    auto buckets=*reinterpret_cast<std::uintptr_t const*>(registry+72),end=*reinterpret_cast<std::uintptr_t const*>(registry+80);
    auto nodes=*reinterpret_cast<std::uintptr_t const*>(registry+104),last=*reinterpret_cast<std::uintptr_t const*>(registry+112);
    if(!buckets || !nodes || end<=buckets || last<nodes || (end-buckets)%8 || (last-nodes)%32) return 0;
    auto n=(end-buckets)/8,count=(last-nodes)/32;
    if(n>(1u<<20) || (n&(n-1)) || count>(1u<<20)) return 0;
    auto index=reinterpret_cast<std::uint64_t const*>(buckets)[hash&(n-1)];
    for(unsigned chain=0;index!=UINT64_MAX && chain<128;++chain){
        if(index>=count) return 0;auto node=nodes+32*index;
        if(*reinterpret_cast<std::uint32_t const*>(node+8)==hash) return *reinterpret_cast<std::uintptr_t const*>(node+16);
        index=*reinterpret_cast<std::uint64_t const*>(node);
    }return 0;
}
bool Member(std::uintptr_t pool,std::uint32_t entity,std::uint32_t& packed){
    if(!pool) return false;
    auto sparse=*reinterpret_cast<std::uintptr_t const*>(pool+8),end=*reinterpret_cast<std::uintptr_t const*>(pool+16);
    auto index=(entity&0x3FFFFu)>>11;
    if(!sparse || end<sparse || end-sparse>128*8 || (end-sparse)%8 || index>=(end-sparse)/8) return false;
    auto page=reinterpret_cast<std::uintptr_t const*>(sparse)[index];if(!page) return false;
    packed=reinterpret_cast<std::uint32_t const*>(page)[entity&0x7FFu];
    return (packed^(entity&0xFFFC0000u))<=0x3FFFEu;
}
bool Id(std::uintptr_t registry,std::uint32_t entity,std::uint32_t hash,std::uint64_t& value){
    auto pool=Pool(registry,hash);std::uint32_t packed{};if(!Member(pool,entity,packed)) return false;
    auto pages=*reinterpret_cast<std::uintptr_t const*>(pool+80);if(!pages) return false;
    auto page=reinterpret_cast<std::uintptr_t const*>(pages)[(packed&0x3FFFFu)>>7];if(!page) return false;
    value=reinterpret_cast<std::uint64_t const*>(page)[packed&0x7Fu];return true;
}
}
ActorIdentity ReadActorIdentity(void const* actor) noexcept {
    __try {
        if(!actor) return {};
        auto p=static_cast<std::byte const*>(actor);ActorIdentity result;
        std::memcpy(&result.registry,p+16,8);std::uint32_t entity{},packed{};std::memcpy(&entity,p+24,4);
        if(!result.registry) return {};
        if(Member(Pool(result.registry,0xACB47B38),entity,packed)) result.role=ActorRole::Local;
        else if(Member(Pool(result.registry,0x122C9226),entity,packed)) result.role=ActorRole::Server;
        else return {};
        if(!Id(result.registry,entity,0x18F957AF,result.unique) || !Id(result.registry,entity,0xFC0DBBB5,result.runtime)) return {};
        return result.Valid()?result:ActorIdentity{};
    }__except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
}
