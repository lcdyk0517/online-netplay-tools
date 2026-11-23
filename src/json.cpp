#include "json.h"

#include <cctype>
#include <cstring>

void JsonReader::ws() {
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
}

bool JsonReader::eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) { i++; return true; }
    return false;
}

static bool hex4(JsonReader &r, unsigned &v) {
    if (r.i + 4 > r.s.size()) return false;
    v = 0;
    for (int k = 0; k < 4; k++) {
        char c = r.s[r.i + k];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    r.i += 4;
    return true;
}

static void utf8_append(std::string &out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

bool JsonReader::str(std::string &out) {
    ws();
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '"') return true;
        if (c < 0x20) return false;             /* raw control character */
        if (c != '\\') { out += (char)c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case '"': case '\\': case '/': out += e; break;
        case 'u': {
            unsigned v;
            if (!hex4(*this, v)) return false;
            if (v >= 0xD800 && v <= 0xDBFF) {
                if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    unsigned lo;
                    if (!hex4(*this, lo)) return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                        utf8_append(out, 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00));
                    else
                        utf8_append(out, 0xFFFD);
                } else {
                    utf8_append(out, 0xFFFD);   /* lone high surrogate */
                }
            } else if (v >= 0xDC00 && v <= 0xDFFF) {
                utf8_append(out, 0xFFFD);       /* lone low surrogate */
            } else {
                utf8_append(out, v);
            }
            break;
        }
        default: return false;                  /* invalid escape */
        }
    }
    return false;
}

bool JsonReader::boolean(bool &out) {
    ws();
    if (s.compare(i, 4, "true") == 0) { i += 4; out = true; return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = false; return true; }
    return false;
}

void JsonReader::skip_value() {
    ws();
    if (i >= s.size()) return;
    char c = s[i];
    if (c == '"') { std::string d; str(d); return; }
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < s.size()) {
            char ch = s[i];
            if (ch == '"') {
                /* Skip strings tolerantly: an invalid escape must not stop
                 * the structural scan early. */
                i++;
                while (i < s.size()) {
                    if (s[i] == '\\') { i += 2; continue; }
                    if (s[i] == '"') { i++; break; }
                    i++;
                }
                continue;
            }
            if (ch == open) depth++;
            else if (ch == close && --depth == 0) { i++; return; }
            i++;
        }
        return;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') i++;
}

static bool json_object_walk(const std::string &obj, const char *key,
                             std::string *value, std::vector<std::string> *items) {
    JsonReader r(obj);
    if (!r.eat('{')) return false;
    bool found = false;
    while (true) {
        r.ws();
        if (r.i >= r.s.size()) return false;
        if (r.s[r.i] == '}') { r.i++; return found; }
        std::string k;
        if (!r.str(k) || !r.eat(':')) return false;
        r.ws();
        if (r.i >= r.s.size()) return false;
        char c = r.s[r.i];
        if (value && k == key) {
            if (c == '"') {
                if (!r.str(*value)) return false;
            } else if (c == '{' || c == '[') {
                size_t start = r.i;
                r.skip_value();
                *value = obj.substr(start, r.i - start);
            } else {
                size_t start = r.i;
                while (r.i < r.s.size() && r.s[r.i] != ',' && r.s[r.i] != '}')
                    r.i++;
                *value = obj.substr(start, r.i - start);
            }
            found = true;
        } else if (items && k == key && c == '[') {
            r.i++;
            while (true) {
                r.ws();
                if (r.i >= r.s.size()) return false;
                if (r.s[r.i] == ']') { r.i++; break; }
                if (r.s[r.i] == '"') {
                    std::string tmp;
                    if (!r.str(tmp)) return false;
                    items->push_back("\"" + tmp + "\""); /* keep raw-ish */
                } else if (r.s[r.i] == '{' || r.s[r.i] == '[') {
                    size_t start = r.i;
                    r.skip_value();
                    items->push_back(obj.substr(start, r.i - start));
                } else {
                    size_t start = r.i;
                    while (r.i < r.s.size() && r.s[r.i] != ',' && r.s[r.i] != ']')
                        r.i++;
                    std::string raw = obj.substr(start, r.i - start);
                    while (!raw.empty() && isspace((unsigned char)raw.back()))
                        raw.pop_back();
                    items->push_back(raw);
                }
                r.ws();
                if (r.i < r.s.size() && r.s[r.i] == ',') { r.i++; continue; }
            }
            found = true;
        } else if (c == '{' || c == '[') {
            r.skip_value();
        } else if (c == '"') {
            std::string tmp;
            if (!r.str(tmp)) return false;
        } else {
            while (r.i < r.s.size() && r.s[r.i] != ',' && r.s[r.i] != '}')
                r.i++;
        }
        r.ws();
        if (r.i < r.s.size() && r.s[r.i] == ',') { r.i++; continue; }
        if (r.i < r.s.size() && r.s[r.i] == '}') { r.i++; return found; }
        return found;
    }
}

bool json_object_field(const std::string &obj, const char *key, std::string &val) {
    return json_object_walk(obj, key, &val, nullptr);
}

bool json_object_array_items(const std::string &obj, const char *key,
                             std::vector<std::string> &items) {
    items.clear();
    return json_object_walk(obj, key, nullptr, &items);
}

bool json_string_array(const std::string &obj, const char *key,
                       std::vector<std::string> &out) {
    out.clear();
    std::vector<std::string> raw;
    if (!json_object_array_items(obj, key, raw)) return false;
    for (auto &v : raw) {
        JsonReader r(v);
        std::string s;
        if (r.str(s)) out.push_back(s);
    }
    return true;
}

bool json_string_array_first(const std::string &obj, const char *key,
                             std::string &out) {
    std::vector<std::string> v;
    if (!json_string_array(obj, key, v) || v.empty()) return false;
    out = v[0];
    return true;
}
