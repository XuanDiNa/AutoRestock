#include "AutoRestock/ConfigReload.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL at " + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace autorestock;
struct Fixture {
    std::filesystem::path dir,path;
    Fixture() {
        dir=std::filesystem::temp_directory_path()/
            ("AutoRestock-config-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        CHECK(std::filesystem::create_directory(dir)); path=dir/"config.json";
    }
    ~Fixture() { std::error_code e; std::filesystem::remove(path,e); std::filesystem::remove(dir,e); }
    void Write(std::string const& value) {
        std::ofstream s(path,std::ios::binary|std::ios::trunc); CHECK(s); s<<value; CHECK(s);
    }
    std::string Read() {
        std::ifstream s(path,std::ios::binary); return {std::istreambuf_iterator<char>(s),{}};
    }
};
int main() {
    try {
        Fixture f; ConfigReload reload(f.path);
        CHECK(reload.Value()==Config{});
        // Removing file, loading invalid JSON or transient partial writes must
        // not re-enable a previously disabled setting or rewrite the file.
        CHECK(reload.Poll(0)==ReloadResult::Rejected);
        CHECK(!std::filesystem::exists(f.path));
        CHECK(!reload.Error().empty());
        f.Write(R"({"enabled":false,"preRestockThreshold":6})");
        CHECK(reload.Poll(499)==ReloadResult::NotDue);
        CHECK(reload.Poll(500)==ReloadResult::Applied);
        CHECK(!reload.Value().enabled && reload.Error().empty());
        auto stamp=std::filesystem::last_write_time(f.path);
        f.Write(R"({"enabled":false,"preRestockThreshold":9})");
        std::filesystem::last_write_time(f.path,stamp);
        CHECK(reload.Poll(999)==ReloadResult::NotDue);
        CHECK(reload.Poll(1000)==ReloadResult::Applied); // same timestamp + size
        CHECK(reload.Value().preRestockThreshold==9);
        CHECK(reload.Poll(1500)==ReloadResult::Unchanged);
        auto saved=reload.Value();
        f.Write("{");
        CHECK(reload.Poll(2000)==ReloadResult::Rejected);
        CHECK(reload.Value()==saved && f.Read()=="{");
        f.Write(R"({"enabled":true,"enabled":false})");
        auto invalid=f.Read();
        CHECK(reload.Poll(2500)==ReloadResult::Rejected);
        CHECK(reload.Value()==saved && f.Read()==invalid);
        CHECK(std::filesystem::remove(f.path));
        CHECK(reload.Poll(3000)==ReloadResult::Rejected);
        CHECK(reload.Value()==saved && !std::filesystem::exists(f.path));
        f.Write(R"({"enabled":true,"preRestockThreshold":1})");
        CHECK(reload.Poll(3500)==ReloadResult::Applied);
        CHECK(reload.Value().enabled && reload.Value().preRestockThreshold==1);
        f.Write(R"({"enabled":false,"preRestockThreshold":64})");
        CHECK(reload.Poll(1)==ReloadResult::Applied); // monotonic-clock reset
        CHECK(!reload.Value().enabled && reload.Value().preRestockThreshold==64);
        f.Write(std::string(16385,' '));
        CHECK(reload.Poll(501)==ReloadResult::Rejected);
        CHECK(!reload.Value().enabled && reload.Value().preRestockThreshold==64);
        CHECK(std::filesystem::file_size(f.path)==16385);
        std::cout<<"PASS: 500-ms reload, same-stamp edits, invalid/missing/oversized retains settings, no rewriting\n";
        return 0;
    } catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
