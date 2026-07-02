#include <app/Config.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace pinx::app {

using json = nlohmann::json;

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
