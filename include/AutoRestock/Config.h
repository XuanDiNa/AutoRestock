#pragma once
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>

namespace autorestock {
struct Config {
    bool enabled = true;
    int preRestockThreshold = 6;
    bool operator==(Config const&) const = default;
};
bool ParseConfig(std::string_view text, Config& out, std::string& error);
bool ReadConfig(std::istream& input,Config& out,std::string& error);
bool LoadConfig(std::filesystem::path const& path, Config& out, std::string& error);
}
