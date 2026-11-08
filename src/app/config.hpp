#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app/keymap.hpp"
#include "layout/layout.hpp"

namespace clibooks {

/// Reading theme. Colors are 0xRRGGBB; `useTerminal` keeps the terminal's own
/// foreground/background (the terminal counterpart of Books' "Original" theme
/// that follows the system appearance).
struct Theme {
    std::string name;
    bool useTerminal = false;
    uint32_t fg = 0x000000, bg = 0xFFFFFF;
    uint32_t dim = 0x808080, accent = 0x0A84FF, link = 0x0A60C0, heading = 0x000000, code = 0x8B4513;
    uint32_t statusFg = 0x555555, statusBg = 0xEEEEEE;
    uint32_t selection = 0xB4D5FE, search = 0xFFD54F;
    uint32_t highlights[5] = {0xFFE36E, 0xA8E6A1, 0xA6D0FF, 0xFFB3C7, 0xD7B8FF};
    uint32_t highlightFg = 0x000000;
    uint32_t underline = 0xE53935;
    bool boldText = false;  // Books' "Bold" theme
};

std::vector<Theme> builtinThemes();

struct Config {
    // Appearance (Books: Themes & Settings panel)
    std::string theme = "original";
    int width = 72;           // text column width (Books: font size / margins)
    bool justify = true;      // Books: "Justify Text"
    bool hyphenate = true;    // -webkit-hyphens: auto
    std::string hyphenLanguage;  // override the book's language for hyphenation
    ParagraphStyle paragraphs = ParagraphStyle::Auto;
    int lineSpacing = 1;
    bool bookStyles = true;   // Books: "Original" font keeps publisher styles
    std::string mode = "paged";   // paged | scroll (Books: Turn Pages / Vertical Scrolling)
    std::string columns = "auto";  // 1 | 2 | auto (two-page spread on wide terminals)
    int columnGap = 6;
    int marginTop = 1, marginBottom = 0;
    bool showHeader = true, showStatus = true;
    std::string statusFormat = "auto";
    // Images
    std::string images = "auto";  // auto | kitty | iterm | sixel | blocks | none
    int imageMaxRows = 0;         // 0 = page height
    // Behaviour
    std::string language = "auto";
    std::vector<std::string> libraryDirs;
    std::string ttsCommand;   // empty = platform default ("say", "espeak-ng --stdin")
    std::string dictCommand;  // "%s" is replaced with the word
    bool osc52 = true;
    bool smartcase = true;
    bool wrapSearch = true;
    int keyTimeoutMs = 1000;

    std::vector<Theme> themes;
    Keymap keys;
    std::vector<std::string> warnings;

    const Theme& currentTheme() const;
    int themeIndex(const std::string& name) const;

    static std::filesystem::path defaultPath();
    void load(const std::filesystem::path& file);
    /// Runtime preferences changed in the app (theme, width…), stored in the data dir.
    void loadPrefs();
    void savePrefs() const;
    static std::string exampleToml();
};

}  // namespace clibooks
