#include "image/image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#include <stb_image.h>
#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "util/strings.hpp"

namespace clipub::img {

Protocol parseProtocol(const std::string& s) {
    std::string l = str::lower(s);
    if (l == "kitty") return Protocol::Kitty;
    if (l == "iterm" || l == "iterm2") return Protocol::Iterm;
    if (l == "sixel") return Protocol::Sixel;
    if (l == "blocks" || l == "halfblocks" || l == "unicode") return Protocol::Blocks;
    if (l == "none" || l == "off" || l == "false") return Protocol::None;
    return detectProtocol();
}

std::string protocolName(Protocol p) {
    switch (p) {
        case Protocol::Kitty: return "kitty";
        case Protocol::Iterm: return "iterm";
        case Protocol::Sixel: return "sixel";
        case Protocol::Blocks: return "blocks";
        default: return "none";
    }
}

static std::string env(const char* n) {
    const char* v = std::getenv(n);
    return v ? v : "";
}

Protocol detectProtocol() {
    std::string term = env("TERM"), prog = env("TERM_PROGRAM"), lc = env("LC_TERMINAL");
    // Inside tmux/screen, graphics passthrough is unreliable: use blocks.
    if (!env("TMUX").empty() || str::startsWith(term, "screen") || str::startsWith(term, "tmux")) return Protocol::Blocks;
    if (!env("KITTY_WINDOW_ID").empty() || term == "xterm-kitty" || term == "xterm-ghostty" || prog == "ghostty")
        return Protocol::Kitty;
    if (prog == "WezTerm") return Protocol::Kitty;
    if (prog == "iTerm.app" || lc == "iTerm2") return Protocol::Iterm;
    if (!env("KONSOLE_VERSION").empty()) return Protocol::Kitty;
    if (term == "foot" || str::startsWith(term, "foot") || term == "mlterm" || !env("WT_SESSION").empty())
        return Protocol::Sixel;
    if (prog == "vscode") return Protocol::Sixel;
    return Protocol::Blocks;
}

bool terminalHasTruecolor() {
    std::string ct = str::lower(env("COLORTERM"));
    if (ct == "truecolor" || ct == "24bit") return true;
    std::string prog = env("TERM_PROGRAM");
    if (prog == "iTerm.app" || prog == "WezTerm" || prog == "ghostty" || prog == "vscode") return true;
    if (!env("KITTY_WINDOW_ID").empty()) return true;
    std::string term = env("TERM");
    return term.find("direct") != std::string::npos || term == "xterm-kitty" || term == "xterm-ghostty";
}

// ------------------------------------------------------------------ cache
std::pair<int, int> Cache::size(const std::string& path) {
    if (auto it = sizes_.find(path); it != sizes_.end()) return it->second;
    std::pair<int, int> r{-1, -1};
    if (auto data = loader_(path)) {
        int w, h, c;
        if (stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(data->data()), static_cast<int>(data->size()), &w, &h,
                                  &c))
            r = {w, h};
    }
    sizes_[path] = r;
    return r;
}

const Image* Cache::get(const std::string& path) {
    if (auto it = images_.find(path); it != images_.end()) return it->second.get();
    std::unique_ptr<Image> im;
    if (auto data = loader_(path)) {
        int w, h, c;
        stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data->data()),
                                            static_cast<int>(data->size()), &w, &h, &c, 4);
        if (px) {
            im = std::make_unique<Image>();
            im->w = w;
            im->h = h;
            im->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
            stbi_image_free(px);
        }
    }
    auto* p = im.get();
    images_[path] = std::move(im);
    return p;
}

std::shared_ptr<Image> Cache::scaled(const std::string& path, int tw, int th, uint32_t bg, bool flatten) {
    std::string key = path + "|" + std::to_string(tw) + "x" + std::to_string(th) + "|" + std::to_string(flatten ? bg : 0xFFFFFFFFu);
    if (auto it = scaledCache_.find(key); it != scaledCache_.end()) return it->second;
    const Image* src = get(path);
    if (!src || tw <= 0 || th <= 0) return nullptr;
    auto out = std::make_shared<Image>();
    out->w = tw;
    out->h = th;
    out->rgba.resize(static_cast<size_t>(tw) * th * 4);
    double sx = static_cast<double>(src->w) / tw, sy = static_cast<double>(src->h) / th;
    float br = ((bg >> 16) & 0xFF), bgc = ((bg >> 8) & 0xFF), bb = (bg & 0xFF);
    for (int y = 0; y < th; ++y) {
        int y0 = static_cast<int>(y * sy), y1 = std::max(y0 + 1, static_cast<int>((y + 1) * sy));
        y1 = std::min(y1, src->h);
        for (int x = 0; x < tw; ++x) {
            int x0 = static_cast<int>(x * sx), x1 = std::max(x0 + 1, static_cast<int>((x + 1) * sx));
            x1 = std::min(x1, src->w);
            double acc[4] = {0, 0, 0, 0};
            int n = 0;
            // Box filter with a sampling stride cap for huge downscales.
            int stepY = std::max(1, (y1 - y0) / 8), stepX = std::max(1, (x1 - x0) / 8);
            for (int yy = y0; yy < y1; yy += stepY)
                for (int xx = x0; xx < x1; xx += stepX) {
                    const uint8_t* p = &src->rgba[(static_cast<size_t>(yy) * src->w + xx) * 4];
                    double a = p[3] / 255.0;
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    acc[3] += p[3];
                    ++n;
                }
            uint8_t* d = &out->rgba[(static_cast<size_t>(y) * tw + x) * 4];
            double alpha = n ? acc[3] / n / 255.0 : 0;
            for (int k = 0; k < 3; ++k) {
                double premul = n ? acc[k] / n : 0;
                double v = flatten ? premul + (1.0 - alpha) * (k == 0 ? br : k == 1 ? bgc : bb)
                                   : (alpha > 0 ? premul / alpha : 0);
                d[k] = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
            }
            d[3] = flatten ? 255 : static_cast<uint8_t>(std::clamp(alpha * 255.0, 0.0, 255.0));
        }
    }
    if (scaledCache_.size() > 64) scaledCache_.clear();
    scaledCache_[key] = out;
    return out;
}

std::string Cache::png(const std::string& path, int w, int h) {
    std::string key = path + "|" + std::to_string(w) + "x" + std::to_string(h);
    if (auto it = pngCache_.find(key); it != pngCache_.end()) return it->second;
    auto im = scaled(path, w, h, 0, false);
    if (!im) return {};
    size_t len = 0;
    void* data = tdefl_write_image_to_png_file_in_memory(im->rgba.data(), im->w, im->h, 4, &len);
    std::string out;
    if (data) {
        out.assign(static_cast<const char*>(data), len);
        mz_free(data);
    }
    if (pngCache_.size() > 32) pngCache_.clear();
    pngCache_[key] = out;
    return out;
}

void Cache::clear() {
    images_.clear();
    scaledCache_.clear();
    pngCache_.clear();
}

// ------------------------------------------------------------------ encoders
namespace {

std::string base64(const std::string& in) {
    static const char* b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += b[v >> 18];
        out += b[(v >> 12) & 63];
        out += b[(v >> 6) & 63];
        out += b[v & 63];
    }
    if (i < in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        if (i + 1 < in.size()) v |= uint8_t(in[i + 1]) << 8;
        out += b[v >> 18];
        out += b[(v >> 12) & 63];
        out += (i + 1 < in.size()) ? b[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string moveTo(int row, int col) { return "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H"; }

int xterm256(uint8_t r, uint8_t g, uint8_t b) {
    auto cube = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
    int cr = cube(r), cg = cube(g), cb = cube(b);
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    int cubeIdx = 16 + 36 * cr + 6 * cg + cb;
    int dc = (lv[cr] - r) * (lv[cr] - r) + (lv[cg] - g) * (lv[cg] - g) + (lv[cb] - b) * (lv[cb] - b);
    int avg = (r + g + b) / 3;
    int gi = avg > 238 ? 23 : std::max(0, (avg - 3) / 10);
    int gv = 8 + gi * 10;
    int dg = (gv - r) * (gv - r) + (gv - g) * (gv - g) + (gv - b) * (gv - b);
    return dg < dc ? 232 + gi : cubeIdx;
}

std::string renderBlocks(Cache& cache, const Placement& p, uint32_t bg, bool truecolor) {
    auto im = cache.scaled(p.path, p.cols, p.rows * 2, bg, true);
    if (!im) return {};
    std::string out;
    int first = p.clipTop, last = p.clipTop + (p.visibleRows ? p.visibleRows : p.rows);
    for (int r = first; r < last && r < p.rows; ++r) {
        out += moveTo(p.row + (r - p.clipTop), p.col);
        int prevF = -1, prevB = -1;
        uint32_t prevFt = 0xFFFFFFFF, prevBt = 0xFFFFFFFF;
        for (int c = 0; c < p.cols; ++c) {
            const uint8_t* t = &im->rgba[(static_cast<size_t>(2 * r) * im->w + c) * 4];
            const uint8_t* b = &im->rgba[(static_cast<size_t>(2 * r + 1) * im->w + c) * 4];
            if (truecolor) {
                uint32_t ft = (t[0] << 16) | (t[1] << 8) | t[2];
                uint32_t bt = (b[0] << 16) | (b[1] << 8) | b[2];
                if (ft != prevFt)
                    out += "\x1b[38;2;" + std::to_string(t[0]) + ";" + std::to_string(t[1]) + ";" + std::to_string(t[2]) + "m";
                if (bt != prevBt)
                    out += "\x1b[48;2;" + std::to_string(b[0]) + ";" + std::to_string(b[1]) + ";" + std::to_string(b[2]) + "m";
                prevFt = ft;
                prevBt = bt;
            } else {
                int f = xterm256(t[0], t[1], t[2]), bb = xterm256(b[0], b[1], b[2]);
                if (f != prevF) out += "\x1b[38;5;" + std::to_string(f) + "m";
                if (bb != prevB) out += "\x1b[48;5;" + std::to_string(bb) + "m";
                prevF = f;
                prevB = bb;
            }
            out += "\xe2\x96\x80";  // ▀
        }
        out += "\x1b[0m";
    }
    return out;
}

std::string renderSixel(Cache& cache, const Placement& p, int cellW, int cellH, uint32_t bg) {
    int vis = p.visibleRows ? p.visibleRows : p.rows;
    int pw = p.cols * cellW, ph = p.rows * cellH;
    auto im = cache.scaled(p.path, pw, ph, bg, true);
    if (!im) return {};
    int y0 = p.clipTop * cellH, y1 = std::min(ph, (p.clipTop + vis) * cellH);
    // Quantize to a 6x6x6 cube with 4x4 ordered dithering.
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    int h = y1 - y0;
    std::vector<uint8_t> idx(static_cast<size_t>(pw) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < pw; ++x) {
            const uint8_t* px = &im->rgba[(static_cast<size_t>(y + y0) * pw + x) * 4];
            double d = (bayer[y & 3][x & 3] / 16.0 - 0.5) * 51.0;
            auto q = [&](int v) { return std::clamp(static_cast<int>(std::lround((v + d) / 51.0)), 0, 5); };
            idx[static_cast<size_t>(y) * pw + x] = static_cast<uint8_t>(q(px[0]) * 36 + q(px[1]) * 6 + q(px[2]));
        }
    std::string out = moveTo(p.row, p.col);
    out += "\x1bPq\"1;1;" + std::to_string(pw) + ";" + std::to_string(h);
    for (int i = 0; i < 216; ++i) {
        int r = i / 36, g = (i / 6) % 6, b = i % 6;
        out += "#" + std::to_string(i) + ";2;" + std::to_string(r * 20) + ";" + std::to_string(g * 20) + ";" +
               std::to_string(b * 20);
    }
    for (int band = 0; band < h; band += 6) {
        std::vector<bool> used(216, false);
        for (int y = band; y < std::min(band + 6, h); ++y)
            for (int x = 0; x < pw; ++x) used[idx[static_cast<size_t>(y) * pw + x]] = true;
        bool firstColor = true;
        for (int c = 0; c < 216; ++c) {
            if (!used[c]) continue;
            if (!firstColor) out += '$';
            firstColor = false;
            out += "#" + std::to_string(c);
            char prev = 0;
            int run = 0;
            auto flushRun = [&]() {
                if (!run) return;
                if (run > 3) out += "!" + std::to_string(run) + prev;
                else out.append(static_cast<size_t>(run), prev);
                run = 0;
            };
            for (int x = 0; x < pw; ++x) {
                int bits = 0;
                for (int k = 0; k < 6 && band + k < h; ++k)
                    if (idx[static_cast<size_t>(band + k) * pw + x] == c) bits |= 1 << k;
                char ch = static_cast<char>(63 + bits);
                if (ch == prev) ++run;
                else {
                    flushRun();
                    prev = ch;
                    run = 1;
                }
            }
            flushRun();
        }
        out += '-';
    }
    out += "\x1b\\";
    return out;
}

std::string renderKitty(Cache& cache, const Placement& p, int cellW, int cellH, uint32_t id) {
    int pw = p.cols * cellW, ph = p.rows * cellH;
    std::string png = cache.png(p.path, pw, ph);
    if (png.empty()) return {};
    std::string b64 = base64(png);
    std::string out = moveTo(p.row, p.col);
    int vis = p.visibleRows ? p.visibleRows : p.rows;
    std::string common = "a=T,f=100,q=2,C=1,i=" + std::to_string(id) + ",c=" + std::to_string(p.cols) +
                         ",r=" + std::to_string(vis);
    if (p.clipTop > 0 || vis < p.rows) {
        // Source rectangle crop for partially visible images.
        common += ",y=" + std::to_string(p.clipTop * cellH) + ",h=" + std::to_string(vis * cellH);
    }
    const size_t chunk = 4096;
    for (size_t i = 0; i < b64.size(); i += chunk) {
        bool more = i + chunk < b64.size();
        out += "\x1b_G";
        if (i == 0) out += common + ",";
        out += std::string("m=") + (more ? "1" : "0") + ";" + b64.substr(i, chunk) + "\x1b\\";
    }
    return out;
}

std::string renderIterm(Cache& cache, const Placement& p, int cellW, int cellH) {
    int vis = p.visibleRows ? p.visibleRows : p.rows;
    std::string png;
    if (p.clipTop > 0 || vis < p.rows) {
        // Crop by encoding only the visible part.
        auto im = cache.scaled(p.path, p.cols * cellW, p.rows * cellH, 0, false);
        if (!im) return {};
        int y0 = p.clipTop * cellH, hh = vis * cellH;
        size_t len = 0;
        void* data = tdefl_write_image_to_png_file_in_memory(&im->rgba[static_cast<size_t>(y0) * im->w * 4], im->w, hh, 4, &len);
        if (!data) return {};
        png.assign(static_cast<const char*>(data), len);
        mz_free(data);
    } else {
        png = cache.png(p.path, p.cols * cellW, p.rows * cellH);
    }
    if (png.empty()) return {};
    return moveTo(p.row, p.col) + "\x1b]1337;File=inline=1;size=" + std::to_string(png.size()) +
           ";width=" + std::to_string(p.cols) + ";height=" + std::to_string(vis) +
           ";preserveAspectRatio=0;doNotMoveCursor=1:" + base64(png) + "\x07";
}

}  // namespace

std::string kittyClear() { return "\x1b_Ga=d,d=a,q=2\x1b\\"; }

std::string render(Cache& cache, const Placement& p, Protocol proto, int cellW, int cellH, uint32_t bg,
                   bool truecolor, uint32_t kittyId) {
    if (cellW <= 0) cellW = 8;
    if (cellH <= 0) cellH = 16;
    switch (proto) {
        case Protocol::Blocks: return renderBlocks(cache, p, bg, truecolor);
        case Protocol::Sixel: return renderSixel(cache, p, cellW, cellH, bg);
        case Protocol::Kitty: return renderKitty(cache, p, cellW, cellH, kittyId);
        case Protocol::Iterm: return renderIterm(cache, p, cellW, cellH);
        default: return {};
    }
}

}  // namespace clipub::img
