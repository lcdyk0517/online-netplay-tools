#pragma once

#include <string>

// Optional short-code lookup: POST /publish {"nwid":...} -> {"code":"a1b2c3"}
//                                   GET  /resolve?code=a1b2c3 -> {"nwid":...}
// The base URL comes from SHORTURL in ra-lobby.cfg (empty disables the feature).
bool short_valid_code(const std::string &code);
bool short_publish(const std::string &nwid, std::string &code, std::string &err,
                   long long *expires = nullptr);
bool short_resolve(const std::string &code, std::string &nwid, std::string &err);
