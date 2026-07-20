# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Embed a binary file as an unsigned-char array TU (binary twin of
# embed_text.cmake — that one writes a raw string literal, unusable for
# arbitrary bytes).  Usage:
#   cmake -DIN=<file> -DOUT=<file.cpp> -DSYMBOL=<name> -P embed_binary.cmake
# Emits:  extern const unsigned char <SYMBOL>[];
#         extern const unsigned long <SYMBOL>_len;
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "// Generated from ${IN} by cmake/embed_binary.cmake — do not edit.
extern const unsigned char ${SYMBOL}[] = {
${bytes}
};
extern const unsigned long ${SYMBOL}_len = sizeof(${SYMBOL});
")
