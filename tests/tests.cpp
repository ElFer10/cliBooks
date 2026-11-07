// Minimal self-contained test runner (no external framework).
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "app/keymap.hpp"
#include "epub/zip_archive.hpp"
#include "epub/css.hpp"
#include "epub/html_parser.hpp"
#include "image/image.hpp"
#include "util/platform.hpp"
#include "layout/hyphen.hpp"
#include "layout/layout.hpp"
#include "util/strings.hpp"
#include "util/utf8.hpp"

using namespace clipub;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) ++g_pass;                                               \
        else {                                                            \
            ++g_fail;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

static std::string lineText(const Line& l) {
    std::string s;
    int col = 0;
    for (auto& sg : l.segs) {
        while (col < sg.col) {
            s += ' ';
            ++col;
        }
        s += sg.text;
        col += utf8::width(sg.text);
    }
    return s;
}

static Chapter parse(const std::string& body, const std::string& css = "") {
    std::string html = "<?xml version='1.0' encoding='utf-8'?><html xmlns='http://www.w3.org/1999/xhtml' "
                       "xmlns:epub='http://www.idpf.org/2007/ops'><head><style>" +
                       css + "</style></head><body>" + body + "</body></html>";
    StyleLoader none = [](const std::string&) -> const css::StyleSheet* { return nullptr; };
    return parseChapter(html, "OEBPS/ch1.xhtml", none);
}

int main() {
    // ---- utf8
    CHECK(utf8::width("año") == 3);
    CHECK(utf8::width("漢字") == 4);
    CHECK(utf8::foldCopy("ÁrBOL Ñandú") == "arbol nandu");
    CHECK(utf8::fold(0x10D) == 'c');  // č
    CHECK(utf8::fold(0x142) == 'l');  // ł
    CHECK(utf8::fold(0x17E) == 'z');  // ž
    CHECK(utf8::truncate("abcdef", 4) == "abc…");

    // ---- strings
    CHECK(str::resolvePath("OEBPS/text", "../images/a%20b.png") == "OEBPS/images/a b.png");
    CHECK(str::decodeEntities("a&nbsp;b&#233;&hellip;") == "a\xC2\xA0" "bé…");
    CHECK(str::parseHexColor("#fff") == 0xFFFFFFu);

    // ---- css
    css::StyleSheet ss;
    ss.parse("p.c { text-align: center; font-weight: bold } .it{font-style:italic} p{text-indent:1.5em} "
             "@media amzn-kf8 { p { color: red } } aside.x { display:none }");
    auto st = ss.compute("p", {"c"}, "");
    CHECK(st.align && *st.align == css::Align::Center);
    CHECK(st.bold && *st.bold);
    CHECK(st.textIndent && *st.textIndent);
    CHECK(ss.paragraphsIndented());

    // ---- parser
    auto ch = parse("<h1 id='top'>Title</h1><p>Hello <b>bold</b> and <i>it</i>.</p>"
                    "<p>Note<a epub:type='noteref' href='#fn1'>1</a></p>"
                    "<aside epub:type='footnote' id='fn1'><p>The footnote.</p></aside>"
                    "<ul><li>one</li><li>two</li></ul><hr/><pre>a  b\n c</pre>"
                    "<table><tr><th>H1</th><th>H2</th></tr><tr><td>x</td><td>y</td></tr></table>"
                    "<p>Line<br/>break</p><img src='../img/p.png' alt='Pic'/>");
    CHECK(ch.title == "Title");
    CHECK(ch.anchors.count("top") == 1);
    CHECK(ch.notes.count("fn1") == 1);
    CHECK(ch.links.size() == 1 && ch.links[0].noteref && ch.links[0].target == "OEBPS/ch1.xhtml#fn1");
    bool sawList = false, sawPre = false, sawTable = false, sawImg = false, sawRule = false;
    for (auto& b : ch.blocks) {
        if (b.marker == "•") sawList = true;
        if (b.kind == BlockKind::Pre) sawPre = true;
        if (b.kind == BlockKind::Table) sawTable = b.table->rows.size() == 2;
        if (b.kind == BlockKind::Image) sawImg = b.image == "img/p.png" && b.alt == "Pic";
        if (b.kind == BlockKind::Rule) sawRule = true;
    }
    CHECK(sawList && sawPre && sawTable && sawImg && sawRule);
    CHECK(ch.text.find("The footnote") == std::string::npos);  // hidden in flow
    CHECK(ch.text.find("Hello bold and it.") != std::string::npos);

    // Whitespace collapsing between inline elements
    auto ch2 = parse("<p>  a   <em>b</em>\n <strong>c</strong>  </p>");
    CHECK(ch2.blocks.size() == 1 && ch2.text == "a b c\n");

    // Malformed HTML gets repaired
    StyleLoader none = [](const std::string&) -> const css::StyleSheet* { return nullptr; };
    auto ch3 = parseChapter("<html><body><p>one<br>two &copy; three & four</p></body></html>", "x.html", none);
    CHECK(ch3.text.find("two © three & four") != std::string::npos);

    // ---- layout
    auto ch4 = parse("<p>The quick brown fox jumps over the lazy dog and keeps running far away.</p>");
    LayoutOptions lo;
    lo.width = 20;
    lo.justify = true;
    lo.hyphenate = false;
    auto cl = layoutChapter(ch4, lo, nullptr);
    CHECK(cl.lines.size() >= 4);
    for (size_t i = 0; i + 1 < cl.lines.size(); ++i) CHECK(utf8::width(lineText(cl.lines[i])) == 20);  // justified
    CHECK(utf8::width(lineText(cl.lines.back())) <= 20);
    // Offsets map back to the source
    auto& seg = cl.lines[0].segs[0];
    CHECK(ch4.text.substr(seg.off, seg.text.size()) == seg.text);
    paginate(cl, 3);
    CHECK(cl.pages.size() >= 2);

    // Images take rows and stay together
    auto ch5 = parse("<p>a</p><img src='x.png'/><p>b</p>");
    ImageSizer sizer = [](const std::string&) { return std::pair<int, int>{400, 300}; };
    lo.width = 40;
    lo.maxImageRows = 8;
    auto cl5 = layoutChapter(ch5, lo, sizer);
    int imgRows = 0;
    for (auto& l : cl5.lines)
        if (l.kind == LineKind::Image) ++imgRows;
    CHECK(imgRows > 0 && imgRows <= 8);

    // ---- hyphenation (tiny pattern set)
    Hyphenator h;
    h.loadPatterns("1ba\n1be\n1bi\n1bo\n1bu\n1ca\n1co\n1la\n1ma\n1na\n1ra\n1ta\n1to\n");
    auto pts = h.points("caramelo");
    CHECK(!pts.empty());

    // ---- keymap
    Keymap km;
    Action a;
    CHECK(km.lookup(Mode::Reader, parseKeys("g"), a) == Keymap::Match::Partial);
    CHECK(km.lookup(Mode::Reader, parseKeys("gg"), a) == Keymap::Match::Full && a == Action::First);
    CHECK(km.lookup(Mode::Reader, parseKeys("<C-f>"), a) == Keymap::Match::Full && a == Action::PageDown);
    CHECK(km.lookup(Mode::Reader, parseKeys(":"), a) == Keymap::Match::Full && a == Action::Command);
    km.setBindings(Mode::Reader, Action::PageDown, {"x"});
    CHECK(km.lookup(Mode::Reader, parseKeys("<Space>"), a) == Keymap::Match::None);
    CHECK(parseKeys("<C-F><space>a").size() == 3);

    // ---- image encoders (uses the generated sample EPUB if present)
    {
        std::string err;
        auto ar = Archive::open(CLIPUB_SOURCE_DIR "/tests/fixtures/sample.epub", err);
        if (ar) {
            img::Cache cache([&](const std::string& p) { return ar->read(p); });
            auto sz = cache.size("OEBPS/images/gradient.png");
            CHECK(sz.first == 200 && sz.second == 120);
            img::Placement p{"OEBPS/images/gradient.png", 2, 3, 4, 10, 0, 4};
            auto blocks = img::render(cache, p, img::Protocol::Blocks, 8, 16, 0xFFFFFF, true, 1);
            CHECK(blocks.find("\x1b[3;4H") == 0 && blocks.find("\xe2\x96\x80") != std::string::npos);
            auto sixel = img::render(cache, p, img::Protocol::Sixel, 8, 16, 0xFFFFFF, true, 1);
            CHECK(sixel.find("\x1bPq") != std::string::npos && str::endsWith(sixel, "\x1b\\"));
            auto kitty = img::render(cache, p, img::Protocol::Kitty, 8, 16, 0xFFFFFF, true, 7);
            CHECK(kitty.find("\x1b_Ga=T,f=100") != std::string::npos && kitty.find("i=7") != std::string::npos);
            auto iterm = img::render(cache, p, img::Protocol::Iterm, 8, 16, 0xFFFFFF, true, 1);
            CHECK(iterm.find("\x1b]1337;File=inline=1") != std::string::npos);
            auto png = cache.png("OEBPS/images/gradient.png", 80, 64);
            CHECK(png.size() > 8 && png.substr(1, 3) == "PNG");
            CHECK(cache.size("missing.svg").first == -1);
        } else {
            std::printf("(skipping image tests: run tests/make_sample_epub.py)\n");
        }
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
