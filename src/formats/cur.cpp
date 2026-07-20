// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/cur.hpp"

#include <algorithm>
#include <cctype>

#include "formats/lzss.hpp"

namespace olduvai::formats {

namespace {

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::uint16_t read_u16le(const std::vector<std::uint8_t>& raw,
                         std::size_t pos) {
    return static_cast<std::uint16_t>(raw[pos]) |
           (static_cast<std::uint16_t>(raw[pos + 1]) << 8);
}

std::uint32_t read_u32le(const std::vector<std::uint8_t>& raw,
                         std::size_t pos) {
    return static_cast<std::uint32_t>(raw[pos]) |
           (static_cast<std::uint32_t>(raw[pos + 1]) << 8) |
           (static_cast<std::uint32_t>(raw[pos + 2]) << 16) |
           (static_cast<std::uint32_t>(raw[pos + 3]) << 24);
}

}  // namespace

bool is_compressed_name(const std::string& name) {
    const std::string lower = to_lower(name);
    // The one .mat stored raw.
    if (lower == "charset1.mat") return false;
    const auto dot = lower.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lower.substr(dot);
    return ext == ".mat" || ext == ".mdi" || ext == ".pc1";
}

CurArchive::CurArchive(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 2) {
        throw CurError("archive too short for data-offset header");
    }
    const std::uint16_t off_data = read_u16le(raw, 0);

    // Parse the FAT: {u32le size, NUL-terminated name} until off_data or a
    // 4-byte zero terminator.
    std::vector<std::pair<std::string, std::uint32_t>> fat;
    std::size_t pos = 2;
    while (pos < off_data) {
        if (pos + 4 > raw.size()) break;
        const std::uint32_t file_size = read_u32le(raw, pos);
        if (file_size == 0 &&
            raw[pos] == 0 && raw[pos + 1] == 0 &&
            raw[pos + 2] == 0 && raw[pos + 3] == 0) {
            break;  // zero terminator
        }
        pos += 4;
        std::string name;
        while (pos < raw.size() && raw[pos] != 0) {
            name.push_back(static_cast<char>(raw[pos++]));
        }
        if (pos >= raw.size()) {
            throw CurError("unterminated name in FAT");
        }
        ++pos;  // skip NUL
        fat.emplace_back(std::move(name), file_size);
    }

    // Extract back-to-back file data in FAT order.
    std::size_t data_pos = off_data;
    for (const auto& [name, size] : fat) {
        if (data_pos + size > raw.size()) {
            throw CurError("archive truncated: data for entry " + name);
        }
        ArchiveEntry entry;
        entry.name = name;
        entry.stored_data.assign(raw.begin() + static_cast<long>(data_pos),
                                 raw.begin() + static_cast<long>(data_pos + size));
        data_pos += size;
        entry.compressed = is_compressed_name(name);
        entry.data = entry.compressed ? lzss_decompress(entry.stored_data)
                                      : entry.stored_data;
        entries_.push_back(std::move(entry));
    }
}

const ArchiveEntry* CurArchive::find(const std::string& name) const {
    const std::string key = to_lower(name);
    for (const auto& e : entries_) {
        if (to_lower(e.name) == key) return &e;
    }
    return nullptr;
}

bool CurArchive::contains(const std::string& name) const {
    return find(name) != nullptr;
}

const ArchiveEntry& CurArchive::get(const std::string& name) const {
    const ArchiveEntry* e = find(name);
    if (e == nullptr) {
        throw CurError("no such archive entry: " + name);
    }
    return *e;
}

}  // namespace olduvai::formats
