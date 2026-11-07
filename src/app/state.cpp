#include "app/state.hpp"

#include <toml.hpp>

#include <chrono>
#include <sstream>

#include "util/platform.hpp"

namespace fs = std::filesystem;

namespace clipub {

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

StateStore::StateStore() : dir_(platform::dataDir() / "books") {}

namespace {

BookState fromToml(const toml::table& t) {
    BookState s;
    s.id = t["id"].value_or(std::string());
    s.path = t["path"].value_or(std::string());
    s.title = t["title"].value_or(std::string());
    s.author = t["author"].value_or(std::string());
    s.pos.chapter = static_cast<int>(t["position"]["chapter"].value_or(int64_t{0}));
    s.pos.offset = static_cast<size_t>(t["position"]["offset"].value_or(int64_t{0}));
    s.progress = t["progress"].value_or(0.0);
    s.lastOpened = t["last_opened"].value_or(int64_t{0});
    s.readingSeconds = t["reading_seconds"].value_or(int64_t{0});
    if (auto arr = t["bookmarks"].as_array()) {
        for (auto& n : *arr) {
            auto* b = n.as_table();
            if (!b) continue;
            Bookmark bm;
            bm.pos.chapter = static_cast<int>((*b)["chapter"].value_or(int64_t{0}));
            bm.pos.offset = static_cast<size_t>((*b)["offset"].value_or(int64_t{0}));
            bm.created = (*b)["created"].value_or(int64_t{0});
            bm.label = (*b)["label"].value_or(std::string());
            s.bookmarks.push_back(bm);
        }
    }
    if (auto arr = t["highlights"].as_array()) {
        for (auto& n : *arr) {
            auto* h = n.as_table();
            if (!h) continue;
            Highlight hl;
            hl.chapter = static_cast<int>((*h)["chapter"].value_or(int64_t{0}));
            hl.start = static_cast<size_t>((*h)["start"].value_or(int64_t{0}));
            hl.end = static_cast<size_t>((*h)["end"].value_or(int64_t{0}));
            hl.style = static_cast<int>((*h)["style"].value_or(int64_t{0}));
            hl.note = (*h)["note"].value_or(std::string());
            hl.text = (*h)["text"].value_or(std::string());
            hl.created = (*h)["created"].value_or(int64_t{0});
            if (hl.end > hl.start) s.highlights.push_back(hl);
        }
    }
    return s;
}

}  // namespace

std::optional<BookState> StateStore::load(const std::string& id) const {
    auto data = platform::readFile(dir_ / (id + ".toml"));
    if (!data) return std::nullopt;
    auto res = toml::parse(*data);
    if (!res) return std::nullopt;
    return fromToml(res.table());
}

bool StateStore::save(const BookState& s) const {
    toml::table t{
        {"id", s.id},
        {"path", s.path},
        {"title", s.title},
        {"author", s.author},
        {"progress", s.progress},
        {"last_opened", s.lastOpened},
        {"reading_seconds", s.readingSeconds},
        {"position", toml::table{{"chapter", s.pos.chapter}, {"offset", static_cast<int64_t>(s.pos.offset)}}},
    };
    toml::array bms;
    for (auto& b : s.bookmarks)
        bms.push_back(toml::table{{"chapter", b.pos.chapter},
                                  {"offset", static_cast<int64_t>(b.pos.offset)},
                                  {"created", b.created},
                                  {"label", b.label}});
    t.insert("bookmarks", bms);
    toml::array hls;
    for (auto& h : s.highlights)
        hls.push_back(toml::table{{"chapter", h.chapter},
                                  {"start", static_cast<int64_t>(h.start)},
                                  {"end", static_cast<int64_t>(h.end)},
                                  {"style", h.style},
                                  {"note", h.note},
                                  {"text", h.text},
                                  {"created", h.created}});
    t.insert("highlights", hls);
    std::ostringstream ss;
    ss << t << "\n";
    return platform::writeFileAtomic(dir_ / (s.id + ".toml"), ss.str());
}

std::vector<BookState> StateStore::all() const {
    std::vector<BookState> v;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir_, ec)) {
        if (e.path().extension() != ".toml") continue;
        if (auto s = load(e.path().stem().string())) v.push_back(*s);
    }
    return v;
}

bool StateStore::remove(const std::string& id) const {
    std::error_code ec;
    return fs::remove(dir_ / (id + ".toml"), ec);
}

}  // namespace clipub
