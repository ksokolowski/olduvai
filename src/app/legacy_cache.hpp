// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One-shot removal of the cache directory earlier versions created.
//
// Up to 0.9.4 the engine kept a cache directory holding a prepare manifest,
// an HD upscale bake and decoded SFX WAVs.  None of that is read any more —
// the manifest was only ever consumed by its own CLI verbs, and the bakes were
// opt-in paths that no longer exist.  The directory is therefore pure litter,
// and it is not small: one machine reached 411 MB of HD bake simply by trying
// the different upscalers.
//
// The same release that stops writing it also removed --purge-cache, so
// nothing else can clean it up.  This does, once, on startup.
//
// DELETE THIS FILE once 0.9.5 is far enough back that nobody upgrades across
// it.  It is migration code, not engine code: it knows the OLD locations only,
// and deliberately does not define where a cache "should" live, because there
// is no longer any such place.

#pragma once

namespace olduvai::app {

// Remove the pre-0.9.5 cache directory if it is still there.  Best-effort and
// silent: a failure here must never stop the game from starting.  Cheap when
// there is nothing to do (one stat on a missing directory).
void remove_legacy_cache_dir();

}  // namespace olduvai::app
