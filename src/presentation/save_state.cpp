// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/save_state.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace olduvai::presentation {

namespace {

// ── tiny binary archive helpers ──────────────────────────────────────────
template <class T>
void put(std::vector<std::uint8_t>& b, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}

template <class T>
bool get(const std::vector<std::uint8_t>& b, std::size_t& pos, T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (pos + sizeof(T) > b.size()) return false;
    std::memcpy(&v, b.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
}

void put_snaps(std::vector<std::uint8_t>& b,
               const std::vector<EntitySnapshot>& v) {
    put(b, static_cast<std::uint32_t>(v.size()));
    for (const auto& s : v) put(b, s);
}

bool get_snaps(const std::vector<std::uint8_t>& b, std::size_t& pos,
               std::vector<EntitySnapshot>& v) {
    std::uint32_t n = 0;
    if (!get(b, pos, n)) return false;
    if (pos + static_cast<std::size_t>(n) * sizeof(EntitySnapshot) > b.size())
        return false;  // guard against a corrupt/huge count
    v.resize(n);
    for (auto& s : v)
        if (!get(b, pos, s)) return false;
    return true;
}

}  // namespace

EntitySnapshot snapshot_entity(const core::Entity& e) {
    EntitySnapshot s;
    s.obj_type = static_cast<std::int32_t>(e.obj_type);
    s.x = e.x; s.y = e.y; s.state = e.state; s.sprite = e.sprite;
    s.facing_left = e.facing_left; s.active = e.active; s.visible = e.visible;
    s.frame_counter = e.frame_counter; s.respawns = e.respawns;
    s.direction = e.direction; s.state_counter = e.state_counter;
    s.ko_counter = e.ko_counter; s.current_y = e.current_y; s.dy = e.dy;
    s.throw_flag = e.throw_flag; s.throw_x = e.throw_x; s.throw_y = e.throw_y;
    s.fireball_request = e.fireball_request; s.counter = e.counter;
    s.bird_height_idx = e.bird_height_idx; s.bird_anim = e.bird_anim;
    s.draw_dy = e.draw_dy; s.direction_flag = e.direction_flag;
    s.spider_di = e.spider_di;
    return s;
}

void overlay_entity(const EntitySnapshot& s, core::Entity& e) {
    e.x = s.x; e.y = s.y; e.state = s.state; e.sprite = s.sprite;
    e.facing_left = s.facing_left != 0; e.active = s.active != 0;
    e.visible = s.visible != 0; e.frame_counter = s.frame_counter;
    e.respawns = s.respawns; e.direction = s.direction;
    e.state_counter = s.state_counter; e.ko_counter = s.ko_counter;
    e.current_y = s.current_y; e.dy = s.dy; e.throw_flag = s.throw_flag;
    e.throw_x = s.throw_x; e.throw_y = s.throw_y;
    e.fireball_request = s.fireball_request; e.counter = s.counter;
    e.bird_height_idx = s.bird_height_idx; e.bird_anim = s.bird_anim;
    e.draw_dy = s.draw_dy; e.direction_flag = s.direction_flag;
    e.spider_di = s.spider_di;
}

SaveHeader capture_header(const systems::SystemsState& st, int display_level) {
    SaveHeader h;
    h.level = display_level;
    h.player = st.player;
    h.current_screen = st.current_screen;
    h.screen_height = st.screen_height;
    h.frame_counter = st.frame_counter;
    h.timer = st.timer;
    h.timer_counter = st.timer_counter;
    h.cave_flag = st.cave_flag; h.cave_index = st.cave_index;
    h.cave_return_screen = st.cave_return_screen;
    h.cave_return_x = st.cave_return_x; h.cave_return_y = st.cave_return_y;
    h.secret_flag = st.secret_flag; h.secret_index = st.secret_index;
    h.secret_return_screen = st.secret_return_screen;
    h.secret_return_x = st.secret_return_x; h.secret_return_y = st.secret_return_y;
    h.cave_entrance_mask = st.cave_entrance_mask;
    h.glider_active = st.glider_active ? 1 : 0;
    h.glider_x = st.glider_x; h.glider_y = st.glider_y;
    h.halo_flight_flag = st.halo_flight_flag;
    h.score = st.score; h.next_life_score = st.next_life_score;
    h.food_count = st.food_count; h.bonus_trigger = st.bonus_trigger;
    h.l3a_phase_counter = st.l3a_phase_counter;
    h.fireball_flag = st.fireball_flag;
    h.fireball_x = st.fireball_x; h.fireball_y = st.fireball_y;
    h.stone_state = st.stone_state; h.stone_x = st.stone_x; h.stone_y = st.stone_y;
    h.death_halo_active = st.death_halo_active ? 1 : 0;
    h.death_halo_x = st.death_halo_x; h.death_halo_y = st.death_halo_y;
    h.get_ready_counter = st.get_ready_counter;
    h.screen_clear = st.screen_clear_of_monsters ? 1 : 0;
    h.level_complete = st.level_complete ? 1 : 0;
    h.current_level = st.current_level;
    return h;
}

void apply_header(const SaveHeader& h, systems::SystemsState& st) {
    st.player = h.player;
    st.current_screen = h.current_screen;
    st.screen_height = h.screen_height;
    st.frame_counter = h.frame_counter;
    st.timer = h.timer;
    st.timer_counter = h.timer_counter;
    st.cave_flag = h.cave_flag; st.cave_index = h.cave_index;
    st.cave_return_screen = h.cave_return_screen;
    st.cave_return_x = h.cave_return_x; st.cave_return_y = h.cave_return_y;
    st.secret_flag = h.secret_flag; st.secret_index = h.secret_index;
    st.secret_return_screen = h.secret_return_screen;
    st.secret_return_x = h.secret_return_x; st.secret_return_y = h.secret_return_y;
    st.cave_entrance_mask = h.cave_entrance_mask;
    st.glider_active = h.glider_active != 0;
    st.glider_x = h.glider_x; st.glider_y = h.glider_y;
    st.halo_flight_flag = h.halo_flight_flag;
    st.score = h.score; st.next_life_score = h.next_life_score;
    st.food_count = h.food_count; st.bonus_trigger = h.bonus_trigger;
    st.l3a_phase_counter = h.l3a_phase_counter;
    st.fireball_flag = h.fireball_flag;
    st.fireball_x = h.fireball_x; st.fireball_y = h.fireball_y;
    st.stone_state = h.stone_state; st.stone_x = h.stone_x; st.stone_y = h.stone_y;
    st.death_halo_active = h.death_halo_active != 0;
    st.death_halo_x = h.death_halo_x; st.death_halo_y = h.death_halo_y;
    st.get_ready_counter = h.get_ready_counter;
    st.screen_clear_of_monsters = h.screen_clear != 0;
    st.level_complete = h.level_complete != 0;
    st.current_level = h.current_level;
}

std::vector<std::uint8_t> serialize(const SaveState& s) {
    std::vector<std::uint8_t> b;
    put(b, s.hdr);
    // v3 layout tag: rejects saves whose memcpy'd header layout differs
    // (PlayerState/SaveHeader field change, or cross-compiler padding skew).
    put(b, static_cast<std::uint32_t>(sizeof(SaveHeader)));
    put_snaps(b, s.entities);
    put(b, static_cast<std::uint32_t>(s.store.size()));
    for (const auto& se : s.store) {
        put(b, se.key);
        put_snaps(b, se.entities);
    }
    put(b, s.bound_key);
    return b;
}

std::optional<SaveState> deserialize(const std::vector<std::uint8_t>& b) {
    std::size_t pos = 0;
    SaveState s;
    if (!get(b, pos, s.hdr)) return std::nullopt;
    if (std::memcmp(s.hdr.magic, "OLSV", 4) != 0) return std::nullopt;
    // v3 (layout-tagged) is the sole save format. The pre-release v2 (untagged)
    // read branch was dropped for the first public release — there are no
    // public v2 saves to honour, and one format means one code path.
    if (s.hdr.version != 3) return std::nullopt;
    std::uint32_t layout = 0;
    if (!get(b, pos, layout)) return std::nullopt;
    if (layout != sizeof(SaveHeader)) return std::nullopt;
    // Semantic ranges: these fields index arrays downstream (kOrder[level-1],
    // g.tiles.screens[screen]) — a bit-flipped or hand-edited save must fail
    // here, not as UB at the bind site.
    if (s.hdr.level < 1 || s.hdr.level > 7) return std::nullopt;
    if (s.hdr.current_level < 1 || s.hdr.current_level > 7) return std::nullopt;
    if (s.hdr.current_screen < 0 || s.hdr.current_screen > 2999)
        return std::nullopt;
    if (!get_snaps(b, pos, s.entities)) return std::nullopt;
    std::uint32_t nstore = 0;
    if (!get(b, pos, nstore)) return std::nullopt;
    if (nstore > 4096) return std::nullopt;  // sanity
    s.store.resize(nstore);
    for (auto& se : s.store) {
        if (!get(b, pos, se.key)) return std::nullopt;
        if (!get_snaps(b, pos, se.entities)) return std::nullopt;
    }
    if (!get(b, pos, s.bound_key)) return std::nullopt;
    return s;
}

bool save_to_file(const SaveState& s, const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    // Write-to-temp + rename so a crash/full-disk mid-write can never leave
    // a truncated quicksave in place of the previous good one.
    const std::string tmp = path + ".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const auto bytes = serialize(s);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) return false;
    // Close before rename: Windows cannot rename a file that is still open
    // (sharing violation); POSIX allowed it, which masked this.
    f.close();
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

std::optional<SaveState> load_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    return deserialize(bytes);
}

}  // namespace olduvai::presentation
