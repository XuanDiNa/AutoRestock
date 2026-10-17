#include "../src/Plugin.cpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include "ActorFixture.h"
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
int flushes{},responses{},operations{};
void *m=reinterpret_cast<void*>(0x1234),*vectorArg=reinterpret_cast<void*>(0x5678),*o=reinterpret_cast<void*>(0x8765);
std::intptr_t Flush(void* p){CHECK(p==m);++flushes;return 0x12345678;}
std::intptr_t Response(void* p,void* q){CHECK(p==m && q==vectorArg);++responses;return 0x12345679;}
std::intptr_t Operation(void* p,void* q,void* r){CHECK(p==m && q==vectorArg && r==o && nativeDepth==1);++operations;return 0x1234567A;}
void* testPacket{};void* testPlayer{};void* testTxn{};int serverCalls{},normalCalls{};
std::uint32_t Normal(void* transaction,void* player,std::uint32_t authority){
    CHECK(transaction==testTxn && player==testPlayer && authority==0);++normalCalls;CHECK(!integrated.Poll());return 1;
}
void Server(void* packet,void* player,void* context){
    CHECK(packet==testPacket && player==testPlayer && context==o);++serverCalls;
    CHECK(HookNormal(testTxn,player,0)==1);CHECK(!integrated.Poll()); // publish only AFTER whole original packet handler
}
int nestedMode{},nestedCalls{};
void* outerPacket{};void* innerPacket{};void* outerTransaction{};void* innerTransaction{};
void NestedServer(void* packet,void* player,void* context){
    CHECK(player==testPlayer && context==o);++nestedCalls;
    auto outer=packet==outerPacket;auto txn=outer?outerTransaction:innerTransaction;
    if(outer)HookServerPacket(innerPacket,player,context);
    testTxn=txn;
    // When only the unrelated inner packet handles an ordinary transaction,
    // its result must not leak into the outer matching request's frame.
    if(!outer || nestedMode==1)CHECK(HookNormal(txn,player,0)==1);
    CHECK(!integrated.Poll());
}
// Regression: a native rejection must expose the actual compared values,
// without extra getter/comparison calls, native writes or unrelated packets.
alignas(8) std::byte validationAction[512]{},authoritativeStack[152]{};
void* validationFunctor=reinterpret_cast<void*>(0x4242);
int verificationCalls{},comparisonCalls{},comparisonResult{};
std::uint32_t verificationResult=3;
std::uint32_t Compare(void const* actual,void const* expected){
    CHECK(actual==authoritativeStack && expected==validationAction+208);
    ++comparisonCalls;return static_cast<std::uint32_t>(comparisonResult);
}
std::uint32_t Verify(void* functor,void* player,void const* action,std::uint8_t const* authority){
    CHECK(functor==validationFunctor && player==testPlayer && action==validationAction && *authority==0);
    ++verificationCalls;
    CHECK(HookNativeCompare(authoritativeStack,validationAction+208)==static_cast<std::uint32_t>(comparisonResult));
    return verificationResult;
}
std::uint32_t ValidatingNormal(void* transaction,void* player,std::uint32_t authority){
    CHECK(transaction==testTxn && player==testPlayer && authority==0);
    std::uint8_t nativeAuthority{};
    CHECK(HookVerifySlot(validationFunctor,player,validationAction,&nativeAuthority)==verificationResult);
    return verificationResult;
}
void ValidatingServer(void* packet,void* player,void* context){
    CHECK(packet==testPacket && player==testPlayer && context==o);
    CHECK(HookNormal(testTxn,player,0)==verificationResult);
}
std::byte* postHandlerStacks{};
void ApplyingServer(void* packet,void* player,void* context){
    CHECK(packet==testPacket && player==testPlayer && context==o);
    CHECK(!integrated.AuthoritativeCounts(ReadActorIdentity(testPlayer),GetTickCount64())); // server identity is never a client key
    ActorFixture::Write(postHandlerStacks,2*152+34,std::uint8_t{5});
}
int main(){try{
    worldApi.profile=FindNativeProfile(GameVersion::V52);
    originalFlush=Flush;originalResponse=Response;originalOperation=Operation;
    for(int state:{0,-1,1}){
        status.store(state);runtime=nullptr;
        CHECK(HookFlush(m)==0x12345678);
        CHECK(HookResponse(m,vectorArg)==0x12345679);
        CHECK(HookOperation(m,vectorArg,o)==0x1234567A);
        CHECK(nativeDepth==0);
    }
    CHECK(flushes==3 && responses==3 && operations==3);
    CHECK(responseCalls.load()==3 && responseOwnerCalls.load()==0 && responseMatches.load()==0);
    ActorFixture local,server(false);auto l=ReadActorIdentity(local.actor.data()),s=ReadActorIdentity(server.actor.data());
    auto now=GetTickCount64();integrated.Local(l,now);
    alignas(8) std::byte packet[392]{},txn[104]{};
    ActorFixture::Write(packet,56,int{-4});ActorFixture::Write(packet,368,static_cast<void*>(txn));
    imageBase=0x140000000;ActorFixture::Write(txn,0,imageBase+0xE6D6C00);ActorFixture::Write(txn,8,int{});
    testPacket=packet;testTxn=txn;testPlayer=server.actor.data();originalNormal=Normal;originalServerPacket=Server;
    runtime=reinterpret_cast<Runtime*>(1);status.store(1);
    HookServerPacket(packet,testPlayer,o);
    CHECK(integrated.Ready(l,GetTickCount64())); // stub fails: in-process server observation missing
    CHECK(integrated.Arm(l,GetTickCount64(),-4,17));
    HookServerPacket(packet,testPlayer,o);
    auto result=integrated.Poll();CHECK(result && result->requestId==17 && result->nativeStatus==1 && result->result==Completion::Accepted);
    CHECK(serverCalls==2 && normalCalls==2);
    integrated.Clear();
    alignas(8) std::byte inner[392]{},innerTxn[104]{};
    ActorFixture::Write(inner,56,int{-8});ActorFixture::Write(inner,368,static_cast<void*>(innerTxn));
    ActorFixture::Write(innerTxn,0,imageBase+0xE6D6C00);
    outerPacket=packet;innerPacket=inner;outerTransaction=txn;innerTransaction=innerTxn;
    originalServerPacket=NestedServer;
    CHECK(integrated.Arm(l,GetTickCount64(),-4,18));nestedMode=0;
    HookServerPacket(packet,testPlayer,o);CHECK(!integrated.Poll() && integrated.Pending());
    nestedMode=1;HookServerPacket(packet,testPlayer,o);
    result=integrated.Poll();CHECK(result && result->requestId==18 && result->result==Completion::Accepted);
    CHECK(nestedCalls==4 && currentServerFrame==nullptr);
    integrated.Clear();originalNormal=ValidatingNormal;originalServerPacket=ValidatingServer;
    originalVerifySlot=Verify;originalNativeCompare=Compare;
    ActorFixture::Write(validationAction,12,int{2});
    ActorFixture::Write(validationAction,32,std::uint16_t{5});
    ActorFixture::Write(validationAction,128,std::uint16_t{64});
    ActorFixture::Write(validationAction,242,std::uint8_t{5});
    ActorFixture::Write(validationAction,394,std::uint8_t{64});
    ActorFixture::Write(authoritativeStack,34,std::uint8_t{6});
    testPacket=packet;testTxn=txn;
    auto savedAction=std::array<std::byte,512>{};std::memcpy(savedAction.data(),validationAction,512);
    auto savedActual=std::array<std::byte,152>{};std::memcpy(savedActual.data(),authoritativeStack,152);
    // An unarmed packet must still execute the original validator, but must
    // neither emit a plugin trace nor invent a plugin completion.
    HookServerPacket(packet,testPlayer,o);
    CHECK(!serverValidation.Pop() && !integrated.Poll());
    CHECK(integrated.Arm(l,GetTickCount64(),-4,19));
    HookServerPacket(packet,testPlayer,o);
    auto trace=serverValidation.Pop();
    CHECK(trace); // test3 fails: it discards the authoritative mismatch evidence
    CHECK(trace->ticket.policyId==19 && trace->ticket.nativeId==-4);
    CHECK(trace->slot==2 && trace->sourceType==0 && trace->container==0 && trace->status==3);
    CHECK(trace->beforeDescriptorCount==5 && trace->afterDescriptorCount==64 && trace->afterCount==64);
    CHECK(trace->comparison==0 && trace->actual.readable && trace->expected.readable);
    CHECK(trace->actual.count==6 && trace->expected.count==5);
    auto traceText=ValidationText(*trace);
    CHECK(traceText.find("actual={readable=1,count=6,")!=std::string::npos);
    CHECK(traceText.find("expected={readable=1,count=5,")!=std::string::npos);
    CHECK(!serverValidation.Pop());
    CHECK(verificationCalls==2 && comparisonCalls==2);
    CHECK(std::memcmp(savedAction.data(),validationAction,512)==0 && std::memcmp(savedActual.data(),authoritativeStack,152)==0);
    result=integrated.Poll();CHECK(result && result->requestId==19 && result->nativeStatus==3 && result->result==Completion::Rejected);
    // Metadata observation follows the proven native stack/item/block layouts.
    integrated.Clear();
    alignas(8) std::byte item[176]{},block[280]{};void* itemPointer=item;
    ActorFixture::Write(item,170,std::int16_t{42});ActorFixture::Write(block,276,std::int32_t{1234});
    for(auto p:{authoritativeStack,validationAction+208}){
        ActorFixture::Write(p,8,&itemPointer);ActorFixture::Write(p,24,static_cast<void*>(block));
        ActorFixture::Write(p,32,std::int16_t{7});ActorFixture::Write(p,34,std::uint8_t{5});
        ActorFixture::Write(p,35,std::uint8_t{1});ActorFixture::Write(p,144,std::uint8_t{2});
        ActorFixture::Write(p,136,std::int32_t{-10});
    }
    comparisonResult=1;verificationResult=1;
    CHECK(integrated.Arm(l,GetTickCount64(),-4,20));HookServerPacket(packet,testPlayer,o);
    trace=serverValidation.Pop();CHECK(trace && trace->status==1 && trace->comparison==1);
    CHECK(trace->actual.itemId==42 && trace->expected.itemId==42 && trace->actual.blockId==1234);
    CHECK(trace->actual.aux==7 && trace->actual.networkId==-10 && trace->actual.variant==2);
    CHECK(verificationCalls==3 && comparisonCalls==3 && currentValidationFrame==nullptr);
    integrated.Clear();originalServerPacket=NestedServer;
    CHECK(integrated.Arm(l,GetTickCount64(),-4,21));nestedMode=1;
    HookServerPacket(packet,testPlayer,o);
    trace=serverValidation.Pop();CHECK(trace && trace->ticket.policyId==21 && !serverValidation.Pop());
    CHECK(verificationCalls==5 && comparisonCalls==5 && !currentValidationFrame && !currentServerFrame);
    result=integrated.Poll();CHECK(result && result->requestId==21 && result->result==Completion::Accepted);
    integrated.Clear();originalServerPacket=ValidatingServer;testTxn=txn;
    for(int state:{0,-1}){
        status.store(state);HookServerPacket(packet,testPlayer,o);CHECK(!serverValidation.Pop());
    }
    status.store(1);CHECK(!ObserveStack(nullptr).readable);
    CHECK(HookNativeCompare(authoritativeStack,validationAction+208)==1 && !serverValidation.Pop());
    // Bounded transport is non-blocking, reports overflow, and never overwrites
    // a pending event. The worker formats only copied scalar values.
    ServerValidationQueue transport;
    ServerValidationEvent event{};event.slot=7;
    for(unsigned i=0;i<64;++i)CHECK(transport.Push(event));
    CHECK(!transport.Push(event) && transport.Dropped()==1);
    for(unsigned i=0;i<64;++i){auto e=transport.Pop();CHECK(e && e->slot==7);}
    CHECK(!transport.Pop() && transport.Push(event));
    ServerValidationQueue concurrent;
    std::atomic<unsigned> finished{},accepted{},consumed{};std::atomic<bool> healthy{true};
    std::array<bool,10000> seen{};
    auto produce=[&](int start){
        for(int i=0;i<5000;++i){ServerValidationEvent e;e.slot=start+i;if(concurrent.Push(e))++accepted;}
        finished.fetch_add(1,std::memory_order_release);
    };
    std::thread consumer([&]{
        for(;;){
            if(auto e=concurrent.Pop()){
                if(e->slot<0 || e->slot>=10000 || seen[e->slot])healthy.store(false);
                else seen[e->slot]=true;
                ++consumed;
            }else if(finished.load(std::memory_order_acquire)==2){
                // A producer can publish after the preceding empty read and
                // before finished becomes 2; drain again after acquiring it.
                while(auto tail=concurrent.Pop()){
                    if(tail->slot<0 || tail->slot>=10000 || seen[tail->slot])healthy.store(false);
                    else seen[tail->slot]=true;
                    ++consumed;
                }
                break;
            }else std::this_thread::yield();
        }
    });
    std::thread producer1(produce,0),producer2(produce,5000);
    producer1.join();producer2.join();consumer.join();
    CHECK(healthy.load() && consumed.load()==accepted.load() && consumed.load()+concurrent.Dropped()==10000);
    {
        alignas(8) std::byte player[1472]{},supplies[192]{},inventory[440]{},stacks[36*152]{};
        server.Attach(player);testPlayer=player;testPacket=packet;postHandlerStacks=stacks;originalServerPacket=ApplyingServer;
        ActorFixture::Write(player,1464,static_cast<void*>(supplies));ActorFixture::Write(supplies,184,static_cast<void*>(inventory));
        ActorFixture::Write(inventory,0,imageBase+0xE6AFE10);ActorFixture::Write(inventory,8,std::uint8_t{255});
        ActorFixture::Write(inventory,432,static_cast<void*>(player));ActorFixture::Write(inventory,408,static_cast<void*>(stacks));
        ActorFixture::Write(inventory,416,static_cast<void*>(stacks+36*152));ActorFixture::Write(stacks,2*152+34,std::uint8_t{6});
        auto samplesBefore=serverInventorySamples.load();
        integrated.Local(l,GetTickCount64());HookServerPacket(packet,player,o);
        auto counts=integrated.AuthoritativeCounts(l,GetTickCount64());CHECK(counts && (*counts)[2]==5);
        CHECK(serverInventorySamples.load()==samplesBefore+1);
        CHECK(!integrated.Poll()); // a post-handler count sample is not transaction acceptance
    }
    // Missing result/progress distinctions would make a native acceptance look
    // like a completed transfer or make timeout evidence impossible to read.
    Diagnostic outcome;outcome.kind=DiagnosticKind::NativeResult;outcome.integrated=true;
    outcome.requestId=23;outcome.nativeId=-1782;outcome.nativeStatus=1;outcome.time=505;
    outcome.sourceCount=53;outcome.destinationCount=4;outcome.amount=53;
    outcome.wrapperSourceCount=0;outcome.wrapperDestinationCount=56;
    outcome.actualSourceCount=0;outcome.actualDestinationCount=56;outcome.inventorySampled=true;outcome.realMatches=true;
    auto text=DiagnosticText(outcome);
    CHECK(text.starts_with("NativeResult policy=23 native=-1782") && text.find("nativeOutcome=accepted")!=std::string::npos);
    CHECK(text.find("observedAt=505")!=std::string::npos && text.find("expectedDestinationCount=57")!=std::string::npos);
    CHECK(text.find("actualDestinationCount=56")!=std::string::npos && text.find("exactProgress=0")!=std::string::npos);
    outcome.kind=DiagnosticKind::Timeout;text=DiagnosticText(outcome);
    CHECK(text.find("stage=awaiting-exact-progress")!=std::string::npos);
    outcome.nativeStatus=-1;text=DiagnosticText(outcome);
    CHECK(text.find("stage=awaiting-native-result")!=std::string::npos);
    outcome.kind=DiagnosticKind::Reconciled;text=DiagnosticText(outcome);
    CHECK(text.find("provenConsumption=0")!=std::string::npos); // copied proof must be visible, never inferred from count loss
    outcome.kind=DiagnosticKind::NativeResult;outcome.nativeStatus=1;outcome.provenConsumption=1;
    outcome.consumptionChainValid=true;outcome.accountedProgress=true;text=DiagnosticText(outcome);
    CHECK(text.find("provenConsumption=1")!=std::string::npos && text.find("accountedDestinationCount=56")!=std::string::npos);
    CHECK(text.find("accountedProgress=1")!=std::string::npos && text.find("exactProgress=0")!=std::string::npos);
    runtime=nullptr;integrated.Clear();
    std::cout<<"PASS: original hook calls/returns; exact-ticket read-only native mismatch traces; nested/inactive isolation; bounded concurrent scalar transport\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
