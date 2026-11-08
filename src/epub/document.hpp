#pragma once
// In-memory representation of a parsed XHTML content document (one spine item).
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "epub/css.hpp"

namespace clibooks {

enum Attr : uint16_t {
    kNone = 0,
    kBold = 1 << 0,
    kItalic = 1 << 1,
    kUnderline = 1 << 2,
    kStrike = 1 << 3,
    kCode = 1 << 4,
    kLink = 1 << 5,
    kSup = 1 << 6,
    kSub = 1 << 7,
    kHeading = 1 << 8,
    kDim = 1 << 9,  // ruby annotations, captions
    kSmallCaps = 1 << 10,
};

struct Run {
    std::string text;
    uint16_t attrs = 0;
    int link = -1;  // index into Chapter::links
};

enum class BlockKind { Text, Heading, Pre, Rule, Image, Table };

struct TableData {
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> header;  // per row
};

struct Block {
    BlockKind kind = BlockKind::Text;
    int level = 0;  // heading level
    css::Align align = css::Align::Left;
    bool alignExplicit = false;
    int indent = 0;  // left margin in cells
    int quoteDepth = 0;
    bool firstIndent = false;
    bool pageBreakBefore = false;
    std::string marker;  // list item marker ("•", "3.")
    std::vector<Run> runs;
    std::string image;  // archive path of the image
    std::string alt;
    std::shared_ptr<TableData> table;
    size_t offset = 0;  // byte offset of the block in Chapter::text
    size_t length = 0;  // byte length of block text
};

struct Link {
    std::string target;  // "path/in/archive.xhtml#frag" or absolute URL
    bool external = false;
    bool noteref = false;  // epub:type="noteref" or looks like a footnote marker
};

struct Chapter {
    std::string path;
    std::string title;  // first heading, if any
    std::vector<Block> blocks;
    std::vector<Link> links;
    std::unordered_map<std::string, size_t> anchors;      // id -> text offset
    std::unordered_map<std::string, std::string> notes;   // hidden footnote asides: id -> text
    std::string text;  // plain text of all blocks, each terminated by '\n'
    bool linear = true;
};

}  // namespace clibooks
