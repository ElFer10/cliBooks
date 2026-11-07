#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "epub/document.hpp"
#include "epub/html_parser.hpp"
#include "epub/zip_archive.hpp"

namespace clipub {

struct Position {
    int chapter = 0;
    size_t offset = 0;  // byte offset into Chapter::text
    auto operator<=>(const Position&) const = default;
};

struct TocEntry {
    std::string title;
    int depth = 0;
    std::string href;
    Position pos;
    bool resolved = false;
};

struct PageTarget {
    std::string label;  // printed page label ("iv", "123")
    Position pos;
};

struct Metadata {
    std::string title, language, publisher, description, identifier, date, rights;
    std::vector<std::string> authors;
    std::string coverImage;  // archive path
    std::string authorLine() const;
};

class Book {
public:
    static std::unique_ptr<Book> open(const std::filesystem::path& file, std::string& error,
                                      const ParseOptions& opts = {});
    /// Metadata only (fast; used by the library scanner).
    static std::optional<Metadata> peek(const std::filesystem::path& file);

    const Metadata& meta() const { return meta_; }
    const std::vector<Chapter>& chapters() const { return chapters_; }
    const std::vector<TocEntry>& toc() const { return toc_; }
    const std::vector<PageTarget>& pageList() const { return pageList_; }
    const std::filesystem::path& file() const { return file_; }
    const std::string& id() const { return id_; }
    bool fixedLayout() const { return fixedLayout_; }

    std::optional<std::string> resource(const std::string& archivePath) const;
    /// Resolves an internal "path#frag" target to a position.
    std::optional<Position> resolve(const std::string& target) const;
    /// Title of the TOC entry containing the position (falls back to the chapter heading).
    std::string sectionTitle(const Position& p) const;
    /// Index of the deepest TOC entry at or before the position (-1 if none).
    int tocIndexAt(const Position& p) const;
    /// Total text bytes (for percentage computations).
    size_t totalLength() const { return totalLength_; }
    size_t absoluteOffset(const Position& p) const;
    Position fromAbsolute(size_t abs) const;
    /// Footnote text for a link target (hidden aside, or the anchored paragraph).
    std::optional<std::string> footnoteText(const std::string& target) const;

private:
    bool load(std::string& error, const ParseOptions& opts);
    void parseNav(const std::string& navPath);
    void parseNcx(const std::string& ncxPath);
    const css::StyleSheet* stylesheet(const std::string& path);

    std::filesystem::path file_;
    std::unique_ptr<Archive> archive_;
    Metadata meta_;
    std::string opfDir_;
    std::string id_;
    bool fixedLayout_ = false;
    std::vector<Chapter> chapters_;
    std::unordered_map<std::string, int> spineIndex_;  // archive path -> chapter
    std::vector<TocEntry> toc_;
    std::vector<PageTarget> pageList_;
    std::map<std::string, std::unique_ptr<css::StyleSheet>> cssCache_;
    std::vector<size_t> chapterStart_;  // absolute offsets
    size_t totalLength_ = 0;
};

}  // namespace clipub
