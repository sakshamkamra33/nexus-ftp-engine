// ============================================================================
// config.h — Simple key=value config file parser
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ftp {

class Config {
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            // Strip \r for cross-platform
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            data_[key] = val;
        }
        return true;
    }

    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : def;
    }

    int getInt(const std::string& key, int def = 0) const {
        auto it = data_.find(key);
        if (it == data_.end()) return def;
        try { return std::stoi(it->second); }
        catch (...) { return def; }
    }

    bool getBool(const std::string& key, bool def = false) const {
        auto it = data_.find(key);
        if (it == data_.end()) return def;
        std::string v = it->second;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        return (v == "true" || v == "1" || v == "yes");
    }

private:
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        size_t end   = s.find_last_not_of(" \t");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    std::unordered_map<std::string, std::string> data_;
};

} // namespace ftp
