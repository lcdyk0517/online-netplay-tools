#pragma once

#include <string>
#include <vector>

// Minimal JSON reader used for the lobby list and ZeroTier's local API.
// Strings handle the full escape set (including surrogate pairs); unknown
// structures are skipped with proper nesting.
struct JsonReader {
    const std::string &s;
    size_t i = 0;

    explicit JsonReader(const std::string &src) : s(src) {}

    void ws();
    bool eat(char c);
    bool str(std::string &out);
    bool boolean(bool &out);
    void skip_value();
};

// Raw JSON helpers for flat objects. Array/object values are returned as
// their source text so callers can walk them with another JsonReader.
bool json_object_field(const std::string &obj, const char *key, std::string &val);
bool json_object_array_items(const std::string &obj, const char *key,
                             std::vector<std::string> &items);
bool json_string_array(const std::string &obj, const char *key,
                       std::vector<std::string> &out);
bool json_string_array_first(const std::string &obj, const char *key,
                             std::string &out);
