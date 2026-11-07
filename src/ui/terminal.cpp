#include "ui/terminal.hpp"

#ifdef _WIN32
#include <curses.h>
#else
#include <curses.h>
#include <unistd.h>
#endif

#include <clocale>
#include <cstdio>
#include <cstdlib>

#include "util/utf8.hpp"

namespace clipub {

bool Terminal::init() {
    std::setlocale(LC_ALL, "");
#ifndef _WIN32
    // Short ESC delay so <Esc> is responsive (vim users press it a lot).
    setenv("ESCDELAY", "25", 0);
#endif
    if (!initscr()) return false;
    active_ = true;
    raw();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    curs_set(0);
    leaveok(stdscr, TRUE);
    set_escdelay(25);
    if (has_colors()) {
        start_color();
        use_default_colors();
        colors_ = COLORS;
    }
    return true;
}

void Terminal::shutdown() {
    if (!active_) return;
    endwin();
    active_ = false;
}

int Terminal::width() const { return COLS; }
int Terminal::height() const { return LINES; }

namespace {

int xterm256(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    auto cube = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    int cr = cube(r), cg = cube(g), cb = cube(b);
    int dc = (lv[cr] - r) * (lv[cr] - r) + (lv[cg] - g) * (lv[cg] - g) + (lv[cb] - b) * (lv[cb] - b);
    int avg = (r + g + b) / 3;
    int gi = avg > 238 ? 23 : std::max(0, (avg - 3) / 10);
    int gv = 8 + gi * 10;
    int dg = (gv - r) * (gv - r) + (gv - g) * (gv - g) + (gv - b) * (gv - b);
    return dg < dc ? 232 + gi : 16 + 36 * cr + 6 * cg + cb;
}

int basic8(uint32_t rgb, bool allow16) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    static const int pal[16][3] = {{0, 0, 0},       {205, 0, 0},   {0, 205, 0},   {205, 205, 0},
                                   {0, 0, 238},     {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
                                   {127, 127, 127}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                   {92, 92, 255},   {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    int best = 0, bestD = 1 << 30;
    for (int i = 0; i < (allow16 ? 16 : 8); ++i) {
        int d = (pal[i][0] - r) * (pal[i][0] - r) + (pal[i][1] - g) * (pal[i][1] - g) + (pal[i][2] - b) * (pal[i][2] - b);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

}  // namespace

short Terminal::color(uint32_t rgb, bool) const {
    if (colors_ >= 256) return static_cast<short>(xterm256(rgb));
    return static_cast<short>(basic8(rgb, colors_ >= 16));
}

void Terminal::applyTheme(const Theme& t) {
    useTerminal_ = t.useTerminal;
    bgRgb_ = t.useTerminal ? 0x000000 : t.bg;
    if (!colors_) return;
    short fg = t.useTerminal ? -1 : color(t.fg, false);
    short bg = t.useTerminal ? -1 : color(t.bg, true);
    auto c = [&](uint32_t rgb) { return color(rgb, false); };
    short dim = t.useTerminal ? (colors_ >= 16 ? 8 : COLOR_WHITE) : c(t.dim);
    short link = t.useTerminal ? (colors_ >= 16 ? 12 : COLOR_BLUE) : c(t.link);
    short heading = t.useTerminal ? -1 : c(t.heading);
    short code = t.useTerminal ? (colors_ >= 16 ? 3 : COLOR_YELLOW) : c(t.code);
    short sfg = t.useTerminal ? dim : c(t.statusFg);
    short sbg = t.useTerminal ? -1 : c(t.statusBg);
    init_pair(kPText, fg, bg);
    init_pair(kPDim, dim, bg);
    init_pair(kPLink, link, bg);
    init_pair(kPHeading, heading, bg);
    init_pair(kPCode, code, bg);
    init_pair(kPStatus, t.useTerminal ? -1 : c(t.fg), sbg);
    init_pair(kPStatusDim, sfg, sbg);
    short hfg = c(t.highlightFg);
    init_pair(kPSelection, t.useTerminal ? COLOR_BLACK : c(t.fg), c(t.selection));
    init_pair(kPSearch, hfg, c(t.search));
    for (int i = 0; i < 5; ++i) init_pair(static_cast<short>(kPHl0 + i), hfg, c(t.highlights[i]));
    init_pair(kPUnderline, c(t.underline), bg);
    init_pair(kPAccent, c(t.accent), bg);
    init_pair(kPPopup, t.useTerminal ? -1 : c(t.fg), t.useTerminal ? -1 : c(t.statusBg));
    init_pair(kPPopupBorder, t.useTerminal ? dim : c(t.dim), t.useTerminal ? -1 : c(t.statusBg));
    init_pair(kPCursor, t.useTerminal ? COLOR_BLACK : c(t.bg), t.useTerminal ? COLOR_WHITE : c(t.fg));
    init_pair(kPHint, COLOR_BLACK, colors_ >= 256 ? 220 : COLOR_YELLOW);
    init_pair(kPError, colors_ >= 16 ? 9 : COLOR_RED, bg);
    bkgdset(static_cast<chtype>(' ') | COLOR_PAIR(kPText));
}

unsigned Terminal::italicAttr() const {
#ifdef A_ITALIC
    return A_ITALIC;
#else
    return A_UNDERLINE;
#endif
}

std::optional<std::string> Terminal::readKey(int timeoutMs) {
    timeout(timeoutMs);
    wint_t ch;
    int r = get_wch(&ch);
    if (r == ERR) return std::nullopt;
    if (r == KEY_CODE_YES) {
        switch (ch) {
            case KEY_RESIZE: return "<Resize>";
            case KEY_UP: return "<Up>";
            case KEY_DOWN: return "<Down>";
            case KEY_LEFT: return "<Left>";
            case KEY_RIGHT: return "<Right>";
            case KEY_HOME: return "<Home>";
            case KEY_END: return "<End>";
            case KEY_PPAGE: return "<PageUp>";
            case KEY_NPAGE: return "<PageDown>";
            case KEY_BACKSPACE: return "<BS>";
            case KEY_DC: return "<Del>";
            case KEY_ENTER: return "<CR>";
            case KEY_BTAB: return "<S-Tab>";
            case KEY_MOUSE: return std::nullopt;
            default:
                if (ch >= KEY_F(1) && ch <= KEY_F(12)) return "<F" + std::to_string(ch - KEY_F(0)) + ">";
                return std::nullopt;
        }
    }
    if (ch == 27) {
        // Alt/Meta combos arrive as ESC + key; we treat them as plain <Esc>.
        timeout(0);
        wint_t next;
        if (get_wch(&next) != ERR) {
            // swallow
        }
        return "<Esc>";
    }
    if (ch == 13 || ch == 10) return "<CR>";
    if (ch == 9) return "<Tab>";
    if (ch == 127 || ch == 8) return "<BS>";
    if (ch == ' ') return "<Space>";
    if (ch == 0) return "<C-Space>";
    if (ch < 32) return std::string("<C-") + static_cast<char>('a' + ch - 1) + ">";
    return utf8::encode(static_cast<char32_t>(ch));
}

void Terminal::put(int y, int x, const std::string& s, short pair, unsigned attrs) {
    if (y < 0 || y >= LINES || x >= COLS) return;
    attrset(static_cast<attr_t>(attrs) | COLOR_PAIR(pair));
    int maxw = COLS - x;
    std::string t = utf8::width(s) > maxw ? utf8::truncate(s, maxw, false) : s;
    mvaddstr(y, x, t.c_str());
    attrset(A_NORMAL);
}

void Terminal::fill(int y, int x, int w, short pair, unsigned attrs) {
    if (w <= 0) return;
    put(y, x, std::string(static_cast<size_t>(w), ' '), pair, attrs);
}

void Terminal::clear(short pair) {
    bkgdset(static_cast<chtype>(' ') | COLOR_PAIR(pair));
    erase();
}

void Terminal::refresh() { ::refresh(); }

void Terminal::forceFullRedraw() { clearok(curscr, TRUE); }

void Terminal::writeRaw(const std::string& s) {
    if (s.empty()) return;
    std::fflush(stdout);
#ifndef _WIN32
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(STDOUT_FILENO, s.data() + off, s.size() - off);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
    // Put the cursor back where ncurses believes it is.
    int y = getcury(curscr), x = getcurx(curscr);
    std::string mv = "\x1b[0m\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
    (void)!::write(STDOUT_FILENO, mv.data(), mv.size());
#else
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
#endif
}

}  // namespace clipub
