#pragma once
// Vim-style key bindings: multi-key sequences ("gg", "]]"), counts ("5j", "50%"),
// vim key notation ("<C-f>", "<Space>", "<CR>") and per-mode tables that the user
// can override from config.toml.
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace clipub {

enum class Mode { Reader, Visual, List, Hints };

enum class Action {
    None,
    // movement
    ScrollDown, ScrollUp, PageDown, PageUp, HalfPageDown, HalfPageUp, NextChapter, PrevChapter,
    First, Last, GotoPercent, Back, Forward,
    // panels
    Toc, Bookmarks, Annotations, Library, Help, Info, Themes, SearchResults,
    // actions
    ToggleBookmark, SearchForward, SearchBackward, SearchNext, SearchPrev, Command, Quit,
    Visual, FollowLink, ThemeNext, ToggleMode, WidthInc, WidthDec, WidthReset, ToggleJustify,
    Redraw, Speak, NextAnnotation, PrevAnnotation,
    // visual mode
    CharLeft, CharRight, LineDown, LineUp, WordNext, WordBack, WordEnd, LineStart, LineEnd,
    SwapAnchor, Yank, Highlight, Color1, Color2, Color3, Color4, Color5, Underline, AddNote, LookUp,
    SpeakSelection, SearchSelection, ExitMode,
    // lists
    Select, Close, Delete, Edit, Filter, ChangeColor,
};

std::string actionName(Action a);
std::optional<Action> actionFromName(const std::string& name);

/// Parses "gg", "<C-f>", "<Space>j" … into key tokens.
std::vector<std::string> parseKeys(const std::string& notation);
std::string keysToString(const std::vector<std::string>& keys);

class Keymap {
public:
    Keymap();  // installs defaults
    void bind(Mode m, const std::vector<std::string>& seq, Action a);
    void unbind(Mode m, const std::vector<std::string>& seq);
    /// Replaces every binding of `a` in mode `m` with `seqs`.
    void setBindings(Mode m, Action a, const std::vector<std::string>& notations);

    enum class Match { None, Partial, Full };
    Match lookup(Mode m, const std::vector<std::string>& seq, Action& out) const;
    /// All key notations bound to an action (for the help screen).
    std::vector<std::string> keysFor(Mode m, Action a) const;
    const std::map<std::vector<std::string>, Action>& table(Mode m) const { return maps_.at(m); }

private:
    std::map<Mode, std::map<std::vector<std::string>, Action>> maps_;
};

Mode modeFromName(const std::string& s, bool& ok);

}  // namespace clipub
