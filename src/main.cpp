// clipub — a terminal EPUB reader with vim-style keys.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "app/config.hpp"
#include "app/i18n.hpp"
#include "epub/book.hpp"
#include "image/image.hpp"
#include "layout/layout.hpp"
#include "ui/app.hpp"
#include "util/platform.hpp"
#include "util/utf8.hpp"

namespace fs = std::filesystem;
using namespace clipub;

static void usage() {
    std::puts(
        "clipub " CLIPUB_VERSION " — terminal EPUB reader\n\n"
        "Usage:\n"
        "  clipub [options] [book.epub | unpacked-epub-folder]\n\n"
        "Options:\n"
        "  -c, --config FILE     use FILE instead of ~/.config/clipub/config.toml\n"
        "  -t, --theme NAME      theme for this session (original quiet paper bold calm focus sepia night)\n"
        "  -i, --images PROTO    auto | kitty | iterm | sixel | blocks | none\n"
        "  -w, --width N         text column width\n"
        "      --scroll          vertical scrolling instead of pages\n"
        "      --dump            print the formatted book to stdout and exit\n"
        "      --check           parse the book and print statistics\n"
        "      --write-config    write an example config file (if none exists)\n"
        "  -h, --help            this help\n"
        "  -v, --version         version\n\n"
        "Inside the app press g? or F1 for the key reference and :help for commands.");
}

static int dumpBook(const fs::path& p, const Config& cfg, bool statsOnly) {
    std::string err;
    ParseOptions po;
    po.useBookStyles = cfg.bookStyles;
    auto t0 = std::chrono::steady_clock::now();
    auto book = Book::open(p, err, po);
    if (!book) {
        std::fprintf(stderr, "clipub: %s: %s\n", p.string().c_str(), err.c_str());
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    LayoutOptions lo;
    lo.width = cfg.width;
    lo.justify = cfg.justify;
    lo.hyphenate = cfg.hyphenate;
    lo.paragraphStyle = cfg.paragraphs;
    lo.showImages = false;
    lo.hyphenator = Hyphenator::forLanguage(cfg.hyphenLanguage.empty() ? book->meta().language : cfg.hyphenLanguage);
    size_t lines = 0, pages = 0, images = 0, links = 0, notes = 0;
    for (auto& ch : book->chapters()) {
        for (auto& b : ch.blocks)
            if (b.kind == BlockKind::Image) ++images;
        links += ch.links.size();
        notes += ch.notes.size();
        auto cl = layoutChapter(ch, lo, nullptr);
        paginate(cl, 40);
        lines += cl.lines.size();
        pages += cl.pages.size();
        if (!statsOnly) {
            for (auto& l : cl.lines) {
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
                std::puts(s.c_str());
            }
            std::puts("\n――――――――\n");
        }
    }
    auto t2 = std::chrono::steady_clock::now();
    if (statsOnly) {
        auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
        std::printf("title:      %s\n", book->meta().title.c_str());
        std::printf("authors:    %s\n", book->meta().authorLine().c_str());
        std::printf("language:   %s\n", book->meta().language.c_str());
        std::printf("spine:      %zu items\n", book->chapters().size());
        std::printf("toc:        %zu entries\n", book->toc().size());
        std::printf("page-list:  %zu\n", book->pageList().size());
        std::printf("text:       %zu bytes\n", book->totalLength());
        std::printf("images:     %zu   links: %zu   hidden footnotes: %zu\n", images, links, notes);
        std::printf("cover:      %s\n", book->meta().coverImage.empty() ? "-" : book->meta().coverImage.c_str());
        std::printf("layout:     %zu lines, %zu pages (w=%d h=40)\n", lines, pages, lo.width);
        std::printf("hyphenator: %s\n", lo.hyphenator ? "yes" : "no");
        std::printf("time:       parse %lld ms, layout %lld ms\n", static_cast<long long>(ms(t0, t1)),
                    static_cast<long long>(ms(t1, t2)));
    }
    return 0;
}

int main(int argc, char** argv) {
    std::optional<fs::path> file, configPath;
    std::optional<std::string> theme, images;
    std::optional<int> width;
    bool dump = false, check = false, writeConfig = false, scroll = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "clipub: %s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "-v" || a == "--version") {
            std::puts("clipub " CLIPUB_VERSION);
            return 0;
        } else if (a == "-c" || a == "--config") configPath = next();
        else if (a == "-t" || a == "--theme") theme = next();
        else if (a == "-i" || a == "--images") images = next();
        else if (a == "-w" || a == "--width") width = std::atoi(next().c_str());
        else if (a == "--scroll") scroll = true;
        else if (a == "--dump") dump = true;
        else if (a == "--check") check = true;
        else if (a == "--write-config") writeConfig = true;
        else if (!a.empty() && a[0] == '-' && a != "-") {
            std::fprintf(stderr, "clipub: unknown option %s\n", a.c_str());
            return 2;
        } else file = a;
    }

    if (writeConfig) {
        fs::path p = configPath.value_or(Config::defaultPath());
        if (fs::exists(p)) {
            std::printf("%s already exists\n", p.string().c_str());
            return 0;
        }
        if (!platform::writeFileAtomic(p, Config::exampleToml())) {
            std::fprintf(stderr, "clipub: cannot write %s\n", p.string().c_str());
            return 1;
        }
        std::printf("wrote %s\n", p.string().c_str());
        return 0;
    }

    Config cfg;
    cfg.load(configPath.value_or(Config::defaultPath()));
    cfg.loadPrefs();
    if (theme) cfg.theme = *theme;
    if (cfg.themeIndex(cfg.theme) < 0) cfg.theme = "original";
    if (images) cfg.images = *images;
    if (width) cfg.width = std::max(20, *width);
    if (scroll) cfg.mode = "scroll";
    setLanguage(cfg.language);

    if (dump || check) {
        if (!file) {
            std::fprintf(stderr, "clipub: --dump/--check need a book\n");
            return 2;
        }
        return dumpBook(*file, cfg, check);
    }
    if (!isatty(0) || !isatty(1)) {
        std::fprintf(stderr, "clipub: needs an interactive terminal\n");
        return 1;
    }
    App app(cfg);
    return app.run(file);
}
