#pragma once
#include "Config.h"
#include <cstdint>
#include <filesystem>
#include <string>

namespace autorestock {
enum class ReloadResult { NotDue, Unchanged, Applied, Rejected };
// Worker-owned, not a game-thread file reader. Publish Value() as one immutable
// settings snapshot; a bad or temporarily missing file never replaces it.
class ConfigReload {
public:
    explicit ConfigReload(std::filesystem::path path) : path_(std::move(path)) {}
    ReloadResult Poll(std::uint64_t now);
    Config Value() const noexcept { return value_; }
    std::string const& Error() const noexcept { return error_; }
private:
    std::filesystem::path path_;
    Config value_;
    std::string error_;
    std::uint64_t lastPoll_{};
    bool polled_{};
};
}
