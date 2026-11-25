#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The licence texts the release packages carry (packaging/licenses.sh)
# against THIRD-PARTY-NOTICES.md: every listed text copies in both naming
# styles, every listed component has a row in the notices, and every row
# compiled into the binary has its text listed or is a documented exception.
# Two MIT notices (OmniScale, MMPX) were owed and missing until 0.9.12.
#
#   tests/licenses.sh <repo root>

ROOT="${1:-$(dirname "$0")/..}"
NOTICES="${ROOT}/THIRD-PARTY-NOTICES.md"
. "${ROOT}/packaging/licenses.sh"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/olduvai_licenses.XXXXXX")
trap 'rm -rf "${TMP}"' EXIT
rc=0
fail() { echo "licenses: FAIL — $*"; rc=1; }

for style in port desktop; do
    copy_license_texts "${ROOT}" "${TMP}/${style}" "${style}" \
        || fail "copy_license_texts ${style} reported a missing text"
done
while read -r kind src name; do
    for f in "${TMP}/port/${kind}.${name}.txt" "${TMP}/desktop/${name}-${kind}.txt"; do
        [ -s "$f" ] || fail "${f##*/} missing or empty (from ${src})"
    done
    grep -q "^| ${name}[ |(]" "${NOTICES}" \
        || fail "${name} has no row in THIRD-PARTY-NOTICES.md"
done <<EOF
$(license_texts)
EOF

# Rows compiled into the binary: each named in the list, or an exception
# packaging/licenses.sh explains (stb, the public domain alternative its
# headers offer).
for name in $(grep -E '^\| [^|]+ \|.*\| \*\*Compiled into every binary|Compiled in' "${NOTICES}" \
                  | sed -E 's/^\| ([A-Za-z0-9-]+).*/\1/'); do
    case ${name} in stb) continue ;; esac
    license_texts | grep -q " ${name}\$" \
        || fail "${name} is compiled in but has no text in packaging/licenses.sh"
done

[ ${rc} -eq 0 ] && echo "licenses: OK — $(license_texts | wc -l | tr -d ' ') texts, both styles, notices consistent"
exit ${rc}
