#include "layout/layout.hpp"

#include <algorithm>
#include <cmath>

#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace clibooks {
namespace {

struct Piece {
    std::string text;
    uint16_t attrs = 0;
    int link = -1;
    size_t off = 0;
    size_t srcLen = 0;
};

struct Token {
    std::vector<Piece> pieces;
    int width = 0;
    bool spaceBefore = false;
    bool softHyphen = false;  // a line break after this token shows "-"
    bool forcedBreak = false;
    size_t spaceOff = 0;  // source offset of the preceding space
};

char32_t superscript(char32_t c) {
    switch (c) {
        case '0': return 0x2070;
        case '1': return 0x00B9;
        case '2': return 0x00B2;
        case '3': return 0x00B3;
        case '4': return 0x2074;
        case '5': return 0x2075;
        case '6': return 0x2076;
        case '7': return 0x2077;
        case '8': return 0x2078;
        case '9': return 0x2079;
        case '+': return 0x207A;
        case '-': return 0x207B;
        case '=': return 0x207C;
        case '(': return 0x207D;
        case ')': return 0x207E;
        case 'n': return 0x207F;
        case 'i': return 0x2071;
        default: return 0;
    }
}

char32_t subscript(char32_t c) {
    if (c >= '0' && c <= '9') return 0x2080 + (c - '0');
    switch (c) {
        case '+': return 0x208A;
        case '-': return 0x208B;
        case '=': return 0x208C;
        case '(': return 0x208D;
        case ')': return 0x208E;
        default: return 0;
    }
}

char32_t upperSimple(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    return c;
}

std::string displayTransform(const std::string& text, uint16_t attrs) {
    if (attrs & (kSup | kSub)) {
        std::string out;
        for (size_t i = 0; i < text.size();) {
            char32_t c = utf8::decode(text, i);
            char32_t m = (attrs & kSup) ? superscript(c) : subscript(c);
            if (!m) return text;  // not fully mappable: keep the original
            utf8::append(out, m);
        }
        return out;
    }
    if (attrs & kSmallCaps) {
        std::string out;
        for (size_t i = 0; i < text.size();) utf8::append(out, upperSimple(utf8::decode(text, i)));
        return out;
    }
    return text;
}

int tokenWidth(const Token& t) {
    int w = 0;
    for (auto& p : t.pieces) w += utf8::width(p.text);
    return w;
}


std::vector<Token> tokenize(const Block& b) {
    std::vector<Token> toks;
    Token cur;
    bool pendingSpace = false;
    size_t pendingSpaceOff = 0;
    size_t off = b.offset;
    auto finish = [&]() {
        if (!cur.pieces.empty()) {
            for (auto& p : cur.pieces) {
                std::string d = displayTransform(p.text, p.attrs);
                p.text = std::move(d);
            }
            cur.width = tokenWidth(cur);
            toks.push_back(std::move(cur));
        }
        cur = Token{};
    };
    auto startIfNeeded = [&]() {
        if (cur.pieces.empty()) {
            cur.spaceBefore = pendingSpace;
            cur.spaceOff = pendingSpaceOff;
            pendingSpace = false;
        }
    };
    auto addChar = [&](const std::string& bytes, const Run& r, size_t srcOff) {
        startIfNeeded();
        if (cur.pieces.empty() || cur.pieces.back().attrs != r.attrs || cur.pieces.back().link != r.link ||
            cur.pieces.back().off + cur.pieces.back().srcLen != srcOff) {
            cur.pieces.push_back(Piece{"", r.attrs, r.link, srcOff, 0});
        }
        cur.pieces.back().text += bytes;
        cur.pieces.back().srcLen += bytes.size();
    };
    for (const auto& r : b.runs) {
        const std::string& s = r.text;
        for (size_t i = 0; i < s.size();) {
            size_t st = i;
            char32_t cp = utf8::decode(s, i);
            size_t srcOff = off + st;
            if (cp == '\n') {
                finish();
                Token br;
                br.forcedBreak = true;
                toks.push_back(br);
                pendingSpace = false;
                continue;
            }
            if (cp == ' ' || cp == '\t' || (utf8::isSpace(cp) && cp != 0xA0)) {
                finish();
                pendingSpace = true;
                pendingSpaceOff = srcOff;
                continue;
            }
            if (cp == 0xAD) {  // soft hyphen: break opportunity
                if (!cur.pieces.empty()) {
                    cur.softHyphen = true;
                    finish();
                }
                continue;
            }
            if (utf8::isCJK(cp)) {
                finish();
                addChar(s.substr(st, i - st), r, srcOff);
                finish();
                continue;
            }
            std::string bytes = (cp == 0xA0) ? std::string(" ") : s.substr(st, i - st);
            // NBSP keeps the word together; it's displayed as a space but has 1 byte vs 2.
            if (cp == 0xA0) {
                startIfNeeded();
                cur.pieces.push_back(Piece{" ", r.attrs, r.link, srcOff, 2});
                continue;
            }
            addChar(bytes, r, srcOff);
            bool breakAfter = (cp == '-' || cp == 0x2014 || cp == 0x2013 || cp == '/') && tokenWidth(cur) > 1;
            if (breakAfter) {
                // Only break after a dash if more letters follow (avoid orphaned dashes).
                if (i < s.size() && !utf8::isSpace(static_cast<unsigned char>(s[i]))) finish();
            }
        }
        off += s.size();
    }
    finish();
    return toks;
}

struct Placed {
    const Token* tok;
    std::vector<Piece> pieces;  // possibly a hyphenated prefix
    int width;
    bool space;
    size_t spaceOff;
    bool hyphenEnd = false;
};

class BlockLayouter {
public:
    BlockLayouter(const Block& b, int blockIdx, const LayoutOptions& opt, std::vector<Line>& out)
        : b_(b), idx_(blockIdx), opt_(opt), out_(out) {}

    void layoutText(bool firstIndent) {
        auto toks = tokenize(b_);
        int baseIndent = std::min(b_.indent, std::max(0, opt_.width - 20));
        int markerW = b_.marker.empty() ? 0 : utf8::width(b_.marker) + 1;
        textIndent_ = baseIndent + markerW;
        avail_ = std::max(10, opt_.width - textIndent_);
        firstLineExtra_ = firstIndent ? 2 : 0;
        align_ = b_.alignExplicit ? b_.align : (opt_.justify ? css::Align::Justify : css::Align::Left);
        if (b_.kind == BlockKind::Heading && !b_.alignExplicit) align_ = css::Align::Left;
        firstLine_ = true;
        size_t startLines = out_.size();

        for (size_t ti = 0; ti < toks.size(); ++ti) {
            const Token& t = toks[ti];
            if (t.forcedBreak) {
                emit(true);
                continue;
            }
            Placed p{&t, t.pieces, t.width, t.spaceBefore && !cur_.empty(), t.spaceOff};
            int need = (p.space ? 1 : 0) + p.width;
            if (curWidth_ + need <= lineAvail()) {
                push(std::move(p));
                continue;
            }
            // Doesn't fit: try hyphenation.
            Token rest = t;
            bool placedAll = false;
            while (true) {
                int room = lineAvail() - curWidth_ - (cur_.empty() ? 0 : (rest.spaceBefore ? 1 : 0));
                if (tryHyphenate(rest, room)) {
                    emit(false);
                    rest.spaceBefore = false;
                    if (rest.width <= lineAvail()) {
                        push(Placed{&t, rest.pieces, rest.width, false, 0, false});
                        placedAll = true;
                        break;
                    }
                    continue;
                }
                break;
            }
            if (placedAll) continue;
            if (!cur_.empty()) emit(false);
            // Still too long for an empty line: hard-split by characters.
            while (rest.width > lineAvail()) {
                Token head, tail;
                splitAtWidth(rest, lineAvail(), head, tail);
                if (head.pieces.empty()) break;
                push(Placed{&t, head.pieces, head.width, false, 0, false});
                emit(false);
                rest = tail;
            }
            if (!rest.pieces.empty()) push(Placed{&t, rest.pieces, rest.width, false, 0, false});
        }
        if (!cur_.empty()) emit(true);
        finalize(startLines);
    }

    void layoutPre() {
        size_t startLines = out_.size();
        int indent = std::min(b_.indent, std::max(0, opt_.width - 20));
        int avail = std::max(10, opt_.width - indent);
        // Rebuild per physical line with char-level wrapping.
        std::vector<Seg> segs;
        int col = 0;
        size_t lineOff = b_.offset;
        auto flushLine = [&](size_t endOff) {
            Line l;
            l.kind = LineKind::Text;
            l.block = idx_;
            l.segs = std::move(segs);
            l.off = lineOff;
            l.end = endOff;
            out_.push_back(std::move(l));
            segs.clear();
            col = 0;
        };
        size_t off = b_.offset;
        for (auto& r : b_.runs) {
            for (size_t i = 0; i < r.text.size();) {
                size_t st = i;
                char32_t cp = utf8::decode(r.text, i);
                size_t so = off + st;
                if (cp == '\n') {
                    flushLine(so);
                    lineOff = so + 1;
                    continue;
                }
                int w = utf8::width(cp);
                if (col + w > avail) {
                    flushLine(so);
                    lineOff = so;
                }
                if (segs.empty() || segs.back().attrs != r.attrs || segs.back().link != r.link ||
                    segs.back().off + segs.back().srcLen != so) {
                    Seg s;
                    s.col = indent + col;
                    s.attrs = r.attrs;
                    s.link = r.link;
                    s.off = so;
                    segs.push_back(s);
                }
                segs.back().text += r.text.substr(st, i - st);
                segs.back().srcLen += i - st;
                col += w;
            }
            off += r.text.size();
        }
        flushLine(off);
        finalize(startLines);
    }

private:
    int lineAvail() const { return avail_ - (firstLine_ ? firstLineExtra_ : 0); }

    void push(Placed p) {
        curWidth_ += (p.space ? 1 : 0) + p.width;
        cur_.push_back(std::move(p));
    }

    static void splitAtWidth(const Token& t, int maxW, Token& head, Token& tail) {
        head = Token{};
        tail = Token{};
        int w = 0;
        bool inTail = false;
        for (auto& p : t.pieces) {
            for (size_t i = 0; i < p.text.size();) {
                size_t st = i;
                char32_t cp = utf8::decode(p.text, i);
                int cw = utf8::width(cp);
                if (!inTail && w + cw > maxW) inTail = true;
                Token& dst = inTail ? tail : head;
                bool sameLen = p.text.size() == p.srcLen;
                size_t so = sameLen ? p.off + st : p.off;
                if (dst.pieces.empty() || dst.pieces.back().attrs != p.attrs || dst.pieces.back().link != p.link)
                    dst.pieces.push_back(Piece{"", p.attrs, p.link, so, 0});
                dst.pieces.back().text += p.text.substr(st, i - st);
                dst.pieces.back().srcLen += sameLen ? (i - st) : 0;
                if (!inTail) w += cw;
            }
            if (p.text.size() != p.srcLen) {
                // Attribute the whole source span to the head piece.
                if (!head.pieces.empty()) head.pieces.back().srcLen = p.srcLen;
            }
        }
        head.width = tokenWidth(head);
        tail.width = tokenWidth(tail);
        tail.softHyphen = t.softHyphen;
    }

    bool tryHyphenate(Token& t, int room) {
        if (!opt_.hyphenate || !opt_.hyphenator || room < 3 || t.pieces.size() != 1) return false;
        const Piece& p = t.pieces[0];
        if (p.text.size() != p.srcLen) return false;
        // Strip trailing punctuation from the candidate word.
        std::string word = p.text;
        size_t wend = word.size();
        while (wend > 0) {
            size_t k = wend - 1;
            while (k > 0 && (static_cast<unsigned char>(word[k]) & 0xC0) == 0x80) --k;
            size_t tmp = k;
            if (utf8::isWordChar(utf8::decode(word, tmp))) break;
            wend = k;
        }
        size_t wstart = 0;
        while (wstart < wend) {
            size_t tmp = wstart;
            char32_t c = utf8::decode(word, tmp);
            if (utf8::isWordChar(c)) break;
            wstart = tmp;
        }
        if (wend <= wstart) return false;
        auto pts = opt_.hyphenator->points(std::string_view(word).substr(wstart, wend - wstart));
        size_t best = 0;
        for (size_t pt : pts) {
            size_t cut = wstart + pt;
            if (utf8::width(std::string_view(word).substr(0, cut)) + 1 <= room) best = cut;
        }
        if (!best) return false;
        Piece head{word.substr(0, best), p.attrs, p.link, p.off, best};
        Piece tail{word.substr(best), p.attrs, p.link, p.off + best, word.size() - best};
        Placed pl{&t, {head}, utf8::width(head.text), t.spaceBefore && !cur_.empty(), t.spaceOff, true};
        push(std::move(pl));
        t.pieces = {tail};
        t.width = utf8::width(tail.text);
        return true;
    }

    void emit(bool lastOfParagraph) {
        Line l;
        l.kind = LineKind::Text;
        l.block = idx_;
        int startCol = textIndent_ + (firstLine_ ? firstLineExtra_ : 0);
        int avail = lineAvail();
        int extra = std::max(0, avail - curWidth_);
        int gaps = 0;
        for (size_t i = 1; i < cur_.size(); ++i)
            if (cur_[i].space) ++gaps;
        bool hyphenAtEnd = !cur_.empty() && (cur_.back().hyphenEnd || (cur_.back().tok->softHyphen && !lastOfParagraph));
        if (hyphenAtEnd) extra = std::max(0, extra - 1);
        int shift = 0;
        std::vector<int> gapExtra(cur_.size(), 0);
        if (align_ == css::Align::Center) shift = extra / 2;
        else if (align_ == css::Align::Right) shift = extra;
        else if (align_ == css::Align::Justify && !lastOfParagraph && gaps > 0 && extra <= gaps * 4) {
            int base = extra / gaps, rem = extra % gaps;
            int g = 0;
            for (size_t i = 1; i < cur_.size(); ++i) {
                if (!cur_[i].space) continue;
                // Spread the remainder alternating from both ends for a more even look.
                bool more = (lineCount_ % 2 == 0) ? g < rem : g >= gaps - rem;
                gapExtra[i] = base + (more ? 1 : 0);
                ++g;
            }
        }
        int col = startCol + shift;
        if (firstLine_ && !b_.marker.empty()) {
            Seg m;
            m.col = std::min(b_.indent, std::max(0, opt_.width - 20));
            m.text = b_.marker;
            m.decor = true;
            m.off = b_.offset;
            l.segs.push_back(m);
        }
        bool first = true;
        for (size_t i = 0; i < cur_.size(); ++i) {
            auto& p = cur_[i];
            if (p.space) {
                int w = 1 + gapExtra[i];
                Seg s;
                s.col = col;
                s.text = std::string(static_cast<size_t>(w), ' ');
                // Spaces inherit attributes shared by both neighbours (continuous underline/links).
                uint16_t prevA = l.segs.empty() ? 0 : l.segs.back().attrs;
                uint16_t nextA = p.pieces.empty() ? 0 : p.pieces.front().attrs;
                int prevL = l.segs.empty() ? -1 : l.segs.back().link;
                int nextL = p.pieces.empty() ? -1 : p.pieces.front().link;
                s.attrs = prevA & nextA & (kUnderline | kStrike | kLink | kCode);
                s.link = prevL == nextL ? prevL : -1;
                if (s.link < 0) s.attrs &= ~kLink;
                s.off = p.spaceOff;
                s.srcLen = 1;
                l.segs.push_back(s);
                col += w;
            }
            for (auto& pc : p.pieces) {
                Seg s;
                s.col = col;
                s.text = pc.text;
                s.attrs = pc.attrs;
                s.link = pc.link;
                s.off = pc.off;
                s.srcLen = pc.srcLen;
                if (first) {
                    l.off = pc.off;
                    first = false;
                }
                l.end = pc.off + pc.srcLen;
                col += utf8::width(pc.text);
                l.segs.push_back(std::move(s));
            }
        }
        if (hyphenAtEnd) {
            Seg h;
            h.col = col;
            h.text = "-";
            h.decor = true;
            h.off = l.end;
            l.segs.push_back(h);
        }
        if (first) {
            l.off = lastEnd_;
            l.end = lastEnd_;
        }
        lastEnd_ = l.end;
        out_.push_back(std::move(l));
        cur_.clear();
        curWidth_ = 0;
        firstLine_ = false;
        ++lineCount_;
    }

    void finalize(size_t startLines) {
        int n = static_cast<int>(out_.size() - startLines);
        for (int i = 0; i < n; ++i) {
            auto& l = out_[startLines + static_cast<size_t>(i)];
            l.paraLine = i;
            l.paraLines = n;
            if (l.off < b_.offset) l.off = b_.offset;
            if (b_.kind == BlockKind::Heading) l.keepWithNext = true;
        }
        if (n > 0 && b_.pageBreakBefore) out_[startLines].breakBefore = true;
        // Quote bars.
        if (b_.quoteDepth > 0) {
            for (int i = 0; i < n; ++i) {
                auto& l = out_[startLines + static_cast<size_t>(i)];
                for (int d = 0; d < b_.quoteDepth; ++d) {
                    Seg bar;
                    bar.col = std::max(0, b_.indent - 2 * (b_.quoteDepth - d));
                    bar.text = "│";
                    bar.decor = true;
                    bar.attrs = kDim;
                    bar.off = l.off;
                    l.segs.insert(l.segs.begin(), bar);
                }
            }
        }
    }

    const Block& b_;
    int idx_;
    const LayoutOptions& opt_;
    std::vector<Line>& out_;
    std::vector<Placed> cur_;
    int curWidth_ = 0;
    int textIndent_ = 0;
    int avail_ = 0;
    int firstLineExtra_ = 0;
    css::Align align_ = css::Align::Left;
    bool firstLine_ = true;
    int lineCount_ = 0;
    size_t lastEnd_ = 0;
};

// Simple word wrap for table cells.
std::vector<std::string> wrapPlain(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::string cur;
    int curW = 0;
    for (auto& w : str::splitWs(text)) {
        int ww = utf8::width(w);
        std::string word = w;
        while (ww > width) {
            std::string head = utf8::truncate(word, width, false);
            if (!cur.empty()) {
                lines.push_back(cur);
                cur.clear();
                curW = 0;
            }
            lines.push_back(head);
            word = word.substr(head.size());
            ww = utf8::width(word);
        }
        if (curW + (cur.empty() ? 0 : 1) + ww > width) {
            lines.push_back(cur);
            cur.clear();
            curW = 0;
        }
        if (!cur.empty()) {
            cur += ' ';
            ++curW;
        }
        cur += word;
        curW += ww;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

void layoutTable(const Block& b, int idx, const LayoutOptions& opt, std::vector<Line>& out) {
    const auto& t = *b.table;
    size_t ncols = 0;
    for (auto& r : t.rows) ncols = std::max(ncols, r.size());
    int indent = std::min(b.indent, 8);
    int avail = opt.width - indent;
    auto makeLine = [&](std::vector<Seg> segs) {
        Line l;
        l.kind = LineKind::Text;
        l.block = idx;
        l.off = b.offset;
        l.end = b.offset + b.length;
        l.segs = std::move(segs);
        out.push_back(std::move(l));
    };
    auto decorSeg = [&](int col, std::string s) {
        Seg sg;
        sg.col = col;
        sg.text = std::move(s);
        sg.decor = true;
        sg.attrs = kDim;
        sg.off = b.offset;
        return sg;
    };
    std::vector<int> natural(ncols, 1), minW(ncols, 1);
    for (auto& r : t.rows)
        for (size_t c = 0; c < r.size(); ++c) {
            natural[c] = std::max(natural[c], utf8::width(r[c]));
            for (auto& w : str::splitWs(r[c])) minW[c] = std::max(minW[c], std::min(utf8::width(w), 12));
        }
    int borders = static_cast<int>(ncols) * 3 + 1;
    int totalNatural = borders;
    for (int w : natural) totalNatural += w;
    int totalMin = borders;
    for (int w : minW) totalMin += std::min(w, 6);
    std::vector<int> widths = natural;
    if (totalNatural > avail) {
        if (totalMin > avail || ncols > 8) {
            // Too narrow for a grid: stacked "header: value" rows.
            const std::vector<std::string>* header = (!t.header.empty() && t.header[0]) ? &t.rows[0] : nullptr;
            for (size_t ri = header ? 1 : 0; ri < t.rows.size(); ++ri) {
                const auto& r = t.rows[ri];
                for (size_t c = 0; c < r.size(); ++c) {
                    std::string label = header && c < header->size() ? (*header)[c] + ": " : "";
                    auto wrapped = wrapPlain(label + r[c], avail - 2);
                    for (size_t k = 0; k < wrapped.size(); ++k) {
                        std::vector<Seg> segs;
                        if (k == 0 && c == 0) segs.push_back(decorSeg(indent, "▪"));
                        Seg s;
                        s.col = indent + 2;
                        s.text = wrapped[k];
                        s.off = b.offset;
                        s.srcLen = 0;
                        s.attrs = (k == 0 && !label.empty()) ? 0 : 0;
                        segs.push_back(s);
                        makeLine(std::move(segs));
                    }
                }
                makeLine({});
                out.back().kind = LineKind::Blank;
            }
            return;
        }
        // Shrink the widest columns until it fits.
        int excess = totalNatural - avail;
        while (excess > 0) {
            auto it = std::max_element(widths.begin(), widths.end());
            size_t c = static_cast<size_t>(std::distance(widths.begin(), it));
            if (*it <= std::max(minW[c], 4)) {
                // Fall back to proportional squeeze.
                bool changed = false;
                for (size_t k = 0; k < widths.size() && excess > 0; ++k)
                    if (widths[k] > 3) {
                        --widths[k];
                        --excess;
                        changed = true;
                    }
                if (!changed) break;
                continue;
            }
            --*it;
            --excess;
        }
    }
    auto border = [&](const char* l, const char* m, const char* r) {
        std::string s = l;
        for (size_t c = 0; c < ncols; ++c) {
            for (int k = 0; k < widths[c] + 2; ++k) s += "─";
            s += (c + 1 < ncols) ? m : r;
        }
        return s;
    };
    makeLine({decorSeg(indent, border("┌", "┬", "┐"))});
    for (size_t ri = 0; ri < t.rows.size(); ++ri) {
        const auto& r = t.rows[ri];
        std::vector<std::vector<std::string>> cells(ncols);
        size_t h = 1;
        for (size_t c = 0; c < ncols; ++c) {
            cells[c] = wrapPlain(c < r.size() ? r[c] : "", widths[c]);
            h = std::max(h, cells[c].size());
        }
        bool header = ri < t.header.size() && t.header[ri];
        for (size_t k = 0; k < h; ++k) {
            std::vector<Seg> segs;
            int col = indent;
            for (size_t c = 0; c < ncols; ++c) {
                segs.push_back(decorSeg(col, "│"));
                col += 2;
                Seg s;
                s.col = col;
                s.text = k < cells[c].size() ? cells[c][k] : "";
                s.attrs = header ? kBold : 0;
                s.off = b.offset;
                s.srcLen = 0;
                s.decor = false;
                if (!s.text.empty()) segs.push_back(s);
                col += widths[c] + 1;
            }
            segs.push_back(decorSeg(col, "│"));
            makeLine(std::move(segs));
        }
        if (header && ri + 1 < t.rows.size()) makeLine({decorSeg(indent, border("├", "┼", "┤"))});
    }
    makeLine({decorSeg(indent, border("└", "┴", "┘"))});
}

}  // namespace

std::pair<int, int> imageCells(int pw, int ph, int maxCols, int maxRows, double aspect, int cellPixelW) {
    if (maxCols < 1) maxCols = 1;
    if (maxRows < 1) maxRows = 1;
    if (pw <= 0 || ph <= 0) return {std::min(maxCols, 30), std::min(maxRows, 10)};
    int cpw = std::max(cellPixelW, 1);
    // Natural size in cells (never upscale tiny images beyond ~2x).
    double cols = std::min<double>(maxCols, std::max(1.0, std::ceil(pw / static_cast<double>(cpw))));
    double rows = cols * (static_cast<double>(ph) / pw) / aspect;
    if (rows > maxRows) {
        rows = maxRows;
        cols = rows * aspect * pw / static_cast<double>(ph);
    }
    int c = std::clamp(static_cast<int>(std::lround(cols)), 1, maxCols);
    int r = std::clamp(static_cast<int>(std::lround(rows)), 1, maxRows);
    return {c, r};
}

ChapterLayout layoutChapter(const Chapter& ch, const LayoutOptions& opt, const ImageSizer& sizer) {
    ChapterLayout cl;
    auto& out = cl.lines;
    BlockKind prevKind = BlockKind::Text;
    bool prevIndentedPara = false;
    for (size_t bi = 0; bi < ch.blocks.size(); ++bi) {
        const Block& b = ch.blocks[bi];
        int idx = static_cast<int>(bi);
        // Vertical spacing before the block.
        bool indentStyle = false;
        if (b.kind == BlockKind::Text) {
            if (opt.paragraphStyle == ParagraphStyle::Indented) indentStyle = b.marker.empty() && b.align != css::Align::Center;
            else if (opt.paragraphStyle == ParagraphStyle::Auto) indentStyle = b.firstIndent;
        }
        int blanks = 1;
        if (b.kind == BlockKind::Heading) blanks = out.empty() ? 0 : 2;
        else if (indentStyle && prevKind == BlockKind::Text && !out.empty()) blanks = 0;
        else if (b.kind == BlockKind::Text && !b.marker.empty() && prevKind == BlockKind::Text &&
                 !ch.blocks[bi - 1].marker.empty())
            blanks = 0;  // consecutive list items stay compact
        if (out.empty()) blanks = 0;
        if (prevKind == BlockKind::Heading && blanks > 1) blanks = 1;
        for (int k = 0; k < blanks; ++k) {
            Line l;
            l.kind = LineKind::Blank;
            l.off = b.offset;
            l.end = b.offset;
            l.block = idx;
            out.push_back(l);
        }
        size_t start = out.size();
        switch (b.kind) {
            case BlockKind::Text:
            case BlockKind::Heading: {
                BlockLayouter bl(b, idx, opt, out);
                bool fi = indentStyle && !(prevKind == BlockKind::Heading) && prevIndentedPara;
                if (opt.paragraphStyle == ParagraphStyle::Indented) fi = indentStyle && prevKind == BlockKind::Text;
                if (opt.paragraphStyle == ParagraphStyle::Auto) fi = indentStyle;
                bl.layoutText(fi);
                break;
            }
            case BlockKind::Pre: {
                BlockLayouter bl(b, idx, opt, out);
                bl.layoutPre();
                break;
            }
            case BlockKind::Rule: {
                Line l;
                l.kind = LineKind::Rule;
                l.block = idx;
                l.off = b.offset;
                l.end = b.offset;
                int w = std::min(24, opt.width / 3);
                Seg s;
                s.col = (opt.width - w) / 2;
                for (int i = 0; i < w; ++i) s.text += "─";
                s.decor = true;
                s.attrs = kDim;
                s.off = b.offset;
                l.segs.push_back(s);
                out.push_back(l);
                break;
            }
            case BlockKind::Table:
                layoutTable(b, idx, opt, out);
                break;
            case BlockKind::Image: {
                auto [pw, ph] = sizer ? sizer(b.image) : std::pair<int, int>{0, 0};
                if (!opt.showImages || pw < 0) {
                    Line l;
                    l.kind = LineKind::Text;
                    l.block = idx;
                    l.off = b.offset;
                    l.end = b.offset;
                    Seg s;
                    s.text = "[▣ " + (b.alt.empty() ? std::string("image") : b.alt) + "]";
                    s.text = utf8::truncate(s.text, opt.width);
                    s.col = std::max(0, (opt.width - utf8::width(s.text)) / 2);
                    s.decor = true;
                    s.attrs = kDim | kItalic;
                    s.off = b.offset;
                    l.segs.push_back(s);
                    out.push_back(l);
                    break;
                }
                int maxCols = std::max(4, opt.width - b.indent);
                auto [cols, rows] = imageCells(pw, ph, maxCols, std::max(3, opt.maxImageRows), opt.cellAspect, opt.cellPixelW);
                for (int r = 0; r < rows; ++r) {
                    Line l;
                    l.kind = LineKind::Image;
                    l.block = idx;
                    l.off = b.offset;
                    l.end = b.offset;
                    l.image = b.image;
                    l.alt = b.alt;
                    l.imageRow = r;
                    l.imageRows = rows;
                    l.imageCols = cols;
                    l.imageCol = b.indent + (maxCols - cols) / 2;
                    l.paraLine = r;
                    l.paraLines = rows;
                    out.push_back(std::move(l));
                }
                if (b.pageBreakBefore && start < out.size()) out[start].breakBefore = true;
                break;
            }
        }
        if (b.pageBreakBefore && start < out.size()) {
            out[start].breakBefore = true;
            // Drop the blank lines we added just before a forced break.
            while (start > 0 && out[start - 1].kind == LineKind::Blank) {
                out.erase(out.begin() + static_cast<long>(start - 1));
                --start;
            }
        }
        if (opt.lineSpacing > 1 && (b.kind == BlockKind::Text || b.kind == BlockKind::Heading)) {
            // Interleave blank lines between the text lines of this block.
            std::vector<Line> spaced;
            for (size_t i = start; i < out.size(); ++i) {
                if (i > start) {
                    Line bl;
                    bl.kind = LineKind::Blank;
                    bl.off = out[i].off;
                    bl.end = out[i].off;
                    bl.block = idx;
                    spaced.push_back(bl);
                }
                spaced.push_back(std::move(out[i]));
            }
            out.resize(start);
            for (auto& l : spaced) out.push_back(std::move(l));
        }
        prevIndentedPara = indentStyle;
        prevKind = b.kind;
    }
    // Ensure monotonic offsets.
    size_t last = 0;
    for (auto& l : out) {
        if (l.off < last) l.off = last;
        last = l.off;
        if (l.end < l.off) l.end = l.off;
    }
    if (out.empty()) {
        Line l;
        l.kind = LineKind::Blank;
        out.push_back(l);
    }
    return cl;
}

void paginate(ChapterLayout& cl, int H) {
    auto& L = cl.lines;
    cl.pages.clear();
    if (H < 3) H = 3;
    size_t i = 0;
    // Skip leading blanks.
    while (i < L.size() && L[i].kind == LineKind::Blank) ++i;
    if (i >= L.size()) {
        cl.pages.push_back(0);
        return;
    }
    while (i < L.size()) {
        cl.pages.push_back(i);
        size_t start = i;
        int used = 0;
        size_t j = i;
        while (j < L.size()) {
            if (j > start && L[j].breakBefore) break;
            if (L[j].kind == LineKind::Image && L[j].imageRow == 0) {
                int rows = L[j].imageRows;
                if (used + rows > H && used > 0) break;
                if (used + rows > H) {  // taller than a page (shouldn't happen): clip
                    j += static_cast<size_t>(rows);
                    used = H;
                    break;
                }
                j += static_cast<size_t>(rows);
                used += rows;
                continue;
            }
            if (used + 1 > H) break;
            ++used;
            ++j;
        }
        // j is the first line of the next page. Adjust for widows/orphans/headings.
        if (j < L.size() && j > start + 2) {
            size_t brk = j;
            const Line& last = L[brk - 1];
            const Line& next = L[brk];
            // Orphan: only the first line of a paragraph at the bottom.
            if (last.kind == LineKind::Text && last.paraLine == 0 && last.paraLines > 1) brk -= 1;
            // Widow: only the last line of a paragraph at the top of the next page.
            else if (next.kind == LineKind::Text && next.paraLines >= 3 && next.paraLine == next.paraLines - 1 &&
                     L[brk - 1].block == next.block)
                brk -= 1;
            // Keep headings with the following text.
            size_t k = brk;
            while (k > start + 1 && (L[k - 1].kind == LineKind::Blank || L[k - 1].keepWithNext)) {
                if (L[k - 1].keepWithNext) {
                    // move the heading (and blanks) down
                    size_t h = k - 1;
                    while (h > start + 1 && L[h - 1].keepWithNext) --h;
                    k = h;
                    brk = k;
                    break;
                }
                --k;
            }
            if (brk > start) j = brk;
        }
        // Skip blank lines at the top of the next page.
        while (j < L.size() && L[j].kind == LineKind::Blank && !L[j].breakBefore) ++j;
        if (j <= i) j = i + 1;  // progress guarantee
        i = j;
    }
}

size_t ChapterLayout::lineForOffset(size_t off) const {
    // Last line whose off <= target, preferring non-blank lines.
    auto it = std::upper_bound(lines.begin(), lines.end(), off, [](size_t v, const Line& l) { return v < l.off; });
    if (it == lines.begin()) return 0;
    size_t idx = static_cast<size_t>(std::distance(lines.begin(), it)) - 1;
    // Walk back over lines with identical offsets to the first one (e.g. image rows).
    while (idx > 0 && lines[idx - 1].off == lines[idx].off && lines[idx].kind != LineKind::Text) --idx;
    return idx;
}

size_t ChapterLayout::pageForLine(size_t line) const {
    if (pages.empty()) return 0;
    auto it = std::upper_bound(pages.begin(), pages.end(), line);
    if (it == pages.begin()) return 0;
    return static_cast<size_t>(std::distance(pages.begin(), it)) - 1;
}

}  // namespace clibooks
