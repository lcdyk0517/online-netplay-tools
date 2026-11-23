#pragma once

#include <map>
#include <string>
#include <vector>

struct LocalGame {
    bool found = false;
    bool by_crc = false;
    std::string path;
};

struct CoreRef {
    std::string path64, path32;
};

struct EsSystem {
    std::string name, fullname, path, exts;
    std::vector<std::string> cores64, cores32;
    std::vector<std::string> hostable;  // installed + netplay-capable, ES order
};

struct GameEntry {
    std::string name, path;
};

std::map<std::string, CoreRef> scan_cores();
void scan_playlists(std::map<std::string, std::string> &crcs);
LocalGame find_game(const std::string &name, const std::string &crc,
                    const std::map<std::string, std::string> &names,
                    const std::map<std::string, std::string> &crcs);
std::string find_core(const std::string &corename,
                      const std::map<std::string, CoreRef> &cores,
                      std::string &out32);

std::vector<EsSystem> load_es_systems(const char *path);

// Hosting gate: only cores that ship a .info file are offered; a missing
// .info means "cannot host" (dArkOS ships .info next to every core it
// supports). savestate=false or savestate_features=basic|serialized also
// disqualify the core.
bool core_info_netplay(const std::string &key, bool is64);

void scan_es_games(std::map<std::string, std::string> &names);

bool dir_has_game(const std::string &dir, const std::string &exts, int depth);
void scan_games_dir(const std::string &dir, const std::string &exts, int depth,
                    std::vector<GameEntry> &out);
