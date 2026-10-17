#include "AutoRestock/Config.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("FAIL: " #x); } while (false)
using namespace autorestock;
int main() {
    try {
        Config config;
        std::string error;
        CHECK(ParseConfig("{}", config, error));
        CHECK(config.enabled && config.preRestockThreshold == 6);
        CHECK(ParseConfig(R"({"enabled":false,"preRestockThreshold":1})", config, error));
        CHECK(!config.enabled && config.preRestockThreshold == 1);
        CHECK(ParseConfig(R"({"enabled":true,"preRestockThreshold":64})", config, error));
        auto previous = config;
        for (auto text : {"{", "[]", "null", R"({"enabled":1})", R"({"enabled":"false"})",
             R"({"preRestockThreshold":0})", R"({"preRestockThreshold":65})",
             R"({"preRestockThreshold":6.0})", R"({"preRestockThreshold":true})",
             R"({"preRestockThreshold":18446744073709551615})", R"({"toggleKey":"F5"})",
             R"({"enabled":true,"enabled":false})"}) {
            CHECK(!ParseConfig(text, config, error));
            CHECK(config == previous && !error.empty());
        }
        CHECK(!ParseConfig(std::string(16385, ' '), config, error));
        std::istringstream growing(std::string(1024*1024,' '));
        CHECK(!ReadConfig(growing,config,error));
        CHECK(growing.tellg()==16385 && config==previous); // actual read stays bounded even if a file grows after file_size
        std::istringstream exact(std::string(16382,' ')+"{}");
        CHECK(ReadConfig(exact,config,error));CHECK(config==Config{});
        config=previous;
        auto missing = std::filesystem::path("AutoRestock-nonexistent-test-config.json");
        CHECK(!std::filesystem::exists(missing));
        CHECK(!LoadConfig(missing, config, error));
        CHECK(config == previous && !std::filesystem::exists(missing));
        std::cout << "PASS: defaults, valid edges, strict types/range/keys/duplicates, invalid retains prior config, missing file not rewritten\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
