#pragma once
// Per-book persistent state (reading position, bookmarks, highlights & notes),
// stored as TOML files in $XDG_DATA_HOME/clibooks/books/<book-id>.toml.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "epub/book.hpp"

namespace clibooks {

struct Bookmark {
    Position pos;
    int64_t created = 0;
    std::string label;  // section title + snippet at creation time
};

enum HighlightStyle : int { kYellow = 0, kGreen, kBlue, kPink, kPurple, kUnderlineStyle };

struct Highlight {
    int chapter = 0;
    size_t start = 0, end = 0;  // byte offsets in Chapter::text
    int style = kYellow;
    std::string note;
    std::string text;  // quoted passage (for lists / export)
    int64_t created = 0;
};

struct BookState {
    std::string id, path, title, author;
    Position pos;
    double progress = 0.0;  // 0..1
    int64_t lastOpened = 0;
    int64_t readingSeconds = 0;
    std::vector<Bookmark> bookmarks;
    std::vector<Highlight> highlights;
};

class StateStore {
public:
    StateStore();
    std::optional<BookState> load(const std::string& id) const;
    bool save(const BookState& s) const;
    std::vector<BookState> all() const;
    bool remove(const std::string& id) const;
    const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

int64_t nowSeconds();

}  // namespace clibooks
