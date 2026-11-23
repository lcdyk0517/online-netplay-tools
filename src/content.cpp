#include "content.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "util.h"

// ---------------- local content index ----------------
void scan_playlists(std::map<std::string, std::string> &crcs) {
    std::string root = "/home/ark/.config/retroarch/playlists";
    DIR *d = opendir(root.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (!ends_with(n, ".lpl")) continue;
        std::ifstream in(root + "/" + n);
        std::string line, path, crc;
        while (std::getline(in, line)) {
            auto val = [&](const char *key, std::string &out) {
                size_t p = line.find(std::string("\"") + key + "\"");
                if (p == std::string::npos) return false;
                p = line.find(':', p);
                if (p == std::string::npos) return false;
                p = line.find('"', p);
                if (p == std::string::npos) return false;
                size_t q = line.find('"', p + 1);
                if (q == std::string::npos) return false;
                out = line.substr(p + 1, q - p - 1); return true;
            };
            if (val("path", path) && val("crc32", crc) && !path.empty()) crcs[lower(crc)] = path;
        }
    }
    closedir(d);
}

std::map<std::string, CoreRef> scan_cores() {
    std::map<std::string, CoreRef> cores;
    const char *dirs[2] = { "/home/ark/.config/retroarch/cores", "/home/ark/.config/retroarch32/cores" };
    for (int k = 0; k < 2; k++) {
        DIR *d = opendir(dirs[k]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (!ends_with(n, "_libretro.so")) continue;
            std::string key = n.substr(0, n.size() - strlen("_libretro.so"));
            std::string p = std::string(dirs[k]) + "/" + n;
            if (k == 0) cores[key].path64 = p; else cores[key].path32 = p;
        }
        closedir(d);
    }
    return cores;
}

LocalGame find_game(const std::string &name, const std::string &crc,
                           const std::map<std::string, std::string> &names,
                           const std::map<std::string, std::string> &crcs) {
    LocalGame g;
    if (!crc.empty()) {
        auto it = crcs.find(lower(crc));
        if (it != crcs.end() && access(it->second.c_str(), R_OK) == 0) {
            g.found = true; g.by_crc = true; g.path = it->second;
            return g;
        }
    }
    std::string n = norm(name);
    auto it = names.find(n);
    if (it == names.end()) it = names.find(norm(base_stem(name)));
    if (it != names.end()) { g.found = true; g.path = it->second; return g; }
    /* Loose match for translations/scene tags. Prefix matches are trusted;
     * substring matches must be close in length so that e.g.
     * "supermariobrospc10" does not pick "mariobros". */
    if (n.size() >= 4) {
        int best_score = 99;
        size_t best_delta = (size_t)-1;
        std::string best_path, tie_path;
        bool ambiguous = false;
        for (auto &kv : names) {
            const std::string &k = kv.first;
            if (k.size() < 4) continue;
            int score = 99;
            if (k.rfind(n, 0) == 0 || n.rfind(k, 0) == 0) score = 1;
            else if (k.find(n) != std::string::npos || n.find(k) != std::string::npos) {
                size_t big = std::max(k.size(), n.size());
                size_t small = std::min(k.size(), n.size());
                if (big - small <= std::max<size_t>(4, n.size() / 4)) score = 2;
            }
            if (score >= 99) continue;
            size_t delta = k.size() > n.size() ? k.size() - n.size() : n.size() - k.size();
            if (score < best_score || (score == best_score && delta < best_delta)) {
                best_score = score;
                best_delta = delta;
                best_path = kv.second;
                ambiguous = false;
            } else if (score == best_score && delta == best_delta &&
                       !best_path.empty() && kv.second != best_path) {
                /* Same rank, different file: refuse to guess. */
                ambiguous = true;
                tie_path = kv.second;
            }
        }
        (void)tie_path;
        if (!best_path.empty() && !ambiguous) { g.found = true; g.path = best_path; }
    }
    return g;
}

std::string find_core(const std::string &corename, const std::map<std::string, CoreRef> &cores, std::string &out32) {
    auto resolve = [&](std::map<std::string, CoreRef>::const_iterator it) -> std::string {
        out32 = it->second.path32;
        return it->second.path64.empty() ? it->second.path32 : it->second.path64;
    };
    auto pick = [&](std::initializer_list<const char *> names) -> std::string {
        for (const char *nm : names) {
            auto j = cores.find(nm);
            if (j != cores.end()) return resolve(j);
        }
        return "";
    };

    std::string n = norm(corename);
    auto it = cores.find(corename);
    if (it == cores.end()) {
        for (auto &kv : cores) if (norm(kv.first) == n) { it = cores.find(kv.first); break; }
    }
    if (it != cores.end()) return resolve(it);

    /* RetroArch advertises the core's library_name (netplay_frontend.c uses
     * system->library_name), which for several cores does not match the .so
     * filename. Normalise the common renames and retry so an installed core
     * is never reported as missing. */
    if (n == "finalburnneo" || n == "finalburnneoplus" || n == "fbneoplus" ||
        n == "fbneo" || n == "finalburn") {
        std::string r = pick({ "fbneo", "fbneo_plus", "fbneo_32b", "fbalpha2012_32b" });
        if (!r.empty()) return r;
    }
    if (n.rfind("beetle", 0) == 0) {
        std::string a = "mednafen" + n.substr(6);
        auto j = cores.find(a);
        if (j != cores.end()) return resolve(j);
        /* Prefer an exact-normalised match, then the most specific (longest)
         * candidate so "Beetle PCE Fast" does not collapse onto "PCE". */
        for (auto &kv : cores) if (norm(kv.first) == a) return resolve(cores.find(kv.first));
        std::map<std::string, CoreRef>::const_iterator best = cores.end();
        size_t bestLen = 0;
        for (auto &kv : cores) {
            std::string k = norm(kv.first);
            if (k.find(a) == std::string::npos && a.find(k) == std::string::npos) continue;
            if (k.size() > bestLen) { bestLen = k.size(); best = cores.find(kv.first); }
        }
        if (best != cores.end()) return resolve(best);
    }

    for (auto &kv : cores) {
        std::string k = norm(kv.first);
        if (k.find(n) != std::string::npos || n.find(k) != std::string::npos) { it = cores.find(kv.first); break; }
    }
    if (it != cores.end()) return resolve(it);
    return "";
}



// ---------------- EmulationStation systems (host picker) ----------------
std::string xml_tag(const std::string &b, const char *tag) {
    std::string open = std::string("<") + tag + ">";
    std::string close = std::string("</") + tag + ">";
    size_t a = b.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t e = b.find(close, a);
    if (e == std::string::npos) return "";
    return trim_str(b.substr(a, e - a));
}

std::vector<std::string> xml_emulator_cores(const std::string &b, const char *emu) {
    std::vector<std::string> out;
    std::string open = std::string("<emulator name=\"") + emu + "\">";
    size_t a = b.find(open);
    if (a == std::string::npos) return out;
    a += open.size();
    size_t e = b.find("</emulator>", a);
    if (e == std::string::npos) return out;
    std::string body = b.substr(a, e - a);
    size_t p = 0;
    while ((p = body.find("<core>", p)) != std::string::npos) {
        p += 6;
        size_t q = body.find("</core>", p);
        if (q == std::string::npos) break;
        out.push_back(trim_str(body.substr(p, q - p)));
        p = q + 7;
    }
    return out;
}

std::string quoted_value(const std::string &line) {
    size_t a = line.find('"');
    if (a == std::string::npos) return "";
    size_t b = line.find('"', a + 1);
    if (b == std::string::npos) return "";
    return line.substr(a + 1, b - a - 1);
}

// Hosting gate: only cores that ship a .info file are offered. A missing
// .info is treated as "cannot host" (dArkOS ships .info next to every core
// it supports); savestate=false or savestate_features=basic|serialized also
// disqualify the core.
bool core_info_netplay(const std::string &key, bool is64) {
    std::string path = std::string(is64 ? "/home/ark/.config/retroarch/cores/"
                                        : "/home/ark/.config/retroarch32/cores/")
                     + key + "_libretro.info";
    std::ifstream in(path);
    if (!in) return false;
    std::string sv, sf, line;
    while (std::getline(in, line)) {
        std::string t = trim_str(line);
        if (t.rfind("savestate_features", 0) == 0) sf = quoted_value(t);
        else if (t.rfind("savestate", 0) == 0) sv = quoted_value(t);
    }
    if (sv == "false") return false;
    if (sf == "basic" || sf == "serialized") return false;
    return true;
}

std::vector<EsSystem> load_es_systems(const char *path) {
    std::vector<EsSystem> out;
    std::ifstream f(path);
    if (!f) return out;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string xml = ss.str();
    size_t p = 0;
    while ((p = xml.find("<system>", p)) != std::string::npos) {
        size_t e = xml.find("</system>", p);
        if (e == std::string::npos) break;
        std::string b = xml.substr(p, e - p);
        EsSystem s;
        s.name = xml_tag(b, "name");
        s.fullname = xml_tag(b, "fullname");
        s.path = xml_tag(b, "path");
        s.exts = xml_tag(b, "extension");
        s.cores64 = xml_emulator_cores(b, "retroarch");
        s.cores32 = xml_emulator_cores(b, "retroarch32");
        if (s.cores64.empty() && s.cores32.empty()) {
            std::string cmd = xml_tag(b, "command");
            size_t at = cmd.find("/retroarch/cores/");
            if (at != std::string::npos) {
                size_t st = at + strlen("/retroarch/cores/");
                size_t en = cmd.find("_libretro.so", st);
                if (en != std::string::npos) s.cores64.push_back(cmd.substr(st, en - st));
            }
        }
        if (!s.name.empty() && !s.path.empty()) out.push_back(s);
        p = e + strlen("</system>");
    }
    return out;
}

static bool ext_ok(const std::string &file, const std::string &exts) {
    size_t dot = file.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = lower(file.substr(dot));
    std::stringstream ss(exts);
    std::string t;
    while (ss >> t) if (lower(t) == e) return true;
    return false;
}

static void list_files_rec(const std::string &dir, std::vector<std::string> &out, int depth) {
    if (depth > 2) return;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n[0] == '.') continue;
        std::string p = dir + "/" + n;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) list_files_rec(p, out, depth + 1);
        else out.push_back(p);
    }
    closedir(d);
}

/* Local game index straight from es_systems.cfg: a file counts when it sits
 * in that system's folder and carries one of that system's extensions. No
 * gamelist.xml involvement. */
void scan_es_games(std::map<std::string, std::string> &names) {
    std::map<std::string, std::vector<std::string> > cache;
    std::map<std::string, bool> scanned;
    for (EsSystem &s : load_es_systems("/etc/emulationstation/es_systems.cfg")) {
        std::string rel = s.path;
        for (const char *pre : { "/roms2/", "/roms/" })
            if (rel.rfind(pre, 0) == 0) { rel = rel.substr(strlen(pre)); break; }
        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
        if (!rel.empty() && rel.back() == '/') rel.pop_back();
        for (const char *root : { "/roms", "/roms2" }) {
            std::string dir = std::string(root) + "/" + rel;
            if (!scanned[dir]) { list_files_rec(dir, cache[dir], 0); scanned[dir] = true; }
            for (auto &p : cache[dir]) {
                size_t sl = p.find_last_of('/');
                std::string fn = sl == std::string::npos ? p : p.substr(sl + 1);
                if (!ext_ok(fn, s.exts)) continue;
                names[norm(base_stem(fn))] = p;
                names[norm(fn)] = p;
            }
        }
    }
}


bool dir_has_game(const std::string &dir, const std::string &exts, int depth) {
    if (depth > 2) return false;
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d))) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n[0] == '.') continue;
        std::string p = dir + "/" + n;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) found = dir_has_game(p, exts, depth + 1);
        else found = ext_ok(n, exts);
    }
    closedir(d);
    return found;
}

void scan_games_dir(const std::string &dir, const std::string &exts, int depth,
                    std::vector<GameEntry> &out) {
    if (depth > 2) return;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n[0] == '.') continue;
        std::string p = dir + "/" + n;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { scan_games_dir(p, exts, depth + 1, out); continue; }
        if (ext_ok(n, exts)) out.push_back({ n, p });
    }
    closedir(d);
}
