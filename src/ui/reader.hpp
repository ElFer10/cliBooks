#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/config.hpp"
#include "app/state.hpp"
#include "epub/book.hpp"
#include "image/image.hpp"
#include "layout/layout.hpp"
#include "ui/terminal.hpp"

namespace clipub {

struct SearchMatches {
    std::string pattern;  // folded
    std::map<int, std::vector<std::pair<size_t, size_t>>> byChapter;
};

struct HintLabel {
    std::string label;
    int y = 0, x = 0;
};

struct DrawContext {
    const std::vector<Highlight>* highlights = nullptr;
    const SearchMatches* search = nullptr;
    bool visual = false;
    Position selStart, selEnd;  // [start, end)
    Position cursor;
    bool showCursor = false;
    const std::vector<HintLabel>* hints = nullptr;
};

struct CellInfo {
    int y = 0, x = 0, w = 1;
    int chapter = 0;
    size_t off = 0, bytes = 1;
    char32_t cp = ' ';
    int row = 0;  // visual row index (for j/k)
};

struct VisibleLink {
    int chapter = 0;
    int link = -1;
    int y = 0, x = 0;
};

class Reader {
public:
    Reader(Book& book, Config& cfg, img::Cache& images, img::Protocol proto);

    void setScreen(int w, int h);
    void relayout();

    // ---- navigation
    Position pos() const;
    void goTo(const Position& p);
    bool pageDown();
    bool pageUp();
    bool scrollLines(int n);
    bool halfPage(int dir);
    bool nextChapter();
    bool prevChapter();
    void first();
    void last();
    void gotoGlobalPage(int page);  // 1-based
    void gotoFraction(double f);
    bool scrollMode() const { return cfg_.mode == "scroll"; }

    // ---- information
    int currentGlobalPage() const;  // 1-based
    int totalPages() const;
    int pageAt(const Position& p) const;  // 1-based global page of a position
    int pagesLeftInChapter() const;
    double fraction() const;
    int columns() const { return cols_; }
    int textTop() const { return top_; }
    int textHeight() const { return pageH_; }
    std::pair<Position, Position> visibleRange() const;
    bool atEnd() const;

    // ---- drawing
    void draw(Terminal& t, const DrawContext& ctx);
    const std::vector<CellInfo>& cells() const { return cells_; }
    const std::vector<VisibleLink>& links() const { return links_; }
    const std::vector<img::Placement>& placements() const { return placements_; }
    std::string visibleText() const;
    std::string textBetween(const Position& a, const Position& b) const;
    img::Protocol protocol() const { return proto_; }
    void setProtocol(img::Protocol p) { proto_ = p; }

private:
    void drawLine(Terminal& t, int y, int x0, const Line& line, int chapter, const DrawContext& ctx);
    const ChapterLayout& layout(int ch) const { return layouts_[static_cast<size_t>(ch)]; }
    int chapterCount() const { return static_cast<int>(layouts_.size()); }

    Book& book_;
    Config& cfg_;
    img::Cache& images_;
    img::Protocol proto_;
    std::shared_ptr<Hyphenator> hyph_;
    bool hyphLoaded_ = false;

    int screenW_ = 0, screenH_ = 0;
    int cols_ = 1, colWidth_ = 72, colX_[2] = {0, 0};
    int top_ = 0, pageH_ = 10;
    std::vector<ChapterLayout> layouts_;
    std::vector<int> pageStart_;  // global page index of each chapter's first page
    int totalPages_ = 0;
    std::string layoutKey_;

    int chapter_ = 0;
    size_t page_ = 0;  // paged mode: first page of the spread
    size_t line_ = 0;  // scroll mode: top line

    std::vector<CellInfo> cells_;
    std::vector<VisibleLink> links_;
    std::vector<img::Placement> placements_;
    int rowCounter_ = 0;
};

}  // namespace clipub
