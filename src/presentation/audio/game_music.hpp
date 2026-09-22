// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Playing one of the game's MDI tracks by entry name.
//
// Five call sites used to open FILESA.CUR / FILESB.CUR themselves, find the
// entry and derive its track id: the level music, the boss music, the tally
// and the title intro (twice).  One of them lower-cased the name by hand for
// mdi_track_id, the others spelled the lower-case name out a second time.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace olduvai::presentation {

class SdlAudio;

// Play `raw_mdi` as the track `name` (any case, e.g. "ROCKY.MDI").  No-op when
// `audio` or `raw_mdi` is null, or the audio device has no music backend.
void play_mdi(SdlAudio* audio, const std::vector<std::uint8_t>* raw_mdi,
              const std::string& name);

// Find `name` in the user's game archives (prepare::GameArchives — all four
// are read, measured at 9.3 ms) and play it.  Same no-op cases as play_mdi,
// plus a missing entry.
void play_game_music(SdlAudio* audio, const std::filesystem::path& game_dir,
                     const std::string& name);

// The score tally's music: fade the level or boss track out (MDI_FadeStop
// 1f75:00e4), then BONUS.MDI (1f75:01bb; FUN_270a_01b4
// play_music(MUSIC_BONUS)).  The BONUSBUZ.MDI variant (DS:0x8db5=='I') is not
// played.
void play_tally_music(SdlAudio* audio, const std::filesystem::path& game_dir);

}  // namespace olduvai::presentation
