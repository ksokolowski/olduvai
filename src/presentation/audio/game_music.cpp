// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/audio/game_music.hpp"

#include "formats/mdi.hpp"
#include "prepare/game_archives.hpp"
#include "presentation/audio/audio.hpp"

#include <cctype>

namespace olduvai::presentation {

void play_mdi(SdlAudio* audio, const std::vector<std::uint8_t>* raw_mdi,
              const std::string& name) {
    if (audio == nullptr || raw_mdi == nullptr || !audio->music_available())
        return;
    std::string lower = name;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    audio->play_music(*raw_mdi, formats::mdi_track_id(lower));
}

void play_game_music(SdlAudio* audio, const std::filesystem::path& game_dir,
                     const std::string& name) {
    if (audio == nullptr || !audio->music_available()) return;
    const prepare::GameArchives archives(game_dir);
    play_mdi(audio, archives.entry(name), name);
}

void play_tally_music(SdlAudio* audio, const std::filesystem::path& game_dir) {
    if (audio == nullptr || !audio->music_available()) return;
    // Fade, don't cut: the EXE fades the level track before BONUS.MDI.
    audio->fade_out_music();
    play_game_music(audio, game_dir, "BONUS.MDI");
}

}  // namespace olduvai::presentation
