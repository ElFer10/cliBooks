#pragma once
// Image decoding (stb_image) and terminal graphics encoders:
//   kitty  – Kitty graphics protocol (kitty, Ghostty, WezTerm, Konsole)
//   iterm  – iTerm2 inline images (iTerm2, WezTerm, mintty)
//   sixel  – DEC sixel (xterm -ti vt340, foot, mlterm, Windows Terminal ≥1.22)
//   blocks – Unicode half blocks with 24-bit/256 colours (works everywhere)
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clipub::img {

enum class Protocol { None, Blocks, Sixel, Kitty, Iterm };

Protocol parseProtocol(const std::string& s);  // "auto" handled by detectProtocol
std::string protocolName(Protocol p);
Protocol detectProtocol();
bool terminalHasTruecolor();

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

/// Decodes and caches images from the book.
class Cache {
public:
    using Loader = std::function<std::optional<std::string>(const std::string&)>;
    explicit Cache(Loader loader) : loader_(std::move(loader)) {}
    /// Pixel size without full decode; {-1,-1} if the format is unsupported (e.g. SVG).
    std::pair<int, int> size(const std::string& path);
    const Image* get(const std::string& path);
    /// Scaled RGBA (box filter) composited over `bg` when `flatten` is set.
    std::shared_ptr<Image> scaled(const std::string& path, int w, int h, uint32_t bg, bool flatten);
    std::string png(const std::string& path, int w, int h);  // encoded PNG of the scaled image
    void clear();

private:
    Loader loader_;
    std::map<std::string, std::pair<int, int>> sizes_;
    std::map<std::string, std::unique_ptr<Image>> images_;
    std::map<std::string, std::shared_ptr<Image>> scaledCache_;
    std::map<std::string, std::string> pngCache_;
};

struct Placement {
    std::string path;
    int row = 0, col = 0;    // screen cell of the top-left corner
    int rows = 0, cols = 0;  // size in cells
    int clipTop = 0;         // rows hidden above the viewport (scroll mode)
    int visibleRows = 0;
};

/// Produces the escape sequences to draw `p` (including cursor positioning).
std::string render(Cache& cache, const Placement& p, Protocol proto, int cellW, int cellH, uint32_t bg,
                   bool truecolor, uint32_t kittyId);
/// Escape sequence that removes all kitty placements.
std::string kittyClear();

}  // namespace clipub::img
