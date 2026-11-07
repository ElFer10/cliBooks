#pragma once
// Liang/TeX hyphenation using LibreOffice/hunspell "hyph_xx.dic" pattern files —
// the terminal counterpart of Books' `-webkit-hyphens: auto`.
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clipub {

class Hyphenator {
public:
    bool loadFile(const std::filesystem::path& p);
    void loadPatterns(std::string_view text);  // one pattern per line (UTF-8)
    /// Finds a dictionary for a BCP-47 language tag ("es", "en-US"…) in the usual places.
    static std::shared_ptr<Hyphenator> forLanguage(const std::string& lang,
                                                   const std::vector<std::filesystem::path>& extraDirs = {});
    /// Byte offsets inside `word` after which a hyphen may be inserted.
    std::vector<size_t> points(std::string_view word) const;
    bool empty() const { return patterns_.empty(); }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> patterns_;
    size_t maxLen_ = 0;
    int leftMin_ = 2, rightMin_ = 3;
};

}  // namespace clipub
