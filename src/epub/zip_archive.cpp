#include "epub/zip_archive.hpp"

#include <miniz.h>

#include <cstring>
#include <unordered_map>

#include "util/platform.hpp"
#include "util/strings.hpp"

namespace fs = std::filesystem;

namespace clibooks {
namespace {

class ZipArchive final : public Archive {
public:
    bool init(const fs::path& path, std::string& error) {
        std::memset(&zip_, 0, sizeof zip_);
        if (!mz_zip_reader_init_file(&zip_, path.string().c_str(), 0)) {
            error = "not a valid ZIP/EPUB file";
            return false;
        }
        open_ = true;
        mz_uint n = mz_zip_reader_get_num_files(&zip_);
        for (mz_uint i = 0; i < n; ++i) {
            char name[1024];
            mz_zip_reader_get_filename(&zip_, i, name, sizeof name);
            index_[name] = i;
            lowerIndex_[str::lower(name)] = i;
        }
        return true;
    }
    ~ZipArchive() override {
        if (open_) mz_zip_reader_end(&zip_);
    }
    std::optional<std::string> read(const std::string& name) const override {
        auto idx = find(name);
        if (!idx) return std::nullopt;
        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip_, *idx, &size, 0);
        if (!p) return std::nullopt;
        std::string out(static_cast<const char*>(p), size);
        mz_free(p);
        return out;
    }
    bool exists(const std::string& name) const override { return find(name).has_value(); }
    std::vector<std::string> list() const override {
        std::vector<std::string> v;
        for (auto& [k, _] : index_) v.push_back(k);
        return v;
    }

private:
    std::optional<mz_uint> find(const std::string& name) const {
        if (auto it = index_.find(name); it != index_.end()) return it->second;
        // Many EPUBs have case mismatches between manifest and actual names.
        if (auto it = lowerIndex_.find(str::lower(name)); it != lowerIndex_.end()) return it->second;
        return std::nullopt;
    }
    mutable mz_zip_archive zip_{};
    bool open_ = false;
    std::unordered_map<std::string, mz_uint> index_, lowerIndex_;
};

class DirArchive final : public Archive {
public:
    explicit DirArchive(fs::path root) : root_(std::move(root)) {}
    std::optional<std::string> read(const std::string& name) const override {
        auto p = safe(name);
        if (!p) return std::nullopt;
        return platform::readFile(*p);
    }
    bool exists(const std::string& name) const override {
        auto p = safe(name);
        return p && fs::is_regular_file(*p);
    }
    std::vector<std::string> list() const override {
        std::vector<std::string> v;
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(root_, ec))
            if (e.is_regular_file()) v.push_back(fs::relative(e.path(), root_).generic_string());
        return v;
    }

private:
    std::optional<fs::path> safe(const std::string& name) const {
        // Reject paths escaping the book folder.
        auto norm = str::resolvePath("", name);
        if (norm.empty() || str::startsWith(norm, "..")) return std::nullopt;
        return root_ / fs::path(norm);
    }
    fs::path root_;
};

}  // namespace

std::unique_ptr<Archive> Archive::open(const fs::path& path, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        if (!fs::exists(path / "META-INF" / "container.xml")) {
            error = "folder is not an unpacked EPUB (missing META-INF/container.xml)";
            return nullptr;
        }
        return std::make_unique<DirArchive>(path);
    }
    if (!fs::exists(path, ec)) {
        error = "file not found";
        return nullptr;
    }
    auto z = std::make_unique<ZipArchive>();
    if (!z->init(path, error)) return nullptr;
    return z;
}

}  // namespace clibooks
