#pragma once

#include <cstddef>
#include <string>

std::string trim_str(std::string s);
std::string lower(std::string s);
std::string norm(std::string s);
bool ends_with(const std::string &s, const std::string &e);
std::string base_stem(const std::string &p);

// libcurl write callback appending to a std::string.
size_t curl_write(char *p, size_t sz, size_t n, void *ud);
