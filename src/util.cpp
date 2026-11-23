#include "util.h"

#include <cstdio>
#include <string>

std::string trim_str(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}


std::string lower(std::string s) {
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
std::string norm(std::string s) {
    std::string o;
    for (char c : lower(s)) if (isalnum((unsigned char)c)) o += c;
    return o;
}
bool ends_with(const std::string &s, const std::string &e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string base_stem(const std::string &p) {
    size_t slash = p.find_last_of('/');
    std::string b = slash == std::string::npos ? p : p.substr(slash + 1);
    size_t dot = b.find_last_of('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

size_t curl_write(char *p, size_t sz, size_t n, void *ud) {
    ((std::string *)ud)->append(p, sz * n);
    return sz * n;
}
