#pragma once
// Only the external ABI fixture is shared; Runtime/Policy/Transaction are real.
#define AUTORESTOCK_RUNTIME_FIXTURE_ONLY
#include "RuntimeTests.cpp"
#undef AUTORESTOCK_RUNTIME_FIXTURE_ONLY

struct OrdinaryFixture : Fixture {
    std::array<std::uintptr_t,229> playerTable{};
    std::array<std::byte,88> nativeMap{};
    int balance{},sends{},changedSlots{},setterCalls{};
    bool keepPending{};
};
OrdinaryFixture* ordinaryFixture;
int ordinaryCopies{},ordinaryBusyAt{};
void PrepareOrdinary(OrdinaryFixture& x){
    Prepare(x);ordinaryFixture=&x;ordinaryCopies=ordinaryBusyAt=0;
    Put(x.manager,8,std::uint8_t{});Put(x.manager,56,std::uintptr_t{});
    Put(x.player,0,x.playerTable.data());Put(x.player,2672,static_cast<void*>(x.player));
    x.playerTable[1808/8]=base+0x476A510;x.playerTable[1816/8]=base+0x476AA30;
    x.playerTable[1824/8]=base+0x476ABD0;
    x.actualSlots[9]=x.source;x.actualSlots[2]=x.destination;
    if(x.useSecond)x.actualSlots[10]=x.secondSource;
}
void const* OrdinaryGet(void const* inventory,int slot){
    CHECK(inventory==f->inventory && slot>=0 && slot<36);return &f->actualSlots[slot];
}
void OrdinarySet(void* inventory,int slot,void const* stack,bool network){
    CHECK(inventory==f->inventory && !network && slot>=0 && slot<36);
    CHECK(Read<int>(f->manager,80)==0 && !Read<void*>(f->manager,96));
    CHECK(std::find(f->owned.begin(),f->owned.end(),const_cast<void*>(stack))!=f->owned.end());
    auto& x=*ordinaryFixture;auto before=f->actualSlots[slot];
    // Original recorder runs BEFORE copying, and sends only when balanced.
    Put(f->player,2680,static_cast<void*>(x.nativeMap.data()));
    x.balance+=int(Read<std::uint8_t>(stack,34))-Read<std::uint8_t>(&before,34);
    ++x.setterCalls;++x.changedSlots;
    if(x.balance==0 && !x.keepPending){
        CHECK(x.changedSlots==2 && x.setterCalls%2==0);
        ++x.sends;x.changedSlots=0;Put(f->player,2680,std::uintptr_t{});
    }
    // Copies preserve normal IDs. No scope, kind2 assignment or modern map.
    f->actualSlots[slot]=*static_cast<StackStorage const*>(stack);
}
void* OrdinaryCopyBusy(void* output,void const* borrowed){
    Copy(output,borrowed);Put(f->player,2680,static_cast<void*>(ordinaryFixture->nativeMap.data()));return output;
}
void* OrdinaryCopyStale(void* output,void const* borrowed){
    Copy(output,borrowed);Put(&f->actualSlots[9],34,std::uint8_t{19});return output;
}
void* OrdinaryCopyTransient(void* output,void const* borrowed){
    Copy(output,borrowed);
    if(ordinaryBusyAt && ++ordinaryCopies==ordinaryBusyAt)Put(f->player,2680,static_cast<void*>(ordinaryFixture->nativeMap.data()));
    return output;
}
NativeApi OrdinaryApi(){
    auto api=Api();api.stack.merge=FullMetadata;api.stack.equal=FullMetadata;api.stack.setDamage=ToolDamage;
    api.context=+[](void*)->void*{throw std::runtime_error("ordinary must not access a modern context");};
    api.getItem=+[](void const*,int)->void const*{throw std::runtime_error("ordinary must not access a modern wrapper");};
    api.legacy.beginScope=+[](void*,void*){throw std::runtime_error("ordinary must not create a legacy scope");};
    api.legacy.endScope=+[](void*){throw std::runtime_error("ordinary must not end a legacy scope");};
    api.legacy.inventoryItem=OrdinaryGet;api.legacy.setInventory=OrdinarySet;return api;
}
WorldApi OrdinaryWorld(){
    return {Local,World,Input,Top,Name,+[](void* inventory,int n){return OrdinaryGet(inventory,n);},Damageable,FindNativeProfile(GameVersion::V52)};
}
