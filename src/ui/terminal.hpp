#pragma once
// Thin ncurses wrapper: colour pairs derived from the theme, keys as vim
// notation tokens and a raw output channel for terminal graphics.
#include <cstdint>
#include <optional>
#include <string>

#include "app/config.hpp"

namespace clibooks {

enum Pair : short {
    kPText = 1,
    kPDim,
    kPLink,
    kPHeading,
    kPCode,
    kPStatus,
    kPStatusDim,
    kPSelection,
    kPSearch,
    kPHl0,
    kPHl1,
    kPHl2,
    kPHl3,
    kPHl4,
    kPUnderline,
    kPAccent,
    kPPopup,
    kPPopupBorder,
    kPCursor,
    kPHint,
    kPError,
};

class Terminal {
public:
    bool init();
    void shutdown();
    void applyTheme(const Theme& t);
    int width() const;
    int height() const;
    /// Next key as a token ("j", "<C-f>", "<Resize>"); nullopt on timeout.
    std::optional<std::string> readKey(int timeoutMs);
    void put(int y, int x, const std::string& s, short pair, unsigned attrs = 0);
    void fill(int y, int x, int w, short pair, unsigned attrs = 0);
    void clear(short pair);
    void refresh();
    void forceFullRedraw();
    /// Writes raw escape sequences after the ncurses frame (images, OSC 52).
    void writeRaw(const std::string& s);
    bool colorsSupported() const { return colors_ > 0; }
    int colors() const { return colors_; }
    uint32_t themeBg() const { return bgRgb_; }
    unsigned italicAttr() const;
    bool active() const { return active_; }

private:
    short color(uint32_t rgb, bool isBg) const;
    bool active_ = false;
    int colors_ = 0;
    uint32_t bgRgb_ = 0xFFFFFF;
    bool useTerminal_ = true;
};

}  // namespace clibooks
