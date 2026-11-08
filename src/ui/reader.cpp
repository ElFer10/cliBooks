#include "ui/reader.hpp"

#include <curses.h>

#include <algorithm>

#include "util/platform.hpp"
#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace clibooks {

Reader::Reader(Book& book, Config& cfg, img::Cache& images, img::Protocol proto)
    : book_(book), cfg_(cfg), images_(images), proto_(proto) {}

void Reader::setScreen(int w, int h) {
    if (w == screenW_ && h == screenH_ && !layouts_.empty()) return;
    screenW_ = w;
    screenH_ = h;
    relayout();
}

void Reader::relayout() {
    Position keep = layouts_.empty() ? Position{chapter_, 0} : pos();
    int W = std::max(screenW_, 20), H = std::max(screenH_, 6);
    top_ = (cfg_.showHeader ? 1 : 0) + std::max(0, cfg_.marginTop);
    int bottom = (cfg_.showStatus ? 1 : 0) + std::max(0, cfg_.marginBottom);
    pageH_ = std::max(3, H - top_ - bottom);
    int want = std::max(20, cfg_.width);
    bool two = !scrollMode() && (cfg_.columns == "2" || (cfg_.columns == "auto" && W >= 2 * want + cfg_.columnGap + 4));
    if (cfg_.columns == "1") two = false;
    if (two && W < 2 * 20 + cfg_.columnGap) two = false;
    cols_ = two ? 2 : 1;
    colWidth_ = two ? std::min(want, (W - cfg_.columnGap - 2) / 2) : std::min(want, W - 2);
    int total = cols_ * colWidth_ + (cols_ - 1) * cfg_.columnGap;
    colX_[0] = std::max(0, (W - total) / 2);
    colX_[1] = colX_[0] + colWidth_ + cfg_.columnGap;

    if (!hyphLoaded_) {
        hyphLoaded_ = true;
        std::string lang = cfg_.hyphenLanguage.empty() ? book_.meta().language : cfg_.hyphenLanguage;
        hyph_ = Hyphenator::forLanguage(lang);
    }
    LayoutOptions lo;
    lo.width = colWidth_;
    lo.justify = cfg_.justify;
    lo.hyphenate = cfg_.hyphenate;
    lo.hyphenator = hyph_;
    lo.paragraphStyle = cfg_.paragraphs;
    lo.lineSpacing = std::clamp(cfg_.lineSpacing, 1, 2);
    lo.showImages = proto_ != img::Protocol::None;
    lo.maxImageRows = cfg_.imageMaxRows > 0 ? std::min(cfg_.imageMaxRows, pageH_) : pageH_;
    auto cell = platform::cellPixelSize();
    if (cell.w > 0 && cell.h > 0) {
        lo.cellAspect = static_cast<double>(cell.h) / cell.w;
        lo.cellPixelW = cell.w;
    }
    ImageSizer sizer = [this](const std::string& p) { return images_.size(p); };

    std::string key = std::to_string(colWidth_) + "|" + std::to_string(lo.justify) + std::to_string(lo.hyphenate) +
                      std::to_string(static_cast<int>(lo.paragraphStyle)) + std::to_string(lo.lineSpacing) +
                      std::to_string(lo.showImages) + "|" + std::to_string(lo.maxImageRows);
    if (key != layoutKey_) {
        layouts_.clear();
        layouts_.reserve(book_.chapters().size());
        for (auto& ch : book_.chapters()) layouts_.push_back(layoutChapter(ch, lo, sizer));
        layoutKey_ = key;
    }
    pageStart_.clear();
    totalPages_ = 0;
    for (auto& l : layouts_) {
        paginate(l, pageH_);
        pageStart_.push_back(totalPages_);
        totalPages_ += static_cast<int>(l.pages.size());
    }
    goTo(keep);
}

// ------------------------------------------------------------------ navigation
Position Reader::pos() const {
    if (layouts_.empty()) return {chapter_, 0};
    const auto& L = layout(chapter_);
    if (scrollMode()) {
        size_t l = std::min(line_, L.lines.size() - 1);
        // Use the first non-blank line for a stable anchor.
        while (l + 1 < L.lines.size() && L.lines[l].kind == LineKind::Blank) ++l;
        return {chapter_, L.lines[l].off};
    }
    size_t pg = std::min(page_, L.pages.size() - 1);
    return {chapter_, L.lines[L.pages[pg]].off};
}

void Reader::goTo(const Position& p) {
    if (layouts_.empty()) {
        chapter_ = p.chapter;
        return;
    }
    chapter_ = std::clamp(p.chapter, 0, chapterCount() - 1);
    const auto& L = layout(chapter_);
    size_t line = L.lineForOffset(p.offset);
    if (scrollMode()) {
        line_ = line;
    } else {
        page_ = L.pageForLine(line);
        page_ -= page_ % static_cast<size_t>(cols_);
    }
}

bool Reader::pageDown() {
    if (scrollMode()) return scrollLines(std::max(1, pageH_ - 2));
    const auto& L = layout(chapter_);
    if (page_ + static_cast<size_t>(cols_) < L.pages.size()) {
        page_ += static_cast<size_t>(cols_);
        return true;
    }
    if (chapter_ + 1 < chapterCount()) {
        ++chapter_;
        page_ = 0;
        return true;
    }
    return false;
}

bool Reader::pageUp() {
    if (scrollMode()) return scrollLines(-std::max(1, pageH_ - 2));
    if (page_ >= static_cast<size_t>(cols_)) {
        page_ -= static_cast<size_t>(cols_);
        return true;
    }
    if (chapter_ > 0) {
        --chapter_;
        size_t n = layout(chapter_).pages.size();
        page_ = n ? n - 1 : 0;
        page_ -= page_ % static_cast<size_t>(cols_);
        return true;
    }
    return false;
}

bool Reader::scrollLines(int n) {
    if (!scrollMode()) {
        // In paged mode j/k turn pages (like Books' arrow keys).
        bool moved = false;
        for (int i = 0; i < std::abs(n); ++i) moved |= (n > 0 ? pageDown() : pageUp());
        return moved;
    }
    const int gap = 2;  // blank rows between chapters in scroll mode
    long target = static_cast<long>(line_) + n;
    bool moved = false;
    while (target < 0) {
        if (chapter_ == 0) {
            target = 0;
            break;
        }
        --chapter_;
        target += static_cast<long>(layout(chapter_).lines.size()) + gap;
        moved = true;
    }
    while (target >= static_cast<long>(layout(chapter_).lines.size()) + (chapter_ + 1 < chapterCount() ? gap : 0)) {
        if (chapter_ + 1 >= chapterCount()) break;
        target -= static_cast<long>(layout(chapter_).lines.size()) + gap;
        ++chapter_;
        moved = true;
    }
    long maxTop = static_cast<long>(layout(chapter_).lines.size()) - 1;
    if (chapter_ + 1 >= chapterCount()) maxTop = std::max(0L, static_cast<long>(layout(chapter_).lines.size()) - pageH_ / 2);
    target = std::clamp(target, 0L, std::max(0L, maxTop));
    moved |= static_cast<size_t>(target) != line_;
    line_ = static_cast<size_t>(target);
    return moved;
}

bool Reader::halfPage(int dir) {
    if (!scrollMode()) return dir > 0 ? pageDown() : pageUp();
    return scrollLines(dir * std::max(1, pageH_ / 2));
}

bool Reader::nextChapter() {
    // Next TOC entry by position (TOC order is not guaranteed); falls back to the next spine item.
    Position cur = pos();
    size_t curLine = layout(cur.chapter).lineForOffset(cur.offset);
    const TocEntry* best = nullptr;
    for (auto& e : book_.toc()) {
        if (!e.resolved || !(cur < e.pos)) continue;
        if (e.pos.chapter == cur.chapter && layout(e.pos.chapter).lineForOffset(e.pos.offset) <= curLine) continue;
        if (!best || e.pos < best->pos) best = &e;
    }
    if (best && (best->pos.chapter <= chapter_ + 1)) {
        goTo(best->pos);
        return true;
    }
    if (chapter_ + 1 < chapterCount()) {
        goTo({chapter_ + 1, 0});
        return true;
    }
    return false;
}

bool Reader::prevChapter() {
    Position cur = pos();
    const auto& toc = book_.toc();
    const TocEntry* best = nullptr;
    const TocEntry* containing = nullptr;
    for (auto& e : toc) {
        if (!e.resolved) continue;
        if (e.pos < cur) {
            if (!best || best->pos <= e.pos) {
                containing = best;
                best = &e;
            }
        }
    }
    // If we are already at the start of a section, go to the previous one.
    if (best) {
        goTo(best->pos);
        if (pos() == cur && containing) goTo(containing->pos);
        return true;
    }
    if (chapter_ > 0) {
        goTo({chapter_ - 1, 0});
        return true;
    }
    return false;
}

void Reader::first() { goTo({0, 0}); }

void Reader::last() {
    chapter_ = chapterCount() - 1;
    const auto& L = layout(chapter_);
    if (scrollMode()) {
        line_ = L.lines.size() > static_cast<size_t>(pageH_) ? L.lines.size() - static_cast<size_t>(pageH_) : 0;
    } else {
        page_ = L.pages.empty() ? 0 : L.pages.size() - 1;
        page_ -= page_ % static_cast<size_t>(cols_);
    }
}

void Reader::gotoGlobalPage(int page) {
    int target = std::clamp(page, 1, std::max(1, totalPages_)) - 1;
    auto it = std::upper_bound(pageStart_.begin(), pageStart_.end(), target);
    int ch = static_cast<int>(std::distance(pageStart_.begin(), it)) - 1;
    ch = std::max(0, ch);
    const auto& L = layout(ch);
    size_t pg = static_cast<size_t>(target - pageStart_[static_cast<size_t>(ch)]);
    pg = std::min(pg, L.pages.size() - 1);
    goTo({ch, L.lines[L.pages[pg]].off});
}

void Reader::gotoFraction(double f) {
    f = std::clamp(f, 0.0, 1.0);
    if (f >= 1.0) {
        last();
        return;
    }
    goTo(book_.fromAbsolute(static_cast<size_t>(f * static_cast<double>(book_.totalLength()))));
}

// ------------------------------------------------------------------ info
int Reader::currentGlobalPage() const {
    if (layouts_.empty()) return 1;
    const auto& L = layout(chapter_);
    size_t pg = scrollMode() ? L.pageForLine(line_) : page_;
    return pageStart_[static_cast<size_t>(chapter_)] + static_cast<int>(pg) + 1;
}

int Reader::totalPages() const { return totalPages_; }

int Reader::pageAt(const Position& p) const {
    if (layouts_.empty() || p.chapter < 0 || p.chapter >= chapterCount()) return 1;
    const auto& L = layout(p.chapter);
    return pageStart_[static_cast<size_t>(p.chapter)] + static_cast<int>(L.pageForLine(L.lineForOffset(p.offset))) + 1;
}

int Reader::pagesLeftInChapter() const {
    if (layouts_.empty()) return 0;
    const auto& L = layout(chapter_);
    size_t pg = scrollMode() ? L.pageForLine(line_) : page_ + static_cast<size_t>(cols_) - 1;
    return std::max(0, static_cast<int>(L.pages.size()) - 1 - static_cast<int>(pg));
}

bool Reader::atEnd() const {
    if (layouts_.empty()) return true;
    if (chapter_ + 1 < chapterCount()) return false;
    const auto& L = layout(chapter_);
    if (scrollMode()) return line_ + static_cast<size_t>(pageH_) >= L.lines.size();
    return page_ + static_cast<size_t>(cols_) >= L.pages.size();
}

double Reader::fraction() const {
    if (atEnd()) return 1.0;
    double total = static_cast<double>(std::max<size_t>(1, book_.totalLength()));
    return std::clamp(static_cast<double>(book_.absoluteOffset(pos())) / total, 0.0, 1.0);
}

std::pair<Position, Position> Reader::visibleRange() const {
    Position a = pos();
    Position b = a;
    if (layouts_.empty()) return {a, b};
    const auto& L = layout(chapter_);
    if (scrollMode()) {
        size_t endLine = std::min(L.lines.size(), line_ + static_cast<size_t>(pageH_));
        b = {chapter_, endLine ? L.lines[endLine - 1].end : 0};
    } else {
        size_t lastPage = std::min(L.pages.size() - 1, page_ + static_cast<size_t>(cols_) - 1);
        size_t endLine = L.pageEnd(lastPage);
        b = {chapter_, endLine ? L.lines[endLine - 1].end : 0};
    }
    return {a, b};
}

// ------------------------------------------------------------------ drawing
namespace {

struct Style {
    short pair = kPText;
    unsigned attrs = 0;
    bool operator==(const Style& o) const { return pair == o.pair && attrs == o.attrs; }
};

}  // namespace

void Reader::draw(Terminal& t, const DrawContext& ctx) {
    cells_.clear();
    links_.clear();
    placements_.clear();
    rowCounter_ = 0;
    if (layouts_.empty()) return;
    if (scrollMode()) {
        const int gap = 2;
        int c = chapter_;
        size_t l = line_;
        int row = 0;
        while (row < pageH_ && c < chapterCount()) {
            const auto& L = layout(c);
            if (l >= L.lines.size()) {
                if (c + 1 >= chapterCount()) break;
                row += gap;
                ++c;
                l = 0;
                continue;
            }
            const Line& line = L.lines[l];
            if (line.kind == LineKind::Image && (line.imageRow == 0 || row == 0)) {
                img::Placement p;
                p.path = line.image;
                p.row = top_ + row;
                p.col = colX_[0] + line.imageCol;
                p.rows = line.imageRows;
                p.cols = line.imageCols;
                p.clipTop = line.imageRow;
                p.visibleRows = std::min(line.imageRows - line.imageRow, pageH_ - row);
                placements_.push_back(p);
            }
            drawLine(t, top_ + row, colX_[0], line, c, ctx);
            ++l;
            ++row;
        }
        if (ctx.hints)
            for (auto& h : *ctx.hints) t.put(h.y, h.x, h.label, kPHint, A_BOLD);
        return;
    }
    const auto& L = layout(chapter_);
    for (int k = 0; k < cols_; ++k) {
        size_t pg = page_ + static_cast<size_t>(k);
        if (pg >= L.pages.size()) break;
        size_t from = L.pages[pg], to = L.pageEnd(pg);
        int row = 0;
        for (size_t i = from; i < to && row < pageH_; ++i, ++row) {
            const Line& line = L.lines[i];
            if (line.kind == LineKind::Image && line.imageRow == 0) {
                img::Placement p;
                p.path = line.image;
                p.row = top_ + row;
                p.col = colX_[k] + line.imageCol;
                p.rows = line.imageRows;
                p.cols = line.imageCols;
                p.visibleRows = std::min(line.imageRows, pageH_ - row);
                placements_.push_back(p);
            }
            drawLine(t, top_ + row, colX_[k], line, chapter_, ctx);
        }
    }
    // Hint labels on top of links.
    if (ctx.hints)
        for (auto& h : *ctx.hints) t.put(h.y, h.x, h.label, kPHint, A_BOLD);
}

void Reader::drawLine(Terminal& t, int y, int x0, const Line& line, int chapter, const DrawContext& ctx) {
    int row = rowCounter_++;
    if (line.kind == LineKind::Image || line.kind == LineKind::Blank) return;
    const Theme& th = cfg_.currentTheme();
    unsigned baseAttr = th.boldText ? A_BOLD : 0;
    // Highlights overlapping this line.
    std::vector<const Highlight*> hls;
    if (ctx.highlights)
        for (auto& h : *ctx.highlights)
            if (h.chapter == chapter && h.start < line.end + 1 && h.end > line.off) hls.push_back(&h);
    const std::vector<std::pair<size_t, size_t>>* matches = nullptr;
    if (ctx.search && !ctx.search->pattern.empty()) {
        auto it = ctx.search->byChapter.find(chapter);
        if (it != ctx.search->byChapter.end()) matches = &it->second;
    }
    auto inMatch = [&](size_t off) {
        if (!matches) return false;
        auto it = std::upper_bound(matches->begin(), matches->end(), std::make_pair(off, SIZE_MAX));
        if (it == matches->begin()) return false;
        --it;
        return off >= it->first && off < it->second;
    };
    auto selected = [&](size_t off) {
        if (!ctx.visual) return false;
        Position p{chapter, off};
        return !(p < ctx.selStart) && p < ctx.selEnd;
    };

    int lastLink = -1;
    for (auto& seg : line.segs) {
        Style base;
        base.attrs = baseAttr;
        uint16_t a = seg.attrs;
        if (a & kHeading) base.pair = kPHeading;
        if (a & kCode) base.pair = kPCode;
        if (a & kLink) base.pair = kPLink;
        if (a & kDim) base.pair = kPDim;
        if (a & kBold) base.attrs |= A_BOLD;
        if (a & kItalic) base.attrs |= t.italicAttr();
        if (a & kUnderline) base.attrs |= A_UNDERLINE;
        if (a & kStrike) base.attrs |= A_DIM;
        int x = x0 + seg.col;
        std::string buf;
        Style cur;
        int bufX = x;
        bool first = true;
        auto flush = [&]() {
            if (!buf.empty()) t.put(y, bufX, buf, cur.pair, cur.attrs);
            buf.clear();
        };
        size_t cpi = 0;
        for (size_t bi = 0; bi < seg.text.size(); ++cpi) {
            size_t st = bi;
            char32_t cp = utf8::decode(seg.text, bi);
            int w = utf8::width(cp);
            Style s = base;
            size_t off = seg.charOffset(st, cpi);
            if (!seg.decor) {
                for (auto* h : hls)
                    if (off >= h->start && off < h->end) {
                        if (h->style == kUnderlineStyle) {
                            s.pair = kPUnderline;
                            s.attrs |= A_UNDERLINE;
                        } else {
                            s.pair = static_cast<short>(kPHl0 + std::clamp(h->style, 0, 4));
                        }
                    }
                if (inMatch(off)) s.pair = kPSearch;
                if (selected(off)) s.pair = kPSelection;
                if (ctx.showCursor && ctx.cursor.chapter == chapter && ctx.cursor.offset == off) {
                    s.pair = kPCursor;
                    s.attrs &= ~static_cast<unsigned>(A_UNDERLINE);
                }
                if (w > 0) {
                    CellInfo ci;
                    ci.y = y;
                    ci.x = x;
                    ci.w = w;
                    ci.chapter = chapter;
                    ci.off = off;
                    ci.bytes = seg.text.size() == seg.srcLen ? bi - st : 1;
                    ci.cp = cp;
                    ci.row = row;
                    cells_.push_back(ci);
                }
                if (seg.link >= 0 && seg.link != lastLink && w > 0) {
                    links_.push_back({chapter, seg.link, y, x});
                }
            }
            if (first || !(s == cur)) {
                flush();
                cur = s;
                bufX = x;
                first = false;
            }
            buf.append(seg.text, st, bi - st);
            x += w;
        }
        flush();
        lastLink = seg.link;
    }
}

std::string Reader::visibleText() const {
    std::string out;
    int lastRow = -1;
    for (auto& c : cells_) {
        if (lastRow >= 0 && c.row != lastRow) out += ' ';
        lastRow = c.row;
        utf8::append(out, c.cp);
    }
    return str::collapseWs(out);
}

std::string Reader::textBetween(const Position& a, const Position& b) const {
    if (a.chapter != b.chapter || a.chapter < 0 || a.chapter >= static_cast<int>(book_.chapters().size())) return {};
    const auto& text = book_.chapters()[static_cast<size_t>(a.chapter)].text;
    size_t s = std::min(a.offset, text.size()), e = std::min(b.offset, text.size());
    if (e <= s) return {};
    std::string r = text.substr(s, e - s);
    // Remove soft hyphens and normalise block separators.
    r = str::replaceAll(r, "\xC2\xAD", "");
    return r;
}

}  // namespace clibooks
