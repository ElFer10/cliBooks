#pragma once
// OS-specific helpers (paths, clipboard, external commands). POSIX first; Windows best-effort.
#include <filesystem>
#include <optional>
#include <string>

namespace clibooks::platform {

std::filesystem::path configDir();  // $XDG_CONFIG_HOME/clibooks or ~/.config/clibooks
std::filesystem::path dataDir();    // $XDG_DATA_HOME/clibooks or ~/.local/share/clibooks

std::optional<std::string> readFile(const std::filesystem::path& p);
bool writeFileAtomic(const std::filesystem::path& p, const std::string& data);

/// Pixel size of a terminal cell, if the terminal reports it (TIOCGWINSZ).
struct CellSize {
    int w = 0, h = 0;
};
CellSize cellPixelSize();

/// Copies text to the system clipboard (pbcopy / wl-copy / xclip / xsel). Returns false if none.
bool copyToClipboard(const std::string& text);
/// OSC 52 sequence (clipboard through the terminal, works over SSH).
std::string osc52(const std::string& text);

/// Opens a URL or file with the desktop handler (open / xdg-open / start).
bool openExternal(const std::string& target);

/// Runs `command` through the shell, writing `input` to its stdin, without waiting.
/// Returns the pid (or -1). Output is discarded.
long spawnWithInput(const std::string& command, const std::string& input);
void killProcess(long pid);
/// Runs a command and captures stdout (blocking, with a size cap).
std::optional<std::string> captureCommand(const std::string& command, size_t maxBytes = 1 << 20);
bool commandExists(const std::string& name);
std::string shellQuote(const std::string& s);

bool isMac();
std::string defaultTtsCommand();
std::string defaultDictCommand();

}  // namespace clibooks::platform
