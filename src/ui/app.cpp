#include "ui/app.hpp"

#include <curses.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

#include "app/i18n.hpp"
#include "util/platform.hpp"
#include "util/strings.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace clipub {

namespace {

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> out;
    for (auto& para : str::split(text, '\n', false)) {
        std::string cur;
        int w = 0;
        if (str::trim(para).empty()) {
            out.emplace_back();
            continue;
        }
        for (auto& word : str::splitWs(para)) {
            int ww = utf8::width(word);
            if (w + (w ? 1 : 0) + ww > width && w) {
                out.push_back(cur);
                cur.clear();
                w = 0;
            }
            std::string wd = word;
            while (utf8::width(wd) > width) {
                std::string head = utf8::truncate(wd, width, false);
                out.push_back(head);
                wd = wd.substr(head.size());
            }
            if (w) {
                cur += ' ';
                ++w;
            }
            cur += wd;
            w += utf8::width(wd);
        }
        out.push_back(cur);
    }
    return out;
}

const char* kColorKeys[] = {"color_yellow", "color_green", "color_blue", "color_pink", "color_purple", "color_underline"};

std::string percent(double f) { return std::to_string(static_cast<int>(std::floor(f * 100.0 + 0.5))) + "%"; }

const std::vector<std::string> kCommands{
    "quit",      "q",      "q!",       "wq",          "x",        "write",     "w",       "open",    "e",
    "library",   "toc",    "bookmarks", "marks",      "notes",    "annotations", "themes", "theme",  "help",
    "info",      "set",    "goto",     "page",        "chapter",  "search",    "nohlsearch", "noh", "bookmark",
    "export",    "speak",  "stop",     "define",      "reset",    "reload",    "first",   "last",   "back",
    "forward",   "images", "redraw",   "highlight",   "yank"};

}  // namespace

App::App(Config& cfg) : cfg_(cfg) {}

// ====================================================================== run loop
int App::run(const std::optional<fs::path>& file) {
    proto_ = img::parseProtocol(cfg_.images);
    truecolor_ = img::terminalHasTruecolor();
    if (!term_.init()) {
        std::fprintf(stderr, "clipub: could not initialise the terminal\n");
        return 1;
    }
    term_.applyTheme(cfg_.currentTheme());
    lastKey_ = lastSave_ = Clock::now();
    for (auto& w : cfg_.warnings) message(w, true);

    bool opened = false;
    if (file) opened = openBook(*file);
    if (!opened) {
        std::string err = message_;
        showLibrary();
        if (file) message(err, true);
    }

    while (running_) {
        render();
        int timeoutMs = pending_.empty() ? 500 : cfg_.keyTimeoutMs;
        auto key = term_.readKey(timeoutMs);
        if (!key) {
            if (!pending_.empty()) {
                // Ambiguous sequence timed out: execute the exact match if any.
                Mode m = visual_ ? Mode::Visual : (list_ || view_ == View::Library) ? Mode::List : Mode::Reader;
                Action a;
                auto r = cfg_.keys.lookup(m, pending_, a);
                int n = count_.empty() ? 0 : std::atoi(count_.c_str());
                pending_.clear();
                count_.clear();
                if (r == Keymap::Match::Partial && a != Action::None) perform(m, a, n);
                (void)r;
            }
            tick();
            continue;
        }
        auto now = Clock::now();
        if (view_ == View::Reader) {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - lastKey_).count();
            state_.readingSeconds += std::min<int64_t>(secs, 300);
        }
        lastKey_ = now;
        handleKey(*key);
    }
    saveState();
    if (ttsPid_ > 0) platform::killProcess(ttsPid_);
    if (proto_ == img::Protocol::Kitty) term_.writeRaw(img::kittyClear());
    term_.shutdown();
    return 0;
}

void App::tick() {
    if (dirty_ && Clock::now() - lastSave_ > std::chrono::seconds(20)) saveState();
}

void App::message(const std::string& m, bool error) {
    message_ = m;
    messageError_ = error;
}

// ====================================================================== books
bool App::openBook(const fs::path& path) {
    message(tr("loading"));
    if (term_.active()) render();
    std::string err;
    ParseOptions po;
    po.useBookStyles = cfg_.bookStyles;
    fs::path abs = fs::absolute(str::expandHome(path.string()));
    auto book = Book::open(abs, err, po);
    if (!book) {
        message(std::string(tr("error_open")) + ": " + err, true);
        return false;
    }
    closeBook();
    book_ = std::move(book);
    Book* bp = book_.get();
    images_ = std::make_unique<img::Cache>([bp](const std::string& p) { return bp->resource(p); });
    reader_ = std::make_unique<Reader>(*book_, cfg_, *images_, proto_);
    if (auto st = store_.load(book_->id())) state_ = *st;
    else state_ = BookState{};
    state_.id = book_->id();
    state_.path = abs.string();
    state_.title = book_->meta().title;
    state_.author = book_->meta().authorLine();
    state_.lastOpened = nowSeconds();
    reader_->setScreen(term_.width(), term_.height());
    reader_->goTo(state_.pos);
    foldedCache_.assign(book_->chapters().size(), std::nullopt);
    back_.clear();
    fwd_.clear();
    matches_ = {};
    view_ = View::Reader;
    list_.reset();
    popup_.reset();
    message("");
    saveState();
    return true;
}

void App::closeBook() {
    if (!book_) return;
    saveState();
    if (ttsPid_ > 0) {
        platform::killProcess(ttsPid_);
        ttsPid_ = -1;
    }
    reader_.reset();
    images_.reset();
    book_.reset();
    visual_ = hinting_ = false;
}

void App::saveState() {
    if (!book_ || !reader_) return;
    state_.pos = reader_->pos();
    state_.progress = reader_->fraction();
    store_.save(state_);
    dirty_ = false;
    lastSave_ = Clock::now();
}

void App::showLibrary() {
    ListPanel l;
    l.title = tr("library");
    l.fullscreen = true;
    l.emptyMsg = tr("library_empty");
    l.footer = "Enter · d · / · :open <ruta> · :q";
    l.refresh = [this]() { showLibrary(); };
    auto states = store_.all();
    std::sort(states.begin(), states.end(), [](auto& a, auto& b) { return a.lastOpened > b.lastOpened; });
    std::set<std::string> known;
    for (auto& s : states) {
        std::error_code ec;
        if (!fs::exists(s.path, ec)) continue;
        known.insert(fs::weakly_canonical(s.path, ec).string());
        ListItem it;
        it.text = s.title.empty() ? fs::path(s.path).stem().string() : s.title;
        if (!s.author.empty()) it.text += "  —  " + s.author;
        it.right = percent(s.progress) + "  " + (s.lastOpened ? str::formatTime(s.lastOpened) : tr("never"));
        std::string path = s.path, id = s.id;
        it.onSelect = [this, path]() { openBook(path); };
        it.onDelete = [this, id]() {
            store_.remove(id);
            showLibrary();
        };
        l.items.push_back(std::move(it));
    }
    // Scan configured folders for books never opened.
    std::vector<std::pair<std::string, fs::path>> found;
    for (auto& d : cfg_.libraryDirs) {
        std::error_code ec;
        size_t n = 0;
        for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && n < 5000; it.increment(ec), ++n) {
            if (ec) break;
            auto ext = str::lower(it->path().extension().string());
            if (ext != ".epub") continue;
            auto canon = fs::weakly_canonical(it->path(), ec).string();
            if (known.count(canon)) continue;
            known.insert(canon);
            std::string title = it->path().stem().string(), author;
            if (it->is_regular_file() || it->is_directory()) {
                if (auto m = Book::peek(it->path())) {
                    if (!m->title.empty()) title = m->title;
                    author = m->authorLine();
                }
            }
            found.emplace_back(title + (author.empty() ? "" : "  —  " + author), it->path());
            if (it->is_directory()) it.disable_recursion_pending();
        }
    }
    std::sort(found.begin(), found.end());
    for (auto& [label, p] : found) {
        ListItem it;
        it.text = label;
        it.right = "—";
        fs::path path = p;
        it.onSelect = [this, path]() { openBook(path); };
        l.items.push_back(std::move(it));
    }
    filterList(l);
    if (library_) {
        l.sel = std::min(library_->sel, std::max(0, static_cast<int>(l.visible.size()) - 1));
        l.filter = library_->filter;
        filterList(l);
    }
    library_ = std::move(l);
    view_ = View::Library;
}

// ====================================================================== drawing
void App::render() {
    term_.clear(kPText);
    bool overlay = list_.has_value() || popup_.has_value();
    if (view_ == View::Reader && reader_) {
        drawReader();
        if (visualEdge_ != 0 && !reader_->cells().empty()) {
            const auto& c = visualEdge_ > 0 ? reader_->cells().front() : reader_->cells().back();
            vCursor_ = {c.chapter, c.off};
            vCursorBytes_ = c.bytes;
            visualEdge_ = 0;
            term_.clear(kPText);
            drawReader();
        }
    } else if (library_) {
        drawList(*library_);
    }
    if (list_) drawList(*list_);
    if (popup_) drawPopup(*popup_);
    drawHeaderStatus();
    emitImages(overlay || view_ != View::Reader);
}

void App::drawReader() {
    DrawContext ctx;
    ctx.highlights = &state_.highlights;
    ctx.search = hlsearch_ ? &matches_ : nullptr;
    if (visual_) {
        ctx.visual = true;
        auto [a, b] = selection();
        ctx.selStart = a;
        ctx.selEnd = b;
        ctx.cursor = vCursor_;
        ctx.showCursor = true;
    }
    if (hinting_) ctx.hints = &hints_;
    reader_->setScreen(term_.width(), term_.height());
    reader_->draw(term_, ctx);
}

void App::drawHeaderStatus() {
    int W = term_.width(), H = term_.height();
    const Theme& th = cfg_.currentTheme();
    (void)th;
    if (view_ == View::Reader && reader_ && cfg_.showHeader) {
        Position p = reader_->pos();
        std::string section = book_->sectionTitle(p);
        std::string title = book_->meta().title;
        std::string head = section.empty() || section == title ? title : title + "  ·  " + section;
        head = utf8::truncate(head, W - 6);
        term_.put(0, std::max(0, (W - utf8::width(head)) / 2), head, kPDim);
        // Bookmark ribbon (Books shows a red ribbon on bookmarked pages).
        auto [a, b] = reader_->visibleRange();
        for (auto& bm : state_.bookmarks)
            if (!(bm.pos < a) && (bm.pos < b || bm.pos == a)) {
                term_.put(0, W - 3, "★", kPUnderline, A_BOLD);
                break;
            }
    }
    int y = H - 1;
    if (prompt_) {
        term_.fill(y, 0, W, kPText);
        std::string before = prompt_->buf.substr(0, prompt_->cursor);
        std::string full = prompt_->prefix + prompt_->buf;
        int cursorX = utf8::width(prompt_->prefix) + utf8::width(before);
        int shift = std::max(0, cursorX - (W - 2));
        // simple horizontal scroll
        std::string shown = full;
        if (shift > 0) {
            size_t i = 0;
            int w = 0;
            while (i < shown.size() && w < shift) w += utf8::width(utf8::decode(shown, i));
            shown = shown.substr(i);
        }
        term_.put(y, 0, shown, kPText);
        std::string under = " ";
        if (prompt_->cursor < prompt_->buf.size()) {
            size_t i = prompt_->cursor;
            utf8::decode(prompt_->buf, i);
            under = prompt_->buf.substr(prompt_->cursor, i - prompt_->cursor);
        }
        term_.put(y, cursorX - shift, under, kPCursor);
        return;
    }
    if (!message_.empty()) {
        term_.fill(y, 0, W, kPText);
        term_.put(y, 1, utf8::truncate(message_, W - 2), messageError_ ? kPError : kPText, messageError_ ? A_BOLD : 0);
        return;
    }
    if (view_ == View::Library) {
        if (library_) term_.put(y, 1, library_->footer, kPDim);
        return;
    }
    if (!reader_ || !cfg_.showStatus) {
        if (visual_ || hinting_) term_.put(y, 1, visual_ ? tr("visual") : tr("hints"), kPAccent, A_BOLD);
        return;
    }
    std::string left;
    int cur = reader_->currentGlobalPage(), total = reader_->totalPages();
    left = std::string(tr("page")) + " " + std::to_string(cur);
    if (reader_->columns() > 1 && cur < total) left += "–" + std::to_string(cur + 1);
    left += " " + std::string(tr("of")) + " " + std::to_string(total);
    if (!book_->pageList().empty()) {
        // Printed page label of the current position (Books: "Page Number").
        Position p = reader_->pos();
        const PageTarget* best = nullptr;
        for (auto& pt : book_->pageList())
            if (!(p < pt.pos)) best = &pt;
        if (best) left += "  (" + std::string(tr("print_page")) + " " + best->label + ")";
    }
    std::string mid;
    if (visual_) mid = tr("visual");
    else if (hinting_) mid = tr("hints");
    else if (!pending_.empty() || !count_.empty()) mid = count_ + keysToString(pending_);
    else if (ttsPid_ > 0) mid = tr("speaking");
    int left_n = reader_->pagesLeftInChapter();
    std::string right = left_n > 0 ? std::to_string(left_n) + " " + tr("pages_left_chapter") : tr("last_page_chapter");
    right += "  ·  " + percent(reader_->fraction());
    term_.put(y, 1, left, kPDim);
    if (!mid.empty()) term_.put(y, std::max(0, (W - utf8::width(mid)) / 2), mid, kPAccent, A_BOLD);
    int rw = utf8::width(right);
    if (rw + utf8::width(left) + 4 < W) term_.put(y, W - rw - 1, right, kPDim);
}

void App::filterList(ListPanel& l) {
    l.visible.clear();
    std::string f = utf8::foldCopy(l.filter);
    for (size_t i = 0; i < l.items.size(); ++i) {
        if (f.empty() || utf8::foldCopy(l.items[i].text + " " + l.items[i].sub).find(f) != std::string::npos)
            l.visible.push_back(static_cast<int>(i));
    }
    l.sel = std::clamp(l.sel, 0, std::max(0, static_cast<int>(l.visible.size()) - 1));
}

void App::drawList(ListPanel& l) {
    int W = term_.width(), H = term_.height();
    int bw, bh, bx, by;
    if (l.fullscreen) {
        bw = W;
        bh = H - 1;
        bx = 0;
        by = 0;
    } else {
        bw = std::min(W - 4, 96);
        bh = std::max(6, H - 4);
        bx = (W - bw) / 2;
        by = 1;
    }
    for (int r = 0; r < bh; ++r) term_.fill(by + r, bx, bw, l.fullscreen ? kPText : kPPopup);
    short body = l.fullscreen ? kPText : kPPopup;
    short border = l.fullscreen ? kPDim : kPPopupBorder;
    if (!l.fullscreen) {
        std::string top = "╭", bot = "╰";
        for (int i = 0; i < bw - 2; ++i) {
            top += "─";
            bot += "─";
        }
        top += "╮";
        bot += "╯";
        term_.put(by, bx, top, border);
        term_.put(by + bh - 1, bx, bot, border);
        for (int r = 1; r < bh - 1; ++r) {
            term_.put(by + r, bx, "│", border);
            term_.put(by + r, bx + bw - 1, "│", border);
        }
    }
    std::string title = " " + l.title + (l.filter.empty() ? "" : "  [" + l.filter + "]") + " ";
    term_.put(by, bx + 2, title, l.fullscreen ? kPAccent : kPPopupBorder, A_BOLD);
    int innerX = bx + 2, innerW = bw - 4;
    int rows = bh - (l.fullscreen ? 2 : 2);
    int y0 = by + 1;
    if (l.fullscreen) {
        y0 = by + 2;
        rows = bh - 2;
    }
    if (l.visible.empty()) {
        for (auto& line : wrapText(l.emptyMsg, innerW)) {
            term_.put(y0, innerX, line, kPDim);
            ++y0;
        }
        return;
    }
    // Each item uses 1 row, or 2 when it has a sub line.
    auto itemH = [&](int idx) { return l.items[static_cast<size_t>(idx)].sub.empty() ? 1 : 2; };
    if (l.sel < l.scroll) l.scroll = l.sel;
    while (true) {
        int used = 0;
        for (int i = l.scroll; i <= l.sel && i < static_cast<int>(l.visible.size()); ++i) used += itemH(l.visible[static_cast<size_t>(i)]);
        if (used <= rows || l.scroll >= l.sel) break;
        ++l.scroll;
    }
    int y = y0;
    for (int vi = l.scroll; vi < static_cast<int>(l.visible.size()) && y < y0 + rows; ++vi) {
        const auto& it = l.items[static_cast<size_t>(l.visible[static_cast<size_t>(vi)])];
        bool sel = vi == l.sel;
        short pair = sel ? kPSelection : body;
        term_.fill(y, innerX - 1, innerW + 2, pair);
        int indent = std::min(it.depth * 2, innerW / 3);
        int x = innerX + indent;
        if (it.markPair) term_.put(y, x, "▌", it.markPair);
        else if (it.current) term_.put(y, x, "▸", sel ? pair : kPAccent, A_BOLD);
        x += 2;
        int rw = utf8::width(it.right);
        int avail = innerX + innerW - x - (rw ? rw + 2 : 0);
        term_.put(y, x, utf8::truncate(it.text, avail), pair, it.current ? A_BOLD : 0);
        if (rw) term_.put(y, innerX + innerW - rw, it.right, sel ? pair : kPDim);
        ++y;
        if (!it.sub.empty() && y < y0 + rows) {
            term_.fill(y, innerX - 1, innerW + 2, pair);
            term_.put(y, innerX + indent + 2, utf8::truncate(it.sub, innerW - indent - 2), sel ? pair : kPDim,
                      term_.italicAttr());
            ++y;
        }
    }
    if (!l.fullscreen) {
        std::string pos = " " + std::to_string(l.sel + 1) + "/" + std::to_string(l.visible.size()) + " ";
        term_.put(by + bh - 1, bx + bw - 2 - utf8::width(pos), pos, kPPopupBorder);
        if (!l.footer.empty()) term_.put(by + bh - 1, bx + 2, " " + l.footer + " ", kPPopupBorder);
    }
}

void App::drawPopup(Popup& p) {
    int W = term_.width(), H = term_.height();
    int bw = std::min(W - 4, 84);
    int innerW = bw - 4;
    p.lines = wrapText(p.text, innerW);
    int bh = std::min(H - 4, static_cast<int>(p.lines.size()) + 2);
    bh = std::max(bh, 3);
    int bx = (W - bw) / 2, by = std::max(1, (H - bh) / 2);
    for (int r = 0; r < bh; ++r) term_.fill(by + r, bx, bw, kPPopup);
    std::string top = "╭", bot = "╰";
    for (int i = 0; i < bw - 2; ++i) {
        top += "─";
        bot += "─";
    }
    top += "╮";
    bot += "╯";
    term_.put(by, bx, top, kPPopupBorder);
    term_.put(by + bh - 1, bx, bot, kPPopupBorder);
    for (int r = 1; r < bh - 1; ++r) {
        term_.put(by + r, bx, "│", kPPopupBorder);
        term_.put(by + r, bx + bw - 1, "│", kPPopupBorder);
    }
    term_.put(by, bx + 2, " " + p.title + " ", kPPopupBorder, A_BOLD);
    int rows = bh - 2;
    p.scroll = std::clamp(p.scroll, 0, std::max(0, static_cast<int>(p.lines.size()) - rows));
    for (int i = 0; i < rows && p.scroll + i < static_cast<int>(p.lines.size()); ++i)
        term_.put(by + 1 + i, bx + 2, p.lines[static_cast<size_t>(p.scroll + i)], kPPopup);
    if (!p.footer.empty()) term_.put(by + bh - 1, bx + 2, " " + p.footer + " ", kPPopupBorder);
    if (static_cast<int>(p.lines.size()) > rows) {
        std::string s = " " + std::to_string(p.scroll + 1) + "-" + std::to_string(std::min<int>(p.scroll + rows, p.lines.size())) +
                        "/" + std::to_string(p.lines.size()) + " ";
        term_.put(by + bh - 1, bx + bw - 2 - utf8::width(s), s, kPPopupBorder);
    }
}

void App::emitImages(bool overlayVisible) {
    std::string sig;
    std::vector<img::Placement> pl;
    if (!overlayVisible && reader_ && proto_ != img::Protocol::None) {
        pl = reader_->placements();
        for (auto& p : pl)
            sig += p.path + "@" + std::to_string(p.row) + "," + std::to_string(p.col) + "," + std::to_string(p.clipTop) +
                   "," + std::to_string(p.visibleRows) + ";";
        if (!sig.empty()) sig += std::to_string(term_.width()) + "x" + std::to_string(term_.height()) + cfg_.theme;
    }
    bool changed = sig != lastImageSig_;
    if (changed && !lastImageSig_.empty() && proto_ != img::Protocol::Kitty) term_.forceFullRedraw();
    term_.refresh();
    if (!changed) return;
    std::string out;
    if (proto_ == img::Protocol::Kitty) out += img::kittyClear();
    auto cell = platform::cellPixelSize();
    uint32_t id = 1;
    for (auto& p : pl) out += img::render(*images_, p, proto_, cell.w, cell.h, term_.themeBg(), truecolor_, id++);
    term_.writeRaw(out);
    lastImageSig_ = sig;
}

void App::onResize() {
    endwin();
    ::refresh();
    term_.forceFullRedraw();
    lastImageSig_.clear();
    if (reader_) reader_->setScreen(term_.width(), term_.height());
}

// ====================================================================== input
void App::handleKey(const std::string& key) {
    if (key == "<Resize>") {
        onResize();
        return;
    }
    if (!prompt_) message("");
    if (confirm_) {
        auto f = std::move(confirm_);
        confirm_ = nullptr;
        if (key == "y" || key == "Y" || key == "s" || key == "S") f();
        return;
    }
    if (prompt_) {
        handlePrompt(key);
        return;
    }
    if (popup_) {
        if (key == "j" || key == "<Down>" || key == "<C-e>") ++popup_->scroll;
        else if (key == "k" || key == "<Up>" || key == "<C-y>") --popup_->scroll;
        else if (key == "<C-d>" || key == "<Space>" || key == "<PageDown>") popup_->scroll += 10;
        else if (key == "<C-u>" || key == "<PageUp>") popup_->scroll -= 10;
        else if (key == "<CR>" && popup_->onEnter) {
            auto f = std::move(popup_->onEnter);
            popup_.reset();
            f();
        } else if (key == "y") {
            copyText(popup_->text);
            popup_.reset();
        } else {
            popup_.reset();
        }
        return;
    }
    if (hinting_) {
        handleHints(key);
        return;
    }
    if (list_) dispatch(Mode::List, key);
    else if (view_ == View::Library) dispatch(Mode::List, key);
    else if (visual_) dispatch(Mode::Visual, key);
    else dispatch(Mode::Reader, key);
}

void App::dispatch(Mode m, const std::string& key) {
    Action a = Action::None;
    if (pending_.empty() && key.size() == 1 && std::isdigit(static_cast<unsigned char>(key[0])) &&
        (key != "0" || !count_.empty())) {
        Action dummy;
        if (cfg_.keys.lookup(m, {key}, dummy) != Keymap::Match::Full) {
            count_ += key;
            return;
        }
    }
    pending_.push_back(key);
    auto r = cfg_.keys.lookup(m, pending_, a);
    if (r == Keymap::Match::Full) {
        int n = count_.empty() ? 0 : std::atoi(count_.c_str());
        pending_.clear();
        count_.clear();
        perform(m, a, n);
        return;
    }
    if (r == Keymap::Match::Partial) {
        pendingSince_ = Clock::now();
        return;
    }
    // No match: if we were in the middle of a sequence, retry with this key alone.
    bool hadPrefix = pending_.size() > 1;
    pending_.clear();
    if (hadPrefix) {
        dispatch(m, key);
        return;
    }
    count_.clear();
}

void App::perform(Mode m, Action a, int count) {
    if (a == Action::Command) {
        openPrompt(":", [this](const std::string& c) { runCommand(c); }, "", &cmdHistory_);
        return;
    }
    switch (m) {
        case Mode::Reader: performReader(a, count); break;
        case Mode::Visual: performVisual(a, count); break;
        case Mode::List: performList(a, count); break;
        default: break;
    }
}

void App::jumpTo(const Position& p, bool record) {
    if (!reader_) return;
    if (record) {
        back_.push_back(reader_->pos());
        if (back_.size() > 100) back_.erase(back_.begin());
        fwd_.clear();
    }
    reader_->goTo(p);
    dirty_ = true;
}

void App::performReader(Action a, int count) {
    if (!reader_) return;
    int n = std::max(1, count);
    bool moved = false;
    switch (a) {
        case Action::ScrollDown: moved = reader_->scrollLines(n); break;
        case Action::ScrollUp: moved = reader_->scrollLines(-n); break;
        case Action::PageDown:
            for (int i = 0; i < n; ++i) moved |= reader_->pageDown();
            if (!moved) message(tr("end"));
            break;
        case Action::PageUp:
            for (int i = 0; i < n; ++i) moved |= reader_->pageUp();
            if (!moved) message(tr("beginning"));
            break;
        case Action::HalfPageDown: moved = reader_->halfPage(1); break;
        case Action::HalfPageUp: moved = reader_->halfPage(-1); break;
        case Action::NextChapter:
            for (int i = 0; i < n; ++i) moved |= reader_->nextChapter();
            break;
        case Action::PrevChapter:
            for (int i = 0; i < n; ++i) moved |= reader_->prevChapter();
            break;
        case Action::First:
            back_.push_back(reader_->pos());
            if (count > 0) reader_->gotoGlobalPage(count);
            else reader_->first();
            moved = true;
            break;
        case Action::Last:
            back_.push_back(reader_->pos());
            if (count > 0) reader_->gotoGlobalPage(count);
            else reader_->last();
            moved = true;
            break;
        case Action::GotoPercent:
            if (count > 0) {
                back_.push_back(reader_->pos());
                reader_->gotoFraction(std::min(count, 100) / 100.0);
                moved = true;
            }
            break;
        case Action::Back:
            if (!back_.empty()) {
                fwd_.push_back(reader_->pos());
                reader_->goTo(back_.back());
                back_.pop_back();
                moved = true;
            }
            break;
        case Action::Forward:
            if (!fwd_.empty()) {
                back_.push_back(reader_->pos());
                reader_->goTo(fwd_.back());
                fwd_.pop_back();
                moved = true;
            }
            break;
        case Action::Toc: showToc(); break;
        case Action::Bookmarks: showBookmarks(); break;
        case Action::Annotations: showAnnotations(); break;
        case Action::Library:
            saveState();
            showLibrary();
            break;
        case Action::Help: showHelp(); break;
        case Action::Info: showInfo(); break;
        case Action::Themes: showThemes(); break;
        case Action::SearchResults:
            if (!searchPattern_.empty()) showSearchResults(searchPattern_);
            else openPrompt("search: ", [this](const std::string& s) { showSearchResults(s); });
            break;
        case Action::ToggleBookmark: toggleBookmark(); break;
        case Action::SearchForward:
        case Action::SearchBackward: {
            bool back = a == Action::SearchBackward;
            openPrompt(back ? "?" : "/", [this, back](const std::string& s) {
                if (s.empty()) {
                    if (!searchPattern_.empty()) search(searchPattern_, back, true);
                } else {
                    search(s, back, false);
                }
            }, "", &searchHistory_);
            break;
        }
        case Action::SearchNext:
        case Action::SearchPrev:
            if (searchPattern_.empty()) break;
            hlsearch_ = true;
            for (int i = 0; i < n; ++i) search(searchPattern_, (a == Action::SearchPrev) != searchBackward_, true);
            break;
        case Action::Quit: running_ = false; break;
        case Action::Visual: enterVisual(); break;
        case Action::FollowLink: startHints(); break;
        case Action::ThemeNext: {
            int i = cfg_.themeIndex(cfg_.theme);
            cfg_.theme = cfg_.themes[static_cast<size_t>((i + 1) % static_cast<int>(cfg_.themes.size()))].name;
            applySettings(false);
            message(cfg_.theme);
            break;
        }
        case Action::ToggleMode:
            cfg_.mode = cfg_.mode == "scroll" ? "paged" : "scroll";
            applySettings(true);
            message(std::string("mode=") + cfg_.mode);
            break;
        case Action::WidthInc:
            cfg_.width += 4 * n;
            applySettings(true);
            message("width=" + std::to_string(cfg_.width));
            break;
        case Action::WidthDec:
            cfg_.width = std::max(20, cfg_.width - 4 * n);
            applySettings(true);
            message("width=" + std::to_string(cfg_.width));
            break;
        case Action::WidthReset:
            cfg_.width = 72;
            applySettings(true);
            break;
        case Action::ToggleJustify:
            cfg_.justify = !cfg_.justify;
            applySettings(true);
            message(cfg_.justify ? "justify" : "nojustify");
            break;
        case Action::Redraw:
            term_.forceFullRedraw();
            lastImageSig_.clear();
            break;
        case Action::Speak:
            if (ttsPid_ > 0) {
                platform::killProcess(ttsPid_);
                ttsPid_ = -1;
                message(tr("speech_stopped"));
            } else {
                speak(reader_->visibleText());
            }
            break;
        case Action::NextAnnotation:
        case Action::PrevAnnotation: {
            Position cur = reader_->pos();
            auto [va, vb] = reader_->visibleRange();
            const Highlight* best = nullptr;
            for (auto& h : state_.highlights) {
                Position hp{h.chapter, h.start};
                if (a == Action::NextAnnotation) {
                    if (vb < hp || (hp.chapter == vb.chapter && hp.offset >= vb.offset))
                        if (!best || hp < Position{best->chapter, best->start}) best = &h;
                } else if (hp < cur) {
                    if (!best || Position{best->chapter, best->start} < hp) best = &h;
                }
            }
            if (best) jumpTo({best->chapter, best->start});
            break;
        }
        default: break;
    }
    if (moved) dirty_ = true;
}

// ====================================================================== visual mode
void App::enterVisual() {
    if (!reader_ || reader_->cells().empty()) return;
    // Start at the first search match on the page, if any, else at the first character.
    const auto& cells = reader_->cells();
    const CellInfo* start = &cells.front();
    if (hlsearch_ && !matches_.pattern.empty())
        for (auto& c : cells) {
            auto it = matches_.byChapter.find(c.chapter);
            if (it == matches_.byChapter.end()) continue;
            bool hit = false;
            for (auto& [s, e] : it->second)
                if (c.off == s) hit = true;
            if (hit) {
                start = &c;
                break;
            }
        }
    visual_ = true;
    vAnchor_ = vCursor_ = {start->chapter, start->off};
    vCursorBytes_ = start->bytes;
}

std::pair<Position, Position> App::selection() const {
    Position a = vAnchor_, b = vCursor_;
    if (b < a) std::swap(a, b);
    // Include the character under the far end.
    size_t bytes = 1;
    if (reader_)
        for (auto& c : reader_->cells())
            if (c.chapter == b.chapter && c.off == b.offset) bytes = c.bytes;
    if (a.chapter != b.chapter) b = {a.chapter, SIZE_MAX / 2};
    return {a, {b.chapter, b.offset + bytes}};
}

void App::moveVisual(Action a, int count) {
    const auto& cells = reader_->cells();
    if (cells.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < cells.size(); ++i)
        if (cells[i].chapter == vCursor_.chapter && cells[i].off == vCursor_.offset) {
            idx = static_cast<int>(i);
            break;
        }
    if (idx < 0) idx = 0;
    int n = std::max(1, count);
    int N = static_cast<int>(cells.size());
    auto isWord = [&](int i) { return i >= 0 && i < N && utf8::isWordChar(cells[static_cast<size_t>(i)].cp); };
    auto sameRow = [&](int i, int j) { return cells[static_cast<size_t>(i)].row == cells[static_cast<size_t>(j)].row; };
    for (int k = 0; k < n; ++k) {
        switch (a) {
            case Action::CharLeft:
                if (idx == 0) {
                    if (reader_->pageUp()) visualEdge_ = -1;
                    return;
                }
                --idx;
                break;
            case Action::CharRight:
                if (idx == N - 1) {
                    if (reader_->pageDown()) visualEdge_ = 1;
                    return;
                }
                ++idx;
                break;
            case Action::LineDown:
            case Action::LineUp: {
                int dir = a == Action::LineDown ? 1 : -1;
                int row = cells[static_cast<size_t>(idx)].row, x = cells[static_cast<size_t>(idx)].x;
                int target = -1;
                int j = idx;
                while (j >= 0 && j < N && cells[static_cast<size_t>(j)].row == row) j += dir;
                if (j < 0 || j >= N) {
                    if (dir > 0 ? reader_->pageDown() : reader_->pageUp()) visualEdge_ = dir;
                    return;
                }
                int nrow = cells[static_cast<size_t>(j)].row;
                int bestD = 1 << 30;
                for (int q = j; q >= 0 && q < N && cells[static_cast<size_t>(q)].row == nrow; q += dir) {
                    int d = std::abs(cells[static_cast<size_t>(q)].x - x);
                    if (d < bestD) {
                        bestD = d;
                        target = q;
                    }
                }
                idx = target < 0 ? j : target;
                break;
            }
            case Action::WordNext: {
                int j = idx;
                while (j < N && isWord(j)) ++j;
                while (j < N && !isWord(j)) ++j;
                if (j >= N) {
                    if (reader_->pageDown()) visualEdge_ = 1;
                    return;
                }
                idx = j;
                break;
            }
            case Action::WordBack: {
                int j = idx - 1;
                while (j >= 0 && !isWord(j)) --j;
                while (j > 0 && isWord(j - 1)) --j;
                if (j < 0) {
                    if (reader_->pageUp()) visualEdge_ = -1;
                    return;
                }
                idx = j;
                break;
            }
            case Action::WordEnd: {
                int j = idx + 1;
                while (j < N && !isWord(j)) ++j;
                while (j + 1 < N && isWord(j + 1)) ++j;
                if (j >= N) return;
                idx = j;
                break;
            }
            case Action::LineStart:
                while (idx > 0 && sameRow(idx, idx - 1)) --idx;
                break;
            case Action::LineEnd:
                while (idx + 1 < N && sameRow(idx, idx + 1)) ++idx;
                break;
            default: break;
        }
    }
    vCursor_ = {cells[static_cast<size_t>(idx)].chapter, cells[static_cast<size_t>(idx)].off};
    vCursorBytes_ = cells[static_cast<size_t>(idx)].bytes;
}

void App::performVisual(Action a, int count) {
    switch (a) {
        case Action::CharLeft:
        case Action::CharRight:
        case Action::LineDown:
        case Action::LineUp:
        case Action::WordNext:
        case Action::WordBack:
        case Action::WordEnd:
        case Action::LineStart:
        case Action::LineEnd: moveVisual(a, count); break;
        case Action::PageDown:
            if (reader_->pageDown()) visualEdge_ = 1;
            break;
        case Action::PageUp:
            if (reader_->pageUp()) visualEdge_ = -1;
            break;
        case Action::SwapAnchor: std::swap(vAnchor_, vCursor_); break;
        case Action::Yank: {
            auto [s, e] = selection();
            copyText(reader_->textBetween(s, e));
            visual_ = false;
            break;
        }
        case Action::Highlight: addHighlight(kYellow, false); break;
        case Action::Color1: addHighlight(kYellow, false); break;
        case Action::Color2: addHighlight(kGreen, false); break;
        case Action::Color3: addHighlight(kBlue, false); break;
        case Action::Color4: addHighlight(kPink, false); break;
        case Action::Color5: addHighlight(kPurple, false); break;
        case Action::Underline: addHighlight(kUnderlineStyle, false); break;
        case Action::AddNote: addHighlight(kYellow, true); break;
        case Action::LookUp: {
            auto [s, e] = selection();
            std::string w = std::string(str::trim(reader_->textBetween(s, e)));
            visual_ = false;
            lookUp(w);
            break;
        }
        case Action::SpeakSelection: {
            auto [s, e] = selection();
            visual_ = false;
            speak(reader_->textBetween(s, e));
            break;
        }
        case Action::SearchSelection: {
            auto [s, e] = selection();
            std::string w = str::collapseWs(reader_->textBetween(s, e));
            visual_ = false;
            if (!w.empty()) search(w, false, true);
            break;
        }
        case Action::ExitMode: visual_ = false; break;
        default: break;
    }
}

void App::addHighlight(int style, bool thenNote) {
    auto [s, e] = selection();
    visual_ = false;
    if (e.offset <= s.offset) return;
    const auto& text = book_->chapters()[static_cast<size_t>(s.chapter)].text;
    e.offset = std::min(e.offset, text.size());
    Highlight h;
    h.chapter = s.chapter;
    h.start = s.offset;
    h.end = e.offset;
    h.style = style;
    h.created = nowSeconds();
    // Merge with overlapping highlights (Books merges overlapping ranges too).
    for (auto it = state_.highlights.begin(); it != state_.highlights.end();) {
        if (it->chapter == h.chapter && it->start < h.end && it->end > h.start) {
            h.start = std::min(h.start, it->start);
            h.end = std::max(h.end, it->end);
            if (h.note.empty()) h.note = it->note;
            it = state_.highlights.erase(it);
        } else {
            ++it;
        }
    }
    h.text = str::collapseWs(reader_->textBetween({h.chapter, h.start}, {h.chapter, h.end}));
    state_.highlights.push_back(h);
    std::sort(state_.highlights.begin(), state_.highlights.end(),
              [](auto& a, auto& b) { return Position{a.chapter, a.start} < Position{b.chapter, b.start}; });
    saveState();
    message(tr("highlight_added"));
    if (thenNote) {
        for (size_t i = 0; i < state_.highlights.size(); ++i)
            if (state_.highlights[i].chapter == h.chapter && state_.highlights[i].start == h.start) {
                editNote(i);
                break;
            }
    }
}

void App::editNote(size_t idx) {
    if (idx >= state_.highlights.size()) return;
    int ch = state_.highlights[idx].chapter;
    size_t start = state_.highlights[idx].start;
    openPrompt(tr("note_prompt"), [this, ch, start](const std::string& note) {
        for (auto& h : state_.highlights)
            if (h.chapter == ch && h.start == start) {
                h.note = note;
                saveState();
                message(tr("note_saved"));
                if (list_ && list_->refresh) list_->refresh();
                return;
            }
    }, state_.highlights[idx].note);
}

// ====================================================================== links / hints
void App::startHints() {
    if (!reader_) return;
    hintTargets_.clear();
    hints_.clear();
    for (auto& l : reader_->links()) hintTargets_.push_back(l);
    if (hintTargets_.empty()) {
        message(tr("no_links"));
        return;
    }
    static const std::string alpha = "asdfghjklqwertyuiopzxcvbnm";
    size_t n = hintTargets_.size();
    bool two = n > alpha.size();
    for (size_t i = 0; i < n; ++i) {
        HintLabel h;
        h.label = two ? std::string{alpha[i / alpha.size() % alpha.size()], alpha[i % alpha.size()]}
                      : std::string(1, alpha[i]);
        h.y = hintTargets_[i].y;
        h.x = hintTargets_[i].x;
        hints_.push_back(h);
    }
    hintBuf_.clear();
    hinting_ = true;
}

void App::handleHints(const std::string& key) {
    if (key == "<Esc>" || key == "<C-c>" || key == "q") {
        hinting_ = false;
        return;
    }
    if (key == "<BS>") {
        if (!hintBuf_.empty()) hintBuf_.pop_back();
        return;
    }
    if (key.size() != 1) return;
    hintBuf_ += key;
    bool prefix = false;
    for (size_t i = 0; i < hints_.size(); ++i) {
        if (hints_[i].label == hintBuf_) {
            hinting_ = false;
            followLink(hintTargets_[i].chapter, hintTargets_[i].link);
            return;
        }
        if (str::startsWith(hints_[i].label, hintBuf_)) prefix = true;
    }
    if (!prefix) hinting_ = false;
}

void App::followLink(int chapter, int linkIdx) {
    const auto& ch = book_->chapters()[static_cast<size_t>(chapter)];
    if (linkIdx < 0 || linkIdx >= static_cast<int>(ch.links.size())) return;
    const Link& l = ch.links[static_cast<size_t>(linkIdx)];
    if (l.external) {
        std::string url = l.target;
        showPopup(tr("external_link"), url, [url]() { platform::openExternal(url); });
        return;
    }
    auto target = book_->resolve(l.target);
    if (!target) {
        message(l.target, true);
        return;
    }
    auto [frag_file, frag] = str::splitFragment(l.target);
    bool isNote = l.noteref;
    const auto& tch = book_->chapters()[static_cast<size_t>(target->chapter)];
    if (!frag.empty() && tch.notes.count(frag)) isNote = true;
    if (isNote) {
        if (auto text = book_->footnoteText(l.target)) {
            Position p = *target;
            showPopup(tr("footnote"), *text, [this, p]() { jumpTo(p); }, "Enter → ir · y copiar · Esc");
            return;
        }
    }
    jumpTo(*target);
}

// ====================================================================== bookmarks
void App::toggleBookmark() {
    auto [a, b] = reader_->visibleRange();
    for (auto it = state_.bookmarks.begin(); it != state_.bookmarks.end(); ++it) {
        if (!(it->pos < a) && (it->pos < b || it->pos == a)) {
            state_.bookmarks.erase(it);
            saveState();
            message(tr("bookmark_removed"));
            return;
        }
    }
    Bookmark bm;
    bm.pos = a;
    bm.created = nowSeconds();
    std::string sect = book_->sectionTitle(a);
    std::string snip = snippetAt(a, 0, 80);
    if (!sect.empty() && str::startsWith(snip, sect)) snip = std::string(str::trim(snip.substr(sect.size())));
    bm.label = (sect.empty() ? "" : sect + " — ") + utf8::truncate(snip, 60);
    state_.bookmarks.push_back(bm);
    std::sort(state_.bookmarks.begin(), state_.bookmarks.end(), [](auto& x, auto& y) { return x.pos < y.pos; });
    saveState();
    message(tr("bookmark_added"));
}

std::string App::snippetAt(const Position& p, size_t before, size_t after) const {
    const auto& t = book_->chapters()[static_cast<size_t>(p.chapter)].text;
    size_t s = p.offset > before ? p.offset - before : 0;
    size_t e = std::min(t.size(), p.offset + after);
    while (s > 0 && (static_cast<unsigned char>(t[s]) & 0xC0) == 0x80) --s;
    while (e < t.size() && (static_cast<unsigned char>(t[e]) & 0xC0) == 0x80) ++e;
    std::string r = str::collapseWs(t.substr(s, e - s));
    return str::replaceAll(r, "\xC2\xAD", "");
}

// ====================================================================== panels
void App::showToc() {
    ListPanel l;
    l.title = tr("contents");
    l.emptyMsg = tr("no_toc");
    l.footer = "Enter · / · q";
    int cur = book_->tocIndexAt(reader_->pos());
    const auto& toc = book_->toc();
    for (size_t i = 0; i < toc.size(); ++i) {
        ListItem it;
        it.text = toc[i].title;
        it.depth = toc[i].depth;
        it.current = static_cast<int>(i) == cur;
        if (toc[i].resolved) {
            it.right = std::to_string(reader_->pageAt(toc[i].pos));
            Position p = toc[i].pos;
            it.onSelect = [this, p]() { jumpTo(p); };
        }
        l.items.push_back(std::move(it));
    }
    filterList(l);
    if (cur >= 0) l.sel = cur;
    list_ = std::move(l);
}

void App::showBookmarks() {
    ListPanel l;
    l.title = tr("bookmarks");
    l.emptyMsg = tr("no_bookmarks");
    l.footer = "Enter · d · q";
    l.refresh = [this]() {
        int sel = list_ ? list_->sel : 0;
        showBookmarks();
        list_->sel = std::min(sel, std::max(0, static_cast<int>(list_->visible.size()) - 1));
    };
    for (size_t i = 0; i < state_.bookmarks.size(); ++i) {
        const auto& b = state_.bookmarks[i];
        ListItem it;
        it.text = b.label;
        it.right = std::string(tr("page")) + " " + std::to_string(reader_->pageAt(b.pos));
        Position p = b.pos;
        it.onSelect = [this, p]() { jumpTo(p); };
        int64_t created = b.created;
        it.onDelete = [this, p, created]() {
            std::erase_if(state_.bookmarks, [&](auto& x) { return x.pos == p && x.created == created; });
            saveState();
        };
        l.items.push_back(std::move(it));
    }
    filterList(l);
    list_ = std::move(l);
}

void App::showAnnotations() {
    ListPanel l;
    l.title = tr("annotations");
    l.emptyMsg = tr("no_annotations");
    l.footer = "Enter · e nota · c color · d · q";
    l.refresh = [this]() {
        int sel = list_ ? list_->sel : 0;
        showAnnotations();
        list_->sel = std::min(sel, std::max(0, static_cast<int>(list_->visible.size()) - 1));
    };
    for (size_t i = 0; i < state_.highlights.size(); ++i) {
        const auto& h = state_.highlights[i];
        ListItem it;
        it.text = "“" + h.text + "”";
        it.sub = h.note.empty() ? "" : "✎ " + h.note;
        it.markPair = h.style == kUnderlineStyle ? kPUnderline : static_cast<short>(kPHl0 + std::clamp(h.style, 0, 4));
        Position p{h.chapter, h.start};
        it.right = std::to_string(reader_->pageAt(p));
        it.onSelect = [this, p]() { jumpTo(p); };
        int ch = h.chapter;
        size_t start = h.start;
        it.onDelete = [this, ch, start]() {
            std::erase_if(state_.highlights, [&](auto& x) { return x.chapter == ch && x.start == start; });
            saveState();
            message(tr("highlight_removed"));
        };
        it.onEdit = [this, i]() { editNote(i); };
        it.onColor = [this, ch, start]() {
            for (auto& x : state_.highlights)
                if (x.chapter == ch && x.start == start) x.style = (x.style + 1) % 6;
            saveState();
        };
        l.items.push_back(std::move(it));
    }
    filterList(l);
    list_ = std::move(l);
}

void App::showThemes() {
    ListPanel l;
    l.title = tr("themes");
    l.footer = "Enter · q";
    for (auto& t : cfg_.themes) {
        ListItem it;
        it.text = t.name;
        it.current = t.name == cfg_.theme;
        std::string name = t.name;
        it.right = t.useTerminal ? "terminal" : "";
        it.onSelect = [this, name]() {
            cfg_.theme = name;
            applySettings(false);
        };
        l.items.push_back(std::move(it));
    }
    filterList(l);
    l.sel = std::max(0, cfg_.themeIndex(cfg_.theme));
    list_ = std::move(l);
}

void App::showHelp() {
    std::ostringstream ss;
    auto section = [&](const char* title, Mode m, std::initializer_list<Action> acts) {
        ss << title << "\n";
        for (Action a : acts) {
            auto keys = cfg_.keys.keysFor(m, a);
            if (keys.empty()) continue;
            std::string k;
            for (size_t i = 0; i < keys.size() && i < 5; ++i) k += (i ? "  " : "") + keys[i];
            ss << "  " << utf8::fit(actionName(a), 18) << " " << k << "\n";
        }
        ss << "\n";
    };
    using A = Action;
    section(currentLanguage() == "es" ? "LECTURA" : "READING", Mode::Reader,
            {A::PageDown, A::PageUp, A::ScrollDown, A::ScrollUp, A::HalfPageDown, A::HalfPageUp, A::NextChapter,
             A::PrevChapter, A::First, A::Last, A::GotoPercent, A::Back, A::Forward, A::Toc, A::Bookmarks,
             A::Annotations, A::ToggleBookmark, A::SearchForward, A::SearchBackward, A::SearchNext, A::SearchPrev,
             A::SearchResults, A::Visual, A::FollowLink, A::NextAnnotation, A::PrevAnnotation, A::ThemeNext,
             A::Themes, A::ToggleMode, A::WidthInc, A::WidthDec, A::WidthReset, A::ToggleJustify, A::Speak, A::Info,
             A::Library, A::Redraw, A::Command, A::Quit});
    section("VISUAL", Mode::Visual,
            {A::CharLeft, A::CharRight, A::LineDown, A::LineUp, A::WordNext, A::WordBack, A::WordEnd, A::LineStart,
             A::LineEnd, A::SwapAnchor, A::Yank, A::Highlight, A::Color1, A::Color2, A::Color3, A::Color4, A::Color5,
             A::Underline, A::AddNote, A::LookUp, A::SpeakSelection, A::SearchSelection, A::ExitMode});
    section(currentLanguage() == "es" ? "LISTAS" : "LISTS", Mode::List,
            {A::LineDown, A::LineUp, A::HalfPageDown, A::HalfPageUp, A::First, A::Last, A::Select, A::Close,
             A::Delete, A::Edit, A::ChangeColor, A::Filter});
    ss << (currentLanguage() == "es" ? "COMANDOS" : "COMMANDS") << "\n"
       << "  :q  :q!  :wq  :x  ZZ          salir / quit\n"
       << "  :open <ruta>  :e <ruta>      abrir libro\n"
       << "  :library                     biblioteca\n"
       << "  :toc  :bookmarks  :notes     paneles\n"
       << "  :theme <nombre>  :themes     tema\n"
       << "  :set width=80  :set justify! :set mode=scroll|paged\n"
       << "  :set columns=1|2|auto  :set paragraphs=auto|spaced|indented\n"
       << "  :set linespacing=2  :set hyphenate!  :set images=kitty|iterm|sixel|blocks|none\n"
       << "  :set header!  :set status!  :set bookstyles!\n"
       << "  :123  :goto 45%  :goto 123   ir a página / porcentaje\n"
       << "  :page <etiqueta>             página impresa (page-list)\n"
       << "  :chapter <n>                 capítulo n del índice\n"
       << "  :search <texto>              lista de resultados\n"
       << "  :noh                         quitar resaltado de búsqueda\n"
       << "  :bookmark                    alternar marcador\n"
       << "  :export [archivo.md]         exportar resaltados y notas\n"
       << "  :define <palabra>            diccionario\n"
       << "  :speak  :stop                leer en voz alta\n"
       << "  :info  :reload  :reset  :redraw\n";
    showPopup(tr("help"), ss.str());
}

void App::showInfo() {
    if (!book_) return;
    const auto& m = book_->meta();
    std::ostringstream ss;
    ss << m.title << "\n";
    if (!m.authors.empty()) ss << tr("author") << ": " << m.authorLine() << "\n";
    if (!m.publisher.empty()) ss << tr("publisher") << ": " << m.publisher << "\n";
    if (!m.language.empty()) ss << tr("language") << ": " << m.language << "\n";
    if (!m.date.empty()) ss << "Date: " << m.date << "\n";
    if (!m.identifier.empty()) ss << tr("identifier") << ": " << m.identifier << "\n";
    ss << tr("file") << ": " << state_.path << "\n\n";
    ss << tr("progress") << ": " << percent(reader_->fraction()) << "  ·  " << tr("page") << " "
       << reader_->currentGlobalPage() << " " << tr("of") << " " << reader_->totalPages() << "\n";
    ss << tr("chapter") << ": " << book_->sectionTitle(reader_->pos()) << "\n";
    int64_t mins = state_.readingSeconds / 60;
    ss << (currentLanguage() == "es" ? "Tiempo de lectura: " : "Reading time: ") << mins / 60 << "h " << mins % 60 << "m\n";
    ss << tr("bookmarks") << ": " << state_.bookmarks.size() << "  ·  " << tr("annotations") << ": "
       << state_.highlights.size() << "\n";
    ss << tr("images") << ": " << img::protocolName(proto_) << (truecolor_ ? " (24-bit)" : "") << "\n";
    ss << "Layout: width=" << cfg_.width << " columns=" << reader_->columns() << " mode=" << cfg_.mode
       << " justify=" << (cfg_.justify ? "on" : "off") << " hyphenate=" << (cfg_.hyphenate ? "on" : "off") << "\n";
    if (!m.description.empty()) ss << "\n" << str::collapseWs(m.description) << "\n";
    showPopup(tr("info"), ss.str());
}

// ====================================================================== search
const utf8::Folded& App::folded(int chapter) {
    auto& slot = foldedCache_[static_cast<size_t>(chapter)];
    if (!slot) slot = utf8::foldString(book_->chapters()[static_cast<size_t>(chapter)].text);
    return *slot;
}

void App::computeMatches() {
    matches_.byChapter.clear();
    matches_.pattern = searchPattern_;
    if (searchPattern_.empty() || !book_) return;
    bool caseSensitive = false;
    if (cfg_.smartcase)
        for (char c : searchPattern_)
            if (std::isupper(static_cast<unsigned char>(c))) caseSensitive = true;
    std::string needle = caseSensitive ? searchPattern_ : utf8::foldCopy(searchPattern_);
    if (needle.empty()) return;
    for (size_t c = 0; c < book_->chapters().size(); ++c) {
        auto& v = matches_.byChapter[static_cast<int>(c)];
        if (caseSensitive) {
            const auto& t = book_->chapters()[c].text;
            for (size_t p = t.find(needle); p != std::string::npos; p = t.find(needle, p + 1))
                v.emplace_back(p, p + needle.size());
        } else {
            const auto& f = folded(static_cast<int>(c));
            for (size_t p = f.text.find(needle); p != std::string::npos; p = f.text.find(needle, p + 1)) {
                size_t s = f.map[p];
                size_t e = p + needle.size() < f.map.size() ? f.map[p + needle.size()]
                                                           : book_->chapters()[c].text.size();
                v.emplace_back(s, e);
            }
        }
        if (v.empty()) matches_.byChapter.erase(static_cast<int>(c));
    }
}

void App::search(const std::string& pattern, bool backward, bool fromNext) {
    if (pattern != searchPattern_ || matches_.pattern != pattern) {
        searchPattern_ = pattern;
        computeMatches();
        lastMatch_ = {-1, 0};
    }
    searchBackward_ = fromNext ? searchBackward_ : backward;
    hlsearch_ = true;
    if (matches_.byChapter.empty()) {
        message(std::string(tr("pattern_not_found")) + ": " + pattern, true);
        return;
    }
    std::vector<Position> all;
    for (auto& [c, v] : matches_.byChapter)
        for (auto& m : v) all.push_back({c, m.first});
    auto [va, vb] = reader_->visibleRange();
    Position from = lastMatch_.chapter >= 0 && !(lastMatch_ < va) && lastMatch_ < vb ? lastMatch_ : Position{va.chapter, va.offset};
    bool strict = lastMatch_.chapter >= 0 && from == lastMatch_;
    const Position* hit = nullptr;
    bool wrapped = false;
    if (!backward) {
        for (auto& p : all)
            if (strict ? from < p : !(p < from)) {
                hit = &p;
                break;
            }
        if (!hit && cfg_.wrapSearch) {
            hit = &all.front();
            wrapped = true;
        }
    } else {
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            if (*it < from) {
                hit = &*it;
                break;
            }
        if (!hit && cfg_.wrapSearch) {
            hit = &all.back();
            wrapped = true;
        }
    }
    if (!hit) {
        message(tr("pattern_not_found"), true);
        return;
    }
    lastMatch_ = *hit;
    auto [a, b] = reader_->visibleRange();
    if (*hit < a || !(*hit < b)) jumpTo(*hit);
    size_t idx = static_cast<size_t>(hit - all.data()) + 1;
    message("[" + std::to_string(idx) + "/" + std::to_string(all.size()) + "] " + pattern +
            (wrapped ? "  (" + std::string(tr("search_wrapped")) + ")" : ""));
}

void App::showSearchResults(const std::string& pattern) {
    if (pattern.empty()) return;
    if (pattern != searchPattern_) {
        searchPattern_ = pattern;
        computeMatches();
    }
    hlsearch_ = true;
    ListPanel l;
    l.title = std::string(tr("search_results")) + ": " + pattern;
    l.emptyMsg = tr("no_results");
    l.footer = "Enter · / · q";
    for (auto& [c, v] : matches_.byChapter)
        for (auto& m : v) {
            Position p{c, m.first};
            ListItem it;
            it.text = snippetAt(p, 30, 50);
            it.sub = book_->sectionTitle(p);
            it.right = std::to_string(reader_->pageAt(p));
            it.onSelect = [this, p]() {
                lastMatch_ = p;
                jumpTo(p);
            };
            l.items.push_back(std::move(it));
            if (l.items.size() > 5000) break;
        }
    filterList(l);
    list_ = std::move(l);
}

// ====================================================================== list mode
void App::performList(Action a, int count) {
    ListPanel* l = list_ ? &*list_ : (library_ ? &*library_ : nullptr);
    if (!l) return;
    int n = std::max(1, count);
    int size = static_cast<int>(l->visible.size());
    auto current = [&]() -> ListItem* {
        if (l->visible.empty()) return nullptr;
        return &l->items[static_cast<size_t>(l->visible[static_cast<size_t>(l->sel)])];
    };
    switch (a) {
        case Action::LineDown: l->sel = std::min(size - 1, l->sel + n); break;
        case Action::LineUp: l->sel = std::max(0, l->sel - n); break;
        case Action::HalfPageDown: l->sel = std::min(size - 1, l->sel + term_.height() / 2); break;
        case Action::HalfPageUp: l->sel = std::max(0, l->sel - term_.height() / 2); break;
        case Action::First: l->sel = count > 0 ? std::min(size - 1, count - 1) : 0; break;
        case Action::Last: l->sel = count > 0 ? std::min(size - 1, count - 1) : size - 1; break;
        case Action::Select:
            if (auto* it = current(); it && it->onSelect) {
                auto f = it->onSelect;
                if (list_) list_.reset();
                f();
            }
            break;
        case Action::Close:
            if (list_) list_.reset();
            else if (view_ == View::Library) {
                if (reader_) view_ = View::Reader;
                else if (!l->filter.empty()) {
                    l->filter.clear();
                    filterList(*l);
                } else running_ = false;
            }
            break;
        case Action::Delete:
            if (auto* it = current(); it && it->onDelete) {
                auto f = it->onDelete;
                message(tr("delete_confirm"));
                bool isLib = !list_;
                confirm_ = [this, f, isLib]() {
                    f();
                    message("");
                    if (!isLib && list_ && list_->refresh) list_->refresh();
                };
            }
            break;
        case Action::Edit:
            if (auto* it = current(); it && it->onEdit) it->onEdit();
            break;
        case Action::ChangeColor:
            if (auto* it = current(); it && it->onColor) {
                it->onColor();
                if (list_ && list_->refresh) list_->refresh();
            }
            break;
        case Action::Filter: {
            bool isLib = !list_;
            std::string initial = l->filter;
            openPrompt(tr("filter"), [](const std::string&) {}, initial);
            prompt_->onChange = [this, isLib](const std::string& s) {
                ListPanel* t = isLib ? (library_ ? &*library_ : nullptr) : (list_ ? &*list_ : nullptr);
                if (!t) return;
                t->filter = s;
                t->sel = 0;
                t->scroll = 0;
                filterList(*t);
            };
            break;
        }
        default: break;
    }
}

// ====================================================================== prompt
void App::openPrompt(const std::string& prefix, std::function<void(const std::string&)> onAccept,
                     const std::string& initial, std::vector<std::string>* history) {
    Prompt p;
    p.prefix = prefix;
    p.buf = initial;
    p.cursor = initial.size();
    p.onAccept = std::move(onAccept);
    p.history = history;
    prompt_ = std::move(p);
}

void App::handlePrompt(const std::string& key) {
    Prompt& p = *prompt_;
    auto changed = [&]() {
        p.completions.clear();
        p.compIdx = -1;
        if (p.onChange) p.onChange(p.buf);
    };
    if (key == "<Esc>" || key == "<C-c>") {
        if (p.onChange) p.onChange(p.prefix == tr("filter") ? std::string() : p.buf);
        prompt_.reset();
        return;
    }
    if (key == "<CR>") {
        auto f = std::move(p.onAccept);
        std::string s = p.buf;
        if (p.history && !s.empty()) {
            std::erase(*p.history, s);
            p.history->push_back(s);
        }
        prompt_.reset();
        if (f) f(s);
        return;
    }
    if (key == "<BS>") {
        if (p.cursor == 0) {
            if (p.buf.empty()) {
                if (p.onChange) p.onChange("");
                prompt_.reset();
            }
            return;
        }
        size_t i = p.cursor - 1;
        while (i > 0 && (static_cast<unsigned char>(p.buf[i]) & 0xC0) == 0x80) --i;
        p.buf.erase(i, p.cursor - i);
        p.cursor = i;
        changed();
        return;
    }
    if (key == "<C-u>") {
        p.buf.erase(0, p.cursor);
        p.cursor = 0;
        changed();
        return;
    }
    if (key == "<C-w>") {
        size_t i = p.cursor;
        while (i > 0 && p.buf[i - 1] == ' ') --i;
        while (i > 0 && p.buf[i - 1] != ' ') --i;
        p.buf.erase(i, p.cursor - i);
        p.cursor = i;
        changed();
        return;
    }
    if (key == "<Left>") {
        if (p.cursor > 0) {
            --p.cursor;
            while (p.cursor > 0 && (static_cast<unsigned char>(p.buf[p.cursor]) & 0xC0) == 0x80) --p.cursor;
        }
        return;
    }
    if (key == "<Right>") {
        if (p.cursor < p.buf.size()) utf8::decode(p.buf, p.cursor);
        return;
    }
    if (key == "<Home>" || key == "<C-a>") {
        p.cursor = 0;
        return;
    }
    if (key == "<End>" || key == "<C-e>") {
        p.cursor = p.buf.size();
        return;
    }
    if ((key == "<Up>" || key == "<Down>") && p.history && !p.history->empty()) {
        int n = static_cast<int>(p.history->size());
        if (p.histIdx < 0) p.histIdx = n;
        p.histIdx = std::clamp(p.histIdx + (key == "<Up>" ? -1 : 1), 0, n);
        p.buf = p.histIdx < n ? (*p.history)[static_cast<size_t>(p.histIdx)] : "";
        p.cursor = p.buf.size();
        return;
    }
    if (key == "<Tab>" || key == "<S-Tab>") {
        if (p.prefix != ":") return;
        if (p.completions.empty()) {
            p.compBase = p.buf;
            p.completions = complete(p.buf);
            p.compIdx = -1;
        }
        if (p.completions.empty()) return;
        int n = static_cast<int>(p.completions.size());
        p.compIdx = (p.compIdx + (key == "<Tab>" ? 1 : n - 1) + (p.compIdx < 0 && key != "<Tab>" ? 1 : 0)) % n;
        p.buf = p.completions[static_cast<size_t>(p.compIdx)];
        p.cursor = p.buf.size();
        return;
    }
    std::string ins;
    if (key == "<Space>") ins = " ";
    else if (key == "<lt>") ins = "<";
    else if (key.size() >= 1 && key[0] == '<' && key.size() > 1 && key.back() == '>') return;  // other special keys
    else ins = key;
    p.buf.insert(p.cursor, ins);
    p.cursor += ins.size();
    changed();
}

std::vector<std::string> App::complete(const std::string& buf) {
    std::vector<std::string> out;
    auto sp = buf.find(' ');
    if (sp == std::string::npos) {
        for (auto& c : kCommands)
            if (str::startsWith(c, buf)) out.push_back(c);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    std::string cmd = buf.substr(0, sp), arg = buf.substr(sp + 1);
    if (cmd == "theme") {
        for (auto& t : cfg_.themes)
            if (str::startsWith(t.name, arg)) out.push_back(cmd + " " + t.name);
    } else if (cmd == "set") {
        for (const char* o : {"width=", "justify!", "hyphenate!", "mode=paged", "mode=scroll", "columns=auto",
                              "columns=1", "columns=2", "paragraphs=auto", "paragraphs=spaced", "paragraphs=indented",
                              "linespacing=1", "linespacing=2", "images=auto", "images=kitty", "images=iterm",
                              "images=sixel", "images=blocks", "images=none", "header!", "status!", "bookstyles!",
                              "theme=", "lang=es", "lang=en"})
            if (str::startsWith(o, arg)) out.push_back(cmd + " " + o);
    } else if (cmd == "open" || cmd == "e") {
        std::string expanded = str::expandHome(arg);
        fs::path p(expanded.empty() ? "." : expanded);
        fs::path dir = expanded.empty() || expanded.back() == '/' ? p : p.parent_path();
        std::string prefix = expanded.empty() || expanded.back() == '/' ? "" : p.filename().string();
        if (dir.empty()) dir = ".";
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::string name = e.path().filename().string();
            if (!str::startsWith(name, prefix) || (name[0] == '.' && prefix.empty())) continue;
            bool isDir = e.is_directory();
            if (!isDir && str::lower(e.path().extension().string()) != ".epub") continue;
            std::string base = arg.substr(0, arg.size() - prefix.size());
            out.push_back(cmd + " " + base + name + (isDir && str::lower(e.path().extension().string()) != ".epub" ? "/" : ""));
        }
        std::sort(out.begin(), out.end());
    }
    return out;
}

// ====================================================================== commands
void App::showPopup(const std::string& title, const std::string& text, std::function<void()> onEnter,
                    const std::string& footer) {
    Popup p;
    p.title = title;
    p.text = text;
    p.onEnter = std::move(onEnter);
    p.footer = footer;
    popup_ = std::move(p);
}

void App::applySettings(bool relayout) {
    term_.applyTheme(cfg_.currentTheme());
    lastImageSig_.clear();
    term_.forceFullRedraw();
    if (reader_ && relayout) reader_->relayout();
    cfg_.savePrefs();
}

void App::setOption(const std::string& exprIn) {
    std::string expr(str::trim(exprIn));
    std::string name = expr, value;
    bool toggle = false, neg = false;
    if (auto eq = expr.find('='); eq != std::string::npos) {
        name = expr.substr(0, eq);
        value = expr.substr(eq + 1);
    } else if (!expr.empty() && expr.back() == '!') {
        name = expr.substr(0, expr.size() - 1);
        toggle = true;
    } else if (str::startsWith(expr, "no")) {
        name = expr.substr(2);
        neg = true;
    }
    auto boolOpt = [&](bool& b) {
        if (toggle) b = !b;
        else if (neg) b = false;
        else if (value.empty()) b = true;
        else b = value == "1" || value == "true" || value == "on" || value == "yes";
    };
    bool relayout = true;
    if (name == "width" || name == "w") cfg_.width = std::max(20, std::atoi(value.c_str()));
    else if (name == "justify") boolOpt(cfg_.justify);
    else if (name == "hyphenate" || name == "hyphens") boolOpt(cfg_.hyphenate);
    else if (name == "mode") cfg_.mode = value == "scroll" ? "scroll" : "paged";
    else if (name == "scroll") {
        bool s = cfg_.mode == "scroll";
        boolOpt(s);
        cfg_.mode = s ? "scroll" : "paged";
    } else if (name == "columns" || name == "cols") cfg_.columns = value.empty() ? "auto" : value;
    else if (name == "paragraphs") cfg_.paragraphs = value == "spaced" ? ParagraphStyle::Spaced
                                                     : value == "indented" ? ParagraphStyle::Indented
                                                                           : ParagraphStyle::Auto;
    else if (name == "linespacing" || name == "line_spacing") cfg_.lineSpacing = std::clamp(std::atoi(value.c_str()), 1, 2);
    else if (name == "header") boolOpt(cfg_.showHeader);
    else if (name == "status") boolOpt(cfg_.showStatus);
    else if (name == "hlsearch") {
        boolOpt(hlsearch_);
        relayout = false;
    } else if (name == "theme") {
        if (cfg_.themeIndex(value) < 0) {
            message("theme? " + value, true);
            return;
        }
        cfg_.theme = value;
        relayout = false;
    } else if (name == "lang" || name == "language") {
        cfg_.language = value;
        setLanguage(value);
        relayout = false;
    } else if (name == "images") {
        cfg_.images = value;
        proto_ = img::parseProtocol(value);
        if (proto_ == img::Protocol::Kitty || !lastImageSig_.empty()) term_.writeRaw(img::kittyClear());
        if (reader_) reader_->setProtocol(proto_);
    } else if (name == "bookstyles") {
        boolOpt(cfg_.bookStyles);
        cfg_.savePrefs();
        if (book_) {
            saveState();
            openBook(state_.path);
        }
        return;
    } else {
        message(std::string(tr("unknown_command")) + ": set " + name, true);
        return;
    }
    applySettings(relayout);
    message(expr);
}

void App::runCommand(const std::string& raw) {
    std::string c(str::trim(raw));
    if (c.empty()) return;
    std::string cmd = c, arg;
    if (auto sp = c.find(' '); sp != std::string::npos) {
        cmd = c.substr(0, sp);
        arg = std::string(str::trim(c.substr(sp + 1)));
    }
    bool inReader = view_ == View::Reader && reader_;
    // ":123" → page 123 (vim: line number)
    if (!cmd.empty() && std::all_of(cmd.begin(), cmd.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); })) {
        if (inReader) {
            back_.push_back(reader_->pos());
            reader_->gotoGlobalPage(std::atoi(cmd.c_str()));
        }
        return;
    }
    if (cmd == "q" || cmd == "quit" || cmd == "q!" || cmd == "wq" || cmd == "x" || cmd == "qa" || cmd == "qa!") {
        running_ = false;
    } else if (cmd == "w" || cmd == "write") {
        saveState();
        cfg_.savePrefs();
        message(tr("settings_saved"));
    } else if (cmd == "open" || cmd == "e" || cmd == "edit") {
        if (arg.empty()) {
            message(":open <file.epub>", true);
            return;
        }
        if (!openBook(arg)) return;
    } else if (cmd == "library" || cmd == "lib") {
        saveState();
        showLibrary();
    } else if (cmd == "help" || cmd == "h") {
        showHelp();
    } else if (cmd == "themes") {
        showThemes();
    } else if (cmd == "theme" || cmd == "colorscheme" || cmd == "colo") {
        if (arg.empty()) showThemes();
        else setOption("theme=" + arg);
    } else if (cmd == "set" || cmd == "se") {
        for (auto& part : str::splitWs(arg)) setOption(part);
    } else if (cmd == "reset") {
        std::error_code ec;
        fs::remove(platform::dataDir() / "prefs.toml", ec);
        Config fresh;
        fresh.load(Config::defaultPath());
        cfg_.theme = fresh.theme;
        cfg_.width = fresh.width;
        cfg_.justify = fresh.justify;
        cfg_.hyphenate = fresh.hyphenate;
        cfg_.paragraphs = fresh.paragraphs;
        cfg_.lineSpacing = fresh.lineSpacing;
        cfg_.mode = fresh.mode;
        cfg_.columns = fresh.columns;
        cfg_.showHeader = fresh.showHeader;
        cfg_.showStatus = fresh.showStatus;
        term_.applyTheme(cfg_.currentTheme());
        if (reader_) reader_->relayout();
        term_.forceFullRedraw();
    } else if (cmd == "redraw" || cmd == "redr") {
        term_.forceFullRedraw();
        lastImageSig_.clear();
    } else if (!inReader) {
        message(std::string(tr("unknown_command")) + ": " + cmd, true);
    } else if (cmd == "toc" || cmd == "contents") {
        showToc();
    } else if (cmd == "bookmarks" || cmd == "marks") {
        showBookmarks();
    } else if (cmd == "notes" || cmd == "annotations" || cmd == "highlights") {
        showAnnotations();
    } else if (cmd == "info") {
        showInfo();
    } else if (cmd == "bookmark" || cmd == "mark") {
        toggleBookmark();
    } else if (cmd == "goto" || cmd == "go") {
        if (arg.empty()) return;
        back_.push_back(reader_->pos());
        if (arg.back() == '%') reader_->gotoFraction(std::atof(arg.c_str()) / 100.0);
        else reader_->gotoGlobalPage(std::atoi(arg.c_str()));
        dirty_ = true;
    } else if (cmd == "first") {
        jumpTo({0, 0});
    } else if (cmd == "last") {
        back_.push_back(reader_->pos());
        reader_->last();
    } else if (cmd == "back") {
        performReader(Action::Back, 0);
    } else if (cmd == "forward") {
        performReader(Action::Forward, 0);
    } else if (cmd == "page") {
        if (book_->pageList().empty()) {
            message(tr("no_page_list"), true);
            return;
        }
        for (auto& pt : book_->pageList())
            if (str::iequals(pt.label, arg)) {
                jumpTo(pt.pos);
                return;
            }
        message(std::string(tr("pattern_not_found")) + ": " + arg, true);
    } else if (cmd == "chapter" || cmd == "ch") {
        int n = std::atoi(arg.c_str());
        std::vector<const TocEntry*> top;
        for (auto& e : book_->toc())
            if (e.depth == 0 && e.resolved) top.push_back(&e);
        if (n >= 1 && n <= static_cast<int>(top.size())) jumpTo(top[static_cast<size_t>(n - 1)]->pos);
        else if (n >= 1 && n <= static_cast<int>(book_->chapters().size())) jumpTo({n - 1, 0});
    } else if (cmd == "search" || cmd == "s" || cmd == "grep") {
        showSearchResults(arg.empty() ? searchPattern_ : arg);
    } else if (cmd == "nohlsearch" || cmd == "noh") {
        hlsearch_ = false;
    } else if (cmd == "export") {
        exportNotes(arg);
    } else if (cmd == "speak") {
        speak(reader_->visibleText());
    } else if (cmd == "stop") {
        if (ttsPid_ > 0) platform::killProcess(ttsPid_);
        ttsPid_ = -1;
        message(tr("speech_stopped"));
    } else if (cmd == "define" || cmd == "dict") {
        lookUp(arg);
    } else if (cmd == "reload") {
        saveState();
        openBook(state_.path);
    } else if (cmd == "yank") {
        copyText(reader_->visibleText());
    } else {
        message(std::string(tr("unknown_command")) + ": " + cmd, true);
    }
}

// ====================================================================== misc features
void App::copyText(const std::string& text) {
    if (text.empty()) return;
    bool ok = platform::copyToClipboard(text);
    if (cfg_.osc52) {
        term_.writeRaw(platform::osc52(text));
        ok = true;
    }
    message(ok ? tr("copied") : "clipboard?", !ok);
}

void App::lookUp(const std::string& wordIn) {
    std::string word = str::collapseWs(wordIn);
    if (word.empty()) return;
    if (!cfg_.dictCommand.empty()) {
        std::string cmd = str::replaceAll(cfg_.dictCommand, "%s", platform::shellQuote(word));
        auto out = platform::captureCommand(cmd);
        if (out && !str::trim(*out).empty()) {
            showPopup(std::string(tr("definition")) + ": " + word, *out);
            return;
        }
    }
    if (platform::isMac()) {
        // Same behaviour as Books' "Look Up": the system Dictionary app.
        platform::openExternal("dict://" + word);
        return;
    }
    message(tr("no_dict"), true);
}

void App::speak(const std::string& text) {
    if (cfg_.ttsCommand.empty()) {
        message(tr("no_tts"), true);
        return;
    }
    if (ttsPid_ > 0) platform::killProcess(ttsPid_);
    ttsPid_ = platform::spawnWithInput(cfg_.ttsCommand, text);
    message(tr("speaking"));
}

void App::exportNotes(const std::string& pathArg) {
    std::string title = book_->meta().title;
    std::string safe;
    for (char c : title) safe += (c == '/' || c == ':' || c == '\\') ? '-' : c;
    fs::path p = pathArg.empty() ? fs::current_path() / (safe + (currentLanguage() == "es" ? " - notas.md" : " - notes.md"))
                                 : fs::path(str::expandHome(pathArg));
    std::ostringstream ss;
    ss << "# " << title << "\n\n";
    if (!book_->meta().authors.empty()) ss << "*" << book_->meta().authorLine() << "*\n\n";
    std::string lastSection;
    for (auto& h : state_.highlights) {
        std::string sec = book_->sectionTitle({h.chapter, h.start});
        if (sec != lastSection) {
            ss << "## " << sec << "\n\n";
            lastSection = sec;
        }
        ss << "> " << h.text << "\n\n";
        if (!h.note.empty()) ss << h.note << "\n\n";
        ss << "<sub>" << tr(kColorKeys[std::clamp(h.style, 0, 5)]) << " · " << tr("page") << " "
           << reader_->pageAt({h.chapter, h.start}) << " · " << str::formatTime(h.created) << "</sub>\n\n";
    }
    if (!state_.bookmarks.empty()) {
        ss << "## " << tr("bookmarks") << "\n\n";
        for (auto& b : state_.bookmarks) ss << "- " << b.label << " (" << tr("page") << " " << reader_->pageAt(b.pos) << ")\n";
    }
    if (platform::writeFileAtomic(p, ss.str())) message(std::string(tr("exported")) + " " + p.string());
    else message("export: " + p.string(), true);
}

}  // namespace clipub
