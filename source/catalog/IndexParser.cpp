#include <catalog/IndexParser.hpp>

#include <cctype>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace pinx::catalog {
namespace {

using json = nlohmann::json;

std::string ToLower(std::string s) {
    for(char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string ToUpper(std::string s) {
    for(char &c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string StripFragment(const std::string &url) {
    const auto hash = url.find('#');
    return (hash == std::string::npos) ? url : url.substr(0, hash);
}

std::string BaseName(const std::string &url) {
    std::string u = url;
    const auto cut = u.find_first_of("?#");
    if(cut != std::string::npos) {
        u = u.substr(0, cut);
    }
    const auto slash = u.find_last_of('/');
    const std::string name = (slash == std::string::npos) ? u : u.substr(slash + 1);
    return name.empty() ? u : name;
}

std::string ExtensionOf(const std::string &name) {
    const auto dot = name.find_last_of('.');
    if(dot == std::string::npos) {
        return "";
    }
    return ToLower(name.substr(dot + 1));
}

std::string Resolve(const std::string &url, const std::string &base) {
    if(url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return url;
    }
    if(base.empty()) {
        return url;
    }
    const bool base_slash = base.back() == '/';
    const bool url_slash  = !url.empty() && url.front() == '/';
    if(base_slash && url_slash) {
        return base.substr(0, base.size() - 1) + url;
    }
    if(!base_slash && !url_slash) {
        return base + "/" + url;
    }
    return base + url;
}

std::string ExtractTitleId(const std::string &filename) {
    auto dot = filename.find_last_of('.');
    std::string stem = (dot == std::string::npos) ? filename : filename.substr(0, dot);
    stem = ToUpper(stem);
    if(stem.size() != 16) {
        return "";
    }
    for(char c : stem) {
        if(!std::isxdigit(static_cast<unsigned char>(c))) {
            return "";
        }
    }
    return stem;
}

struct TitleMeta {
    std::string   name;
    std::uint64_t size = 0;
    std::string   version;
    std::string   icon_url;
};

std::uint64_t AsUint(const json &j) {
    if(j.is_number_unsigned()) {
        return j.get<std::uint64_t>();
    }
    if(j.is_number_integer()) {
        const long long v = j.get<long long>();
        return v > 0 ? static_cast<std::uint64_t>(v) : 0;
    }
    return 0;
}

std::unordered_map<std::string, TitleMeta> ParseTitledb(const json &j) {
    std::unordered_map<std::string, TitleMeta> db;
    if(!j.contains("titledb") || !j["titledb"].is_object()) {
        return db;
    }
    for(auto it = j["titledb"].begin(); it != j["titledb"].end(); ++it) {
        const json &v = it.value();
        if(!v.is_object()) {
            continue;
        }
        TitleMeta m;
        if(v.contains("name") && v["name"].is_string()) {
            m.name = v["name"].get<std::string>();
        }
        if(v.contains("size")) {
            m.size = AsUint(v["size"]);
        }
        if(v.contains("version")) {
            if(v["version"].is_string()) {
                m.version = v["version"].get<std::string>();
            }
            else if(v["version"].is_number_integer()) {
                const long long ver = v["version"].get<long long>();
                if(ver > 0) {
                    m.version = std::to_string(ver);
                }
            }
        }
        if(v.contains("iconUrl") && v["iconUrl"].is_string()) {
            m.icon_url = v["iconUrl"].get<std::string>();
        }
        db.emplace(ToUpper(it.key()), std::move(m));
    }
    return db;
}

}

ParseResult ParseIndex(const std::string &body, const std::string &base_url) {
    ParseResult r;

    json j;
    try {
        j = json::parse(body);
    }
    catch(const std::exception &e) {
        r.error = std::string("invalid JSON: ") + e.what();
        return r;
    }

    if(!j.is_object()) {
        r.error = "index is not a JSON object";
        return r;
    }

    if(j.contains("error") && j["error"].is_string()) {
        const std::string err = j["error"].get<std::string>();
        if(!err.empty()) {
            r.error = err;
            return r;
        }
    }
    if(j.contains("success") && j["success"].is_string()) {
        r.message = j["success"].get<std::string>();
    }

    const std::unordered_map<std::string, TitleMeta> titledb = ParseTitledb(j);

    if(j.contains("directories") && j["directories"].is_array()) {
        for(const auto &d : j["directories"]) {
            if(!d.is_string()) {
                continue;
            }
            CatalogItem it;
            it.kind = EntryKind::Directory;
            it.url  = Resolve(StripFragment(d.get<std::string>()), base_url);
            it.name = BaseName(it.url);
            r.items.push_back(std::move(it));
        }
    }

    if(j.contains("files") && j["files"].is_array()) {
        for(const auto &f : j["files"]) {
            CatalogItem it;
            it.kind = EntryKind::File;

            std::string raw_url;
            if(f.is_string()) {
                raw_url = f.get<std::string>();
            }
            else if(f.is_object()) {
                if(f.contains("url") && f["url"].is_string()) {
                    raw_url = f["url"].get<std::string>();
                }
                if(f.contains("size")) {
                    it.size = AsUint(f["size"]);
                    it.size_authoritative = it.size > 0;
                }
                if(f.contains("name") && f["name"].is_string()) {
                    it.name = f["name"].get<std::string>();
                }
                if(f.contains("version") && f["version"].is_string()) {
                    it.version = f["version"].get<std::string>();
                }
                for(const char *hk : {"sha256", "hash"}) {
                    if(f.contains(hk) && f[hk].is_string()) {
                        it.sha256 = ToLower(f[hk].get<std::string>());
                        break;
                    }
                }
            }
            else {
                continue;
            }

            if(raw_url.empty()) {
                continue;
            }

            const std::string clean = StripFragment(raw_url);
            it.url      = Resolve(clean, base_url);
            it.filename = BaseName(clean);
            it.format   = ExtensionOf(it.filename);

            const std::string tid = ExtractTitleId(it.filename);
            if(!tid.empty()) {
                try { it.title_id = std::stoull(tid, nullptr, 16); } catch(...) {}

                const auto found = titledb.find(tid);
                if(found != titledb.end()) {
                    const TitleMeta &m = found->second;
                    if(it.name.empty())    it.name     = m.name;
                    if(it.size == 0)       it.size     = m.size;
                    if(it.version.empty()) it.version  = m.version;
                    it.icon_url = m.icon_url;
                }
            }
            for(const char *k : {"tid", "title_id", "titleId"}) {
                if(f.is_object() && f.contains(k)) {
                    const auto &tv = f[k];
                    if(tv.is_string()) {
                        try { it.title_id = std::stoull(tv.get<std::string>(), nullptr, 16); } catch(...) {}
                    } else if(tv.is_number_unsigned()) {
                        it.title_id = tv.get<std::uint64_t>();
                    }
                    break;
                }
            }

            if(it.name.empty()) {
                it.name = it.filename;
            }
            r.items.push_back(std::move(it));
        }
    }

    r.ok = true;
    return r;
}

}
