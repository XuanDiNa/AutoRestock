#include "AutoRestock/NativeProfile.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace autorestock {
namespace {
#include "NativeProfileData.inl"
// Explicit shared inventory/object contract, reviewed against each image.
// Differences are applied explicitly below, never by inferred RVA deltas.
constexpr NativeLayout commonLayout{
    .stackSize=152,.count=34,.variant=144,.variantTable=128,.variantValue=136,
    .managerOwner=16,.managerPlayer=2744,.managerMode=8,.managerClient=9,
    .playerClient=3440,.playerSupplies=1464,.suppliesInventory=184,.selectedSlot=16,
    .inventoryFirst=408,.inventoryLast=416,.inventoryOwner=432,
    .sceneHasName=456,.inventoryGet=56,.itemDamageable=112,.itemMaxDamage=288,
    .schedulerOwner=96,.clientGame=424,.gameSecondary=48,.gameNetwork=2080,
    .networkSecondary=32,.networkRegistration=472,.networkFirst=192,.networkLast=200,.networkCapacity=208,
    .handlerClient=88,.handlerEstablished=960,.handlerGuid=696,.handlerType=840,
    .connectionType=168,.connectionGuid=24,.connectionState=176,.connectionRemoved=408,.connectionClosing=409,.connectionPeer=280,
    .packetComplex=368,.packetRequest=56,.packetMode=1600,
    .sendAuto=1808,.sendNormal=1816,.sendPacket=1824,
    .operationImplementation=0,.implementationInvoker=0,.closureCapture=0,.gameModePlayer=8,
    .operationAbi=OperationAbi::FunctionRef,.endRequestAbi=EndRequestAbi::Manager
};
constexpr NativeLayout layout45=[] {
    auto value=commonLayout;
    value.clientGame=416;value.gameNetwork=2032;
    value.handlerEstablished=936;value.handlerGuid=672;value.handlerType=816;
    value.packetComplex=360;
    value.operationImplementation=56;value.implementationInvoker=16;value.closureCapture=8;
    value.operationAbi=OperationAbi::OwningFunction;value.endRequestAbi=EndRequestAbi::Scope16;
    return value;
}();
// These profiles are admitted only after exact disk/loaded PE identity and all
// loaded guards pass in ValidateNativeProfile. Merely recognising a PE or
// passing an oracle never enables hooks.
const NativeProfile profiles[]={
    // Native-origin range excludes the launcher's appended synthetic page.
    {GameVersion::V45,true,0x12888000,layout45,addresses45,guards45},
    {GameVersion::V51,true,0x12C0C000,commonLayout,addresses51,guards51},
    {GameVersion::V52,true,0x12C01000,commonLayout,addresses52,guards52}
};
// Plain data only: the guarded reader must not allocate or run destructors
// inside SEH. Formatting happens afterward, once on the startup worker.
struct LoadedInspection {
    char const* stage="null-module";
    ImageProfile const* image{};
    Identity identity{};
    std::uint64_t headerImageBase{},moduleBase{};
    std::uint32_t rva{};
    std::uint16_t optionalMagic{},optionalBytes{},sections{};
    bool headerKnown{},guardKnown{},observedKnown{};
    std::array<std::uint8_t,32> expected{},observed{};
};
NativeProfile const* InspectLoaded(HMODULE,LoadedInspection&) noexcept;
std::string InspectionError(LoadedInspection const& d){
    std::ostringstream out;
    out<<"Loaded image rejected: stage="<<d.stage;
    if(d.image)out<<" profile="<<d.image->name;
    out<<std::hex<<std::uppercase;
    if(d.headerKnown)out<<" machine=0x"<<d.identity.machine<<" timestamp=0x"<<d.identity.timestamp
        <<" imageSize=0x"<<d.identity.imageSize<<" checksum=0x"<<d.identity.checksum
        <<" headerImageBase=0x"<<d.headerImageBase<<" moduleBase=0x"<<d.moduleBase<<" optionalMagic=0x"<<d.optionalMagic
        <<" optionalBytes=0x"<<d.optionalBytes<<" sections=0x"<<d.sections;
    if(d.guardKnown){
        out<<" rva=0x"<<d.rva<<" expected="<<std::setfill('0');
        for(auto byte:d.expected)out<<std::setw(2)<<static_cast<unsigned>(byte);
        if(d.observedKnown){
            out<<" observed=";
            for(auto byte:d.observed)out<<std::setw(2)<<static_cast<unsigned>(byte);
        }
    }
    return out.str();
}
}
std::uint32_t NativeProfile::Rva(std::uint32_t reference) const noexcept {
    auto found=std::lower_bound(addresses.begin(),addresses.end(),reference,
        [](NativeAddress const& address,std::uint32_t key){return address.reference<key;});
    return found!=addresses.end() && found->reference==reference?found->target:0;
}
NativeProfile const* FindNativeProfile(GameVersion version) noexcept {
    for(auto const& profile:profiles)if(profile.version==version)return &profile;
    return nullptr;
}
NativeProfile const* ValidateNativeProfile(HMODULE module,std::string& error){
    LoadedInspection inspection;
    auto native=InspectLoaded(module,inspection);
    if(!native){error=InspectionError(inspection);return nullptr;}
    if(!native->runtimeReady){error="Exact version native contract is not admitted";return nullptr;}
    std::wstring path(32768,L'\0');auto used=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!used || used>=path.size()){error="Cannot locate loaded executable";return nullptr;}path.resize(used);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE){error="Cannot open loaded executable";return nullptr;}
    LARGE_INTEGER size{};bool valid=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<(1LL<<32);
    HANDLE mapping=valid?CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr):nullptr;
    auto data=mapping?static_cast<std::uint8_t const*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0)):nullptr;
    auto disk=data?InspectFileImage({data,static_cast<std::size_t>(size.QuadPart)}):nullptr;
    valid=disk && disk->version==native->version;
    if(valid){
        auto loaded=reinterpret_cast<std::uint8_t const*>(module);
        auto a=reinterpret_cast<IMAGE_NT_HEADERS64 const*>(loaded+reinterpret_cast<IMAGE_DOS_HEADER const*>(loaded)->e_lfanew);
        auto b=reinterpret_cast<IMAGE_NT_HEADERS64 const*>(data+reinterpret_cast<IMAGE_DOS_HEADER const*>(data)->e_lfanew);
        valid=a->FileHeader.Machine==b->FileHeader.Machine && a->FileHeader.TimeDateStamp==b->FileHeader.TimeDateStamp &&
            a->OptionalHeader.SizeOfImage==b->OptionalHeader.SizeOfImage && a->OptionalHeader.CheckSum==b->OptionalHeader.CheckSum;
    }
    if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);CloseHandle(file);
    error=valid?"":"Disk and loaded image do not match the exact profile";
    return valid?native:nullptr;
}
namespace {
NativeProfile const* InspectLoaded(HMODULE module,LoadedInspection& d) noexcept {
    if(!module)return nullptr;
    __try {
        d.stage="dos-header";
        auto base=reinterpret_cast<std::uint8_t const*>(module);
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew>4096)return nullptr;
        d.stage="nt-header";
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base+dos->e_lfanew);
        d.identity={nt->FileHeader.Machine,nt->FileHeader.TimeDateStamp,nt->OptionalHeader.SizeOfImage,nt->OptionalHeader.CheckSum};
        d.headerImageBase=nt->OptionalHeader.ImageBase;d.moduleBase=reinterpret_cast<std::uintptr_t>(module);
        d.optionalMagic=nt->OptionalHeader.Magic;
        d.optionalBytes=nt->FileHeader.SizeOfOptionalHeader;d.sections=nt->FileHeader.NumberOfSections;d.headerKnown=true;
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
           nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
           // Disk admission still requires the original preferred base. The
           // Windows loader may replace the loaded header value with this
           // module's actual base; no unrelated or inferred base is allowed.
           (nt->OptionalHeader.ImageBase!=0x140000000 && nt->OptionalHeader.ImageBase!=d.moduleBase) ||
           !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections>96)return nullptr;
        d.stage="pe-identity";
        auto image=SelectImageProfile(d.identity);
        if(!image)return nullptr;
        d.image=image;d.stage="native-catalog";
        auto native=FindNativeProfile(image->version);
        if(!native || native->addresses.size()!=96 || native->guards.size()!=65)return nullptr;
        std::uint32_t last{};
        for(auto const& address:native->addresses){
            if(address.reference<=last || !address.target || address.target>=nt->OptionalHeader.SizeOfImage)return nullptr;
            last=address.reference;
        }
        for(auto const& guard:native->guards){
            d.rva=guard.rva;d.expected=guard.bytes;d.guardKnown=true;d.observedKnown=false;d.stage="guard-range";
            if(guard.rva>nt->OptionalHeader.SizeOfImage-32)return nullptr;
            d.stage="guard-read";
            std::memcpy(d.observed.data(),base+guard.rva,32);d.observedKnown=true;
            d.stage="guard-byte-mismatch";
            if(!MatchGuard(d.observed,guard))return nullptr;
        }
        return native;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        d.stage=d.guardKnown?"guard-read-exception":"header-read-exception";
        d.observedKnown=false;return nullptr;
    }
}
}
NativeProfile const* InspectLoadedNativeProfile(HMODULE module) noexcept {
    LoadedInspection inspection;return InspectLoaded(module,inspection);
}
}
