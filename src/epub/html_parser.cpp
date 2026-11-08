#include "epub/html_parser.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace clibooks {
namespace {

std::string localName(const char* raw) {
    std::string n = raw ? raw : "";
    if (auto p = n.find(':'); p != std::string::npos) n = n.substr(p + 1);
    return str::lower(n);
}

std::string attr(const pugi::xml_node& n, const char* name) {
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an == name) return a.value();
    }
    // namespaced fallbacks (xlink:href, epub:type…)
    for (auto a : n.attributes()) {
        std::string an = a.name();
        auto p = an.find(':');
        if (p != std::string::npos && an.substr(p + 1) == name) return a.value();
    }
    return {};
}

std::string epubType(const pugi::xml_node& n) {
    for (auto a : n.attributes()) {
        std::string an = a.name();
        if (an == "epub:type" || an == "role" || str::endsWith(an, ":type")) return str::lower(a.value());
    }
    return {};
}

bool hasWord(std::string_view list, std::string_view w) {
    for (auto& p : str::splitWs(list))
        if (p == w || str::endsWith(p, std::string(":") + std::string(w)) || p == "doc-" + std::string(w)) return true;
    return false;
}

const std::unordered_set<std::string> kBlockTags{
    "p",      "div",    "h1",     "h2",      "h3",    "h4",     "h5",      "h6",     "li",   "blockquote",
    "pre",    "section", "article", "header", "footer", "aside", "figure", "figcaption", "dl", "dt",
    "dd",     "ul",     "ol",     "nav",     "main",  "center", "body",    "address", "details", "summary",
    "table",  "hr",     "html",   "hgroup",  "caption", "fieldset", "legend", "form", "menu"};

const std::unordered_set<std::string> kSkipTags{"head",  "script", "style", "title", "noscript",
                                                "audio", "video",  "rp",    "iframe", "button",
                                                "input", "select", "textarea", "canvas", "embed"};

struct BlockCtx {
    css::Align align = css::Align::Left;
    bool alignExplicit = false;
    int indent = 0;
    int quoteDepth = 0;
    bool firstIndent = false;
    bool pre = false;
    BlockKind kind = BlockKind::Text;
    int level = 0;
};

struct InlineCtx {
    uint16_t attrs = 0;
    int link = -1;
    bool uppercase = false;
};

struct ListCtx {
    bool ordered = false;
    int counter = 1;
};

class Builder {
public:
    Builder(Chapter& ch, const std::string& path, const StyleLoader& loader, const ParseOptions& opts)
        : ch_(ch), dir_(str::dirName(path)), path_(path), loader_(loader), opts_(opts) {}

    void run(const pugi::xml_node& root) {
        // Collect stylesheets first.
        collectStyles(root);
        BlockCtx bc;
        InlineCtx ic;
        walk(root, bc, ic);
        flush();
        // Trim trailing newline mess.
        if (ch_.title.empty()) {
            for (auto& b : ch_.blocks)
                if (b.kind == BlockKind::Heading) {
                    std::string t;
                    for (auto& r : b.runs) t += r.text;
                    ch_.title = str::collapseWs(t);
                    break;
                }
        }
    }

private:
    // ---------------------------------------------------------------- styles
    void collectStyles(const pugi::xml_node& root) {
        if (!opts_.useBookStyles) return;
        for (auto n : root.select_nodes("//*")) {
            auto node = n.node();
            std::string name = localName(node.name());
            if (name == "link") {
                std::string rel = str::lower(attr(node, "rel"));
                std::string href = attr(node, "href");
                if (rel.find("stylesheet") != std::string::npos && !href.empty()) {
                    if (auto* ss = loader_(str::resolvePath(dir_, str::splitFragment(href).first)))
                        sheets_.push_back(ss);
                }
            } else if (name == "style") {
                inlineSheet_.parse(node.child_value());
                hasInline_ = true;
            } else if (name == "body") {
                break;
            }
        }
    }

    css::Style styleFor(const pugi::xml_node& n, const std::string& tag) {
        css::Style st;
        std::string id = attr(n, "id");
        auto classes = str::splitWs(attr(n, "class"));
        if (opts_.useBookStyles) {
            for (auto* s : sheets_) st.merge(s->compute(tag, classes, id));
            if (hasInline_) st.merge(inlineSheet_.compute(tag, classes, id));
            std::string inl = attr(n, "style");
            if (!inl.empty()) st.merge(css::parseDeclarations(inl));
        } else {
            std::string inl = attr(n, "style");
            if (!inl.empty()) {
                auto s = css::parseDeclarations(inl);
                if (s.hidden) st.hidden = s.hidden;
            }
        }
        return st;
    }

    // ---------------------------------------------------------------- text
    size_t currentOffset() const { return ch_.text.size() + cur_.length; }

    void ensureBlock(const BlockCtx& bc) {
        if (hasCur_) return;
        cur_ = Block{};
        cur_.kind = bc.kind;
        cur_.level = bc.level;
        cur_.align = bc.align;
        cur_.alignExplicit = bc.alignExplicit;
        cur_.indent = bc.indent;
        cur_.quoteDepth = bc.quoteDepth;
        cur_.firstIndent = bc.firstIndent;
        cur_.marker = std::move(pendingMarker_);
        cur_.pageBreakBefore = pendingBreak_;
        pendingMarker_.clear();
        pendingBreak_ = false;
        hasCur_ = true;
        pendingSpace_ = false;
    }

    void appendRaw(std::string text, const InlineCtx& ic) {
        if (text.empty()) return;
        if (!cur_.runs.empty() && cur_.runs.back().attrs == ic.attrs && cur_.runs.back().link == ic.link) {
            cur_.runs.back().text += text;
        } else {
            cur_.runs.push_back(Run{text, ic.attrs, ic.link});
        }
        cur_.length += text.size();
    }

    void appendText(std::string_view raw, const BlockCtx& bc, const InlineCtx& ic) {
        std::string text = str::decodeEntities(raw);
        if (ic.uppercase) {
            for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (bc.pre) {
            ensureBlock(bc);
            // Normalise CRLF and tabs.
            std::string t;
            for (char c : text) {
                if (c == '\r') continue;
                if (c == '\t') t += "    ";
                else t += c;
            }
            appendRaw(std::move(t), ic);
            return;
        }
        std::string out;
        bool any = false;
        for (size_t i = 0; i < text.size();) {
            size_t st = i;
            char32_t cp = utf8::decode(text, i);
            if (cp == ' ' || cp == '\n' || cp == '\t' || cp == '\r' || cp == '\f') {
                pendingSpace_ = true;
                continue;
            }
            if (!any) {
                ensureBlock(bc);
                any = true;
            }
            if (pendingSpace_) {
                bool atLineStart = cur_.length == 0 && out.empty();
                bool afterBreak = !out.empty() ? out.back() == '\n'
                                               : (!cur_.runs.empty() && !cur_.runs.back().text.empty() &&
                                                  cur_.runs.back().text.back() == '\n');
                if (!atLineStart && !afterBreak) out += ' ';
                pendingSpace_ = false;
            }
            out.append(text, st, i - st);
        }
        if (!out.empty()) appendRaw(std::move(out), ic);
    }

    void lineBreak(const BlockCtx& bc, const InlineCtx& ic) {
        ensureBlock(bc);
        pendingSpace_ = false;
        appendRaw("\n", ic);
    }

    void flush() {
        if (!hasCur_) return;
        hasCur_ = false;
        // Trim trailing whitespace/newlines (except for pre blocks).
        if (cur_.kind != BlockKind::Pre) {
            while (!cur_.runs.empty()) {
                auto& t = cur_.runs.back().text;
                while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
                if (t.empty()) cur_.runs.pop_back();
                else break;
            }
        } else {
            while (!cur_.runs.empty()) {
                auto& t = cur_.runs.back().text;
                while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
                if (t.empty()) cur_.runs.pop_back();
                else break;
            }
        }
        size_t len = 0;
        for (auto& r : cur_.runs) len += r.text.size();
        cur_.length = len;
        bool hasVisible = false;
        for (auto& r : cur_.runs)
            for (char c : r.text)
                if (c != ' ' && c != '\n') {
                    hasVisible = true;
                    break;
                }
        if (!hasVisible && cur_.kind != BlockKind::Image && cur_.kind != BlockKind::Rule &&
            cur_.kind != BlockKind::Table) {
            if (cur_.pageBreakBefore) pendingBreak_ = true;
            cur_ = Block{};
            return;
        }
        commit(std::move(cur_));
        cur_ = Block{};
    }

    void commit(Block b) {
        b.offset = ch_.text.size();
        std::string text;
        if (b.kind == BlockKind::Table && b.table) {
            for (auto& row : b.table->rows) {
                for (size_t i = 0; i < row.size(); ++i) {
                    if (i) text += " | ";
                    text += row[i];
                }
                text += ' ';
            }
            if (!text.empty()) text.pop_back();
        } else {
            for (auto& r : b.runs) text += r.text;
        }
        b.length = text.size();
        ch_.text += text;
        ch_.text += '\n';
        ch_.blocks.push_back(std::move(b));
    }

    void addImage(const std::string& src, const std::string& alt, const BlockCtx& bc) {
        if (src.empty()) return;
        flush();
        Block b;
        b.kind = BlockKind::Image;
        b.image = str::resolvePath(dir_, str::splitFragment(src).first);
        b.alt = str::collapseWs(alt);
        b.align = css::Align::Center;
        b.indent = bc.indent;
        b.pageBreakBefore = pendingBreak_;
        pendingBreak_ = false;
        commit(std::move(b));
    }

    static std::string textContent(const pugi::xml_node& n) {
        std::string out;
        for (auto c : n.children()) {
            if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) out += c.value();
            else if (c.type() == pugi::node_element) {
                std::string nm = localName(c.name());
                if (nm == "script" || nm == "style" || nm == "rp") continue;
                if (nm == "br" || nm == "p" || nm == "div" || nm == "li") out += ' ';
                out += textContent(c);
            }
        }
        return out;
    }

    void handleTable(const pugi::xml_node& table, const BlockCtx& bc) {
        flush();
        auto data = std::make_shared<TableData>();
        std::function<void(const pugi::xml_node&)> rows = [&](const pugi::xml_node& n) {
            for (auto c : n.children()) {
                if (c.type() != pugi::node_element) continue;
                std::string nm = localName(c.name());
                if (nm == "tr") {
                    std::vector<std::string> cells;
                    bool header = localName(n.name()) == "thead";
                    bool allTh = true;
                    for (auto cell : c.children()) {
                        if (cell.type() != pugi::node_element) continue;
                        std::string cn = localName(cell.name());
                        if (cn != "td" && cn != "th") continue;
                        if (cn != "th") allTh = false;
                        cells.push_back(str::collapseWs(str::decodeEntities(textContent(cell))));
                        // Record anchors inside cells.
                        for (auto x : cell.select_nodes(".//*[@id]"))
                            ch_.anchors.emplace(x.node().attribute("id").value(), currentOffset());
                    }
                    if (!cells.empty()) {
                        data->rows.push_back(std::move(cells));
                        data->header.push_back(header || allTh);
                    }
                } else if (nm == "thead" || nm == "tbody" || nm == "tfoot") {
                    rows(c);
                } else if (nm == "caption") {
                    // Caption becomes its own small block before the table.
                    BlockCtx cb = bc;
                    cb.align = css::Align::Center;
                    InlineCtx ic;
                    ic.attrs = kItalic;
                    appendText(textContent(c), cb, ic);
                    flush();
                }
            }
        };
        rows(table);
        if (data->rows.empty()) return;
        Block b;
        b.kind = BlockKind::Table;
        b.table = data;
        b.indent = bc.indent;
        b.quoteDepth = bc.quoteDepth;
        commit(std::move(b));
    }

    // ---------------------------------------------------------------- walk
    void walk(const pugi::xml_node& node, BlockCtx bc, InlineCtx ic) {
        for (auto child : node.children()) {
            switch (child.type()) {
                case pugi::node_pcdata:
                case pugi::node_cdata:
                    appendText(child.value(), bc, ic);
                    break;
                case pugi::node_element:
                    element(child, bc, ic);
                    break;
                default:
                    break;
            }
        }
    }

    void element(const pugi::xml_node& n, BlockCtx bc, InlineCtx ic) {
        std::string tag = localName(n.name());
        if (kSkipTags.count(tag)) {
            if (tag == "audio" || tag == "video") {
                // Represent media with a placeholder so the reader knows something is there.
                flush();
                InlineCtx dim;
                dim.attrs = kDim | kItalic;
                BlockCtx c = bc;
                c.align = css::Align::Center;
                appendText(tag == "audio" ? "[audio]" : "[video]", c, dim);
                flush();
            }
            return;
        }
        std::string id = attr(n, "id");
        std::string etype = epubType(n);
        css::Style st = styleFor(n, tag);

        // Footnote asides: hidden in flow (as Books does), kept for pop-ups.
        bool footnoteAside = tag == "aside" && (hasWord(etype, "footnote") || hasWord(etype, "rearnote") ||
                                                hasWord(etype, "endnote") || hasWord(etype, "note"));
        if (footnoteAside && hasWord(etype, "footnote")) {
            if (!id.empty()) ch_.notes[id] = str::collapseWs(str::decodeEntities(textContent(n)));
            for (auto x : n.select_nodes(".//*[@id]"))
                ch_.notes[x.node().attribute("id").value()] = str::collapseWs(str::decodeEntities(textContent(n)));
            return;
        }
        if (st.hidden && *st.hidden) return;

        if (!id.empty()) ch_.anchors.emplace(id, currentOffset());
        if (std::string nm = attr(n, "name"); tag == "a" && !nm.empty()) ch_.anchors.emplace(nm, currentOffset());

        // ---- media
        if (tag == "img") {
            addImage(attr(n, "src"), attr(n, "alt"), bc);
            return;
        }
        if (tag == "image") {  // <svg><image xlink:href=…>
            addImage(attr(n, "href"), attr(n, "title"), bc);
            return;
        }
        if (tag == "object") {
            std::string type = attr(n, "type");
            if (str::startsWith(type, "image/")) {
                addImage(attr(n, "data"), textContent(n), bc);
                return;
            }
        }
        if (tag == "svg") {
            // Only <image> children can be shown; vector drawings are skipped.
            if (n.select_node(".//*[local-name()='image']")) walk(n, bc, ic);
            return;
        }
        if (tag == "math") {
            std::string alt = attr(n, "alttext");
            appendText(alt.empty() ? str::collapseWs(textContent(n)) : alt, bc, ic);
            return;
        }
        if (tag == "br") {
            lineBreak(bc, ic);
            return;
        }
        if (tag == "hr") {
            flush();
            Block b;
            b.kind = BlockKind::Rule;
            b.align = css::Align::Center;
            commit(std::move(b));
            return;
        }
        if (tag == "table") {
            if (st.pageBreakBefore && *st.pageBreakBefore) pendingBreak_ = true;
            handleTable(n, bc);
            return;
        }

        // ---- inline styling
        if (tag == "b" || tag == "strong") ic.attrs |= kBold;
        else if (tag == "i" || tag == "em" || tag == "cite" || tag == "dfn" || tag == "var") ic.attrs |= kItalic;
        else if (tag == "u" || tag == "ins") ic.attrs |= kUnderline;
        else if (tag == "s" || tag == "strike" || tag == "del") ic.attrs |= kStrike;
        else if (tag == "code" || tag == "kbd" || tag == "samp" || tag == "tt") ic.attrs |= kCode;
        else if (tag == "sup") ic.attrs |= kSup;
        else if (tag == "sub") ic.attrs |= kSub;
        else if (tag == "rt") ic.attrs |= kDim;
        else if (tag == "small") ic.attrs |= 0;
        if (st.bold) ic.attrs = *st.bold ? (ic.attrs | kBold) : (ic.attrs & ~kBold);
        if (st.italic) ic.attrs = *st.italic ? (ic.attrs | kItalic) : (ic.attrs & ~kItalic);
        if (st.underline) ic.attrs = *st.underline ? (ic.attrs | kUnderline) : (ic.attrs & ~kUnderline);
        if (st.strike && *st.strike) ic.attrs |= kStrike;
        if (st.monospace && *st.monospace) ic.attrs |= kCode;
        if (st.smallCaps) ic.attrs = *st.smallCaps ? (ic.attrs | kSmallCaps) : (ic.attrs & ~kSmallCaps);
        if (st.uppercase) ic.uppercase = *st.uppercase;
        if (st.valign) {
            if (*st.valign == css::VAlign::Super) ic.attrs |= kSup;
            else if (*st.valign == css::VAlign::Sub) ic.attrs |= kSub;
        }

        if (tag == "a") {
            std::string href = attr(n, "href");
            if (!href.empty()) {
                Link l;
                bool external = href.find("://") != std::string::npos || str::startsWith(href, "mailto:");
                l.external = external;
                if (external) {
                    l.target = href;
                } else if (href[0] == '#') {
                    l.target = path_ + href;
                } else {
                    auto [file, frag] = str::splitFragment(href);
                    l.target = str::resolvePath(dir_, file) + (frag.empty() ? "" : "#" + frag);
                }
                l.noteref = hasWord(etype, "noteref");
                if (!l.noteref) {
                    // Heuristic: short markers such as "1", "[12]", "*", "†" are footnote references.
                    std::string t = str::collapseWs(textContent(n));
                    bool marker = !t.empty() && utf8::width(t) <= 5;
                    if (marker) {
                        for (char c : t)
                            if (std::isalpha(static_cast<unsigned char>(c))) marker = false;
                    }
                    if (marker && (ic.attrs & kSup)) l.noteref = true;
                    else if (marker && !t.empty() && (t[0] == '[' || t[0] == '(' || t == "*")) l.noteref = true;
                    else if (marker && n.select_node(".//*[local-name()='sup']")) l.noteref = true;
                    else if (n.parent() && localName(n.parent().name()) == "sup" && marker) l.noteref = true;
                }
                if (l.noteref) {
                    // Render numeric note references as superscripts (¹²³) like a typeset book.
                    std::string t = str::collapseWs(textContent(n));
                    bool digits = !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
                    if (digits) ic.attrs |= kSup;
                }
                ch_.links.push_back(std::move(l));
                ic.link = static_cast<int>(ch_.links.size() - 1);
                ic.attrs |= kLink;
            }
        }

        bool isBlock = kBlockTags.count(tag) > 0;
        if (st.displayBlock) isBlock = *st.displayBlock || (isBlock && tag == "body");
        if (tag == "rt") {
            appendText("(", bc, ic);
            walk(n, bc, ic);
            appendText(")", bc, ic);
            return;
        }
        if (!isBlock) {
            walk(n, bc, ic);
            return;
        }

        // ---- block element
        flush();
        BlockCtx inner = bc;
        inner.kind = BlockKind::Text;
        inner.level = 0;
        if (st.align) {
            inner.align = *st.align;
            inner.alignExplicit = true;
        }
        if (tag == "center") {
            inner.align = css::Align::Center;
            inner.alignExplicit = true;
        }
        if (st.textIndent) inner.firstIndent = *st.textIndent;
        if (st.marginLeft && *st.marginLeft > 0 && (tag == "div" || tag == "p" || tag == "blockquote" || tag == "dd"))
            inner.indent = std::min(bc.indent + *st.marginLeft, bc.indent + 8);
        if (st.pageBreakBefore && *st.pageBreakBefore) pendingBreak_ = true;
        if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
            inner.kind = BlockKind::Heading;
            inner.level = tag[1] - '0';
            ic.attrs |= kBold | kHeading;
            inner.firstIndent = false;
        } else if (tag == "pre" || (st.pre && *st.pre)) {
            inner.kind = BlockKind::Pre;
            inner.pre = true;
            ic.attrs |= (tag == "pre" ? kCode : 0);
            inner.firstIndent = false;
        } else if (tag == "blockquote") {
            inner.quoteDepth = bc.quoteDepth + 1;
            inner.indent = bc.indent + 2;
        } else if (tag == "dd") {
            inner.indent = bc.indent + 4;
        } else if (tag == "dt") {
            ic.attrs |= kBold;
        } else if (tag == "figcaption" || tag == "caption") {
            ic.attrs |= kItalic;
            if (!st.align) {
                inner.align = css::Align::Center;
                inner.alignExplicit = true;
            }
        } else if (tag == "ul" || tag == "ol" || tag == "menu") {
            ListCtx l;
            l.ordered = tag == "ol";
            if (std::string s = attr(n, "start"); !s.empty()) l.counter = std::atoi(s.c_str());
            lists_.push_back(l);
            inner.indent = bc.indent + 2;
            inner.firstIndent = false;
            walk(n, inner, ic);
            flush();
            lists_.pop_back();
            return;
        } else if (tag == "li") {
            inner.firstIndent = false;
            if (!lists_.empty()) {
                auto& l = lists_.back();
                if (std::string v = attr(n, "value"); !v.empty()) l.counter = std::atoi(v.c_str());
                if (l.ordered) pendingMarker_ = std::to_string(l.counter++) + ".";
                else {
                    static const char* bullets[] = {"•", "◦", "▪"};
                    pendingMarker_ = bullets[(lists_.size() - 1) % 3];
                }
            } else {
                pendingMarker_ = "•";
            }
            std::string lsType = attr(n, "style");
            if (lsType.find("list-style-type:none") != std::string::npos ||
                lsType.find("list-style: none") != std::string::npos)
                pendingMarker_.clear();
        }
        walk(n, inner, ic);
        flush();
        pendingMarker_.clear();
    }

    Chapter& ch_;
    std::string dir_;
    std::string path_;
    const StyleLoader& loader_;
    ParseOptions opts_;
    std::vector<const css::StyleSheet*> sheets_;
    css::StyleSheet inlineSheet_;
    bool hasInline_ = false;
    Block cur_;
    bool hasCur_ = false;
    bool pendingSpace_ = false;
    bool pendingBreak_ = false;
    std::string pendingMarker_;
    std::vector<ListCtx> lists_;
};

}  // namespace

std::string repairHtml(const std::string& html) {
    static const char* voids[] = {"br", "hr", "img", "meta", "link", "input", "col", "area", "base", "wbr", "source", "param", "embed"};
    std::string out;
    out.reserve(html.size() + 64);
    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '&') {
            // Escape stray ampersands that aren't entity references.
            size_t j = i + 1;
            while (j < html.size() && j - i < 12 && (std::isalnum(static_cast<unsigned char>(html[j])) || html[j] == '#')) ++j;
            if (j < html.size() && html[j] == ';' && j > i + 1) out += c;
            else out += "&amp;";
            continue;
        }
        if (c == '<' && i + 1 < html.size() && std::isalpha(static_cast<unsigned char>(html[i + 1]))) {
            size_t end = html.find('>', i);
            if (end == std::string::npos) {
                out += html.substr(i);
                break;
            }
            std::string tag = html.substr(i, end - i + 1);
            size_t nameEnd = 1;
            while (nameEnd < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[nameEnd])))) ++nameEnd;
            std::string name = str::lower(tag.substr(1, nameEnd - 1));
            bool isVoid = std::any_of(std::begin(voids), std::end(voids), [&](const char* v) { return name == v; });
            if (isVoid && tag.size() >= 2 && tag[tag.size() - 2] != '/') tag.insert(tag.size() - 1, "/");
            out += tag;
            i = end;
            continue;
        }
        out += c;
    }
    return out;
}

Chapter parseChapter(const std::string& xhtml, const std::string& path, const StyleLoader& loadCss,
                     const ParseOptions& opts) {
    Chapter ch;
    ch.path = path;
    pugi::xml_document doc;
    unsigned flags = pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_cdata;
    flags &= ~pugi::parse_eol;
    auto res = doc.load_buffer(xhtml.data(), xhtml.size(), flags);
    if (!res) {
        std::string fixed = repairHtml(xhtml);
        pugi::xml_document doc2;
        auto res2 = doc2.load_buffer(fixed.data(), fixed.size(), flags);
        // Keep whichever parse got further into the document.
        if (res2 || res2.offset > res.offset) doc.reset(doc2);
    }
    pugi::xml_node root = doc.document_element();
    Builder b(ch, path, loadCss, opts);
    // Styles live in <head>; the walker skips <head> content itself.
    if (root) b.run(root);
    return ch;
}

}  // namespace clibooks
