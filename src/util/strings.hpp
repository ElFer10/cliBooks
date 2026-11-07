#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clipub::str {

std::string_view trim(std::string_view s);
std::string lower(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty = true);
std::vector<std::string> splitWs(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool startsWith(std::string_view s, std::string_view p);
bool endsWith(std::string_view s, std::string_view p);
bool icontains(std::string_view hay, std::string_view needle);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
/// Collapses runs of whitespace into single spaces and trims.
std::string collapseWs(std::string_view s);

/// Percent-decoding for hrefs inside the EPUB.
std::string urlDecode(std::string_view s);
/// Join an EPUB-internal base directory with a relative href and normalize "." / "..".
std::string resolvePath(std::string_view baseDir, std::string_view href);
std::string dirName(std::string_view path);
/// Splits "file.xhtml#frag" into {file, frag}.
std::pair<std::string, std::string> splitFragment(std::string_view href);

/// Decodes HTML named/numeric entities that XML parsers leave untouched (&nbsp; …).
std::string decodeEntities(std::string_view s);

std::string expandHome(std::string_view path);
std::optional<uint32_t> parseHexColor(std::string_view s);  // "#rrggbb" or "#rgb" -> 0xRRGGBB
uint64_t fnv1a(std::string_view s);
std::string hex64(uint64_t v);
std::string formatTime(int64_t unixSeconds);

}  // namespace clipub::str
