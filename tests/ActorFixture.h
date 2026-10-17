#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
// Independent literal Actor+16 registry / Actor+24 entity layout, from IDA.
struct ActorFixture {
    alignas(8) std::array<std::byte,32> actor{};
    alignas(8) std::array<std::byte,128> registry{};
    std::uint64_t bucket{};
    std::array<std::array<std::byte,32>,3> nodes{};
    std::array<std::array<std::uintptr_t,11>,3> pools{};
    std::array<std::array<std::uint32_t,2048>,3> sparse{};
    std::array<std::uintptr_t,3> sparsePages{},componentPages{};
    std::array<std::array<std::uint64_t,128>,3> components{};
    template<class T> static void Write(void* p,std::size_t off,T value){std::memcpy(static_cast<std::byte*>(p)+off,&value,sizeof value);}
    ActorFixture(bool local=true,std::uint32_t entity=7,std::uint64_t uid=42,std::uint64_t rid=77){
        std::uint32_t hashes[]={local?0xACB47B38u:0x122C9226u,0x18F957AFu,0xFC0DBBB5u};
        for(unsigned i=0;i<3;++i){
            sparse[i].fill(UINT32_MAX);sparse[i][entity&0x7FF]=(entity&0xFFFC0000u)|3;
            sparsePages[i]=reinterpret_cast<std::uintptr_t>(sparse[i].data());
            componentPages[i]=reinterpret_cast<std::uintptr_t>(components[i].data());
            pools[i][1]=reinterpret_cast<std::uintptr_t>(&sparsePages[i]);pools[i][2]=pools[i][1]+8;
            pools[i][10]=reinterpret_cast<std::uintptr_t>(&componentPages[i]);
            Write(nodes[i].data(),0,i==2?UINT64_MAX:std::uint64_t(i+1));Write(nodes[i].data(),8,hashes[i]);Write(nodes[i].data(),16,pools[i].data());
        }
        components[1][3]=uid;components[2][3]=rid;
        Write(registry.data(),72,&bucket);Write(registry.data(),80,&bucket+1);
        Write(registry.data(),104,nodes.data());Write(registry.data(),112,nodes.data()+3);
        Attach(actor.data(),entity);
    }
    void Attach(void* target,std::uint32_t entity=7){Write(target,16,registry.data());Write(target,24,entity);}
};
