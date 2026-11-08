#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clibooks {

/// Read-only access to the EPUB container. Supports zipped .epub files and
/// unpacked EPUB folders (Books also accepts both: org.idpf.epub-container / -folder).
class Archive {
public:
    static std::unique_ptr<Archive> open(const std::filesystem::path& path, std::string& error);
    virtual ~Archive() = default;
    virtual std::optional<std::string> read(const std::string& name) const = 0;
    virtual bool exists(const std::string& name) const = 0;
    virtual std::vector<std::string> list() const = 0;
};

}  // namespace clibooks
