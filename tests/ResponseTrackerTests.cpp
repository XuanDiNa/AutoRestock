#include "AutoRestock/ResponseTracker.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace autorestock;
// Independently constructed from 25B23F0 assembly: stride 0x30, status +0,
// signed native request id +0x10. Payloads are not dereferenced by the tracker.
std::array<std::byte,48> Entry(std::int32_t id, unsigned char status=0) {
    std::array<std::byte,48> bytes{};
    bytes[0] = std::byte{status};
    std::memcpy(bytes.data()+16, &id, 4);
    return bytes;
}
int main() {
    try {
        ResponseTracker t;
        auto success=Entry(-3);
        CHECK(!t.Decode(0x1000,success)); // unarmed cannot accept any response
        CHECK(t.Begin(0x1000,-3,17));
        CHECK(!t.Decode(0x2000,success)); // a different player's manager
        CHECK(!t.Decode(0x1000,Entry(-5)));
        auto done=t.Decode(0x1000,success);
        CHECK(done && done->requestId==17 && done->result==Completion::Accepted);
        CHECK(done->nativeId==-3 && done->nativeStatus==0);
        CHECK(!t.Begin(0x1000,-5,18)); // one in flight; never replace it silently
        done=t.Decode(0x1000,Entry(-3,1));
        CHECK(done && done->result==Completion::Rejected);
        done=t.Decode(0x1000,Entry(-3,255));
        CHECK(done && done->result==Completion::Rejected); // unknown status fails closed
        CHECK(done->nativeId==-3 && done->nativeStatus==255);
        CHECK(!t.Decode(0x1000,std::span(success).first(47)));
        CHECK(!t.Decode(0x1000,{}));
        std::array<std::byte,96> batch{};
        auto other=Entry(-101);
        std::memcpy(batch.data(),other.data(),48);
        std::memcpy(batch.data()+48,success.data(),48);
        done=t.Decode(0x1000,batch);
        CHECK(done && done->requestId==17 && done->result==Completion::Accepted);
        auto refused=Entry(-3,9);
        std::memcpy(batch.data(),refused.data(),48);
        done=t.Decode(0x1000,batch);
        CHECK(done && done->result==Completion::Rejected); // conflicting duplicate
        CHECK(done->nativeStatus==9); // retain the actual refusal, not the later success
        std::memcpy(batch.data(),success.data(),48);
        done=t.Decode(0x1000,batch);
        CHECK(done && done->result==Completion::Accepted);
        t.Reset();
        CHECK(!t.Decode(0x1000,success)); // late reply after context/disable change
        for (auto id : {0,1,2,-1,-2,-4,INT32_MIN}) CHECK(!t.Begin(0x1000,id,18));
        CHECK(!t.Begin(0,-3,18)); CHECK(!t.Begin(0x1000,-3,0));
        CHECK(t.Begin(0x1000,-5,18));
        CHECK(!t.Decode(0x1000,success));
        done=t.Decode(0x1000,Entry(-5));
        CHECK(done && done->requestId==18);
        std::cout << "PASS: native reply layout, manager/id correlation, stale/malformed replies, rejection and one-pending guard\n";
        return 0;
    } catch (std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
