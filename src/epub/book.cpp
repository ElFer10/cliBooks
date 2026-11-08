#include "epub/book.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <functional>

#include "util/strings.hpp"

namespace fs = std::filesystem;

namespace clibooks {
namespace {

std::string local(const char* n) {
    std::string s = n ? n : "";
    if (auto p = s.find(':'); p != std::string::npos) s = s.substr(p + 1);
    return str::lower(s);
}

pugi::xml_node childByLocal(const pugi::xml_node& n, const char* name) {
    for (auto c : n.children())
        if (c.type() == pugi::node_element && local(c.name()) == name) return c;
    return {};
}

std::vector<pugi::xml_node> childrenByLocal(const pugi::xml_node& n, const char* name) {
    std::vector<pugi::xml_node> v;
    for (auto c : n.children())
        if (c.type() == pugi::node_element && local(c.name()) == name) v.push_back(c);
    return v;
}

std::string attrLocal(const pugi::xml_node& n, const char* name) {
    for (auto a : n.attributes())
        if (local(a.name()) == name) return a.value();
    return {};
}

std::string textOf(const pugi::xml_node& n) {
    std::string out;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) out += c.value();
        else if (c.type() == pugi::node_element) out += textOf(c);
    }
    return str::collapseWs(str::decodeEntities(out));
}

bool loadXml(pugi::xml_document& doc, const std::string& data) {
    auto r = doc.load_buffer(data.data(), data.size(), pugi::parse_default);
    if (r) return true;
    std::string fixed = repairHtml(data);
    return static_cast<bool>(doc.load_buffer(fixed.data(), fixed.size(), pugi::parse_default)) ||
           doc.document_element();
}

struct Opf {
    std::string path, dir;
    Metadata meta;
    struct Item {
        std::string href, type, props;
    };
    std::unordered_map<std::string, Item> manifest;
    std::vector<std::pair<std::string, bool>> spine;  // idref, linear
    std::string ncxId, navPath;
    bool fixedLayout = false;
};

bool readOpf(const Archive& ar, Opf& opf, std::string& error) {
    auto container = ar.read("META-INF/container.xml");
    if (!container) {
        error = "missing META-INF/container.xml";
        return false;
    }
    pugi::xml_document cdoc;
    if (!loadXml(cdoc, *container)) {
        error = "invalid container.xml";
        return false;
    }
    for (auto x : cdoc.select_nodes("//*[local-name()='rootfile']")) {
        std::string mt = x.node().attribute("media-type").value();
        if (mt.empty() || mt == "application/oebps-package+xml") {
            opf.path = x.node().attribute("full-path").value();
            break;
        }
    }
    if (opf.path.empty()) {
        error = "no OPF rootfile declared";
        return false;
    }
    auto opfData = ar.read(opf.path);
    if (!opfData) {
        error = "OPF not found: " + opf.path;
        return false;
    }
    opf.dir = str::dirName(opf.path);
    pugi::xml_document doc;
    if (!loadXml(doc, *opfData)) {
        error = "invalid OPF";
        return false;
    }
    auto pkg = doc.document_element();
    std::string uidRef = pkg.attribute("unique-identifier").value();
    auto md = childByLocal(pkg, "metadata");
    std::string coverId;
    for (auto c : md.children()) {
        if (c.type() != pugi::node_element) continue;
        std::string n = local(c.name());
        if (n == "title" && opf.meta.title.empty()) opf.meta.title = textOf(c);
        else if (n == "creator") opf.meta.authors.push_back(textOf(c));
        else if (n == "language" && opf.meta.language.empty()) opf.meta.language = textOf(c);
        else if (n == "publisher") opf.meta.publisher = textOf(c);
        else if (n == "description") opf.meta.description = textOf(c);
        else if (n == "date" && opf.meta.date.empty()) opf.meta.date = textOf(c);
        else if (n == "rights") opf.meta.rights = textOf(c);
        else if (n == "identifier") {
            if (opf.meta.identifier.empty() || attrLocal(c, "id") == uidRef) opf.meta.identifier = textOf(c);
        } else if (n == "meta") {
            if (attrLocal(c, "name") == "cover") coverId = attrLocal(c, "content");
            if (attrLocal(c, "property") == "rendition:layout" && textOf(c) == "pre-paginated") opf.fixedLayout = true;
        }
    }
    for (auto item : childrenByLocal(childByLocal(pkg, "manifest"), "item")) {
        Opf::Item it;
        it.href = str::resolvePath(opf.dir, item.attribute("href").value());
        it.type = item.attribute("media-type").value();
        it.props = item.attribute("properties").value();
        std::string id = item.attribute("id").value();
        if (it.props.find("cover-image") != std::string::npos) opf.meta.coverImage = it.href;
        if (it.props.find("nav") != std::string::npos) opf.navPath = it.href;
        opf.manifest[id] = it;
    }
    if (opf.meta.coverImage.empty() && !coverId.empty()) {
        if (auto it = opf.manifest.find(coverId); it != opf.manifest.end()) opf.meta.coverImage = it->second.href;
    }
    auto spine = childByLocal(pkg, "spine");
    opf.ncxId = spine.attribute("toc").value();
    for (auto ir : childrenByLocal(spine, "itemref"))
        opf.spine.emplace_back(ir.attribute("idref").value(), std::string(ir.attribute("linear").value()) != "no");
    if (opf.meta.title.empty()) opf.meta.title = fs::path(opf.path).stem().string();
    return true;
}

bool hasDrm(const Archive& ar) {
    if (ar.exists("META-INF/sinf.xml") || ar.exists("META-INF/rights.xml")) return true;
    auto enc = ar.read("META-INF/encryption.xml");
    if (!enc) return false;
    pugi::xml_document doc;
    if (!loadXml(doc, *enc)) return false;
    for (auto m : doc.select_nodes("//*[local-name()='EncryptionMethod']")) {
        std::string alg = m.node().attribute("Algorithm").value();
        // Font obfuscation is harmless for us (we never use embedded fonts).
        if (alg == "http://www.idpf.org/2008/embedding" || alg == "http://ns.adobe.com/pdf/enc#RC") continue;
        // Check what is being encrypted: if it is content (xhtml), the book is unreadable.
        auto data = m.node().parent();
        auto ref = data.select_node(".//*[local-name()='CipherReference']");
        std::string uri = ref ? ref.node().attribute("URI").value() : "";
        std::string l = str::lower(uri);
        if (l.find(".otf") != std::string::npos || l.find(".ttf") != std::string::npos ||
            l.find(".woff") != std::string::npos)
            continue;
        return true;
    }
    return false;
}

}  // namespace

std::string Metadata::authorLine() const {
    std::string s;
    for (size_t i = 0; i < authors.size(); ++i) {
        if (i) s += ", ";
        s += authors[i];
    }
    return s;
}

std::optional<Metadata> Book::peek(const fs::path& file) {
    std::string err;
    auto ar = Archive::open(file, err);
    if (!ar) return std::nullopt;
    Opf opf;
    if (!readOpf(*ar, opf, err)) return std::nullopt;
    return opf.meta;
}

std::unique_ptr<Book> Book::open(const fs::path& file, std::string& error, const ParseOptions& opts) {
    auto b = std::unique_ptr<Book>(new Book());
    b->file_ = file;
    b->archive_ = Archive::open(file, error);
    if (!b->archive_) return nullptr;
    if (!b->load(error, opts)) return nullptr;
    return b;
}

const css::StyleSheet* Book::stylesheet(const std::string& path) {
    if (auto it = cssCache_.find(path); it != cssCache_.end()) return it->second.get();
    auto sheet = std::make_unique<css::StyleSheet>();
    if (auto data = archive_->read(path)) sheet->parse(*data);
    auto* p = sheet.get();
    cssCache_[path] = std::move(sheet);
    return p;
}

bool Book::load(std::string& error, const ParseOptions& opts) {
    Opf opf;
    if (!readOpf(*archive_, opf, error)) return false;
    if (hasDrm(*archive_)) {
        error = "this book is DRM-protected (FairPlay/Adobe) and cannot be read";
        return false;
    }
    meta_ = opf.meta;
    opfDir_ = opf.dir;
    fixedLayout_ = opf.fixedLayout;
    std::string idSource = meta_.identifier.empty() ? fs::absolute(file_).string() : meta_.identifier + "|" + meta_.title;
    id_ = str::hex64(str::fnv1a(idSource));

    StyleLoader loader = [this](const std::string& p) { return stylesheet(p); };
    for (auto& [idref, linear] : opf.spine) {
        auto it = opf.manifest.find(idref);
        if (it == opf.manifest.end()) continue;
        const auto& item = it->second;
        Chapter ch;
        if (str::startsWith(item.type, "image/")) {
            // Image directly in the spine (comics / picture books).
            ch.path = item.href;
            Block b;
            b.kind = BlockKind::Image;
            b.image = item.href;
            b.align = css::Align::Center;
            ch.blocks.push_back(b);
            ch.text = "\n";
        } else {
            auto data = archive_->read(item.href);
            if (!data) continue;
            ch = parseChapter(*data, item.href, loader, opts);
        }
        ch.linear = linear;
        spineIndex_[item.href] = static_cast<int>(chapters_.size());
        chapters_.push_back(std::move(ch));
    }
    if (chapters_.empty()) {
        error = "the book has no readable content";
        return false;
    }
    size_t acc = 0;
    for (auto& c : chapters_) {
        chapterStart_.push_back(acc);
        acc += c.text.size();
    }
    totalLength_ = acc;

    if (!opf.navPath.empty()) parseNav(opf.navPath);
    if (toc_.empty() && !opf.ncxId.empty()) {
        if (auto it = opf.manifest.find(opf.ncxId); it != opf.manifest.end()) parseNcx(it->second.href);
    }
    if (toc_.empty()) {
        for (auto& [id, item] : opf.manifest)
            if (item.type == "application/x-dtbncx+xml") {
                parseNcx(item.href);
                break;
            }
    }
    if (toc_.empty()) {
        // Synthesize a TOC from chapter headings.
        for (size_t i = 0; i < chapters_.size(); ++i) {
            if (chapters_[i].title.empty()) continue;
            TocEntry e;
            e.title = chapters_[i].title;
            e.pos = {static_cast<int>(i), 0};
            e.resolved = true;
            toc_.push_back(e);
        }
    }
    for (auto& e : toc_) {
        if (e.resolved) continue;
        if (auto p = resolve(e.href)) {
            e.pos = *p;
            e.resolved = true;
        }
    }
    return true;
}

void Book::parseNav(const std::string& navPath) {
    auto data = archive_->read(navPath);
    if (!data) return;
    pugi::xml_document doc;
    if (!loadXml(doc, *data)) return;
    std::string dir = str::dirName(navPath);
    for (auto x : doc.select_nodes("//*[local-name()='nav']")) {
        auto nav = x.node();
        std::string type;
        for (auto a : nav.attributes())
            if (local(a.name()) == "type") type = a.value();
        bool isToc = type.find("toc") != std::string::npos;
        bool isPages = type.find("page-list") != std::string::npos;
        if (!isToc && !isPages) continue;
        std::function<void(const pugi::xml_node&, int)> walkOl = [&](const pugi::xml_node& ol, int depth) {
            for (auto li : childrenByLocal(ol, "li")) {
                pugi::xml_node a = childByLocal(li, "a");
                if (!a) a = childByLocal(li, "span");
                std::string title = a ? textOf(a) : "";
                std::string href = a ? a.attribute("href").value() : "";
                std::string target;
                if (!href.empty()) {
                    auto [file, frag] = str::splitFragment(href);
                    target = str::resolvePath(dir, file) + (frag.empty() ? "" : "#" + frag);
                }
                if (isToc) {
                    TocEntry e;
                    e.title = title;
                    e.depth = depth;
                    e.href = target;
                    if (!title.empty()) toc_.push_back(e);
                } else if (!target.empty()) {
                    if (auto p = resolve(target)) pageList_.push_back({title, *p});
                }
                if (auto sub = childByLocal(li, "ol")) walkOl(sub, depth + 1);
            }
        };
        if (auto ol = nav.select_node(".//*[local-name()='ol']")) walkOl(ol.node(), 0);
    }
}

void Book::parseNcx(const std::string& ncxPath) {
    auto data = archive_->read(ncxPath);
    if (!data) return;
    pugi::xml_document doc;
    if (!loadXml(doc, *data)) return;
    std::string dir = str::dirName(ncxPath);
    std::function<void(const pugi::xml_node&, int)> walk = [&](const pugi::xml_node& parent, int depth) {
        for (auto np : childrenByLocal(parent, "navpoint")) {
            TocEntry e;
            e.depth = depth;
            if (auto lbl = childByLocal(np, "navlabel")) e.title = textOf(childByLocal(lbl, "text"));
            if (auto content = childByLocal(np, "content")) {
                auto [file, frag] = str::splitFragment(content.attribute("src").value());
                e.href = str::resolvePath(dir, file) + (frag.empty() ? "" : "#" + frag);
            }
            if (!e.title.empty()) toc_.push_back(e);
            walk(np, depth + 1);
        }
    };
    if (auto nm = doc.select_node("//*[local-name()='navMap']")) walk(nm.node(), 0);
    if (pageList_.empty()) {
        for (auto pt : doc.select_nodes("//*[local-name()='pageTarget']")) {
            auto n = pt.node();
            std::string label;
            if (auto lbl = childByLocal(n, "navlabel")) label = textOf(childByLocal(lbl, "text"));
            if (label.empty()) label = n.attribute("value").value();
            if (auto content = childByLocal(n, "content")) {
                auto [file, frag] = str::splitFragment(content.attribute("src").value());
                if (auto p = resolve(str::resolvePath(dir, file) + (frag.empty() ? "" : "#" + frag)))
                    pageList_.push_back({label, *p});
            }
        }
    }
}

std::optional<std::string> Book::resource(const std::string& p) const { return archive_->read(p); }

std::optional<Position> Book::resolve(const std::string& target) const {
    auto [file, frag] = str::splitFragment(target);
    auto it = spineIndex_.find(file);
    if (it == spineIndex_.end()) {
        // Case-insensitive fallback
        for (auto& [k, v] : spineIndex_)
            if (str::iequals(k, file)) {
                it = spineIndex_.find(k);
                break;
            }
        if (it == spineIndex_.end()) return std::nullopt;
    }
    Position p{it->second, 0};
    if (!frag.empty()) {
        const auto& ch = chapters_[static_cast<size_t>(it->second)];
        if (auto a = ch.anchors.find(frag); a != ch.anchors.end()) p.offset = a->second;
    }
    return p;
}

int Book::tocIndexAt(const Position& p) const {
    int best = -1;
    for (size_t i = 0; i < toc_.size(); ++i) {
        if (!toc_[i].resolved) continue;
        if (toc_[i].pos <= p) {
            if (best < 0 || toc_[static_cast<size_t>(best)].pos <= toc_[i].pos) best = static_cast<int>(i);
        }
    }
    return best;
}

std::string Book::sectionTitle(const Position& p) const {
    int i = tocIndexAt(p);
    if (i >= 0) return toc_[static_cast<size_t>(i)].title;
    if (p.chapter >= 0 && static_cast<size_t>(p.chapter) < chapters_.size())
        return chapters_[static_cast<size_t>(p.chapter)].title;
    return {};
}

size_t Book::absoluteOffset(const Position& p) const {
    if (chapterStart_.empty()) return 0;
    size_t c = std::min(static_cast<size_t>(std::max(p.chapter, 0)), chapterStart_.size() - 1);
    return chapterStart_[c] + p.offset;
}

Position Book::fromAbsolute(size_t abs) const {
    if (chapterStart_.empty()) return {};
    auto it = std::upper_bound(chapterStart_.begin(), chapterStart_.end(), abs);
    size_t c = static_cast<size_t>(std::distance(chapterStart_.begin(), it)) - 1;
    return {static_cast<int>(c), abs - chapterStart_[c]};
}

std::optional<std::string> Book::footnoteText(const std::string& target) const {
    auto [file, frag] = str::splitFragment(target);
    auto pos = resolve(target);
    if (!pos) return std::nullopt;
    const auto& ch = chapters_[static_cast<size_t>(pos->chapter)];
    if (!frag.empty()) {
        if (auto n = ch.notes.find(frag); n != ch.notes.end()) return n->second;
        if (ch.anchors.find(frag) == ch.anchors.end()) return std::nullopt;
    } else {
        return std::nullopt;
    }
    // Text from the anchor to the end of its block; include following blocks if very short.
    auto blockAt = [&](size_t off) -> int {
        for (size_t i = 0; i < ch.blocks.size(); ++i)
            if (off < ch.blocks[i].offset + ch.blocks[i].length + 1) return static_cast<int>(i);
        return -1;
    };
    int bi = blockAt(pos->offset);
    if (bi < 0) return std::nullopt;
    std::string out;
    size_t start = pos->offset;
    for (int k = 0; k < 4 && static_cast<size_t>(bi + k) < ch.blocks.size(); ++k) {
        const auto& b = ch.blocks[static_cast<size_t>(bi + k)];
        if (k > 0 && b.kind == BlockKind::Heading) break;
        size_t from = std::max(start, b.offset);
        size_t to = b.offset + b.length;
        if (from < to) {
            if (!out.empty()) out += "\n";
            out += ch.text.substr(from, to - from);
        }
        if (out.size() > 40) break;
    }
    out = std::string(str::trim(out));
    if (out.empty()) return std::nullopt;
    return out;
}

}  // namespace clibooks
