#pragma once
// Text layout for a monospaced grid: word wrapping, justification, hyphenation,
// lists, quotes, tables, images and pagination (widows/orphans, keep-with-next).
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "epub/document.hpp"
#include "layout/hyphen.hpp"

namespace clibooks {

enum class ParagraphStyle { Auto, Spaced, Indented };

struct LayoutOptions {
    int width = 72;  // text column width in cells
    bool justify = true;
    bool hyphenate = true;
    ParagraphStyle paragraphStyle = ParagraphStyle::Auto;
    int lineSpacing = 1;  // 1 or 2
    bool showImages = true;
    int maxImageRows = 20;
    double cellAspect = 2.0;  // cell height / cell width in pixels
    int cellPixelW = 8;
    std::shared_ptr<Hyphenator> hyphenator;
};

struct Seg {
    int col = 0;
    std::string text;  // display text
    uint16_t attrs = 0;
    int link = -1;
    size_t off = 0;     // source offset of the first character
    size_t srcLen = 0;  // source byte length covered by this segment
    bool decor = false;  // borders, quote bars, list markers: not selectable
    /// Source offset of the display code point with index cpIndex (byte index bi).
    size_t charOffset(size_t bi, size_t cpIndex) const {
        if (decor) return off;
        if (text.size() == srcLen) return off + bi;
        return off + std::min(cpIndex, srcLen ? srcLen - 1 : 0);
    }
};

enum class LineKind { Text, Blank, Image, Rule };

struct Line {
    LineKind kind = LineKind::Text;
    std::vector<Seg> segs;
    size_t off = 0;     // first source offset on this line
    size_t end = 0;     // one past the last source offset
    int block = -1;
    int paraLine = 0;   // index of the line within its block
    int paraLines = 1;  // total lines of the block
    bool keepWithNext = false;
    bool breakBefore = false;
    // Images
    std::string image;
    std::string alt;
    int imageRow = 0, imageRows = 0, imageCols = 0, imageCol = 0;
};

struct ChapterLayout {
    std::vector<Line> lines;
    std::vector<size_t> pages;  // index of the first line of each page
    size_t lineForOffset(size_t off) const;
    size_t pageForLine(size_t line) const;
    size_t pageEnd(size_t page) const {
        return page + 1 < pages.size() ? pages[page + 1] : lines.size();
    }
};

/// Returns pixel {width, height} of an image in the archive (0,0 if unknown).
using ImageSizer = std::function<std::pair<int, int>(const std::string&)>;

ChapterLayout layoutChapter(const Chapter& ch, const LayoutOptions& opt, const ImageSizer& sizer);
void paginate(ChapterLayout& cl, int pageHeight);

/// Width/height in cells for an image given its pixel size and the layout constraints.
std::pair<int, int> imageCells(int pw, int ph, int maxCols, int maxRows, double aspect, int cellPixelW);

}  // namespace clibooks
