// Config.hpp
#pragma once
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>

struct Config {
    std::string hashindex_file;
    std::string zones_csv_file;
    std::string spatial_eta_path;
    int time_zoning_type;
    std::string routingengine_server;
    std::string engine;
    std::string aggregate_type;

    static Config load(const std::string& path) {
        // Parse key=value file
        std::map<std::string, std::string> kv;
        std::ifstream f(path);
        if (!f.is_open()) throw std::runtime_error("Cannot open config: " + path);

        std::string line;
        while (std::getline(f, line)) {
            // Strip comments and empty lines
            if (line.empty() || (line[0] == '/' && line[1] == '/') || line[0] == '#' || line[0] == ';') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            kv[key] = val;
        }

        Config c;
        c.hashindex_file       = get(kv, "hashindex_file");
        c.zones_csv_file       = get(kv, "zones_csv_file");
        c.spatial_eta_path     = get(kv, "spatial_eta_path");
        c.time_zoning_type     = std::stoi(get(kv, "time_zoning_type"));
        c.routingengine_server = get(kv, "routingengine_server");
        c.engine               = get(kv, "engine");
        c.aggregate_type       = get(kv, "aggregate_type");
        return c;
    }

private:
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end   = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    static std::string get(const std::map<std::string, std::string>& kv,
                           const std::string& key) {
        auto it = kv.find(key);
        if (it == kv.end()) throw std::runtime_error("Missing config key: " + key);
        return it->second;
    }
};