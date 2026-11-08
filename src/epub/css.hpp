#pragma once
// A deliberately small CSS engine: it only understands the properties that can be
// expressed in a terminal (weight, style, decoration, alignment, display:none,
// indentation, small-caps/uppercase, sup/sub, page breaks). Selectors are matched
// on their last compound selector (tag/.class/#id), which covers the vast
// majority of EPUB stylesheets.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clibooks::css {

enum class Align { Left, Center, Right, Justify };
enum class VAlign { Baseline, Super, Sub };

struct Style {
    std::optional<bool> bold, italic, underline, strike, smallCaps, uppercase, hidden, pre, monospace;
    std::optional<Align> align;
    std::optional<VAlign> valign;
    std::optional<int> marginLeft;   // in terminal cells
    std::optional<bool> textIndent;  // first-line indentation present
    std::optional<bool> pageBreakBefore;
    std::optional<bool> displayBlock;  // display:block/inline override

    void merge(const Style& o);  // o overrides this
};

Style parseDeclarations(std::string_view decls);

class StyleSheet {
public:
    void parse(std::string_view css);
    /// Cascaded style for an element (without inheritance; caller inherits).
    Style compute(std::string_view tag, const std::vector<std::string>& classes, std::string_view id) const;
    bool empty() const { return rules_.empty(); }
    /// True if body paragraphs are styled with a text-indent (used for "auto" paragraph style).
    bool paragraphsIndented() const;

private:
    struct Rule {
        std::string tag;
        std::vector<std::string> classes;
        std::string id;
        int specificity = 0;
        size_t order = 0;
        Style style;
    };
    void addRule(std::string_view selector, const Style& st);
    std::vector<Rule> rules_;
};

}  // namespace clibooks::css
