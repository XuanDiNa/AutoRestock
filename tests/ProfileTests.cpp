#include "AutoRestock/NativeProfile.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)
using namespace autorestock;
int main(int argc,char** argv) {
    try {
        CHECK(argc==3);
        CHECK(IdentityAccepted({0x8664,0x6AB54E37,0x12C01000,0x128CD801}));
        CHECK(IdentityAccepted({0x8664,0x6AB54E37,0x12C02000,0x128DA293}));
        CHECK(!IdentityAccepted({0x8664,0x6AB54E37,0x12C01000,0x128DA293}));
        CHECK(!IdentityAccepted({0x8664,0x6AB54E37,0x12C02000,0x128CD801}));
        CHECK(!IdentityAccepted({0x14C,0x6AB54E37,0x12C01000,0x128CD801}));
        CHECK(!IdentityAccepted({0x8664,0,0x12C01000,0x128CD801}));
        CHECK(!FindNativeProfile(GameVersion::V52)->guards.empty());
        for(auto const& g:FindNativeProfile(GameVersion::V52)->guards) {
            CHECK(MatchGuard(g.bytes,g));
            CHECK(!MatchGuard(std::span(g.bytes).first(31),g));
            for(unsigned pos=0;pos<32;++pos){auto bad=g.bytes;bad[pos]^=1;CHECK(!MatchGuard(bad,g));}
        }
        std::string error;
        CHECK(VerifyFileText(argv[1],error) && error.empty());
        CHECK(VerifyFileText(argv[2],error) && error.empty());
        for(int path=1;path<=2;++path){
            std::ifstream file(argv[path],std::ios::binary);CHECK(file);
            IMAGE_DOS_HEADER dos{};CHECK(file.read(reinterpret_cast<char*>(&dos),sizeof dos));
            file.seekg(dos.e_lfanew);IMAGE_NT_HEADERS64 nt{};CHECK(file.read(reinterpret_cast<char*>(&nt),sizeof nt));
            file.seekg(dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader);
            std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
            CHECK(file.read(reinterpret_cast<char*>(sections.data()),sections.size()*sizeof(IMAGE_SECTION_HEADER)));
            for(auto const& guard:FindNativeProfile(GameVersion::V52)->guards){
                bool checked=false;
                for(auto const& section:sections){
                    if(guard.rva<section.VirtualAddress || guard.rva-section.VirtualAddress>=section.SizeOfRawData) continue;
                    CHECK(section.SizeOfRawData-(guard.rva-section.VirtualAddress)>=32);
                    file.seekg(section.PointerToRawData+guard.rva-section.VirtualAddress);
                    std::array<std::uint8_t,32> actual{};CHECK(file.read(reinterpret_cast<char*>(actual.data()),32));
                    CHECK(MatchGuard(actual,guard));checked=true;break;
                }
                CHECK(checked);
            }
        }
        CHECK(!VerifyFileText("__missing_autorestock_executable__",error) && !error.empty());
        CHECK(!ValidateModule(GetModuleHandleW(nullptr),error) && !error.empty());
        CHECK(!ValidateModule(nullptr,error));
        // Malformed readable PE fixtures must not lead to an unchecked header
        // or section-table read; unsupported hosts never become native bindings.
        auto raw=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        CHECK(raw);DWORD previous{};CHECK(VirtualProtect(raw+4096,4096,PAGE_NOACCESS,&previous));
        CHECK(!ValidateModule(reinterpret_cast<HMODULE>(raw),error));
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw);dos->e_magic=IMAGE_DOS_SIGNATURE;
        for(int offset:{-1,0,4095,4096,INT32_MAX}) {
            dos->e_lfanew=offset;CHECK(!ValidateModule(reinterpret_cast<HMODULE>(raw),error));
        }
        CHECK(VirtualFree(raw,0,MEM_RELEASE));
        std::cout<<"PASS: original/patched text identity, every guarded byte, malformed PE and unsupported loaded host\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
