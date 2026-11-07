#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/config.hpp"
#include "app/state.hpp"
#include "epub/book.hpp"
#include "image/image.hpp"
#include "ui/reader.hpp"
#include "ui/terminal.hpp"
#include "util/utf8.hpp"

namespace clipub {

class App {
public:
    explicit App(Config& cfg);
    int run(const std::optional<std::filesystem::path>& file);

private:
    // ---------------------------------------------------------------- UI elements
    struct ListItem {
        std::string text, right, sub;
        int depth = 0;
        bool current = false;
        short markPair = 0;
        std::function<void()> onSelect;
        std::function<void()> onDelete;
        std::function<void()> onEdit;
        std::function<void()> onColor;
    };
    struct ListPanel {
        std::string title, emptyMsg, footer;
        std::vector<ListItem> items;
        std::vector<int> visible;
        int sel = 0, scroll = 0;
        std::string filter;
        bool fullscreen = false;
        std::function<void()> refresh;  // rebuild after delete/edit
    };
    struct Popup {
        std::string title;
        std::string text;
        std::vector<std::string> lines;
        int scroll = 0;
        std::string footer;
        std::function<void()> onEnter;
    };
    struct Prompt {
        std::string prefix, buf;
        size_t cursor = 0;
        std::function<void(const std::string&)> onAccept;
        std::function<void(const std::string&)> onChange;
        std::vector<std::string>* history = nullptr;
        int histIdx = -1;
        std::vector<std::string> completions;
        int compIdx = -1;
        std::string compBase;
    };
    enum class View { Library, Reader };

    // ---------------------------------------------------------------- flow
    bool openBook(const std::filesystem::path& p);
    void closeBook();
    void saveState();
    void showLibrary();
    void render();
    void drawReader();
    void drawHeaderStatus();
    void drawList(ListPanel& l);
    void drawPopup(Popup& p);
    void emitImages(bool overlayVisible);
    void handleKey(const std::string& key);
    void dispatch(Mode m, const std::string& key);
    void perform(Mode m, Action a, int count);
    void performReader(Action a, int count);
    void performVisual(Action a, int count);
    void performList(Action a, int count);
    void handlePrompt(const std::string& key);
    void handleHints(const std::string& key);
    void onResize();
    void tick();

    // ---------------------------------------------------------------- features
    void message(const std::string& m, bool error = false);
    void openPrompt(const std::string& prefix, std::function<void(const std::string&)> onAccept,
                    const std::string& initial = "", std::vector<std::string>* history = nullptr);
    void showPopup(const std::string& title, const std::string& text, std::function<void()> onEnter = {},
                   const std::string& footer = "");
    void runCommand(const std::string& cmd);
    void setOption(const std::string& expr);
    void applySettings(bool relayout);
    void showToc();
    void showBookmarks();
    void showAnnotations();
    void showThemes();
    void showHelp();
    void showInfo();
    void showSearchResults(const std::string& pattern);
    void toggleBookmark();
    void search(const std::string& pattern, bool backward, bool fromNext);
    void computeMatches();
    void jumpTo(const Position& p, bool record = true);
    void followLink(int chapter, int link);
    void startHints();
    void enterVisual();
    void moveVisual(Action a, int count);
    std::pair<Position, Position> selection() const;
    void addHighlight(int style, bool thenNote);
    void editNote(size_t idx);
    void lookUp(const std::string& word);
    void speak(const std::string& text);
    void exportNotes(const std::string& path);
    void copyText(const std::string& text);
    std::vector<std::string> complete(const std::string& buf);
    std::string snippetAt(const Position& p, size_t before, size_t after) const;
    const utf8::Folded& folded(int chapter);
    void filterList(ListPanel& l);

    Config& cfg_;
    Terminal term_;
    StateStore store_;
    View view_ = View::Library;
    std::unique_ptr<Book> book_;
    std::unique_ptr<img::Cache> images_;
    std::unique_ptr<Reader> reader_;
    BookState state_;
    img::Protocol proto_ = img::Protocol::Blocks;
    bool truecolor_ = false;

    std::optional<ListPanel> library_;
    std::optional<ListPanel> list_;
    std::optional<Popup> popup_;
    std::optional<Prompt> prompt_;
    std::function<void()> confirm_;
    std::string message_;
    bool messageError_ = false;

    std::vector<std::string> pending_;
    std::string count_;
    std::chrono::steady_clock::time_point pendingSince_;

    // visual mode
    bool visual_ = false;
    Position vAnchor_, vCursor_;
    size_t vCursorBytes_ = 1;
    int visualEdge_ = 0;  // 1: move cursor to first cell after redraw, -1: last
    // hints
    bool hinting_ = false;
    std::vector<HintLabel> hints_;
    std::vector<VisibleLink> hintTargets_;
    std::string hintBuf_;
    // search
    std::string searchPattern_;
    bool searchBackward_ = false;
    bool hlsearch_ = true;
    SearchMatches matches_;
    Position lastMatch_{-1, 0};
    std::vector<std::optional<utf8::Folded>> foldedCache_;
    // history
    std::vector<Position> back_, fwd_;
    std::vector<std::string> cmdHistory_, searchHistory_;
    // misc
    bool running_ = true;
    bool dirty_ = false;
    std::string lastImageSig_;
    long ttsPid_ = -1;
    std::chrono::steady_clock::time_point lastKey_, lastSave_;
};

}  // namespace clipub
