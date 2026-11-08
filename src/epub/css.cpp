#include "epub/css.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "util/strings.hpp"

namespace clibooks::css {

void Style::merge(const Style& o) {
#define M(f) \
    if (o.f) f = o.f;
    M(bold) M(italic) M(underline) M(strike) M(smallCaps) M(uppercase) M(hidden) M(pre) M(monospace)
    M(align) M(valign) M(marginLeft) M(textIndent) M(pageBreakBefore) M(displayBlock)
#undef M
}

namespace {

std::string stripComments(std::string_view css) {
    std::string out;
    out.reserve(css.size());
    for (size_t i = 0; i < css.size(); ++i) {
        if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            auto end = css.find("*/", i + 2);
            if (end == std::string_view::npos) break;
            i = end + 1;
            continue;
        }
        out += css[i];
    }
    return out;
}

// Converts a CSS length to terminal cells (very rough: 1em ≈ 2 cells).
std::optional<int> lengthToCells(std::string_view v) {
    v = str::trim(v);
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    std::string s(v);
    double n = std::strtod(s.c_str(), &end);
    std::string unit = str::lower(str::trim(end ? std::string_view(end) : std::string_view()));
    double cells;
    if (unit == "em" || unit == "rem") cells = n * 2.0;
    else if (unit == "%") cells = n / 4.0;
    else if (unit == "px") cells = n / 8.0;
    else if (unit == "pt") cells = n / 6.0;
    else if (unit.empty() || unit == "ex" || unit == "ch") cells = n;
    else return std::nullopt;
    int c = static_cast<int>(cells + 0.5);
    return std::clamp(c, 0, 12);
}

bool isPositiveLength(std::string_view v) {
    v = str::trim(v);
    std::string s(v);
    double n = std::strtod(s.c_str(), nullptr);
    return n > 0.01;
}

}  // namespace

Style parseDeclarations(std::string_view decls) {
    Style st;
    for (auto& d : str::split(decls, ';')) {
        auto colon = d.find(':');
        if (colon == std::string::npos) continue;
        std::string prop = str::lower(str::trim(std::string_view(d).substr(0, colon)));
        std::string val = str::lower(str::trim(std::string_view(d).substr(colon + 1)));
        val = str::replaceAll(val, "!important", "");
        val = std::string(str::trim(val));
        if (prop == "font-weight") {
            if (val == "bold" || val == "bolder") st.bold = true;
            else if (val == "normal" || val == "lighter") st.bold = false;
            else if (int w = std::atoi(val.c_str()); w > 0) st.bold = w >= 600;
        } else if (prop == "font-style") {
            st.italic = (val == "italic" || val == "oblique");
        } else if (prop == "text-decoration" || prop == "text-decoration-line") {
            if (val.find("underline") != std::string::npos) st.underline = true;
            if (val.find("line-through") != std::string::npos) st.strike = true;
            if (val == "none") st.underline = st.strike = false;
        } else if (prop == "text-align") {
            if (val == "center") st.align = Align::Center;
            else if (val == "right" || val == "end") st.align = Align::Right;
            else if (val == "justify") st.align = Align::Justify;
            else if (val == "left" || val == "start") st.align = Align::Left;
        } else if (prop == "display") {
            if (val == "none") st.hidden = true;
            else {
                st.hidden = false;
                if (val == "block" || val == "list-item" || val == "table" || val == "flex") st.displayBlock = true;
                else if (val == "inline" || val == "inline-block") st.displayBlock = false;
            }
        } else if (prop == "visibility") {
            if (val == "hidden") st.hidden = true;
        } else if (prop == "font-variant" || prop == "font-variant-caps") {
            st.smallCaps = val.find("small-caps") != std::string::npos;
        } else if (prop == "text-transform") {
            st.uppercase = val == "uppercase";
        } else if (prop == "vertical-align") {
            if (val == "super" || val == "top" || val == "text-top") st.valign = VAlign::Super;
            else if (val == "sub" || val == "bottom") st.valign = VAlign::Sub;
            else if (val == "baseline") st.valign = VAlign::Baseline;
        } else if (prop == "margin-left" || prop == "padding-left" || prop == "margin-inline-start") {
            if (auto c = lengthToCells(val)) st.marginLeft = *c;
        } else if (prop == "margin") {
            auto parts = str::splitWs(val);
            if (parts.size() == 4) {
                if (auto c = lengthToCells(parts[3])) st.marginLeft = *c;
            } else if (parts.size() >= 2) {
                if (auto c = lengthToCells(parts[1])) st.marginLeft = *c;
            } else if (parts.size() == 1) {
                if (auto c = lengthToCells(parts[0])) st.marginLeft = *c;
            }
        } else if (prop == "text-indent") {
            st.textIndent = isPositiveLength(val);
        } else if (prop == "page-break-before" || prop == "break-before") {
            st.pageBreakBefore = (val == "always" || val == "page" || val == "left" || val == "right");
        } else if (prop == "white-space") {
            st.pre = (val == "pre" || val == "pre-wrap" || val == "pre-line");
        } else if (prop == "font-family") {
            if (val.find("mono") != std::string::npos || val.find("courier") != std::string::npos)
                st.monospace = true;
        }
    }
    return st;
}

void StyleSheet::addRule(std::string_view selector, const Style& st) {
    std::string sel(str::trim(selector));
    if (sel.empty()) return;
    // Use the last compound selector only.
    for (char c : std::string_view(">+~")) sel = str::replaceAll(sel, std::string(1, c), " ");
    auto parts = str::splitWs(sel);
    if (parts.empty()) return;
    std::string last = parts.back();
    // Ignore dynamic pseudo-classes; keep structural ones loosely (drop them).
    bool dynamic = last.find(":hover") != std::string::npos || last.find(":active") != std::string::npos ||
                   last.find(":focus") != std::string::npos || last.find(":visited") != std::string::npos ||
                   last.find("::") != std::string::npos || last.find(":before") != std::string::npos ||
                   last.find(":after") != std::string::npos || last.find(":first-letter") != std::string::npos ||
                   last.find(":first-line") != std::string::npos;
    if (dynamic) return;
    if (auto p = last.find(':'); p != std::string::npos) last = last.substr(0, p);
    if (auto p = last.find('['); p != std::string::npos) last = last.substr(0, p);
    if (last.empty()) return;

    Rule r;
    r.style = st;
    r.order = rules_.size();
    size_t i = 0;
    auto readIdent = [&]() {
        size_t s = i;
        while (i < last.size() && last[i] != '.' && last[i] != '#') ++i;
        return last.substr(s, i - s);
    };
    if (last[0] != '.' && last[0] != '#') {
        r.tag = str::lower(readIdent());
        if (r.tag == "*") r.tag.clear();
        else r.specificity += 1;
    }
    while (i < last.size()) {
        char k = last[i++];
        std::string ident = readIdent();
        if (k == '.') {
            r.classes.push_back(ident);
            r.specificity += 10;
        } else {
            r.id = ident;
            r.specificity += 100;
        }
    }
    rules_.push_back(std::move(r));
}

void StyleSheet::parse(std::string_view cssIn) {
    std::string css = stripComments(cssIn);
    size_t i = 0;
    int depth = 0;
    while (i < css.size()) {
        // Skip whitespace and stray braces
        while (i < css.size() && (std::isspace(static_cast<unsigned char>(css[i])))) ++i;
        if (i >= css.size()) break;
        if (css[i] == '}') {
            if (depth > 0) --depth;
            ++i;
            continue;
        }
        if (css[i] == '@') {
            auto brace = css.find_first_of("{;", i);
            if (brace == std::string::npos) break;
            std::string at = str::lower(css.substr(i, brace - i));
            if (css[brace] == ';') {
                i = brace + 1;
                continue;
            }
            if (str::startsWith(at, "@media")) {
                // Enter the block; skip amazon-only media queries.
                if (at.find("amzn") != std::string::npos) {
                    int d = 0;
                    size_t j = brace;
                    for (; j < css.size(); ++j) {
                        if (css[j] == '{') ++d;
                        else if (css[j] == '}' && --d == 0) break;
                    }
                    i = j + 1;
                } else {
                    ++depth;
                    i = brace + 1;
                }
                continue;
            }
            // Skip @font-face, @page, @keyframes … blocks entirely.
            int d = 0;
            size_t j = brace;
            for (; j < css.size(); ++j) {
                if (css[j] == '{') ++d;
                else if (css[j] == '}' && --d == 0) break;
            }
            i = j + 1;
            continue;
        }
        auto open = css.find('{', i);
        if (open == std::string::npos) break;
        auto close = css.find('}', open);
        if (close == std::string::npos) break;
        std::string selectors = css.substr(i, open - i);
        Style st = parseDeclarations(std::string_view(css).substr(open + 1, close - open - 1));
        for (auto& s : str::split(selectors, ',')) addRule(s, st);
        i = close + 1;
    }
}

Style StyleSheet::compute(std::string_view tag, const std::vector<std::string>& classes, std::string_view id) const {
    std::vector<const Rule*> matched;
    for (auto& r : rules_) {
        if (!r.tag.empty() && r.tag != tag) continue;
        if (!r.id.empty() && r.id != id) continue;
        bool ok = true;
        for (auto& c : r.classes)
            if (std::find(classes.begin(), classes.end(), c) == classes.end()) {
                ok = false;
                break;
            }
        if (ok) matched.push_back(&r);
    }
    std::stable_sort(matched.begin(), matched.end(), [](const Rule* a, const Rule* b) {
        return a->specificity != b->specificity ? a->specificity < b->specificity : a->order < b->order;
    });
    Style out;
    for (auto* r : matched) out.merge(r->style);
    return out;
}

bool StyleSheet::paragraphsIndented() const {
    int indented = 0, flat = 0;
    for (auto& r : rules_) {
        if (!r.style.textIndent) continue;
        if (r.tag == "p" || r.tag.empty()) (*r.style.textIndent ? indented : flat)++;
    }
    return indented > 0 && indented >= flat;
}

}  // namespace clibooks::css
