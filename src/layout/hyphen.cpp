#include "layout/hyphen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "util/platform.hpp"
#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace fs = std::filesystem;

namespace clipub {
namespace {

char32_t lowerCp(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    if (c >= 0x100 && c <= 0x17F && (c % 2 == 0)) return c + 1;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    return c;
}

std::string latin1ToUtf8(std::string_view s) {
    std::string out;
    for (unsigned char c : s) utf8::append(out, c);
    return out;
}

}  // namespace

void Hyphenator::loadPatterns(std::string_view text) {
    for (auto& rawLine : str::split(text, '\n')) {
        std::string line(str::trim(rawLine));
        if (line.empty() || line[0] == '%' || line[0] == '#') continue;
        if (str::startsWith(line, "LEFTHYPHENMIN")) {
            leftMin_ = std::max(1, std::atoi(line.c_str() + 13));
            continue;
        }
        if (str::startsWith(line, "RIGHTHYPHENMIN")) {
            rightMin_ = std::max(1, std::atoi(line.c_str() + 14));
            continue;
        }
        if (line == "NEXTLEVEL") break;  // compound-word level: not needed
        if (line.find('/') != std::string::npos) continue;  // non-standard patterns
        if (std::isupper(static_cast<unsigned char>(line[0])) && line.find_first_of("0123456789") == std::string::npos &&
            line.find(' ') != std::string::npos)
            continue;
        std::string letters;
        std::vector<uint8_t> digits{0};
        for (size_t i = 0; i < line.size();) {
            char c = line[i];
            if (c >= '0' && c <= '9') {
                digits.back() = static_cast<uint8_t>(c - '0');
                ++i;
                continue;
            }
            size_t st = i;
            char32_t cp = utf8::decode(line, i);
            (void)st;
            utf8::append(letters, lowerCp(cp));
            digits.push_back(0);
        }
        if (letters.empty()) continue;
        size_t cps = digits.size() - 1;
        maxLen_ = std::max(maxLen_, cps);
        patterns_[letters] = std::move(digits);
    }
}

bool Hyphenator::loadFile(const fs::path& p) {
    auto data = platform::readFile(p);
    if (!data) return false;
    std::string text = *data;
    auto nl = text.find('\n');
    std::string first = str::lower(str::trim(text.substr(0, nl)));
    if (first.find("8859") != std::string::npos || first.find("latin") != std::string::npos) {
        text = latin1ToUtf8(text.substr(nl + 1));
    } else if (str::startsWith(first, "utf") || str::startsWith(first, "koi") || str::startsWith(first, "microsoft")) {
        text = text.substr(nl + 1);
    }
    loadPatterns(text);
    return !patterns_.empty();
}

std::shared_ptr<Hyphenator> Hyphenator::forLanguage(const std::string& langTag,
                                                    const std::vector<fs::path>& extraDirs) {
    if (langTag.empty()) return nullptr;
    std::string lang = str::lower(langTag);
    std::replace(lang.begin(), lang.end(), '-', '_');
    std::string primary = lang.substr(0, lang.find('_'));
    std::vector<fs::path> dirs = extraDirs;
    dirs.push_back(platform::dataDir() / "hyphen");
    for (const char* d : {"/usr/share/hyphen", "/usr/local/share/hyphen", "/opt/homebrew/share/hyphen",
                          "/usr/share/myspell/dicts", "/usr/share/myspell", "/usr/share/hunspell"})
        dirs.emplace_back(d);
    // LibreOffice bundles (macOS / Linux).
    for (const char* base : {"/Applications/LibreOffice.app/Contents/Resources/extensions",
                             "/usr/lib/libreoffice/share/extensions", "/opt/libreoffice/share/extensions"}) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        for (auto& e : fs::directory_iterator(base, ec))
            if (e.is_directory() && str::startsWith(e.path().filename().string(), "dict-")) dirs.push_back(e.path());
    }
    fs::path best;
    int bestScore = 0;
    for (auto& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (auto& e : fs::directory_iterator(d, ec)) {
            std::string fn = str::lower(e.path().filename().string());
            if (!str::startsWith(fn, "hyph_") || !str::endsWith(fn, ".dic")) continue;
            std::string code = fn.substr(5, fn.size() - 9);
            std::replace(code.begin(), code.end(), '-', '_');
            int score = 0;
            if (code == lang) score = 3;
            else if (code == primary) score = 2;
            else if (str::startsWith(code, primary + "_")) score = 1;
            if (score > bestScore) {
                bestScore = score;
                best = e.path();
            }
        }
        if (bestScore == 3) break;
    }
    if (best.empty()) return nullptr;
    auto h = std::make_shared<Hyphenator>();
    if (!h->loadFile(best)) return nullptr;
    return h;
}

std::vector<size_t> Hyphenator::points(std::string_view word) const {
    std::vector<size_t> result;
    if (patterns_.empty()) return result;
    // Decode into code points, remembering byte offsets.
    std::vector<char32_t> cps;
    std::vector<size_t> offs;
    for (size_t i = 0; i < word.size();) {
        offs.push_back(i);
        char32_t c = utf8::decode(word, i);
        if (!utf8::isWordChar(c)) return result;  // only hyphenate pure words
        cps.push_back(lowerCp(c));
    }
    offs.push_back(word.size());
    size_t n = cps.size();
    if (static_cast<int>(n) < leftMin_ + rightMin_) return result;
    std::vector<std::string> enc(n + 2);
    enc[0] = ".";
    for (size_t i = 0; i < n; ++i) enc[i + 1] = utf8::encode(cps[i]);
    enc[n + 1] = ".";
    std::vector<uint8_t> vals(n + 3, 0);
    for (size_t i = 0; i < n + 2; ++i) {
        std::string key;
        for (size_t len = 1; len <= maxLen_ && i + len <= n + 2; ++len) {
            key += enc[i + len - 1];
            auto it = patterns_.find(key);
            if (it == patterns_.end()) continue;
            const auto& d = it->second;
            for (size_t k = 0; k < d.size(); ++k) vals[i + k] = std::max(vals[i + k], d[k]);
        }
    }
    // vals[j] refers to the gap before enc[j]; gap before letter index L (0-based) is vals[L+1].
    for (size_t L = static_cast<size_t>(leftMin_); L + static_cast<size_t>(rightMin_) <= n; ++L) {
        if (vals[L + 1] % 2 == 1) result.push_back(offs[L]);
    }
    return result;
}

}  // namespace clipub
