#pragma once
#include <functional>
#include <optional>
#include <string>

#include "epub/document.hpp"

namespace clibooks {

/// Loads a stylesheet by archive path (cached by the caller).
using StyleLoader = std::function<const css::StyleSheet*(const std::string& path)>;

struct ParseOptions {
    bool useBookStyles = true;  // honour publisher CSS (alignment, emphasis…)
};

/// Converts an XHTML document into blocks of styled runs.
Chapter parseChapter(const std::string& xhtml, const std::string& path, const StyleLoader& loadCss,
                     const ParseOptions& opts = {});

/// Makes sloppy HTML a bit closer to XML (void elements, stray ampersands).
std::string repairHtml(const std::string& html);

}  // namespace clibooks
