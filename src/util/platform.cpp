#include "util/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "util/strings.hpp"

namespace fs = std::filesystem;

namespace clibooks::platform {

static fs::path homeDir() {
    const char* h = std::getenv("HOME");
#ifdef _WIN32
    if (!h) h = std::getenv("USERPROFILE");
#endif
    return h ? fs::path(h) : fs::current_path();
}

// The app used to be called "clipub": move an old folder to the new name the
// first time it is needed so progress, notes and settings are kept.
static fs::path withLegacy(const fs::path& base) {
    fs::path dir = base / "clibooks";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        fs::path old = base / "clipub";
        if (fs::is_directory(old, ec)) fs::rename(old, dir, ec);
    }
    return dir;
}

fs::path configDir() {
    static const fs::path dir = [] {
        if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return withLegacy(x);
#ifdef _WIN32
        if (const char* a = std::getenv("APPDATA")) return withLegacy(a);
#endif
        return withLegacy(homeDir() / ".config");
    }();
    return dir;
}

fs::path dataDir() {
    static const fs::path dir = [] {
        if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return withLegacy(x);
#ifdef _WIN32
        if (const char* a = std::getenv("LOCALAPPDATA")) return withLegacy(a);
#endif
        return withLegacy(homeDir() / ".local" / "share");
    }();
    return dir;
}

std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

CellSize cellPixelSize() {
#ifndef _WIN32
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row && ws.ws_xpixel && ws.ws_ypixel)
        return {ws.ws_xpixel / ws.ws_col, ws.ws_ypixel / ws.ws_row};
#endif
    return {};
}

bool isMac() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

std::string shellQuote(const std::string& s) {
#ifdef _WIN32
    return "\"" + str::replaceAll(s, "\"", "\\\"") + "\"";
#else
    return "'" + str::replaceAll(s, "'", "'\\''") + "'";
#endif
}

bool commandExists(const std::string& name) {
#ifdef _WIN32
    std::string cmd = "where " + name + " >NUL 2>&1";
#else
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

long spawnWithInput(const std::string& command, const std::string& input) {
#ifndef _WIN32
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        dup2(fds[0], STDIN_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[0]);
    // Write input from a detached writer so a large page never blocks the UI.
    pid_t writer = fork();
    if (writer == 0) {
        size_t off = 0;
        while (off < input.size()) {
            ssize_t n = write(fds[1], input.data() + off, input.size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        _exit(0);
    }
    close(fds[1]);
    if (writer > 0) waitpid(writer, nullptr, 0);
    return pid;
#else
    FILE* p = _popen(command.c_str(), "w");
    if (!p) return -1;
    fwrite(input.data(), 1, input.size(), p);
    _pclose(p);
    return 0;
#endif
}

void killProcess(long pid) {
#ifndef _WIN32
    if (pid > 0) {
        kill(-static_cast<pid_t>(pid), SIGTERM);
        kill(static_cast<pid_t>(pid), SIGTERM);
        waitpid(static_cast<pid_t>(pid), nullptr, WNOHANG);
    }
#endif
}

std::optional<std::string> captureCommand(const std::string& command, size_t maxBytes) {
#ifdef _WIN32
    FILE* p = _popen(command.c_str(), "r");
#else
    FILE* p = popen((command + " 2>/dev/null").c_str(), "r");
#endif
    if (!p) return std::nullopt;
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < maxBytes) out.append(buf, n);
#ifdef _WIN32
    int rc = _pclose(p);
#else
    int rc = pclose(p);
#endif
    if (rc != 0 && out.empty()) return std::nullopt;
    return out;
}

bool copyToClipboard(const std::string& text) {
    const char* candidates[] = {
#ifdef __APPLE__
        "pbcopy",
#elif defined(_WIN32)
        "clip",
#else
        "wl-copy", "xclip -selection clipboard", "xsel --clipboard --input",
#endif
    };
    for (const char* c : candidates) {
        std::string bin = str::split(c, ' ')[0];
        if (!commandExists(bin)) continue;
#ifdef _WIN32
        FILE* p = _popen(c, "w");
#else
        FILE* p = popen(c, "w");
#endif
        if (!p) continue;
        fwrite(text.data(), 1, text.size(), p);
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
        return true;
    }
    return false;
}

std::string osc52(const std::string& text) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    size_t i = 0;
    while (i + 2 < text.size()) {
        uint32_t v = (uint8_t(text[i]) << 16) | (uint8_t(text[i + 1]) << 8) | uint8_t(text[i + 2]);
        enc += b64[(v >> 18) & 63];
        enc += b64[(v >> 12) & 63];
        enc += b64[(v >> 6) & 63];
        enc += b64[v & 63];
        i += 3;
    }
    if (i < text.size()) {
        uint32_t v = uint8_t(text[i]) << 16;
        if (i + 1 < text.size()) v |= uint8_t(text[i + 1]) << 8;
        enc += b64[(v >> 18) & 63];
        enc += b64[(v >> 12) & 63];
        enc += (i + 1 < text.size()) ? b64[(v >> 6) & 63] : '=';
        enc += '=';
    }
    return "\x1b]52;c;" + enc + "\x07";
}

bool openExternal(const std::string& target) {
#ifdef __APPLE__
    std::string cmd = "open " + shellQuote(target) + " >/dev/null 2>&1 &";
#elif defined(_WIN32)
    std::string cmd = "start \"\" " + shellQuote(target);
#else
    std::string cmd = "xdg-open " + shellQuote(target) + " >/dev/null 2>&1 &";
#endif
    return std::system(cmd.c_str()) == 0;
}

std::string defaultTtsCommand() {
    if (isMac()) return "say -f -";
    if (commandExists("espeak-ng")) return "espeak-ng --stdin";
    if (commandExists("espeak")) return "espeak --stdin";
    if (commandExists("spd-say")) return "spd-say -e";
    return "";
}

std::string defaultDictCommand() {
    if (commandExists("sdcv")) return "sdcv -n --utf8-output %s";
    if (commandExists("dict")) return "dict %s";
    if (commandExists("trans")) return "trans -no-ansi %s";
    return "";  // macOS: falls back to opening dict:// in Dictionary.app
}

}  // namespace clibooks::platform
