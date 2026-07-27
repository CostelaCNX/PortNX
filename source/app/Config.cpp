#include <app/Config.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace pinx::app {
namespace {

using json = nlohmann::json;

std::string DefaultServerUrl() {
    constexpr unsigned char kKey = 0x5A;
    constexpr unsigned char kEncoded[] = {
        0x32, 0x2E, 0x2E, 0x2A, 0x29, 0x60, 0x75, 0x75,
        0x2A, 0x35, 0x28, 0x2E, 0x34, 0x22, 0x74, 0x39,
        0x35, 0x29, 0x2E, 0x3F, 0x36, 0x3B, 0x38, 0x28,
        0x74, 0x39, 0x35, 0x37, 0x74, 0x38, 0x28, 0x75,
        0x33, 0x34, 0x3E, 0x3F, 0x22, 0x74, 0x2E, 0x3C,
        0x36
    };

    std::string url;
    url.reserve(sizeof(kEncoded));
    for(const unsigned char b : kEncoded) {
        url.push_back(static_cast<char>(b ^ kKey));
    }
    return url;
}

}

std::string Config::Dir()  { return "sdmc:/switch/PortNX"; }
std::string Config::Path() { return Dir() + "/config.json"; }

Config Config::Load() {
    Config c;
    std::ifstream in(Path());
    if(!in.is_open()) return c;
    try {
        json j;
        in >> j;
        if(j.contains("server_url") && j["server_url"].is_string())
            c.server_url = j["server_url"].get<std::string>();

        if(c.server_url.empty() && j.contains("servers") && j["servers"].is_array()) {
            const auto &arr = j["servers"];
            if(!arr.empty() && arr[0].is_object() && arr[0].contains("url")
               && arr[0]["url"].is_string())
                c.server_url = arr[0]["url"].get<std::string>();
        }

        if(j.contains("install_to_nand") && j["install_to_nand"].is_boolean())
            c.install_to_nand = j["install_to_nand"].get<bool>();
        if(j.contains("force_reinstall") && j["force_reinstall"].is_boolean())
            c.force_reinstall = j["force_reinstall"].get<bool>();
        if(j.contains("language") && j["language"].is_string())
            c.language = j["language"].get<std::string>();
    }
    catch(...) {}
    if(c.language.empty()) c.language = "pt-BR";
    return c;
}

std::string Config::EffectiveServerUrl() const {
    return server_url.empty() ? DefaultServerUrl() : server_url;
}

bool Config::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(Dir(), ec);

    json j;
    j["server_url"]      = server_url;
    j["install_to_nand"] = install_to_nand;
    j["force_reinstall"] = force_reinstall;
    j["language"]        = language;

    std::ofstream out(Path(), std::ios::trunc);
    if(!out.is_open()) return false;
    out << j.dump(2);
    return out.good();
}

}
