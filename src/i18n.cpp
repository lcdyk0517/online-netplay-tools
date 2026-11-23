#include "i18n.h"

#include <fstream>
#include <string>
#include <unistd.h>

bool zh = false;  // ES default is English; .cn marker / es_settings switches it

const char *L(const char *cn, const char *en) { return zh ? cn : en; }

void load_language() {
        if (access("/home/ark/.config/.cn", F_OK) == 0) {
            zh = true;
        } else {
            const char *files[] = {
                "/home/ark/.emulationstation/es_settings.cfg",
                "/home/ark/.config/emulationstation/es_settings.cfg" };
            for (const char *f : files) {
                std::ifstream in(f);
                if (!in) continue;
                std::string line;
                while (std::getline(in, line)) {
                    if (line.find("name=\"Language\"") == std::string::npos) continue;
                    size_t v = line.find("value=\"");
                    if (v == std::string::npos) continue;
                    v += 7;
                    size_t e = line.find('"', v);
                    if (e == std::string::npos) continue;
                    zh = line.compare(v, 2, "zh") == 0;
                    break;
                }
                break;  /* first existing settings file wins */
            }
        }
}
