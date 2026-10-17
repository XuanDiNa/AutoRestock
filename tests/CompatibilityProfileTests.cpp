#include "AutoRestock/Profile.h"
#include "AutoRestock/NativeProfile.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while(false)

struct PrivateImage {
    HANDLE file=INVALID_HANDLE_VALUE,mapping{};std::uint8_t* data{};std::size_t size{};
    explicit PrivateImage(wchar_t const* path) {
        file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        CHECK(file!=INVALID_HANDLE_VALUE);LARGE_INTEGER length{};CHECK(GetFileSizeEx(file,&length));
        size=static_cast<std::size_t>(length.QuadPart);
        // Changes are private page copies. No write handle to the game file.
        mapping=CreateFileMappingW(file,nullptr,PAGE_WRITECOPY,0,0,nullptr);CHECK(mapping);
        data=static_cast<std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_COPY,0,0,0));CHECK(data);
    }
    ~PrivateImage(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
};
std::string Hex(std::uint64_t value){std::ostringstream out;out<<std::hex<<std::uppercase<<value;return out.str();}
std::string Bytes(std::uint8_t const* value){
    std::ostringstream out;out<<std::hex<<std::uppercase<<std::setfill('0');
    for(unsigned i=0;i<32;++i)out<<std::setw(2)<<static_cast<unsigned>(value[i]);
    return out.str();
}

int wmain(int argc,wchar_t** argv) {
    try {
        CHECK(argc==7);
        using namespace autorestock;
        constexpr Identity identities[]={
            {0x8664,0x6A8378BA,0x12888000,0x12568F46},
            {0x8664,0x6A8378BA,0x12889000,0x1256BF65},
            {0x8664,0x6AA482FD,0x12C0C000,0x128E3A0F},
            {0x8664,0x6AA482FD,0x12C0D000,0x128DA158},
            {0x8664,0x6AB54E37,0x12C01000,0x128CD801},
            {0x8664,0x6AB54E37,0x12C02000,0x128DA293}};
        constexpr GameVersion expected[]={GameVersion::V45,GameVersion::V45,GameVersion::V51,GameVersion::V51,GameVersion::V52,GameVersion::V52};
        // Independent real original/launcher-image fixtures, not a profile
        // builder used to manufacture its own expected identity or text hash.
        for(int i=1;i<argc;++i) {
            std::string error;
            if(!autorestock::VerifyFileText(std::filesystem::path(argv[i]),error)) {
                std::wcerr << L"Exact supported image rejected: " << argv[i] << L'\n';
                throw std::runtime_error(error);
            }
            CHECK(error.empty());
            auto profile=SelectImageProfile(identities[i-1]);CHECK(profile && profile->version==expected[i-1]);
            PrivateImage image(argv[i]);CHECK(InspectFileImage({image.data,image.size})==profile);
            auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image.data);
            auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data+dos->e_lfanew);
            auto sections=IMAGE_FIRST_SECTION(nt);IMAGE_SECTION_HEADER* text{};
            for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)
                if(std::memcmp(sections[n].Name,".text\0\0\0",8)==0)text=&sections[n];
            CHECK(text);
            auto loaded=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(loaded);
            // Private data-only loaded layout; never executes a game entrypoint.
            std::memcpy(loaded,image.data,nt->OptionalHeader.SizeOfHeaders);
            for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)
                if(sections[n].SizeOfRawData && sections[n].PointerToRawData+sections[n].SizeOfRawData<=image.size)
                    std::memcpy(loaded+sections[n].VirtualAddress,image.data+sections[n].PointerToRawData,sections[n].SizeOfRawData);
            auto native=FindNativeProfile(expected[i-1]);CHECK(native);
            CHECK(InspectLoadedNativeProfile(reinterpret_cast<HMODULE>(loaded))==native);
            CHECK(native->addresses.size()==96 && native->guards.size()==65);
            auto loadedNt=reinterpret_cast<IMAGE_NT_HEADERS64*>(loaded+dos->e_lfanew);
            auto preferredBase=loadedNt->OptionalHeader.ImageBase;
            auto actualBase=reinterpret_cast<std::uintptr_t>(loaded);
            CHECK(actualBase!=preferredBase);
            // Windows may publish the actual mapping base in the loaded PE
            // header. The disk header still has its fixed preferred base.
            // Keeping a disk-only ImageBase predicate in the loaded reader
            // must fail this reproduction before any binding is attempted.
            for(auto headerBase:{preferredBase,static_cast<std::uint64_t>(actualBase)}){
                loadedNt->OptionalHeader.ImageBase=headerBase;
                CHECK(InspectLoadedNativeProfile(reinterpret_cast<HMODULE>(loaded))==native);
                for(auto const& guard:native->guards){
                    loaded[guard.rva]^=1;CHECK(!InspectLoadedNativeProfile(reinterpret_cast<HMODULE>(loaded)));
                    // Catch opaque or misleading admission failures: the user must
                    // be told which exact guard failed without weakening admission.
                    CHECK(!ValidateNativeProfile(reinterpret_cast<HMODULE>(loaded),error));
                    CHECK(error.find("stage=guard-byte-mismatch")!=std::string::npos);
                    CHECK(error.find("profile="+std::string(profile->name))!=std::string::npos);
                    CHECK(error.find("rva=0x"+Hex(guard.rva)+" ")!=std::string::npos);
                    CHECK(error.find("expected="+Bytes(guard.bytes.data()))!=std::string::npos);
                    CHECK(error.find("observed="+Bytes(loaded+guard.rva))!=std::string::npos);
                    loaded[guard.rva]^=1;
                }
            }
            auto savedLoadedTimestamp=loadedNt->FileHeader.TimeDateStamp;
            loadedNt->FileHeader.TimeDateStamp=0x12345678;
            CHECK(!ValidateNativeProfile(reinterpret_cast<HMODULE>(loaded),error));
            CHECK(error.find("stage=pe-identity")!=std::string::npos && error.find("timestamp=0x12345678")!=std::string::npos);
            loadedNt->FileHeader.TimeDateStamp=savedLoadedTimestamp;
            for(auto invalidBase:{std::uint64_t{0},std::uint64_t{0x180000000},static_cast<std::uint64_t>(actualBase+0x10000)}){
                loadedNt->OptionalHeader.ImageBase=invalidBase;
                CHECK(!ValidateNativeProfile(reinterpret_cast<HMODULE>(loaded),error));
                CHECK(error.find("stage=nt-header")!=std::string::npos);
                CHECK(error.find("headerImageBase=0x"+Hex(invalidBase))!=std::string::npos);
                CHECK(error.find("moduleBase=0x"+Hex(actualBase))!=std::string::npos);
            }
            loadedNt->OptionalHeader.ImageBase=actualBase;
            // An unreadable guarded page must be reported and still refused,
            // without allowing an AV to escape or printing stale observed bytes.
            DWORD previousProtection{};auto guardPage=loaded+(native->guards[0].rva&~4095u);
            CHECK(VirtualProtect(guardPage,4096,PAGE_NOACCESS,&previousProtection));
            CHECK(!ValidateNativeProfile(reinterpret_cast<HMODULE>(loaded),error));
            CHECK(error.find("stage=guard-read-exception")!=std::string::npos);
            CHECK(error.find("observed=")==std::string::npos);
            DWORD ignored{};CHECK(VirtualProtect(guardPage,4096,previousProtection,&ignored));
            CHECK(InspectLoadedNativeProfile(reinterpret_cast<HMODULE>(loaded))==native);
            auto foreign=identities[(i+1)%6];auto originalNt=*loadedNt;
            loadedNt->FileHeader.TimeDateStamp=foreign.timestamp;loadedNt->OptionalHeader.SizeOfImage=foreign.imageSize;
            loadedNt->OptionalHeader.CheckSum=foreign.checksum;
            CHECK(!InspectLoadedNativeProfile(reinterpret_cast<HMODULE>(loaded)));*loadedNt=originalNt;
            CHECK(VirtualFree(loaded,0,MEM_RELEASE));
            // Actual-base semantics are loaded-only: an altered on-disk
            // preferred base must not gain admission even with pristine text.
            nt->OptionalHeader.ImageBase=actualBase;
            CHECK(!InspectFileImage({image.data,image.size}));
            nt->OptionalHeader.ImageBase=preferredBase;
            auto raw=text->PointerToRawData;
            image.data[raw+77]^=1;CHECK(!InspectFileImage({image.data,image.size}));image.data[raw+77]^=1;
            // Reject a foreign version's otherwise valid PE identity over this
            // version's original text, not just an unrecognised checksum.
            auto savedTimestamp=nt->FileHeader.TimeDateStamp;auto savedSize=nt->OptionalHeader.SizeOfImage;
            auto savedChecksum=nt->OptionalHeader.CheckSum;auto other=identities[(i+1)%6];
            nt->FileHeader.TimeDateStamp=other.timestamp;nt->OptionalHeader.SizeOfImage=other.imageSize;
            nt->OptionalHeader.CheckSum=other.checksum;
            CHECK(!InspectFileImage({image.data,image.size}));
            nt->FileHeader.TimeDateStamp=savedTimestamp;nt->OptionalHeader.SizeOfImage=savedSize;nt->OptionalHeader.CheckSum=savedChecksum;
            auto savedRaw=text->PointerToRawData;text->PointerToRawData=UINT32_MAX;
            CHECK(!InspectFileImage({image.data,image.size}));text->PointerToRawData=savedRaw;
            auto savedCount=nt->FileHeader.NumberOfSections;nt->FileHeader.NumberOfSections=UINT16_MAX;
            CHECK(!InspectFileImage({image.data,4096}));nt->FileHeader.NumberOfSections=savedCount;
            CHECK(!InspectFileImage({image.data,63}));
            CHECK(InspectFileImage({image.data,image.size})==profile);
        }
        for(auto identity:identities){
            auto bad=identity;bad.machine=0x14C;CHECK(!SelectImageProfile(bad));
            bad=identity;bad.checksum^=1;CHECK(!SelectImageProfile(bad));
            bad=identity;bad.timestamp^=1;CHECK(!SelectImageProfile(bad));
        }
        std::string error;
        CHECK(!autorestock::VerifyFileText(L"__absent_compatibility_image__",error));
        CHECK(!autorestock::ValidateModule(GetModuleHandleW(nullptr),error));
        CHECK(!autorestock::ValidateModule(nullptr,error));
        CHECK(error.find("stage=null-module")!=std::string::npos);
        std::cout << "PASS: all six exact images with original/actual loaded base; all65 guards in both forms; arbitrary base and altered disk base refused; PE and unreadable-page diagnostics; unsupported host refused\n";
        return 0;
    }catch(std::exception const& error){std::cerr << error.what() << '\n';return 1;}
}
