#include "util/utf8.hpp"

#include <algorithm>
#include <array>

namespace clibooks::utf8 {

char32_t decode(std::string_view s, size_t& i) {
    auto c = static_cast<unsigned char>(s[i]);
    size_t len = seqLen(c);
    if (len == 1 || i + len > s.size()) {
        ++i;
        return c < 0x80 ? c : 0xFFFD;
    }
    char32_t cp = 0;
    if (len == 2) cp = c & 0x1F;
    else if (len == 3) cp = c & 0x0F;
    else cp = c & 0x07;
    for (size_t k = 1; k < len; ++k) {
        auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += len;
    return cp;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string encode(char32_t cp) {
    std::string s;
    append(s, cp);
    return s;
}

namespace {
struct Range {
    char32_t lo, hi;
};
constexpr std::array kZeroWidth{
    Range{0x0300, 0x036F}, Range{0x0483, 0x0489}, Range{0x0591, 0x05BD}, Range{0x0610, 0x061A},
    Range{0x064B, 0x065F}, Range{0x0E31, 0x0E31}, Range{0x0E34, 0x0E3A}, Range{0x0E47, 0x0E4E},
    Range{0x1AB0, 0x1AFF}, Range{0x1DC0, 0x1DFF}, Range{0x200B, 0x200F}, Range{0x2028, 0x202E},
    Range{0x2060, 0x2064}, Range{0x20D0, 0x20FF}, Range{0xFE00, 0xFE0F}, Range{0xFE20, 0xFE2F},
    Range{0xFEFF, 0xFEFF}, Range{0xE0100, 0xE01EF}};
constexpr std::array kWide{
    Range{0x1100, 0x115F}, Range{0x231A, 0x231B}, Range{0x2329, 0x232A}, Range{0x23E9, 0x23EC},
    Range{0x25FD, 0x25FE}, Range{0x2614, 0x2615}, Range{0x2648, 0x2653}, Range{0x26AA, 0x26AB},
    Range{0x26BD, 0x26BE}, Range{0x26C4, 0x26C5}, Range{0x26F2, 0x26F5}, Range{0x2705, 0x2705},
    Range{0x270A, 0x270B}, Range{0x2728, 0x2728}, Range{0x274C, 0x274C}, Range{0x2753, 0x2755},
    Range{0x2795, 0x2797}, Range{0x2B1B, 0x2B1C}, Range{0x2E80, 0x303E}, Range{0x3041, 0x33FF},
    Range{0x3400, 0x4DBF}, Range{0x4E00, 0x9FFF}, Range{0xA000, 0xA4CF}, Range{0xA960, 0xA97F},
    Range{0xAC00, 0xD7A3}, Range{0xF900, 0xFAFF}, Range{0xFE10, 0xFE19}, Range{0xFE30, 0xFE6F},
    Range{0xFF00, 0xFF60}, Range{0xFFE0, 0xFFE6}, Range{0x1F300, 0x1F64F}, Range{0x1F680, 0x1F6FF},
    Range{0x1F900, 0x1F9FF}, Range{0x1FA70, 0x1FAFF}, Range{0x20000, 0x3FFFD}};

template <size_t N>
bool inTable(const std::array<Range, N>& t, char32_t cp) {
    auto it = std::upper_bound(t.begin(), t.end(), cp, [](char32_t v, const Range& r) { return v < r.lo; });
    if (it == t.begin()) return false;
    --it;
    return cp >= it->lo && cp <= it->hi;
}
}  // namespace

int width(char32_t cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (cp == 0xAD) return 0;  // soft hyphen is invisible unless used as break
    if (inTable(kZeroWidth, cp)) return 0;
    if (inTable(kWide, cp)) return 2;
    return 1;
}

int width(std::string_view s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) w += width(decode(s, i));
    return w;
}

bool isSpace(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' || cp == 0x3000 ||
           (cp >= 0x2000 && cp <= 0x200A && cp != 0x2007);
}

bool isCJK(char32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

bool isWordChar(char32_t cp) {
    if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
    if (isSpace(cp)) return false;
    // General punctuation / quotes / dashes are not word characters.
    if ((cp >= 0x2010 && cp <= 0x205E) || (cp >= 0x00A1 && cp <= 0x00BF && cp != 0xAA && cp != 0xBA) ||
        cp == 0x00D7 || cp == 0x00F7 || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF01 && cp <= 0xFF0F))
        return false;
    return true;
}

char32_t fold(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0x24F) {
        // Latin-1 supplement + Latin Extended-A/B: map to ASCII base letter.
        static const char* kLatin1 =
            "aaaaaaaceeeeiiiidnooooo*ouuuuyts"   // C0-DF
            "aaaaaaaceeeeiiiidnooooo/ouuuuyty";  // E0-FF
        if (c <= 0xFF) {
            char b = kLatin1[c - 0xC0];
            if (b == '*' || b == '/') return c;
            if (c == 0xC6 || c == 0xE6) return c == 0xC6 ? 0xE6 : c;  // æ
            if (c == 0xDF) return c;                                  // ß
            return static_cast<char32_t>(b);
        }
        // Latin Extended-A (U+0100..U+017F), 128 entries.
        static const char* kExtA =
            "aaaaaa" "cccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "ii" "jj" "kkk"
            "llllllllll" "nnnnnnnnn" "oooooo" "oo" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu" "ww" "yyy"
            "zzzzzz" "s";
        size_t idx = c - 0x100;
        if (idx < std::char_traits<char>::length(kExtA)) return static_cast<char32_t>(kExtA[idx]);
        return c;
    }
    if (c >= 0x391 && c <= 0x3A9) return c + 32;  // Greek
    if (c >= 0x410 && c <= 0x42F) return c + 32;  // Cyrillic
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c == 0x2019 || c == 0x2018) return '\'';
    if (c == 0x201C || c == 0x201D) return '"';
    if (c == 0xA0) return ' ';
    return c;
}

Folded foldString(std::string_view s) {
    Folded f;
    f.text.reserve(s.size());
    f.map.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        size_t start = i;
        char32_t cp = fold(decode(s, i));
        if (cp == 0xAD) continue;  // ignore soft hyphens
        size_t before = f.text.size();
        append(f.text, cp);
        for (size_t k = before; k < f.text.size(); ++k) f.map.push_back(static_cast<uint32_t>(start));
    }
    return f;
}

std::string foldCopy(std::string_view s) { return foldString(s).text; }

std::string truncate(std::string_view s, int cols, bool ellipsis) {
    if (cols <= 0) return {};
    if (width(s) <= cols) return std::string(s);
    std::string out;
    int w = 0;
    int limit = ellipsis ? cols - 1 : cols;
    for (size_t i = 0; i < s.size();) {
        size_t st = i;
        char32_t cp = decode(s, i);
        int cw = width(cp);
        if (w + cw > limit) break;
        out.append(s.substr(st, i - st));
        w += cw;
    }
    if (ellipsis) out += "…";
    return out;
}

std::string fit(std::string_view s, int cols) {
    std::string out = truncate(s, cols);
    int w = width(out);
    if (w < cols) out.append(static_cast<size_t>(cols - w), ' ');
    return out;
}

}  // namespace clibooks::utf8
