#include <app/I18n.hpp>

#include <cstdio>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pinx::i18n {
namespace {

using json = nlohmann::json;

std::unordered_map<std::string, std::string> g_strings;
static const std::string g_empty;

static std::string readFile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return {};
    std::string buf;
    char chunk[1024];
    std::size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf.append(chunk, n);
    fclose(f);
    return buf;
}

static void loadLang(const std::string &lang_code) {
    const std::string path = "romfs:/i18n/" + lang_code + "/portnx.json";
    const std::string raw  = readFile(path.c_str());
    if (raw.empty()) return;

    json j;
    try { j = json::parse(raw); } catch (...) { return; }

    for (auto &[section, vals] : j.items()) {
        if (!vals.is_object()) continue;
        for (auto &[key, val] : vals.items()) {
            if (!val.is_string()) continue;
            g_strings[section + "." + key] = val.get<std::string>();
        }
    }
}

} // namespace

void Init(const std::string &lang_code) {
    g_strings.clear();
    loadLang("en-US");
    if (lang_code == "pt-BR")
        loadLang("pt-BR");
}

const std::string &tr(const std::string &key) {
    auto it = g_strings.find(key);
    return (it != g_strings.end()) ? it->second : key;
}

std::string trf(const std::string &key, std::initializer_list<std::string> args) {
    std::string s = tr(key);
    std::size_t idx = 0;
    for (const auto &arg : args) {
        const std::string ph = "{" + std::to_string(idx++) + "}";
        std::size_t pos = 0;
        while ((pos = s.find(ph, pos)) != std::string::npos) {
            s.replace(pos, ph.size(), arg);
            pos += arg.size();
        }
    }
    return s;
}

} // namespace pinx::i18n
