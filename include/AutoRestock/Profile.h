#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
namespace autorestock {
struct Identity { std::uint16_t machine; std::uint32_t timestamp,imageSize,checksum; };
struct Guard { std::uint32_t rva; std::array<std::uint8_t,32> bytes; };
enum class GameVersion { V45, V51, V52 };
// File identity is distinct from permission to bind native APIs. This catalog
// alone never admits a native layout, installs a hook, or supplies a fallback.
struct ImageProfile {
    GameVersion version;
    char const* name;
    std::array<Identity,2> identities;
    std::uint32_t textBytes;
    std::array<std::uint8_t,32> textHash;
};
ImageProfile const* SelectImageProfile(Identity const&) noexcept;
ImageProfile const* InspectFileImage(std::span<std::uint8_t const>) noexcept;
bool IdentityAccepted(Identity const&) noexcept;
bool MatchGuard(std::span<std::uint8_t const>,Guard const&) noexcept;
bool VerifyFileText(std::filesystem::path const&,std::string& error);
bool ValidateModule(HMODULE,std::string& error);
}
