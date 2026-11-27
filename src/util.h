#pragma once

#include <cstddef>
#include <string>

std::string trim_str(std::string s);
std::string lower(std::string s);
std::string norm(std::string s);
bool ends_with(const std::string &s, const std::string &e);
std::string base_stem(const std::string &p);
// Single-quote a value for safe interpolation into /bin/sh commands.
std::string shq(const std::string &s);

// TCP connect time to "host|port", or -1 on failure/timeout.
int tcp_latency_ms(const std::string &hostport, int timeout_ms);

// Resolve a built-in relay handle ("nyc", ...) through the libretro lobby.
bool lobby_tunnel(const std::string &handle, std::string &addr, int &port);

// libcurl write callback appending to a std::string.
size_t curl_write(char *p, size_t sz, size_t n, void *ud);
