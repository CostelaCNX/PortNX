#pragma once

#include <string>

namespace pinx::app {

// Persisted app configuration in sdmc:/switch/PortNX/config.json.
struct Config {
    std::string server_url;
    std::string language;           // "" = system, "en-US", "pt-BR"

    // Install options
    bool install_to_nand  = false;  // false = SD card (default)
    bool force_reinstall  = false;

    static Config Load();
    bool Save() const;

    static std::string Dir();
    static std::string Path();
};

}
