// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// CUR/VGA archive reader.
//
// Layout: UINT16LE data-offset, then a FAT of {UINT32LE size, NUL-terminated
// ASCII name} records (a 4-byte zero size terminates the FAT early), then
// back-to-back file data in FAT order.  Entries whose extension is .MAT,
// .MDI or .PC1 are LZSS-compressed (exception: CHARSET1.MAT is stored raw).
//
// Behaviour mirrors the validated reference implementation, including the
// zero-size FAT terminator peek and case-insensitive entry lookup.

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace olduvai::formats {

class CurError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ArchiveEntry {
    std::string name;                       // as stored in the FAT
    std::vector<std::uint8_t> data;         // decoded (or raw if stored raw)
    std::vector<std::uint8_t> stored_data;  // exact bytes stored
    bool compressed = false;
};

// True if an entry of this name is LZSS-compressed inside the archive.
bool is_compressed_name(const std::string& name);

class CurArchive {
public:
    // Parse an archive image (entire file contents).
    explicit CurArchive(const std::vector<std::uint8_t>& raw);

    bool contains(const std::string& name) const;
    // Case-insensitive lookup; throws CurError if absent.
    const ArchiveEntry& get(const std::string& name) const;
    const std::vector<ArchiveEntry>& entries() const { return entries_; }

private:
    std::vector<ArchiveEntry> entries_;
    const ArchiveEntry* find(const std::string& name) const;
};

}  // namespace olduvai::formats
