#include "AutoRestock/Profile.h"
#include "AutoRestock/NativeProfile.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace autorestock {
namespace {
constexpr ImageProfile imageProfiles[]={
    {GameVersion::V45,"1.26.45.01",{{{0x8664,0x6A8378BA,0x12888000,0x12568F46},{0x8664,0x6A8378BA,0x12889000,0x1256BF65}}},240686080,
        {0x19,0x40,0x06,0xAC,0xBA,0xE5,0x37,0x11,0x33,0xA0,0x9D,0xC7,0x27,0xE1,0x17,0x62,0x33,0xD3,0x04,0xA9,0xE7,0xA0,0xF7,0xAF,0xC2,0x8D,0x73,0xE4,0x1D,0x37,0xF6,0x85}},
    {GameVersion::V51,"1.26.51.01",{{{0x8664,0x6AA482FD,0x12C0C000,0x128E3A0F},{0x8664,0x6AA482FD,0x12C0D000,0x128DA158}}},240802816,
        {0x0C,0xE1,0x02,0x66,0x51,0x41,0x1C,0xA9,0x46,0x36,0xB2,0xB7,0x13,0xF4,0xB7,0xD6,0x83,0x8A,0xDC,0x3D,0x65,0x2A,0x85,0xDD,0xCC,0xDF,0x54,0xE5,0x3D,0xE0,0xB4,0xF5}},
    {GameVersion::V52,"1.26.52.03",{{{0x8664,0x6AB54E37,0x12C01000,0x128CD801},{0x8664,0x6AB54E37,0x12C02000,0x128DA293}}},240802816,
        {0x6F,0xA2,0x18,0xD2,0x47,0x87,0x49,0x40,0x23,0x5A,0xAC,0xB8,0xFD,0x7E,0x3B,0xBF,0x0E,0x3D,0x1E,0x25,0x96,0x30,0xD7,0xF8,0x3F,0xCB,0x53,0x6F,0x06,0x73,0x3A,0xEB}}
};
}
ImageProfile const* SelectImageProfile(Identity const& identity) noexcept {
    for(auto const& profile:imageProfiles)for(auto const& known:profile.identities)
        if(identity.machine==known.machine && identity.timestamp==known.timestamp &&
           identity.imageSize==known.imageSize && identity.checksum==known.checksum)return &profile;
    return nullptr;
}
bool IdentityAccepted(Identity const& id) noexcept {return SelectImageProfile(id)!=nullptr;}
bool MatchGuard(std::span<std::uint8_t const> bytes, Guard const& guard) noexcept {
    return bytes.size() >= guard.bytes.size() && std::equal(guard.bytes.begin(), guard.bytes.end(), bytes.begin());
}
namespace {
bool HashText(std::uint8_t const* data, std::size_t size, std::array<std::uint8_t, 32>& hash) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE handle{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    DWORD objectSize{}, used{};
    bool success = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                                      sizeof objectSize, &used, 0) >= 0;
    std::vector<std::uint8_t> object(success ? objectSize : 0);
    if (success) success = BCryptCreateHash(algorithm, &handle, object.data(), objectSize, nullptr, 0, 0) >= 0;
    if (success) success = BCryptHashData(handle, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) >= 0;
    if (success) success = BCryptFinishHash(handle, hash.data(), static_cast<ULONG>(hash.size()), 0) >= 0;
    if (handle) BCryptDestroyHash(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}
}
ImageProfile const* InspectFileImage(std::span<std::uint8_t const> bytes) noexcept {
    try {
    auto data=bytes.data();auto size=bytes.size();
    if (size < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 4096) return nullptr;
    auto pe = static_cast<std::size_t>(dos->e_lfanew);
    if (pe>size || sizeof(IMAGE_NT_HEADERS64)>size-pe) return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(data + pe);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.ImageBase!=0x140000000 || !nt->FileHeader.NumberOfSections ||
        nt->FileHeader.NumberOfSections>96)return nullptr;
    auto profile=SelectImageProfile({nt->FileHeader.Machine,nt->FileHeader.TimeDateStamp,
                                    nt->OptionalHeader.SizeOfImage,nt->OptionalHeader.CheckSum});
    if(!profile)return nullptr;
    auto tableOffset = pe + 24 + nt->FileHeader.SizeOfOptionalHeader;
    if (tableOffset + nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > size) return nullptr;
    auto section = reinterpret_cast<IMAGE_SECTION_HEADER const*>(data + tableOffset);
    IMAGE_SECTION_HEADER const* text{};
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text\0\0\0", 8)) continue;
        if(text || section->VirtualAddress!=0x1000 || section->SizeOfRawData!=profile->textBytes ||
           !(section->Characteristics&IMAGE_SCN_MEM_EXECUTE) || section->PointerToRawData<tableOffset+nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER) ||
           section->PointerToRawData>size || section->SizeOfRawData>size-section->PointerToRawData)return nullptr;
        text=section;
    }
    if(!text)return nullptr;
    std::array<std::uint8_t,32> hash{};
    return HashText(data+text->PointerToRawData,text->SizeOfRawData,hash) && hash==profile->textHash?profile:nullptr;
    }catch(...){return nullptr;}
}
bool VerifyFileText(std::filesystem::path const& path, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot open game executable"; return false; }
    LARGE_INTEGER length{};
    bool success = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart < (1LL << 32);
    HANDLE mapping = success ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    auto data = mapping ? static_cast<std::uint8_t const*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    success = data && InspectFileImage({data,static_cast<std::size_t>(length.QuadPart)});
    if (data) UnmapViewOfFile(data);
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    error = success ? "" : "Unsupported executable identity or .text SHA-256";
    return success;
}
bool ValidateModule(HMODULE game, std::string& error) {return ValidateNativeProfile(game,error)!=nullptr;}
}
