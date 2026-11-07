#include "app/config.hpp"


#include <toml.hpp>

#include <sstream>

#include "util/platform.hpp"
#include "util/strings.hpp"

namespace fs = std::filesystem;

namespace clipub {

std::vector<Theme> builtinThemes() {
    std::vector<Theme> v;
    // Names and palettes follow Books' theme set (Original, Quiet, Paper, Bold, Calm,
    // Focus) plus the classic Sepia/Night themes from earlier Books versions.
    Theme original;
    original.name = "original";
    original.useTerminal = true;
    original.statusFg = 0x888888;
    v.push_back(original);

    Theme quiet;
    quiet.name = "quiet";
    quiet.fg = 0xD8D8D8;
    quiet.bg = 0x4A4A4D;
    quiet.dim = 0x9A9A9E;
    quiet.link = 0x9CC8FF;
    quiet.heading = 0xF0F0F0;
    quiet.code = 0xE0C080;
    quiet.statusFg = 0xA0A0A4;
    quiet.statusBg = 0x3C3C3F;
    quiet.selection = 0x5A6E8C;
    quiet.highlightFg = 0x1E1E1E;
    v.push_back(quiet);

    Theme paper;
    paper.name = "paper";
    paper.fg = 0x2B2B2B;
    paper.bg = 0xF2F0EB;
    paper.dim = 0x8A8780;
    paper.link = 0x33658A;
    paper.heading = 0x111111;
    paper.statusFg = 0x77746E;
    paper.statusBg = 0xE6E3DC;
    v.push_back(paper);

    Theme bold;
    bold.name = "bold";
    bold.fg = 0x000000;
    bold.bg = 0xFFFFFF;
    bold.boldText = true;
    bold.statusFg = 0x333333;
    v.push_back(bold);

    Theme calm;
    calm.name = "calm";
    calm.fg = 0x3F3226;
    calm.bg = 0xEFE3CC;
    calm.dim = 0x8C7B66;
    calm.link = 0x7A4E1D;
    calm.heading = 0x2A2018;
    calm.statusFg = 0x8C7B66;
    calm.statusBg = 0xE3D5BA;
    v.push_back(calm);

    Theme focus;
    focus.name = "focus";
    focus.fg = 0x1A1A1A;
    focus.bg = 0xFFFBEA;
    focus.dim = 0x8E8A7A;
    focus.link = 0x2F5D9A;
    focus.statusFg = 0x8E8A7A;
    focus.statusBg = 0xF3EED8;
    v.push_back(focus);

    Theme sepia;
    sepia.name = "sepia";
    sepia.fg = 0x5B4636;
    sepia.bg = 0xF8F1E3;
    sepia.dim = 0x9C8A78;
    sepia.link = 0x8B5A2B;
    sepia.heading = 0x3E2E22;
    sepia.statusFg = 0x9C8A78;
    sepia.statusBg = 0xEDE3CF;
    v.push_back(sepia);

    Theme night;
    night.name = "night";
    night.fg = 0xB8B8B8;
    night.bg = 0x121212;
    night.dim = 0x6E6E6E;
    night.link = 0x7FB2F0;
    night.heading = 0xE0E0E0;
    night.code = 0xD7BA7D;
    night.statusFg = 0x6E6E6E;
    night.statusBg = 0x1E1E1E;
    night.selection = 0x264F78;
    night.highlightFg = 0x101010;
    night.highlights[0] = 0xC9A227;
    night.highlights[1] = 0x6FA86A;
    night.highlights[2] = 0x5C8FD6;
    night.highlights[3] = 0xD06A8C;
    night.highlights[4] = 0x9A7BD1;
    v.push_back(night);
    return v;
}

const Theme& Config::currentTheme() const {
    int i = themeIndex(theme);
    return themes[static_cast<size_t>(i < 0 ? 0 : i)];
}

int Config::themeIndex(const std::string& name) const {
    for (size_t i = 0; i < themes.size(); ++i)
        if (str::iequals(themes[i].name, name)) return static_cast<int>(i);
    return -1;
}

fs::path Config::defaultPath() { return platform::configDir() / "config.toml"; }

namespace {

template <typename T>
void get(const toml::table& t, std::string_view key, T& out) {
    if (auto v = t[key].value<T>()) out = *v;
}

void readColor(const toml::table& t, std::string_view key, uint32_t& out, std::vector<std::string>& warn) {
    if (auto s = t[key].value<std::string>()) {
        if (auto c = str::parseHexColor(*s)) out = *c;
        else warn.push_back("invalid color for " + std::string(key) + ": " + *s);
    }
}

void applyReaderTable(Config& c, const toml::table& r) {
    get(r, "theme", c.theme);
    if (auto w = r["width"].value<int64_t>()) c.width = static_cast<int>(*w);
    get(r, "justify", c.justify);
    get(r, "hyphenate", c.hyphenate);
    get(r, "hyphen_language", c.hyphenLanguage);
    if (auto p = r["paragraphs"].value<std::string>()) {
        if (*p == "spaced") c.paragraphs = ParagraphStyle::Spaced;
        else if (*p == "indented" || *p == "indent") c.paragraphs = ParagraphStyle::Indented;
        else c.paragraphs = ParagraphStyle::Auto;
    }
    if (auto l = r["line_spacing"].value<int64_t>()) c.lineSpacing = static_cast<int>(*l);
    get(r, "book_styles", c.bookStyles);
    get(r, "mode", c.mode);
    if (auto col = r["columns"].value<std::string>()) c.columns = *col;
    else if (auto coln = r["columns"].value<int64_t>()) c.columns = std::to_string(*coln);
    if (auto g = r["column_gap"].value<int64_t>()) c.columnGap = static_cast<int>(*g);
    if (auto m = r["margin_top"].value<int64_t>()) c.marginTop = static_cast<int>(*m);
    if (auto m = r["margin_bottom"].value<int64_t>()) c.marginBottom = static_cast<int>(*m);
    get(r, "show_header", c.showHeader);
    get(r, "show_status", c.showStatus);
    get(r, "images", c.images);
    if (auto m = r["image_max_rows"].value<int64_t>()) c.imageMaxRows = static_cast<int>(*m);
}

}  // namespace

void Config::load(const fs::path& file) {
    themes = builtinThemes();
    if (ttsCommand.empty()) ttsCommand = platform::defaultTtsCommand();
    if (dictCommand.empty()) dictCommand = platform::defaultDictCommand();
    auto data = platform::readFile(file);
    if (!data) return;
    auto res = toml::parse(*data, file.string());
    if (!res) {
        std::ostringstream ss;
        ss << res.error();
        warnings.push_back("config: " + ss.str());
        return;
    }
    const toml::table& root = res.table();
    if (auto r = root["reader"].as_table()) applyReaderTable(*this, *r);
    if (auto g = root["general"].as_table()) {
        get(*g, "language", language);
        get(*g, "tts_command", ttsCommand);
        get(*g, "dict_command", dictCommand);
        get(*g, "osc52", osc52);
        get(*g, "smartcase", smartcase);
        get(*g, "wrap_search", wrapSearch);
        if (auto t = (*g)["key_timeout_ms"].value<int64_t>()) keyTimeoutMs = static_cast<int>(*t);
        if (auto dirs = (*g)["library_dirs"].as_array())
            for (auto& d : *dirs)
                if (auto s = d.value<std::string>()) libraryDirs.push_back(str::expandHome(*s));
    }
    if (auto th = root["themes"].as_table()) {
        for (auto& [name, node] : *th) {
            auto* t = node.as_table();
            if (!t) continue;
            Theme base = themes[0];
            if (auto inherits = (*t)["inherits"].value<std::string>()) {
                int bi = themeIndex(*inherits);
                if (bi >= 0) base = themes[static_cast<size_t>(bi)];
            }
            base.name = std::string(name.str());
            base.useTerminal = false;
            get(*t, "terminal_colors", base.useTerminal);
            readColor(*t, "fg", base.fg, warnings);
            readColor(*t, "bg", base.bg, warnings);
            readColor(*t, "dim", base.dim, warnings);
            readColor(*t, "accent", base.accent, warnings);
            readColor(*t, "link", base.link, warnings);
            readColor(*t, "heading", base.heading, warnings);
            readColor(*t, "code", base.code, warnings);
            readColor(*t, "status_fg", base.statusFg, warnings);
            readColor(*t, "status_bg", base.statusBg, warnings);
            readColor(*t, "selection", base.selection, warnings);
            readColor(*t, "search", base.search, warnings);
            readColor(*t, "underline", base.underline, warnings);
            readColor(*t, "highlight_fg", base.highlightFg, warnings);
            if (auto arr = (*t)["highlights"].as_array()) {
                for (size_t i = 0; i < arr->size() && i < 5; ++i)
                    if (auto s = (*arr)[i].value<std::string>())
                        if (auto c = str::parseHexColor(*s)) base.highlights[i] = *c;
            }
            get(*t, "bold", base.boldText);
            int existing = themeIndex(base.name);
            if (existing >= 0) themes[static_cast<size_t>(existing)] = base;
            else themes.push_back(base);
        }
    }
    if (auto k = root["keys"].as_table()) {
        for (auto& [modeName, node] : *k) {
            bool ok;
            Mode m = modeFromName(std::string(modeName.str()), ok);
            auto* mt = node.as_table();
            if (!ok || !mt) {
                warnings.push_back("keys: unknown mode '" + std::string(modeName.str()) + "'");
                continue;
            }
            for (auto& [actName, val] : *mt) {
                std::string an(actName.str());
                if (an == "unmap") {
                    if (auto arr = val.as_array())
                        for (auto& e : *arr)
                            if (auto s = e.value<std::string>()) keys.unbind(m, parseKeys(*s));
                    continue;
                }
                auto act = actionFromName(an);
                if (!act) {
                    warnings.push_back("keys: unknown action '" + an + "'");
                    continue;
                }
                std::vector<std::string> seqs;
                if (auto s = val.value<std::string>()) seqs.push_back(*s);
                else if (auto arr = val.as_array())
                    for (auto& e : *arr)
                        if (auto s2 = e.value<std::string>()) seqs.push_back(*s2);
                // A key explicitly assigned by the user wins over defaults of other actions.
                for (auto& sq : seqs) keys.unbind(m, parseKeys(sq));
                keys.setBindings(m, *act, seqs);
            }
        }
    }
    if (themeIndex(theme) < 0) {
        warnings.push_back("unknown theme '" + theme + "', using original");
        theme = "original";
    }
}

void Config::loadPrefs() {
    auto data = platform::readFile(platform::dataDir() / "prefs.toml");
    if (!data) return;
    auto res = toml::parse(*data);
    if (!res) return;
    if (auto r = res.table()["reader"].as_table()) applyReaderTable(*this, *r);
    if (themeIndex(theme) < 0) theme = "original";
}

void Config::savePrefs() const {
    toml::table reader{
        {"theme", theme},
        {"width", width},
        {"justify", justify},
        {"hyphenate", hyphenate},
        {"paragraphs", paragraphs == ParagraphStyle::Auto ? "auto" : paragraphs == ParagraphStyle::Spaced ? "spaced" : "indented"},
        {"line_spacing", lineSpacing},
        {"book_styles", bookStyles},
        {"mode", mode},
        {"columns", columns},
        {"show_header", showHeader},
        {"show_status", showStatus},
        {"images", images},
    };
    toml::table root{{"reader", reader}};
    std::ostringstream ss;
    ss << "# Written by clipub when you change settings inside the app (:set, themes…).\n"
          "# Delete this file (or run :reset) to go back to config.toml values.\n\n"
       << root << "\n";
    platform::writeFileAtomic(platform::dataDir() / "prefs.toml", ss.str());
}

std::string Config::exampleToml() {
    return R"TOML(# clipub configuration — ~/.config/clipub/config.toml
# All keys are optional. Settings changed inside the app (:set, themes, width)
# are remembered in ~/.local/share/clipub/prefs.toml and override this file.

[general]
language = "auto"            # auto | es | en
library_dirs = ["~/Books"]   # folders scanned for .epub files in the library view
# tts_command = "say -f -"   # text is written to stdin (macOS: say, Linux: espeak-ng --stdin)
# dict_command = "sdcv -n --utf8-output %s"   # %s = selected word
osc52 = true                 # copy to the clipboard through the terminal (works over SSH)
smartcase = true
wrap_search = true
key_timeout_ms = 1000        # time to wait for the rest of a key sequence ("gg")

[reader]
theme = "original"           # original quiet paper bold calm focus sepia night (or your own)
width = 72                   # text column width
justify = true
hyphenate = true             # needs a hyph_xx.dic (LibreOffice/hunspell) — see README
# hyphen_language = "es"
paragraphs = "auto"          # auto | spaced | indented
line_spacing = 1             # 1 | 2
book_styles = true           # honour publisher CSS (alignment, emphasis, small caps…)
mode = "paged"               # paged | scroll
columns = "auto"             # 1 | 2 | auto (two pages side by side on wide terminals)
column_gap = 6
margin_top = 1
margin_bottom = 0
show_header = true
show_status = true
images = "auto"              # auto | kitty | iterm | sixel | blocks | none
image_max_rows = 0           # 0 = up to the page height

# Custom theme (inherits from a built-in one):
# [themes.solarized]
# inherits = "paper"
# fg = "#657b83"
# bg = "#fdf6e3"
# link = "#268bd2"
# highlights = ["#b58900", "#859900", "#268bd2", "#d33682", "#6c71c4"]

# Key bindings: action = "key" or ["k1", "k2"]. Vim notation: <C-f> <Space> <CR> <Esc> <Tab> <F1>…
# Assigning a key here removes it from any other action in the same mode.
# [keys.reader]
# page_down = ["<Space>", "l", "<C-f>"]
# quit = ["q", "ZZ"]
# unmap = ["C"]
# [keys.visual]
# highlight = ["H", "<CR>"]
# [keys.list]
# select = ["<CR>", "l"]
)TOML";
}

}  // namespace clipub
