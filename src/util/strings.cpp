#include "util/strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <unordered_map>

#include "util/utf8.hpp"

namespace clipub::str {

std::string_view trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            auto part = s.substr(start, i - start);
            if (!skipEmpty || !part.empty()) out.emplace_back(part);
            start = i + 1;
        }
    }
    return out;
}

std::vector<std::string> splitWs(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t st = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > st) out.emplace_back(s.substr(st, i - st));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool icontains(std::string_view hay, std::string_view needle) {
    return utf8::foldCopy(hay).find(utf8::foldCopy(needle)) != std::string::npos;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string collapseWs(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
        } else {
            if (space && !out.empty()) out += ' ';
            space = false;
            out += c;
        }
    }
    return out;
}

std::string urlDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string dirName(std::string_view path) {
    auto p = path.rfind('/');
    return p == std::string_view::npos ? std::string() : std::string(path.substr(0, p));
}

std::string resolvePath(std::string_view baseDir, std::string_view href) {
    std::string h = urlDecode(href);
    std::string full;
    if (!h.empty() && h[0] == '/') full = h.substr(1);
    else full = baseDir.empty() ? h : std::string(baseDir) + "/" + h;
    std::vector<std::string> parts;
    for (auto& p : split(full, '/')) {
        if (p == ".") continue;
        if (p == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(p);
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

std::pair<std::string, std::string> splitFragment(std::string_view href) {
    auto p = href.find('#');
    if (p == std::string_view::npos) return {std::string(href), {}};
    return {std::string(href.substr(0, p)), std::string(href.substr(p + 1))};
}

std::string decodeEntities(std::string_view s) {
    static const std::unordered_map<std::string_view, char32_t> kEntities{
        {"nbsp", 0xA0},   {"amp", '&'},     {"lt", '<'},       {"gt", '>'},      {"quot", '"'},
        {"apos", '\''},   {"shy", 0xAD},    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"laquo", 0xAB},
        {"raquo", 0xBB},  {"copy", 0xA9},   {"reg", 0xAE},     {"trade", 0x2122}, {"deg", 0xB0},
        {"middot", 0xB7}, {"bull", 0x2022}, {"iexcl", 0xA1},   {"iquest", 0xBF},  {"times", 0xD7},
        {"divide", 0xF7}, {"sect", 0xA7},   {"para", 0xB6},    {"dagger", 0x2020}, {"Dagger", 0x2021},
        {"thinsp", 0x2009}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"zwnj", 0x200C},  {"zwj", 0x200D},
        {"aacute", 0xE1}, {"eacute", 0xE9}, {"iacute", 0xED},  {"oacute", 0xF3},  {"uacute", 0xFA},
        {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD},  {"Oacute", 0xD3},  {"Uacute", 0xDA},
        {"ntilde", 0xF1}, {"Ntilde", 0xD1}, {"uuml", 0xFC},    {"Uuml", 0xDC},    {"ouml", 0xF6},
        {"auml", 0xE4},   {"euml", 0xEB},   {"iuml", 0xEF},    {"ccedil", 0xE7},  {"Ccedil", 0xC7},
        {"agrave", 0xE0}, {"egrave", 0xE8}, {"ograve", 0xF2},  {"acirc", 0xE2},   {"ecirc", 0xEA},
        {"ocirc", 0xF4},  {"szlig", 0xDF},  {"oslash", 0xF8},  {"aring", 0xE5},   {"aelig", 0xE6},
        {"euro", 0x20AC}, {"pound", 0xA3},  {"cent", 0xA2},    {"yen", 0xA5},     {"frac12", 0xBD},
        {"frac14", 0xBC}, {"frac34", 0xBE}, {"ordm", 0xBA},    {"ordf", 0xAA},    {"prime", 0x2032},
        {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191},  {"darr", 0x2193},  {"minus", 0x2212}};
    if (s.find('&') == std::string_view::npos) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        auto semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        auto name = s.substr(i + 1, semi - i - 1);
        if (!name.empty() && name[0] == '#') {
            char32_t cp = 0;
            if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
                cp = static_cast<char32_t>(std::strtoul(std::string(name.substr(2)).c_str(), nullptr, 16));
            else
                cp = static_cast<char32_t>(std::strtoul(std::string(name.substr(1)).c_str(), nullptr, 10));
            if (cp) {
                utf8::append(out, cp);
                i = semi;
                continue;
            }
        } else if (auto it = kEntities.find(name); it != kEntities.end()) {
            utf8::append(out, it->second);
            i = semi;
            continue;
        }
        out += s[i];
    }
    return out;
}

std::string expandHome(std::string_view path) {
    if (!path.empty() && path[0] == '~') {
        const char* home = std::getenv("HOME");
#ifdef _WIN32
        if (!home) home = std::getenv("USERPROFILE");
#endif
        if (home) return std::string(home) + std::string(path.substr(1));
    }
    return std::string(path);
}

std::optional<uint32_t> parseHexColor(std::string_view s) {
    s = trim(s);
    if (s.empty() || s[0] != '#') return std::nullopt;
    s.remove_prefix(1);
    if (s.size() == 3) {
        std::string e{s[0], s[0], s[1], s[1], s[2], s[2]};
        return static_cast<uint32_t>(std::strtoul(e.c_str(), nullptr, 16));
    }
    if (s.size() != 6) return std::nullopt;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    return static_cast<uint32_t>(std::strtoul(std::string(s).c_str(), nullptr, 16));
}

uint64_t fnv1a(std::string_view s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex64(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[static_cast<size_t>(i)] = d[v & 0xF];
    return s;
}

std::string formatTime(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

}  // namespace clipub::str
