#include "AutoRestock/Config.h"
#include <json.hpp>
#include <fstream>
#include <array>
#include <iterator>
#include <set>
#include <vector>
namespace autorestock {
bool ParseConfig(std::string_view text, Config& out, std::string& error) {
    try {
        if (text.size() > 16384) throw std::runtime_error("Config exceeds 16 KiB");
        // Reject duplicate fields, rather than silently accepting the final value.
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            using Event = nlohmann::json::parse_event_t;
            if (event == Event::object_start) keys.emplace_back();
            if (event == Event::key && !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate config field");
            if (event == Event::object_end) keys.pop_back();
            return true;
        };
        auto json = nlohmann::json::parse(text, callback);
        if (!json.is_object()) throw std::runtime_error("Config must be a JSON object");
        for (auto const& [key, value] : json.items()) {
            if (key != "enabled" && key != "preRestockThreshold")
                throw std::runtime_error("Unknown config field: " + key);
        }
        Config next;
        if (json.contains("enabled")) {
            if (!json["enabled"].is_boolean()) throw std::runtime_error("enabled must be boolean");
            next.enabled = json["enabled"].get<bool>();
        }
        if (json.contains("preRestockThreshold")) {
            auto const& value = json["preRestockThreshold"];
            if (!value.is_number_integer() || value < 1 || value > 64)
                throw std::runtime_error("preRestockThreshold must be an integer from 1 to 64");
            next.preRestockThreshold = value.get<int>();
        }
        out = next;
        error.clear();
        return true;
    } catch (std::exception const& e) { error = e.what(); return false; }
}
bool ReadConfig(std::istream& input,Config& out,std::string& error){
    std::array<char,16385> buffer{};
    input.read(buffer.data(),buffer.size());auto size=input.gcount();
    if(size>16384){error="Config exceeds 16 KiB";return false;}
    if(input.bad()){error="Cannot read config";return false;}
    return ParseConfig(std::string_view(buffer.data(),static_cast<std::size_t>(size)),out,error);
}
bool LoadConfig(std::filesystem::path const& path, Config& out, std::string& error) {
    try {
        if (std::filesystem::file_size(path) > 16384) {
            error = "Config exceeds 16 KiB"; return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) { error = "Cannot open config"; return false; }
        return ReadConfig(stream,out,error);
    } catch (std::exception const& e) { error = e.what(); return false; }
}
}
