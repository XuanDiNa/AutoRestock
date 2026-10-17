// Catch the observed split: native damages/breaks a working stack, then its
// owner applies that result to the real main hand AFTER the enclosing return.
#define AUTORESTOCK_WORLD_DESTROY_CASES_NAME WorldDestroyOriginalCases
#include "WorldDestroyTests.cpp"
void* hurtStack{};void* hurtPlayer{};unsigned hurtCalls{};
bool hurtBreak=true,hurtThrows=false;
std::intptr_t HurtOriginal(void* stack,int amount,void* player){
    CHECK(stack==hurtStack && player==hurtPlayer && amount==1);++hurtCalls;
    if(hurtThrows)throw std::runtime_error("native-hurt-unwind");
    FixtureABI::Put(stack,60,int{32});
    if(hurtBreak)FixtureABI::Put(stack,34,std::uint8_t{});
    return 0x1234567812345601;
}
autorestock::NativeApi ToolApi(){
    auto api=FixtureABI::Api();api.getItem=FixtureABI::AllSlots;
    api.stack.merge=[](void const* a,void const* b){return FixtureABI::Capacity(b)>1 && FixtureABI::FullMetadata(a,b);};
    api.stack.equal=FixtureABI::FullMetadata;api.stack.setDamage=FixtureABI::ToolDamage;
    api.legacy.inventoryItem=[](void const* p,int n){return FixtureABI::RealItem(const_cast<void*>(p),n);};
    return api;
}
int main(){try{
    CHECK(WorldDestroyOriginalCases()==0);
    for(int scenario=0;scenario<20;++scenario){
        FixtureABI::Fixture x;FixtureABI::Prepare(x);HoldTool(x);FixtureABI::ActorFixture actor;actor.Attach(x.player);
        autorestock::WorldApi worldApiFixture{FixtureABI::Local,FixtureABI::World,FixtureABI::Input,FixtureABI::Top,
            FixtureABI::Name,FixtureABI::RealItem,FixtureABI::Damageable,FindNativeProfile(GameVersion::V52)};
        Runtime r{ToolApi(),worldApiFixture,FixtureABI::base};
        runtime=&r;status.store(1);ownerThread.store(GetCurrentThreadId());nativeDepth=0;
        imageBase=FixtureABI::base;worldApi=worldApiFixture;
        restockManager=reinterpret_cast<std::uintptr_t>(x.manager);restockInventory.store(reinterpret_cast<std::uintptr_t>(x.inventory));
        settings.store((std::uint64_t{1}<<8)|13);fixtureForeground=fixtureHidden=true;
        originalToolHurt=HurtOriginal;hurtBreak=scenario!=1;hurtThrows=scenario==2;
        auto now=GetTickCount64();CHECK(!r.Tick(x.manager,true,true,Config{},now,1));
        OwnedStack working;CHECK(working.Copy(ToolApi().stack,&x.destination));
        hurtStack=const_cast<void*>(working.Get());hurtPlayer=x.player;
        if(scenario==3)FixtureABI::Put(hurtStack,128,int{999}); // another identical tool is not main hand
        if(scenario==4)FixtureABI::Put(hurtStack,56,int{999}); // different complete metadata
        if(scenario==5)fixtureForeground=false;
        if(scenario==6)fixtureHidden=false;
        if(scenario==7)settings.store((std::uint64_t{1}<<8)|12);
        if(scenario==8)restockInventory.store(1);
        if(scenario==9)ownerThread.store(0);
        auto calls=hurtCalls;
        if(scenario==2){bool thrown=false;try{HookToolHurt(hurtStack,1,hurtPlayer);}catch(std::runtime_error const&){thrown=true;}CHECK(thrown);}
        else if(scenario==10){Depth nested;CHECK(HookToolHurt(hurtStack,1,hurtPlayer)==0x1234567812345601);}
        else CHECK(HookToolHurt(hurtStack,1,hurtPlayer)==0x1234567812345601);
        CHECK(hurtCalls==calls+1 && nativeDepth==0 && x.requests==0);
        fixtureForeground=fixtureHidden=true;ownerThread.store(GetCurrentThreadId());
        // Main hand is still old on return. Merely damaging a tool, recent
        // mining, or an idle empty hand must not invent a break.
        CHECK(FixtureABI::Read<std::uint8_t>(&x.destination,34)==1);
        if(scenario==11){CHECK(!r.Tick(x.manager,false,true,Config{},now+1,1));}
        if(scenario==12){FixtureABI::Put(x.supplies,16,int{3});CHECK(!r.Tick(x.manager,true,true,Config{},now+1,1));FixtureABI::Put(x.supplies,16,int{2});}
        if(scenario==13){CHECK(!r.Tick(x.manager,true,true,Config{},now+1,2));}
        if(scenario==14){FixtureABI::Put(&x.destination,56,int{321});CHECK(!r.Tick(x.manager,true,true,Config{},now+1,1));}
        if(scenario==15){CHECK(!r.Tick(x.manager,true,true,Config{},now+600,1));now+=600;}
        if(scenario==16){auto n=GetTickCount64();CHECK(!r.Tick(x.manager,true,true,Config{},n-100,1));}
        if(scenario==17){FixtureABI::Put(&x.source,56,int{456});} // never substitute different name/enchant
        if(scenario==18){CHECK(!r.Tick(x.manager,true,true,Config{},now+1,1));} // wait through unchanged frame
        if(scenario==19){FixtureABI::Put(&x.destination,128,int{998});CHECK(!r.Tick(x.manager,true,true,Config{},now+1,1));}
        FixtureABI::Put(&x.destination,34,std::uint8_t{}); // original owner finally applies the native break
        auto result=r.Tick(x.manager,true,true,Config{},now+2,1);
        if(scenario==0 || scenario==10 || scenario==18){
            CHECK(result==TransferResult::Queued && x.requests==1);
            CHECK(FixtureABI::Read<int>(&x.destination,60)==2); // spare retains its own damage
        }else CHECK(!result && x.requests==0);
        r.Reset();runtime=nullptr;
    }
    std::cout<<"PASS native working-stack break to later main update; identity/slot/UI/epoch/deadline/manual-removal guards; no transfer inside hurt hook\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
