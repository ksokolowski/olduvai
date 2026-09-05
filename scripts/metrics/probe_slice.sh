#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
#
# Free-name probe for a candidate extraction slice (BACKLOG 3.7 D).
#
#   scripts/metrics/probe_slice.sh <file> <first-line> <last-line>
#
# Moves lines [first,last] into a no-argument function, compiles, and lists the
# identifiers that stop resolving.  That count is what an extraction of the
# block would have to carry across the new boundary, and it is the number 3.7
# uses to decide whether a slice needs an owner or just a parameter.
#
# FOUR WAYS THIS LIES, all of them found the hard way:
#
#   1. Truncation UNDERCOUNTS.  clang stops at 20 errors by default and once
#      reported 10 free names for a block that has 35.  Hence -fmax-errors=0
#      (GCC) / -ferror-limit=0 (clang).
#   2. Cascades OVERCOUNT.  A name the block declares ITSELF is reported when
#      its declaration depends on something undefined: `const int sy = g...`
#      fails because `g` is undefined, so every later use of `sy` reports too.
#      Filtered below by dropping names the block declares.
#   3. An UNBALANCED range reports garbage, and it inflates wildly — a 14-line
#      range that split an `if` body reported SEVENTY free names, which reads
#      exactly like "this block is hopeless" instead of "you cut it wrong".
#      Refused outright below; this is the check that matters most, because
#      the other two make you doubt a number while this one makes you believe
#      a false one.
#   4. The COMPILER'S DIALECT.  This grep knew only GCC's "'g' was not
#      declared".  clang says "use of undeclared identifier 'g'", so on a clang
#      host it matched nothing and printed a confident `free names: 0` for a
#      block that produced 59 undeclared-identifier errors — the same
#      believe-a-false-one shape as 3, and it went unnoticed because the number
#      it invents is the number you would love to see.  Both spellings are
#      matched now, and matching NEITHER while the compiler did report errors
#      is refused rather than reported as zero.  The unlimited-errors flag is
#      per-dialect too (-fmax-errors=0 GCC, -ferror-limit=0 clang) and clang
#      merely WARNS that the wrong one is unused before truncating at 20, which
#      is failure mode 1 arriving through the same door.
#
# Localised diagnostics silently match nothing, so LC_ALL=C is not optional.
set -e

FILE="${1:?usage: probe_slice.sh <file> <first-line> <last-line>}"
LO="${2:?}"
HI="${3:?}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DB="${OLDUVAI_COMPILE_DB:-${ROOT}/build/release}"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

cd "${ROOT}"
[ -f "${DB}/compile_commands.json" ] || {
    echo "probe: no compile database at ${DB} — run: cmake --preset release" >&2
    exit 77
}
export DB

python3 - "${FILE}" "${LO}" "${HI}" "${TMP}" <<'PY'
import json, os, re, subprocess, sys
f, lo, hi, tmp = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
src = open(f).read().split('\n')
block = src[lo-1:hi]

def strip(l):
    l = re.sub(r'//.*', '', l)
    return re.sub(r'"(\\.|[^"\\])*"', '', l)

depth = sum(strip(l).count('{') - strip(l).count('}') for l in block)
if depth != 0:
    sys.exit("probe: REFUSED — lines %d-%d have brace delta %+d.\n"
             "  An unbalanced range does not measure the block, it measures\n"
             "  wherever the compiler thinks the function now ends. Move the\n"
             "  boundary to a statement edge and re-run." % (lo, hi, depth))

rest = src[:lo-1] + ['        probe_slice_();'] + src[hi:]
out = '\n'.join(rest)
anchor = out.rfind('\n', 0, out.index('(')) + 1
# place the probe function immediately before the first function definition
m = re.search(r'^[A-Za-z_][\w:<>,&\* ]*\s+[A-Za-z_]\w*\s*\([^;]*\)\s*\{', out, re.M)
i = m.start() if m else 0
open(os.path.join(tmp, 'probe.cpp'), 'w').write(
    out[:i] + 'static void probe_slice_() {\n' + '\n'.join(block) + '\n}\n\n' + out[i:])

db = json.load(open(os.path.join(os.environ['DB'], 'compile_commands.json')))
tu = os.path.abspath(f)
e = [x for x in db if os.path.abspath(x['file']) == tu]
if not e:
    sys.exit("probe: %s is not in the compile database" % f)
cmd = e[0].get('command') or ' '.join(e[0]['arguments'])
cmd = cmd.replace(tu, os.path.join(tmp, 'probe.cpp'))
cmd = re.sub(r' -o \S+', '', cmd).replace(' -c ', ' ')
# The unlimited-errors flag is spelled differently per compiler, and picking
# the wrong one does not fail — clang merely warns that -fmax-errors is unused
# and then truncates at its default 20, which is failure mode 1 wearing a
# disguise.  Ask the compiler what it is rather than guessing from its name:
# AppleClang lives at /usr/bin/c++ and gives nothing away.
cxx = cmd.split()[0]
try:
    ver = subprocess.run([cxx, '--version'], capture_output=True,
                         text=True, timeout=30).stdout
except Exception:
    ver = ''
limit = '-ferror-limit=0' if 'clang' in ver.lower() else '-fmax-errors=0'
open(os.path.join(tmp, 'cmd.sh'), 'w').write(
    cmd + ' -fsyntax-only ' + limit + '\n')
PY


cd "${DB}"
# `|| true` because the compile is EXPECTED to fail — its errors are the
# measurement.  Previously this was the head of a pipeline, so `set -e` saw
# grep's status and never the compiler's; redirecting to a file exposed it.
LC_ALL=C sh "${TMP}/cmd.sh" > "${TMP}/diag" 2>&1 || true

# TWO dialects, and matching neither must not read as "no free names":
#   GCC     error: 'g' was not declared
#   clang   error: use of undeclared identifier 'g'
{
  grep -oE "error: '[^']+' was not declared" "${TMP}/diag" \
    | sed "s/error: '//; s/' was not declared//"
  grep -oE "error: use of undeclared identifier '[^']+'" "${TMP}/diag" \
    | sed "s/.*identifier '//; s/'$//"
} | sort -u > "${TMP}/raw"

# Failure mode 4, and it is the same shape as the unbalanced range: a number
# that is wrong rather than absent.  For years this grep knew only GCC's
# spelling, so on a clang host it matched nothing and the script printed a
# confident "free names: 0" for a block with 59 undeclared-identifier errors
# in it.  If the compiler complained and we understood none of it, say so.
if [ ! -s "${TMP}/raw" ] && grep -q "error:" "${TMP}/diag"; then
    echo "probe: REFUSED — the compiler reported errors, but none of them" >&2
    echo "  matched a known 'undeclared name' diagnostic.  That means this" >&2
    echo "  compiler's dialect is unrecognised, NOT that the block is free of" >&2
    echo "  outside references.  First error was:" >&2
    grep -m1 "error:" "${TMP}/diag" | sed 's/^/    /' >&2
    exit 1
fi

# Drop cascade hits: a name the block declares itself is not a free name.
cd "${ROOT}"
python3 - "${FILE}" "${LO}" "${HI}" "${TMP}/raw" <<'PY'
import re, sys
f, lo, hi, raw = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
block = '\n'.join(re.sub(r'//.*', '', l) for l in open(f).read().split('\n')[lo-1:hi])
names = [n for n in open(raw).read().split() if n]
free, cascade = [], []
for n in names:
    # A declarator list is the common case here — `const int sx = a, sy = b;`
    # declares sy after arbitrary text, so anything up to the `;` may sit
    # between the type keyword and the name.
    # Anchored at the statement start: a declaration BEGINS a statement, so a
    # bare `running = false;` cannot be mistaken for one just because some
    # capitalised token appears earlier on the line. A declarator list is the
    # common case (`const int sx = a, sy = b;`), so allow anything up to `;`
    # between the type keyword and the name.
    decl = re.search(r'^\s*(?:const\s+)?(?:int|bool|auto|float|double|unsigned'
                     r'|std::\w+|[A-Z]\w*)\b[^;\n]*\b%s\b\s*[=;{]'
                     % re.escape(n), block, re.M)
    (cascade if decl else free).append(n)
print("free names: %d" % len(free))
for n in free:
    print("   %s" % n)
if cascade:
    print("cascade (declared inside the block, not free): %s" % ' '.join(cascade))
PY
