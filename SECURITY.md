# Security policy

Olduvai's core job is parsing binary game files the user supplies
(CUR/LZSS archives, MAT sprites, PC1 images, DUR collision, MDI music,
VOC samples, and tables read from the game executable). Those files often
come from old disks or downloads of unknown provenance, so the decoders
are treated as an untrusted-input attack surface: they are fuzzed
(`cmake --preset fuzz`, libFuzzer) and CI runs the test corpus under
ASan + fatal UBSan (`cmake --preset asan`).

## Reporting a vulnerability

If you find a memory-safety or other security issue (a crafted game file
that crashes or corrupts the engine, for example), please report it
**privately** via GitHub's *Report a vulnerability* (Security tab →
Private Vulnerability Reporting) rather than a public issue.

Only the **latest release** is supported with fixes. There is no bounty
program — this is a hobby project — but reports are read and credited.
