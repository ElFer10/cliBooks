#pragma once
// Minimal UTF-8 helpers: decoding, encoding, display width and search folding.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace clibooks::utf8 {

/// Decodes the code point starting at s[i]; advances i. Invalid bytes yield U+FFFD.
char32_t decode(std::string_view s, size_t& i);
void append(std::string& out, char32_t cp);
std::string encode(char32_t cp);

/// Terminal column width of a code point (0, 1 or 2).
int width(char32_t cp);
/// Display width of a UTF-8 string.
int width(std::string_view s);

/// Byte length of the code point starting with lead byte c.
inline size_t seqLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

bool isSpace(char32_t cp);
bool isWordChar(char32_t cp);
bool isCJK(char32_t cp);

/// Lowercases and strips common Latin diacritics (search is case/diacritic-insensitive like Books).
char32_t fold(char32_t cp);

/// Folded copy of s plus, for each byte of the result, the byte offset in s it came from.
struct Folded {
    std::string text;
    std::vector<uint32_t> map;  // map[i] = offset in original of folded byte i
};
Folded foldString(std::string_view s);
std::string foldCopy(std::string_view s);

/// Truncate s to at most `cols` display columns (adds an ellipsis when cut).
std::string truncate(std::string_view s, int cols, bool ellipsis = true);
/// Pad/truncate s to exactly `cols` columns.
std::string fit(std::string_view s, int cols);

}  // namespace clibooks::utf8
