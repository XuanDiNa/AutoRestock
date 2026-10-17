#include "AutoRestock/Transaction.h"
#include <cstring>
#include <limits>
namespace autorestock {
namespace {
template<class T> bool Read(void const* p,std::size_t offset,T& out) noexcept {
    auto a=reinterpret_cast<std::uintptr_t>(p);
    if (!a || a>std::numeric_limits<std::uintptr_t>::max()-offset-sizeof(T)) return false;
    __try {std::memcpy(&out,reinterpret_cast<void const*>(a+offset),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> T Value(void const* p,std::size_t offset) noexcept {T out{};Read(p,offset,out);return out;}
template<class T> void Put(void* p,std::size_t o,T x){std::memcpy(static_cast<std::byte*>(p)+o,&x,sizeof x);}
bool SameSnapshot(StackApi const& api,void const* a,void const* b){
    if (!a || !b || Value<std::uint8_t>(a,34)!=Value<std::uint8_t>(b,34)) return false;
    return api.empty(a) ? api.empty(b) : !api.empty(b) && SameNativeItem(api,a,b) && api.capacity(a)==api.capacity(b);
}
struct alignas(8) SlotInfo {std::array<std::byte,40> bytes{};};
// Reconstruct the three proven value-only alternatives, as 33F5EB0/33F6460
// and 25B67E0 do. These descriptors own no native allocation. No shallow copy
// of an owning native variant or game stack is performed.
bool Describe(NativeProfile const& profile,std::uintptr_t base,void const* stack,int index,SlotInfo& out){
    auto kind=Value<std::uint8_t>(stack,144);
    if(kind>2 && !(kind==255 && Value<std::uint8_t>(stack,34)==0)) return false;
    Put(&out,0,static_cast<std::uint8_t>(index>=9?29:28));
    Put(&out,12,static_cast<std::uint8_t>(index));Put(&out,32,kind);
    if(kind==0) Put(&out,16,Value<std::int32_t>(stack,128));
    else if(kind<=2) {
        auto table=base+profile.Rva(kind==1?0xE827E10:0xE827E30);
        if(Value<std::uintptr_t>(stack,128)!=table) return false;
        Put(&out,16,table);Put(&out,24,Value<std::int32_t>(stack,136));
    }
    return true;
}
bool SameID(SlotInfo const& a,SlotInfo const& b){return std::memcmp(a.bytes.data()+16,b.bytes.data()+16,24)==0;}
struct ActionOwner {
    NativeApi const& api;void* value{};
    ~ActionOwner(){if(value) api.destroyPlace(value,1);}
    void Consume(void* manager){
        // Native add owns success and refusal. Refusal frees without zeroing
        // the argument: drop our ownership BEFORE crossing that ABI.
        auto transferred=value;value=nullptr;api.add(manager,&transferred);
    }
};
bool Registered(void* component,std::int32_t id,void* wrapper){
    auto head=Value<void*>(component,72);auto count=Value<std::size_t>(component,80);
    if(!head || !count || count>4096) return false;
    auto node=Value<void*>(head,0);
    for(std::size_t i=0;i<count && node && node!=head;++i){
        if(Value<std::int32_t>(node,24)==id){
            auto first=Value<std::uintptr_t>(node,32),last=Value<std::uintptr_t>(node,40);
            if(!first || first%8 || last<first || (last-first)%8 || last-first>16*8) return false;
            for(auto p=first;p<last;p+=8){
                auto adapter=Value<void*>(reinterpret_cast<void*>(p),0);
                if(Value<void*>(adapter,8)==wrapper) return true;
            }
            return false;
        }
        node=Value<void*>(node,0);
    }
    return false;
}
bool Queued(void* manager,void* request){
    if(Value<void*>(manager,96)) return false;
    auto queue=Value<void*>(manager,104);
    auto first=Value<std::uintptr_t>(queue,0),last=Value<std::uintptr_t>(queue,8);
    if(!first || first%8 || last<first || (last-first)%8 || last-first>4096*8) return false;
    for(auto p=first;p<last;p+=8) if(Value<void*>(reinterpret_cast<void*>(p),0)==request) return true;
    return false;
}
struct LegacyScope {
    LegacyApi const& api;alignas(8) std::array<std::byte,72> bytes{};bool active{};
    ~LegacyScope(){if(active)api.endScope(bytes.data());}
};
bool NativeIdle(NativeProfile const& profile,std::uintptr_t base,void const* player){
    void *owner{},*pending{},*table{};std::uintptr_t first{},last{},capacity{};
    // 20BA30 calls the manager embedded at player+2672. 266FD10 owns its
    // map+8 and dedup vector+16..32; never merge or clear another operation.
    if(!Read(player,2672,owner) || owner!=player || !Read(player,2680,pending) || pending ||
        !Read(player,2688,first) || !Read(player,2696,last) || !Read(player,2704,capacity) ||
        first!=last || capacity<last || (capacity-first)%512 || capacity-first>4096*512 ||
        (!first && capacity) || first%8 || !Read(player,0,table))return false;
    // IDA table-E8E22C0: balanced auto-flush -> normal complex -> packet send.
    return Value<std::uintptr_t>(table,profile.layout.sendAuto)==base+profile.Rva(0x476A510) &&
        Value<std::uintptr_t>(table,profile.layout.sendNormal)==base+profile.Rva(0x476AA30) && Value<std::uintptr_t>(table,profile.layout.sendPacket)==base+profile.Rva(0x476ABD0);
}
bool ActualInventory(NativeProfile const& profile,std::uintptr_t base,WorldInventoryView const& view){
    auto first=Value<std::uintptr_t>(view.inventory,408),last=Value<std::uintptr_t>(view.inventory,416);
    return Value<std::uintptr_t>(view.inventory,0)==base+profile.Rva(0xE6AFE10) &&
        Value<std::uint8_t>(view.inventory,8)==255 && Value<void*>(view.inventory,432)==view.player &&
        first && first%8==0 && last>=first && last-first==36*152;
}
// Read-only inspection of the native legacy modified-slot map. This is NOT
// Registered(): that helper deliberately inspects a different, modern map.
bool LegacySlot(void const* context,int slot){
    auto head=Value<void*>(context,8);auto count=Value<std::size_t>(context,16);
    if(!head || !count || count>16) return false;
    auto node=Value<void*>(head,0);
    for(std::size_t i=0;i<count && node && node!=head;++i,node=Value<void*>(node,0)){
        if(Value<std::uint8_t>(node,16)!=29)continue;
        auto slots=Value<void*>(node,32);auto size=Value<std::size_t>(node,40);
        if(!slots || !size || size>36)return false;
        auto entry=Value<void*>(slots,0);
        for(std::size_t n=0;n<size && entry && entry!=slots;++n,entry=Value<void*>(entry,0))
            if(Value<std::uint8_t>(entry,16)==slot)return true;
        return false;
    }
    return false;
}
bool ActualSnapshot(NativeApi const& api,std::uintptr_t base,void const* inventory,int slot,void const* expected){
    auto actual=api.legacy.inventoryItem(inventory,slot);
    SlotInfo a,b;
    return SameSnapshot(api.stack,actual,expected) && Describe(*api.profile,base,actual,slot,a) &&
        Describe(*api.profile,base,expected,slot,b) && SameID(a,b);
}
bool LegacyAfter(NativeApi const& api,std::uintptr_t base,void const* context,void const* stack,int slot,std::int32_t id){
    return api.stack.empty(stack) || (Value<std::uint8_t>(stack,144)==2 &&
        Value<std::uintptr_t>(stack,128)==base+api.profile->Rva(0xE827E30) && Value<std::int32_t>(stack,136)==id && LegacySlot(context,slot));
}
}
TransferResult ExecuteOrdinaryTransfer(NativeApi const& api,std::uintptr_t base,void* manager,void* player,Transfer const& t,
    void const* expectedSource,void const* expectedDestination){
    if(!api.profile || FindNativeProfile(api.profile->version)!=api.profile)return TransferResult::Deferred;
    if(t.source<9 || t.source>35 || t.destination<0 || t.destination>8 || t.amount<1 || t.amount>64 || !t.requestId)
        return TransferResult::Rejected;
    auto view=ResolveOrdinaryInventory(api,base,manager,player);
    if(!view || !NativeIdle(*api.profile,base,player))return TransferResult::Deferred;
    auto supplies=Value<void*>(player,1464);
    if(Value<int>(supplies,16)!=t.destination)return TransferResult::Rejected;
    auto source=api.legacy.inventoryItem(view->inventory,t.source),destination=api.legacy.inventoryItem(view->inventory,t.destination);
    if(!ActualSnapshot(api,base,view->inventory,t.source,expectedSource) ||
        !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination) || api.stack.empty(source))return TransferResult::Rejected;
    auto sc=Value<std::uint8_t>(source,34),dc=Value<std::uint8_t>(destination,34),capacity=api.stack.capacity(source);
    if(!capacity || capacity>64 || sc>capacity || t.amount>sc || dc+t.amount>capacity ||
        (!api.stack.empty(destination) && !SameNativeItem(api.stack,source,destination)))return TransferResult::Rejected;
    OwnedStack remainder,filled;
    if(!remainder.Copy(api.stack,source) || !filled.Copy(api.stack,api.stack.empty(destination)?source:destination) ||
        !remainder.SetCount(sc-t.amount) || !filled.SetCount(dc+t.amount))return TransferResult::Rejected;
    auto current=ResolveOrdinaryInventory(api,base,manager,player);
    if(!current || current->inventory!=view->inventory || !NativeIdle(*api.profile,base,player))return TransferResult::Deferred;
    if(Value<void*>(player,1464)!=supplies || Value<int>(supplies,16)!=t.destination ||
        !ActualSnapshot(api,base,view->inventory,t.source,expectedSource) ||
        !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination))return TransferResult::Rejected;
    // mode0, no legacy scope: the two ACTUAL setters record a balanced pair
    // through 20BA30/266FD10 and native auto-send once. manager+80 remains0.
    // No plugin-created packet, ID, manual send or borrowed-stack count edit.
    api.legacy.setInventory(view->inventory,t.source,remainder.Get(),false);
    api.legacy.setInventory(view->inventory,t.destination,filled.Get(),false);
    bool valid=NativeIdle(*api.profile,base,player) && ResolveOrdinaryInventory(api,base,manager,player).has_value() &&
        SameSnapshot(api.stack,api.legacy.inventoryItem(view->inventory,t.source),remainder.Get()) &&
        SameSnapshot(api.stack,api.legacy.inventoryItem(view->inventory,t.destination),filled.Get());
    return valid?TransferResult::Queued:TransferResult::Fault;
}
TransferResult ExecuteIntegratedTransfer(NativeApi const& api,std::uintptr_t base,void* manager,void* player,Transfer const& t,
    void const* expectedSource,void const* expectedDestination,IntegratedBridge& bridge,ActorIdentity const& identity,std::uint64_t now){
    if(!api.legacy.Complete() || bridge.Pending() || !bridge.Ready(identity,now)) return TransferResult::Deferred;
    if(t.source<9 || t.source>35 || t.destination<0 || t.destination>8 || t.amount<1 || t.amount>64 || !t.requestId) return TransferResult::Rejected;
    auto view=ResolveWorldInventory(api,base,manager,player);
    if(!view || !ActualInventory(*api.profile,base,*view) || !NativeIdle(*api.profile,base,player) || !api.canBegin(manager) || Value<int>(manager,80)!=0 || Value<void*>(manager,104)) return TransferResult::Deferred;
    auto supplies=Value<void*>(player,1464);
    if(Value<int>(supplies,16)!=t.destination) return TransferResult::Rejected;
    auto source=api.getItem(view->wrapper,t.source),destination=api.getItem(view->wrapper,t.destination);
    if(!SameSnapshot(api.stack,source,expectedSource) || !SameSnapshot(api.stack,destination,expectedDestination) || api.stack.empty(source) ||
        !ActualSnapshot(api,base,view->inventory,t.source,expectedSource) || !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination)) return TransferResult::Rejected;
    SlotInfo si,di,es,ed;
    if(!Describe(*api.profile,base,source,t.source,si) || !Describe(*api.profile,base,destination,t.destination,di) ||
        !Describe(*api.profile,base,expectedSource,t.source,es) || !Describe(*api.profile,base,expectedDestination,t.destination,ed) || !SameID(si,es) || !SameID(di,ed)) return TransferResult::Rejected;
    auto sc=Value<std::uint8_t>(source,34),dc=Value<std::uint8_t>(destination,34),capacity=api.stack.capacity(source);
    if(!capacity || capacity>64 || sc>capacity || t.amount>sc || dc+t.amount>capacity ||
        (!api.stack.empty(destination) && !SameNativeItem(api.stack,source,destination))) return TransferResult::Rejected;
    auto synchronized=[&]{
        auto counts=bridge.AuthoritativeCounts(identity,now);
        return counts && (*counts)[t.source]==sc && (*counts)[t.destination]==dc;
    };
    if(!synchronized())return TransferResult::Deferred;
    OwnedStack remainder,filled;
    if(!remainder.Copy(api.stack,source) || !filled.Copy(api.stack,api.stack.empty(destination)?source:destination) ||
        !remainder.SetCount(sc-t.amount) || !filled.SetCount(dc+t.amount)) return TransferResult::Rejected;
    auto current=ResolveWorldInventory(api,base,manager,player);
    if(!current || current->wrapper!=view->wrapper || current->component!=view->component || current->inventory!=view->inventory || !ActualInventory(*api.profile,base,*current) ||
        !NativeIdle(*api.profile,base,player) || !api.canBegin(manager) || Value<int>(manager,80) || Value<void*>(manager,104)) return TransferResult::Deferred;
    SlotInfo latestS,latestD;
    source=api.getItem(view->wrapper,t.source);destination=api.getItem(view->wrapper,t.destination);
    if(Value<int>(supplies,16)!=t.destination || !SameSnapshot(api.stack,source,expectedSource) || !SameSnapshot(api.stack,destination,expectedDestination) ||
        !Describe(*api.profile,base,source,t.source,latestS) || !Describe(*api.profile,base,destination,t.destination,latestD) || !SameID(si,latestS) || !SameID(di,latestD) ||
        !ActualSnapshot(api,base,view->inventory,t.source,expectedSource) || !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination)) return TransferResult::Rejected;
    if(!synchronized())return TransferResult::Deferred;
    LegacyScope scope{api.legacy};api.legacy.beginScope(manager,scope.bytes.data());
    auto id=Value<std::int32_t>(manager,80);
    scope.active=Value<std::uintptr_t>(scope.bytes.data(),0)==base+api.profile->Rva(0xE828410) &&
        Value<void*>(scope.bytes.data(),8)==manager && Value<void*>(scope.bytes.data(),16)==view->component &&
        Value<void*>(scope.bytes.data(),56)==scope.bytes.data() && Value<std::uint8_t>(scope.bytes.data(),64)==1;
    if(!scope.active || id>=-1 || (id&1)) return TransferResult::Fault;
    if(!bridge.Arm(identity,now,id,t.requestId)) return TransferResult::Deferred;
    // Actual setter creates native kind2 IDs/bookkeeping and records its own
    // action via 20BA30 -> 266FD10. First action remains unbalanced, second
    // balances and auto-sends BEFORE the second slot copy. Arm first and keep
    // the scope alive. Never construct/send an additional manual transaction.
    api.legacy.setInventory(view->inventory,t.source,remainder.Get(),false);
    api.legacy.setInventory(view->inventory,t.destination,filled.Get(),false);
    auto actualSource=api.legacy.inventoryItem(view->inventory,t.source);
    auto actualDestination=api.legacy.inventoryItem(view->inventory,t.destination);
    bool valid=NativeIdle(*api.profile,base,player) && SameSnapshot(api.stack,actualSource,remainder.Get()) && SameSnapshot(api.stack,actualDestination,filled.Get()) &&
        LegacyAfter(api,base,view->component,actualSource,t.source,id) && LegacyAfter(api,base,view->component,actualDestination,t.destination,id) &&
        SameSnapshot(api.stack,api.getItem(view->wrapper,t.source),remainder.Get()) &&
        SameSnapshot(api.stack,api.getItem(view->wrapper,t.destination),filled.Get());
    // On a postcondition failure, retain the bridge fence and stop. Native
    // ownership/reconciliation remains untouched: no clear, rollback or resend.
    return valid?TransferResult::Queued:TransferResult::Fault;
}
TransferResult ExecuteTransfer(NativeApi const& api,std::uintptr_t base,void* manager,void* player,Transfer const& t,
    void const* expectedSource,void const* expectedDestination,ResponseTracker& tracker) {
    if(!api.profile || FindNativeProfile(api.profile->version)!=api.profile)return TransferResult::Deferred;
    if(tracker.Pending()) return TransferResult::Deferred;
    if(t.source<9 || t.source>35 || t.destination<0 || t.destination>8 || t.amount<1 || t.amount>64 || !t.requestId)
        return TransferResult::Rejected;
    auto view=ResolveWorldInventory(api,base,manager,player);
    if(!view || !api.legacy.inventoryItem || !ActualInventory(*api.profile,base,*view) ||
        !api.canBegin(manager) || Value<int>(manager,80) || Value<void*>(manager,104)) return TransferResult::Deferred;
    auto supplies=Value<void*>(player,1464);
    if(Value<int>(supplies,16)!=t.destination) return TransferResult::Rejected;
    auto source=api.getItem(view->wrapper,t.source),destination=api.getItem(view->wrapper,t.destination);
    if(!SameSnapshot(api.stack,source,expectedSource) || !SameSnapshot(api.stack,destination,expectedDestination) || api.stack.empty(source))
        return TransferResult::Rejected;
    SlotInfo sourceInfo,destinationInfo,expectedSourceInfo,expectedDestinationInfo;
    if(!Describe(*api.profile,base,source,t.source,sourceInfo) || !Describe(*api.profile,base,destination,t.destination,destinationInfo) ||
        !Describe(*api.profile,base,expectedSource,t.source,expectedSourceInfo) || !Describe(*api.profile,base,expectedDestination,t.destination,expectedDestinationInfo) ||
        !SameID(sourceInfo,expectedSourceInfo) || !SameID(destinationInfo,expectedDestinationInfo)) return TransferResult::Rejected;
    auto sourceCount=Value<std::uint8_t>(source,34),destinationCount=Value<std::uint8_t>(destination,34);
    auto capacity=api.stack.capacity(source);
    if(capacity<1 || capacity>64 || sourceCount>capacity || t.amount>sourceCount || destinationCount+t.amount>capacity ||
        (!api.stack.empty(destination) && !SameNativeItem(api.stack,source,destination))) return TransferResult::Rejected;
    // The default-world wrapper may still cache an earlier prediction. Real
    // inventory disagreement is temporary unavailability, not permission to
    // predict another move and not a reason to consume the original use intent.
    if(!ActualSnapshot(api,base,view->inventory,t.source,expectedSource) ||
        !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination)) return TransferResult::Deferred;
    OwnedStack remainder,filled;
    if(!remainder.Copy(api.stack,source) || !filled.Copy(api.stack,api.stack.empty(destination)?source:destination) ||
        !remainder.SetCount(sourceCount-t.amount) || !filled.SetCount(destinationCount+t.amount)) return TransferResult::Rejected;
    ActionOwner action{api};auto amount=static_cast<std::uint8_t>(t.amount);
    api.place(&action.value,&amount,&sourceInfo,&destinationInfo);
    if(!action.value) return TransferResult::Rejected;
    auto currentView=ResolveWorldInventory(api,base,manager,player);
    if(!currentView || currentView->wrapper!=view->wrapper || currentView->component!=view->component ||
        currentView->inventory!=view->inventory || !ActualInventory(*api.profile,base,*currentView) ||
        !api.canBegin(manager) || Value<int>(manager,80) || Value<void*>(manager,104))
        return TransferResult::Deferred;
    auto currentSource=api.getItem(view->wrapper,t.source),currentDestination=api.getItem(view->wrapper,t.destination);
    SlotInfo latestSourceInfo,latestDestinationInfo;
    if(Value<int>(supplies,16)!=t.destination || !SameSnapshot(api.stack,currentSource,expectedSource) ||
        !SameSnapshot(api.stack,currentDestination,expectedDestination) ||
        !Describe(*api.profile,base,currentSource,t.source,latestSourceInfo) || !Describe(*api.profile,base,currentDestination,t.destination,latestDestinationInfo) ||
        !SameID(sourceInfo,latestSourceInfo) || !SameID(destinationInfo,latestDestinationInfo)) return TransferResult::Rejected;
    if(!ActualSnapshot(api,base,view->inventory,t.source,expectedSource) ||
        !ActualSnapshot(api,base,view->inventory,t.destination,expectedDestination)) return TransferResult::Deferred;
    api.begin(manager,false);
    auto request=Value<void*>(manager,96);auto nativeId=Value<std::int32_t>(request,8);
    if(!request || Value<std::uintptr_t>(request,0)!=base+api.profile->Rva(0xE827E10) || nativeId>-3 || (nativeId&1)==0)
        return TransferResult::Fault;
    if(!tracker.Begin(reinterpret_cast<std::uintptr_t>(manager),nativeId,t.requestId)) {
        EndNativeRequest(api,manager);return TransferResult::Rejected;
    }
    // Type1 is always allowed by 25AB8E0. Add before prediction has no slot
    // side effects, allowing refusal detection before changing either slot.
    auto first=Value<std::uintptr_t>(request,48),last=Value<std::uintptr_t>(request,56);
    auto actionAddress=action.value;
    action.Consume(manager);
    auto addedFirst=Value<std::uintptr_t>(request,48),addedLast=Value<std::uintptr_t>(request,56);
    if(first!=last || !addedFirst || addedFirst%8 || addedLast<addedFirst || addedLast-addedFirst!=8 ||
        Value<void*>(reinterpret_cast<void*>(addedFirst),0)!=actionAddress){
        EndNativeRequest(api,manager);tracker.Reset();return TransferResult::Rejected;
    }
    api.setItem(view->wrapper,t.source,remainder.Get());
    api.setItem(view->wrapper,t.destination,filled.Get());
    bool registered=Registered(view->component,nativeId,view->wrapper);
    auto predictedSource=api.getItem(view->wrapper,t.source),predictedDestination=api.getItem(view->wrapper,t.destination);
    bool progress=SameSnapshot(api.stack,predictedSource,remainder.Get()) && SameSnapshot(api.stack,predictedDestination,filled.Get());
    // Complete an accepted request even when verification fails. Native server
    // reconciliation must remain intact; the caller disables further work.
    EndNativeRequest(api,manager);
    return registered && progress && Queued(manager,request) ? TransferResult::Queued : TransferResult::Fault;
}
}
