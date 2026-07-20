// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/viewer.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"
#include "presentation/window_util.hpp"

#include <cstdio>
#include <fstream>
#include <optional>
#include <vector>

#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"

namespace olduvai::presentation {

namespace {

using formats::CurArchive;
using formats::MatFile;
using formats::Pc1Image;
using formats::Rgb;
using formats::Sprite;
using formats::SpriteFormat;

constexpr int kWinW = 960;   // 320 × 3
constexpr int kWinH = 600;   // 200 × 3

// Standard EGA 16-colour palette — viewer fallback for sprite previews
// (real gameplay palettes come from the level backgrounds).
constexpr Rgb kEga[16] = {
    {0, 0, 0},       {0, 0, 170},     {0, 170, 0},     {0, 170, 170},
    {170, 0, 0},     {170, 0, 170},   {170, 85, 0},    {170, 170, 170},
    {85, 85, 85},    {85, 85, 255},   {85, 255, 85},   {85, 255, 255},
    {255, 85, 85},   {255, 85, 255},  {255, 255, 85},  {255, 255, 255},
};

std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

struct Pc1Item {
    std::string label;
    Pc1Image image;
};

struct MatItem {
    std::string label;
    MatFile mat;
};

struct Rgba {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;  // w*h*4
};

Rgba pc1_to_rgba(const Pc1Image& img) {
    Rgba out{img.width, img.height, {}};
    out.px.resize(static_cast<std::size_t>(img.width) * img.height * 4);
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        const std::uint8_t idx = img.pixels[i];
        const Rgb c = idx < img.palette.size() ? img.palette[idx] : Rgb{};
        out.px[i * 4] = c.r;
        out.px[i * 4 + 1] = c.g;
        out.px[i * 4 + 2] = c.b;
        out.px[i * 4 + 3] = 255;
    }
    return out;
}

Rgba sprite_to_rgba(const Sprite& s) {
    Rgba out{s.width, s.height, {}};
    out.px.resize(static_cast<std::size_t>(s.width) * s.height * 4);
    const auto pixels = s.decode_indexed();
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (!pixels[i].opaque) continue;
        const Rgb c = kEga[pixels[i].color & 0x0F];
        out.px[i * 4] = c.r;
        out.px[i * 4 + 1] = c.g;
        out.px[i * 4 + 2] = c.b;
        out.px[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace

int run_viewer(const ViewerOptions& opts) {
    // ── load assets ──
    std::vector<Pc1Item> pc1s;
    std::vector<MatItem> mats;
    for (const char* a : {"FILESA.CUR", "FILESB.CUR",
                          "FILESA.VGA", "FILESB.VGA"}) {
        const auto path = opts.game_dir / a;
        if (!std::filesystem::exists(path)) continue;
        CurArchive ar(slurp(path));
        for (const auto& e : ar.entries()) {
            if (e.name.size() < 4) continue;
            std::string ext = e.name.substr(e.name.size() - 4);
            for (auto& ch : ext) ch = static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch)));
            const std::string label = std::string(a) + "/" + e.name;
            if (ext == ".PC1") {
                pc1s.push_back({label, formats::parse_pc1(e.data)});
            } else if (ext == ".MAT") {
                mats.push_back({label, MatFile(e.data, e.name)});
            }
        }
    }
    if (pc1s.empty() && mats.empty()) {
        std::fprintf(stderr, "viewer: no game images found in %s\n",
                     opts.game_dir.string().c_str());
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "viewer: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        "Olduvai asset viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWinW, kWinH, SDL_WINDOW_SHOWN);
    if (win != nullptr) set_window_icon(win);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (ren == nullptr) ren = SDL_CreateRenderer(win, -1, 0);

    bool pc1_mode = !pc1s.empty();
    std::size_t pc1_idx = 0, mat_idx = 0, sprite_idx = 0;
    int frames_left = opts.frames;
    bool running = true;

    while (running) {
        // ── compose current item ──
        Rgba rgba;
        std::string title;
        if (pc1_mode && !pc1s.empty()) {
            const auto& it = pc1s[pc1_idx];
            rgba = pc1_to_rgba(it.image);
            title = it.label;
        } else if (!mats.empty()) {
            const auto& it = mats[mat_idx];
            if (!it.mat.sprites().empty()) {
                sprite_idx %= it.mat.sprites().size();
                rgba = sprite_to_rgba(it.mat.sprites()[sprite_idx]);
            }
            title = it.label + " [" + std::to_string(sprite_idx) + "/" +
                    std::to_string(it.mat.sprites().size()) + "]";
        }
        SDL_SetWindowTitle(win, ("Olduvai — " + title).c_str());

        SDL_SetRenderDrawColor(ren, 24, 24, 32, 255);
        SDL_RenderClear(ren);
        if (rgba.w > 0) {
            SDL_Texture* tex = SDL_CreateTexture(
                ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                rgba.w, rgba.h);
            SDL_UpdateTexture(tex, nullptr, rgba.px.data(), rgba.w * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            // Integer-scale, centred.
            const int scale = std::max(1, std::min(kWinW / rgba.w,
                                                   kWinH / rgba.h));
            const SDL_Rect dst = {(kWinW - rgba.w * scale) / 2,
                                  (kWinH - rgba.h * scale) / 2,
                                  rgba.w * scale, rgba.h * scale};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }

        if (!opts.screenshot.empty()) {
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(
                0, kWinW, kWinH, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                 shot->pixels, shot->pitch);
            save_surface_image(shot, opts.screenshot);
            SDL_FreeSurface(shot);
            running = false;
        }
        SDL_RenderPresent(ren);

        if (frames_left > 0 && --frames_left == 0) running = false;

        SDL_Event ev;
        while (running && SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type != SDL_KEYDOWN) continue;
            switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q: running = false; break;
                case SDLK_TAB: pc1_mode = !pc1_mode; break;
                case SDLK_RIGHT:
                    if (pc1_mode) pc1_idx = (pc1_idx + 1) % pc1s.size();
                    else ++sprite_idx;
                    break;
                case SDLK_LEFT:
                    if (pc1_mode) pc1_idx = (pc1_idx + pc1s.size() - 1) % pc1s.size();
                    else if (sprite_idx > 0) --sprite_idx;
                    break;
                case SDLK_DOWN:
                    if (!pc1_mode && !mats.empty()) {
                        mat_idx = (mat_idx + 1) % mats.size();
                        sprite_idx = 0;
                    }
                    break;
                case SDLK_UP:
                    if (!pc1_mode && !mats.empty()) {
                        mat_idx = (mat_idx + mats.size() - 1) % mats.size();
                        sprite_idx = 0;
                    }
                    break;
                default: break;
            }
        }
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace olduvai::presentation
