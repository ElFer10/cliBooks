#include "app/keymap.hpp"

#include <algorithm>
#include <array>

#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace clipub {
namespace {

struct ActionInfo {
    Action a;
    const char* name;
};

constexpr std::array kActions{
    ActionInfo{Action::ScrollDown, "scroll_down"},     ActionInfo{Action::ScrollUp, "scroll_up"},
    ActionInfo{Action::PageDown, "page_down"},         ActionInfo{Action::PageUp, "page_up"},
    ActionInfo{Action::HalfPageDown, "half_page_down"}, ActionInfo{Action::HalfPageUp, "half_page_up"},
    ActionInfo{Action::NextChapter, "next_chapter"},   ActionInfo{Action::PrevChapter, "prev_chapter"},
    ActionInfo{Action::First, "first"},                ActionInfo{Action::Last, "last"},
    ActionInfo{Action::GotoPercent, "goto_percent"},   ActionInfo{Action::Back, "back"},
    ActionInfo{Action::Forward, "forward"},            ActionInfo{Action::Toc, "toc"},
    ActionInfo{Action::Bookmarks, "bookmarks"},        ActionInfo{Action::Annotations, "annotations"},
    ActionInfo{Action::Library, "library"},            ActionInfo{Action::Help, "help"},
    ActionInfo{Action::Info, "info"},                  ActionInfo{Action::Themes, "themes"},
    ActionInfo{Action::SearchResults, "search_results"}, ActionInfo{Action::ToggleBookmark, "toggle_bookmark"},
    ActionInfo{Action::SearchForward, "search_forward"}, ActionInfo{Action::SearchBackward, "search_backward"},
    ActionInfo{Action::SearchNext, "search_next"},     ActionInfo{Action::SearchPrev, "search_prev"},
    ActionInfo{Action::Command, "command"},            ActionInfo{Action::Quit, "quit"},
    ActionInfo{Action::Visual, "visual"},              ActionInfo{Action::FollowLink, "follow_link"},
    ActionInfo{Action::ThemeNext, "theme_next"},       ActionInfo{Action::ToggleMode, "toggle_mode"},
    ActionInfo{Action::WidthInc, "width_increase"},    ActionInfo{Action::WidthDec, "width_decrease"},
    ActionInfo{Action::WidthReset, "width_reset"},     ActionInfo{Action::ToggleJustify, "toggle_justify"},
    ActionInfo{Action::Redraw, "redraw"},              ActionInfo{Action::Speak, "speak"},
    ActionInfo{Action::NextAnnotation, "next_annotation"}, ActionInfo{Action::PrevAnnotation, "prev_annotation"},
    ActionInfo{Action::CharLeft, "char_left"},         ActionInfo{Action::CharRight, "char_right"},
    ActionInfo{Action::LineDown, "line_down"},         ActionInfo{Action::LineUp, "line_up"},
    ActionInfo{Action::WordNext, "word_next"},         ActionInfo{Action::WordBack, "word_back"},
    ActionInfo{Action::WordEnd, "word_end"},           ActionInfo{Action::LineStart, "line_start"},
    ActionInfo{Action::LineEnd, "line_end"},           ActionInfo{Action::SwapAnchor, "swap_anchor"},
    ActionInfo{Action::Yank, "yank"},                  ActionInfo{Action::Highlight, "highlight"},
    ActionInfo{Action::Color1, "color_1"},             ActionInfo{Action::Color2, "color_2"},
    ActionInfo{Action::Color3, "color_3"},             ActionInfo{Action::Color4, "color_4"},
    ActionInfo{Action::Color5, "color_5"},             ActionInfo{Action::Underline, "underline"},
    ActionInfo{Action::AddNote, "add_note"},           ActionInfo{Action::LookUp, "look_up"},
    ActionInfo{Action::SpeakSelection, "speak_selection"}, ActionInfo{Action::SearchSelection, "search_selection"},
    ActionInfo{Action::ExitMode, "exit"},              ActionInfo{Action::Select, "select"},
    ActionInfo{Action::Close, "close"},                ActionInfo{Action::Delete, "delete"},
    ActionInfo{Action::Edit, "edit"},                  ActionInfo{Action::Filter, "filter"},
    ActionInfo{Action::ChangeColor, "change_color"},
};

}  // namespace

std::string actionName(Action a) {
    for (auto& i : kActions)
        if (i.a == a) return i.name;
    return "none";
}

std::optional<Action> actionFromName(const std::string& name) {
    for (auto& i : kActions)
        if (name == i.name) return i.a;
    return std::nullopt;
}

std::vector<std::string> parseKeys(const std::string& s) {
    std::vector<std::string> keys;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '<') {
            auto end = s.find('>', i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::string inner = s.substr(i + 1, end - i - 1);
                std::string l = str::lower(inner);
                std::string tok;
                if (l == "space") tok = "<Space>";
                else if (l == "cr" || l == "enter" || l == "return") tok = "<CR>";
                else if (l == "esc") tok = "<Esc>";
                else if (l == "tab") tok = "<Tab>";
                else if (l == "s-tab") tok = "<S-Tab>";
                else if (l == "bs" || l == "backspace") tok = "<BS>";
                else if (l == "del") tok = "<Del>";
                else if (l == "up" || l == "down" || l == "left" || l == "right" || l == "home" || l == "end") {
                    tok = "<" + std::string(1, static_cast<char>(std::toupper(l[0]))) + l.substr(1) + ">";
                } else if (l == "pageup" || l == "pgup") tok = "<PageUp>";
                else if (l == "pagedown" || l == "pgdn") tok = "<PageDown>";
                else if (l == "lt") tok = "<";
                else if (l.size() >= 2 && l[0] == 'f' && std::isdigit(static_cast<unsigned char>(l[1]))) tok = "<F" + l.substr(1) + ">";
                else if (l.size() == 3 && (l[0] == 'c') && l[1] == '-') tok = std::string("<C-") + static_cast<char>(std::tolower(static_cast<unsigned char>(inner[2]))) + ">";
                else tok = "<" + inner + ">";
                keys.push_back(tok);
                i = end + 1;
                continue;
            }
        }
        size_t st = i;
        utf8::decode(s, i);
        std::string ch = s.substr(st, i - st);
        if (ch == " ") ch = "<Space>";
        keys.push_back(ch);
    }
    return keys;
}

std::string keysToString(const std::vector<std::string>& keys) {
    std::string s;
    for (auto& k : keys) s += k;
    return s;
}

Mode modeFromName(const std::string& s, bool& ok) {
    ok = true;
    if (s == "reader" || s == "normal") return Mode::Reader;
    if (s == "visual") return Mode::Visual;
    if (s == "list") return Mode::List;
    if (s == "hints") return Mode::Hints;
    ok = false;
    return Mode::Reader;
}

void Keymap::bind(Mode m, const std::vector<std::string>& seq, Action a) {
    if (!seq.empty()) maps_[m][seq] = a;
}

void Keymap::unbind(Mode m, const std::vector<std::string>& seq) { maps_[m].erase(seq); }

void Keymap::setBindings(Mode m, Action a, const std::vector<std::string>& notations) {
    auto& t = maps_[m];
    for (auto it = t.begin(); it != t.end();) {
        if (it->second == a) it = t.erase(it);
        else ++it;
    }
    for (auto& n : notations) t[parseKeys(n)] = a;
}

Keymap::Match Keymap::lookup(Mode m, const std::vector<std::string>& seq, Action& out) const {
    auto it = maps_.find(m);
    if (it == maps_.end()) return Match::None;
    const auto& t = it->second;
    bool exact = false, longer = false;
    for (auto i = t.lower_bound(seq); i != t.end(); ++i) {
        const auto& k = i->first;
        if (k.size() < seq.size() || !std::equal(seq.begin(), seq.end(), k.begin())) break;
        if (k.size() == seq.size()) {
            exact = true;
            out = i->second;
        } else {
            longer = true;
            break;
        }
    }
    if (exact && !longer) return Match::Full;
    // Ambiguous ("g" bound while "gg" exists) or a pure prefix: wait for more keys.
    return longer ? Match::Partial : Match::None;
}

std::vector<std::string> Keymap::keysFor(Mode m, Action a) const {
    std::vector<std::string> r;
    auto it = maps_.find(m);
    if (it == maps_.end()) return r;
    for (auto& [k, v] : it->second)
        if (v == a) r.push_back(keysToString(k));
    return r;
}

Keymap::Keymap() {
    auto B = [&](Mode m, Action a, std::initializer_list<const char*> keys) {
        for (auto* k : keys) bind(m, parseKeys(k), a);
    };
    using A = Action;
    const Mode R = Mode::Reader;
    B(R, A::ScrollDown, {"j", "<Down>", "<C-e>", "<CR>"});
    B(R, A::ScrollUp, {"k", "<Up>", "<C-y>"});
    B(R, A::PageDown, {"<Space>", "<C-f>", "l", "<Right>", "<PageDown>"});
    B(R, A::PageUp, {"b", "<C-b>", "h", "<Left>", "<PageUp>", "<BS>"});
    B(R, A::HalfPageDown, {"<C-d>"});
    B(R, A::HalfPageUp, {"<C-u>"});
    B(R, A::NextChapter, {"]]", "}"});
    B(R, A::PrevChapter, {"[[", "{"});
    B(R, A::First, {"gg", "<Home>"});
    B(R, A::Last, {"G", "<End>"});
    B(R, A::GotoPercent, {"%"});
    B(R, A::Back, {"<C-o>", "``"});
    B(R, A::Forward, {"<Tab>"});
    B(R, A::Toc, {"t", "<F2>"});
    B(R, A::Bookmarks, {"'", "M"});
    B(R, A::Annotations, {"A", "<F3>"});
    B(R, A::Library, {"gl"});
    B(R, A::Help, {"g?", "<F1>"});
    B(R, A::Info, {"<C-g>"});
    B(R, A::Themes, {"gt"});
    B(R, A::SearchResults, {"g/"});
    B(R, A::ToggleBookmark, {"m"});
    B(R, A::SearchForward, {"/"});
    B(R, A::SearchBackward, {"?"});
    B(R, A::SearchNext, {"n"});
    B(R, A::SearchPrev, {"N"});
    B(R, A::Command, {":"});
    B(R, A::Quit, {"q", "ZZ"});
    B(R, A::Visual, {"v"});
    B(R, A::FollowLink, {"f"});
    B(R, A::ThemeNext, {"C"});
    B(R, A::ToggleMode, {"S"});
    B(R, A::WidthInc, {"+", ">"});
    B(R, A::WidthDec, {"-", "<lt>"});
    B(R, A::WidthReset, {"="});
    B(R, A::ToggleJustify, {"J"});
    B(R, A::Redraw, {"<C-l>"});
    B(R, A::Speak, {"gr"});
    B(R, A::NextAnnotation, {"]a"});
    B(R, A::PrevAnnotation, {"[a"});

    const Mode V = Mode::Visual;
    B(V, A::CharLeft, {"h", "<Left>"});
    B(V, A::CharRight, {"l", "<Right>"});
    B(V, A::LineDown, {"j", "<Down>"});
    B(V, A::LineUp, {"k", "<Up>"});
    B(V, A::WordNext, {"w", "W"});
    B(V, A::WordBack, {"b", "B"});
    B(V, A::WordEnd, {"e", "E"});
    B(V, A::LineStart, {"0", "^", "<Home>"});
    B(V, A::LineEnd, {"$", "<End>"});
    B(V, A::SwapAnchor, {"o"});
    B(V, A::Yank, {"y"});
    B(V, A::Highlight, {"H", "<CR>"});
    B(V, A::Color1, {"1"});
    B(V, A::Color2, {"2"});
    B(V, A::Color3, {"3"});
    B(V, A::Color4, {"4"});
    B(V, A::Color5, {"5"});
    B(V, A::Underline, {"U"});
    B(V, A::AddNote, {"a", "n"});
    B(V, A::LookUp, {"K"});
    B(V, A::SpeakSelection, {"gr"});
    B(V, A::SearchSelection, {"*"});
    B(V, A::PageDown, {"<C-f>", "<Space>"});
    B(V, A::PageUp, {"<C-b>"});
    B(V, A::ExitMode, {"<Esc>", "v", "q"});

    const Mode L = Mode::List;
    B(L, A::LineDown, {"j", "<Down>", "<C-n>"});
    B(L, A::LineUp, {"k", "<Up>", "<C-p>"});
    B(L, A::HalfPageDown, {"<C-d>", "<PageDown>", "<C-f>"});
    B(L, A::HalfPageUp, {"<C-u>", "<PageUp>", "<C-b>"});
    B(L, A::First, {"gg", "<Home>"});
    B(L, A::Last, {"G", "<End>"});
    B(L, A::Select, {"<CR>", "l", "o", "<Right>"});
    B(L, A::Close, {"q", "<Esc>", "h", "<Left>"});
    B(L, A::Delete, {"d", "x", "<Del>"});
    B(L, A::Edit, {"e"});
    B(L, A::ChangeColor, {"c"});
    B(L, A::Filter, {"/"});
    B(L, A::Command, {":"});

    B(Mode::Hints, A::ExitMode, {"<Esc>"});
}

}  // namespace clipub
