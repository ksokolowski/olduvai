#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Classify an unknown handheld from its SD card.  READ-ONLY by default.
#
# WHY.  Powkiddy/Anbernic model numbers are reused across wildly different
# silicon — the same badge has shipped RK3326 (an excellent target) and
# F1C100s-class parts with 32-64 MB of RAM, where Olduvai's measured 35 MB
# classic floor stops being comfortable.  Guessing the SoC costs a toolchain.
# The card settles it in seconds.
#
#   handheld_probe.sh                     list candidate disks
#   handheld_probe.sh /dev/sda            classify one            (Linux)
#   handheld_probe.sh /dev/disk4          classify one            (macOS)
#   handheld_probe.sh /dev/sda --identify  ... and read the rootfs arch/ABI
#   handheld_probe.sh /dev/sda --eject     ... and eject when done
#
# TWO PLATFORMS, ONE SCRIPT.  The spike splits roles across two machines
# (docs/internal/SPIKE_HANDHELD.md): the Mac keeps the engine, a Linux box
# does the port work.  Both end up holding cards, so both need this — as ONE
# body, not two.  This codebase's expensive failures have all been one concept
# with several bodies that drifted apart (BACKLOG §1), and a probe whose
# verdict text existed twice would be exactly that shape.  What differs is
# only ever four things: listing disks, reading the partition map, finding
# mounts, and ejecting.  The verdict, the folder-layout read and the footer
# are SHARED and must stay that way.
#
# ── macOS: THE PLATFORM WRITES TO FAT CARDS BY ITSELF ───────────────────────
# Spotlight indexing and fseventsd create .Spotlight-V100 / .fseventsd /
# .Trashes on any FAT volume macOS mounts — nothing here writes, but simply
# having the card mounted is enough.  On a device whose firmware you cannot
# reinstall that is a real risk, and it is not hypothetical: the Powkiddy X39
# card carries .Spotlight-V100 and .fseventsd stamped 2026-09-07 10:39, left
# by the probe run that ruled the device out.
#
# Safest: an adapter with a physical write-protect switch.  Failing that, eject
# as soon as you are done (--eject does it), or mount read-only first:
#
#   diskutil unmount /dev/disk4s1
#   diskutil mount readOnly /dev/disk4s1
#
# ── Linux: NOTHING WRITES BY ITSELF, BUT `mount -o ro` STILL CAN ────────────
# There is no Spotlight here, and a desktop auto-mount is read-write but inert.
# The real trap is subtler: mounting an ext2/3/4 filesystem READ-ONLY still
# REPLAYS ITS JOURNAL, and a journal replay is a write to the device.  That is
# the macOS hazard above wearing different clothes, and it lands on precisely
# the devices this script exists to survey.  `ro,noload` is the option that
# actually means read-only; --identify uses it, after setting the whole device
# read-only at the block layer as a second line of defence.
#
# READ-ONLY CONTRACT.  Without --identify this script never mounts, unmounts or
# writes.  It reads the partition map, reads the GAPS between partitions (see
# below), and lists the top level of whatever the platform already mounted.
# --identify is the single exception, and it says so on the way in.
#
# WHY THE GAPS MATTER.  A partition table is not the whole card.  Allwinner,
# Rockchip and Ingenic parts keep SPL/u-boot in RAW SECTORS ahead of the first
# partition with no table entry at all — a 4 MiB hole at the front of an
# otherwise empty-looking card is the normal shape for a Linux handheld that
# boots from SD.  Neither `fdisk` nor macOS will show it to you.  So "one FAT
# partition and 456 MB unallocated" is not a finding; what is IN those 456 MB
# is the finding.  On the X39 the answer was 0x00 at the front and 0xFF in the
# tail — erased flash, no bootloader — which is what turned a partition-map
# guess into a ruling.
set -eu

OS=$(uname -s)

DISK=""
DO_EJECT=0
DO_IDENTIFY=0
DO_FORCE=0

# Set by identify_linux and unwound by cleanup().  Declared here so the trap
# can reference them before --identify has run.
TMPROOT=""
RO_SET=""

usage() {
    cat <<'USAGE'
handheld_probe.sh — classify an unknown handheld from its SD card.

  handheld_probe.sh                  list candidate disks
  handheld_probe.sh DISK             classify one (read-only, never mounts)
  handheld_probe.sh DISK --identify  Linux, root: set the device read-only,
                                     mount each rootfs ro,noload and report
                                     its architecture and ABI
  handheld_probe.sh DISK --eject     classify, then eject
  handheld_probe.sh DISK --force     probe a disk the safety guard refused

DISK is a WHOLE disk: /dev/sda or /dev/mmcblk0 on Linux, /dev/disk4 on macOS.
USAGE
}

for ARG in "$@"; do
    case "${ARG}" in
        --eject)     DO_EJECT=1 ;;
        --identify)  DO_IDENTIFY=1 ;;
        --force)     DO_FORCE=1 ;;
        -h|--help)   usage; exit 0 ;;
        -*)          echo "handheld_probe: unknown option '${ARG}'" >&2
                     echo "" >&2; usage >&2; exit 2 ;;
        *)           if [ -n "${DISK}" ]; then
                         echo "handheld_probe: more than one disk given ('${DISK}', '${ARG}')" >&2
                         exit 2
                     fi
                     DISK="${ARG}" ;;
    esac
done

# Unwind whatever --identify did, on every exit path including a failed mount
# halfway through the loop.  Mount points are the subdirectories of TMPROOT, so
# this needs no bookkeeping variable that a pipeline subshell could lose.
cleanup() {
    if [ -n "${TMPROOT}" ] && [ -d "${TMPROOT}" ]; then
        for CU_MP in "${TMPROOT}"/*; do
            [ -d "${CU_MP}" ] || continue
            umount "${CU_MP}" 2>/dev/null || true
            rmdir  "${CU_MP}" 2>/dev/null || true
        done
        # rm -rf only AFTER every subdirectory is unmounted above; the scratch
        # files (geometry dump, raw-sector sample) live here too and rmdir
        # would leave the directory behind for them.
        rm -rf "${TMPROOT}" 2>/dev/null || true
    fi
    if [ -n "${RO_SET}" ]; then
        blockdev --setrw "${RO_SET}" >/dev/null 2>&1 || true
    fi
    return 0
}
trap cleanup EXIT HUP INT TERM

# ─────────────────────────── shared: the judgement ──────────────────────────
# These three run identically on both platforms.  Adding a per-OS branch inside
# any of them is the drift this script's single body exists to prevent.

LINUXFS=0
FAT=0

classify_verdict() {
    echo "─── verdict ───"
    if [ "${LINUXFS}" -eq 1 ] && [ "${FAT}" -eq 1 ]; then
        echo "LINUX DEVICE — boot partition + Linux rootfs."
        echo "  A plausible target.  Next: check whether Batocera / ArkOS / JELOS"
        echo "  list this model.  If any do, the port is mostly the aarch64/armhf"
        echo "  build you already need for the others."
    elif [ "${LINUXFS}" -eq 1 ]; then
        echo "LINUX ROOTFS, no obvious boot partition — unusual layout."
        echo "  Probably still a Linux device.  Worth a closer look."
    elif [ "${FAT}" -eq 1 ]; then
        echo "FAT ONLY — no Linux rootfs on this card."
        echo "  This alone does NOT settle it: plenty of Linux handhelds boot from"
        echo "  internal storage and use the SD for content only (the TrimUI Smart"
        echo "  Pro does exactly that).  The FOLDER LAYOUT below is the real test."
        echo ""
        echo "    ports/ or a CFW name        -> Linux device, likely a target"
        echo "    game|music|photo|video/     -> sealed player, OS in flash"
        echo "    per-system ROM dirs, no     -> sealed player: the frontend is"
        echo "      ports/ and no boot files     baked in and will not load ours"
        echo ""
        echo "  If it reads as sealed, treat it as a NON-TARGET unless a CFW"
        echo "  project names this exact model: no toolchain effort makes a device"
        echo "  run code its firmware will not load."
    else
        echo "UNRECOGNISED partition map."
        echo "  Either locked firmware, or a scheme this platform does not parse."
    fi
    echo ""
}

show_layout() {
    # $1 = an already-mounted directory.
    #
    # `ls -A`, NOT `ls -1`.  The macOS pass over the X39 listed the top level
    # with `ls -1` and so never saw .emu_cfg, game/.bios, game/.date or System
    # Volume Information.  The verdict above calls the folder layout "the real
    # test" — and the real test was running blind to every hidden entry, which
    # on a sealed player is where the frontend keeps its bookkeeping.
    echo ""
    echo "  $1"
    ls -A "$1" 2>/dev/null | head -25 | sed 's/^/      /'
}

footer() {
    echo ""
    echo "Record the result in docs/internal/SPIKE_HANDHELD.md: replace the"
    echo "device's inventory row and raise its confidence marker."
}

# ───────────────────────────── backend: macOS ───────────────────────────────

list_darwin() {
    # NOT `diskutil list external`: a Mac's BUILT-IN SD reader presents the
    # card as "internal, physical", so the external filter hides exactly the
    # case this script exists for.  Found the first time it was pointed at a
    # real card (2026-09-07) — it reported "no removable disk" with the card
    # sitting in the slot.  List everything and let the operator pick.
    EXT=$(diskutil list 2>/dev/null || true)
    # `diskutil list` SUCCEEDS with empty output when nothing matches, so
    # testing its exit status tells you nothing — test whether it named a disk.
    if echo "${EXT}" | grep -q '^/dev/disk'; then
        echo "Disks (pick the CARD — check the size; a built-in reader"
        echo "reports it as 'internal, physical', not external):"
        echo ""
        echo "${EXT}"
        echo ""
        echo "Then: $0 /dev/diskN"
    else
        echo "No disks listed at all — insert the device's SD card first."
        echo ""
        echo "If it is inserted and still not listed, macOS may not recognise"
        echo "its partition scheme at all, which is itself a signal: see the"
        echo "UNRECOGNISED case in docs/internal/SPIKE_HANDHELD.md.  Then try:"
        echo "    diskutil list"
    fi
}

validate_darwin() {
    case "${DISK}" in
        /dev/disk*) ;;
        *) echo "handheld_probe: expected a whole disk like /dev/disk4, got '${DISK}'" >&2
           exit 2 ;;
    esac

    if ! diskutil info "${DISK}" >/dev/null 2>&1; then
        echo "handheld_probe: ${DISK} is not a disk macOS can see" >&2
        exit 1
    fi
}

map_darwin() {
    diskutil list "${DISK}"
    echo ""

    MAP=$(diskutil list "${DISK}" 2>/dev/null)
    has() { echo "${MAP}" | grep -qi "$1"; }

    has "Linux Filesystem" && LINUXFS=1
    has "Linux_Ext"        && LINUXFS=1
    has "FAT_32"           && FAT=1
    has "DOS_FAT"          && FAT=1
    has "Windows_FAT"      && FAT=1
    return 0
}

gaps_darwin() {
    # macOS gives no cheap, reliable per-partition sector geometry without
    # parsing localised `diskutil list` output, and the machine that holds the
    # cards for this spike is the Linux box by design.  Say so rather than
    # printing a half-answer that reads like a clean bill of health.
    echo "─── gaps between partitions ───"
    echo "  Not read on macOS.  Raw sectors ahead of the first partition are"
    echo "  where Allwinner/Rockchip/Ingenic parts keep SPL/u-boot, and a card"
    echo "  can look empty while booting perfectly.  Re-probe on the Linux box"
    echo "  to settle it:  handheld_probe.sh /dev/sdX"
    echo ""
}

mounts_darwin() {
    # Top level of any FAT partition macOS already mounted.  Folder names are
    # the giveaway: roms/ + ports/ + a CFW name is a Linux CFW; a flat pile of
    # BIOS blobs and a single opaque .bin is a player.
    echo "─── mounted partitions from this disk ───"
    # Mount points can contain SPACES — the first real card was labelled
    # "NO NAME" and an awk $3 split it into "/Volumes/NO", so the listing came
    # back empty and the card looked emptier than it was.  Parse the " on X ("
    # form that `mount` actually prints, and read it line-wise.
    mount | grep "^${DISK}" | sed 's/^.* on \(.*\) (.*$/\1/' | while IFS= read -r MP; do
        [ -n "${MP}" ] || continue
        show_layout "${MP}"
    done
    if ! mount | grep -q "^${DISK}"; then
        echo "  (none mounted — the map above is still the answer)"
    fi
}

eject_darwin() {
    echo ""
    if [ "${DO_EJECT}" -eq 1 ]; then
        echo "─── ejecting ───"
        diskutil eject "${DISK}" 2>&1 | sed 's/^/  /'
    else
        echo "─── when you are done ───"
        echo "  diskutil eject ${DISK}"
        echo "  (macOS keeps writing Spotlight metadata while it stays mounted)"
    fi
}

# ───────────────────────────── backend: Linux ───────────────────────────────

list_linux() {
    # NOT filtered to removable.  A built-in SD reader presents the card as
    # /dev/mmcblk0 with RM=0 — the same trap as the Mac's internal reader
    # reporting "internal, physical", and the reason the macOS half above
    # lists everything too.  Print the columns that let the operator judge and
    # let them pick.  loop devices are the one safe thing to hide: on a snap
    # desktop there are forty of them and none is ever a card.
    echo "Disks (pick the CARD — check SIZE and TRAN; a built-in reader"
    echo "reports RM=0, so 'removable' alone is not the test):"
    echo ""
    lsblk -d -o NAME,SIZE,RM,HOTPLUG,TRAN,MODEL,LABEL 2>/dev/null \
        | grep -v '^loop' | sed 's/^/  /'
    echo ""
    echo "Then: $0 /dev/NAME"
}

validate_linux() {
    if [ ! -b "${DISK}" ]; then
        echo "handheld_probe: ${DISK} is not a block device" >&2
        exit 2
    fi

    # Ask lsblk what it is rather than pattern-matching the name: sd*, mmcblk*,
    # nvme*n* and vd* all name whole disks with incompatible partition-suffix
    # rules (sda1 vs mmcblk0p1), and a regex that gets that wrong refuses the
    # card or accepts a partition.
    VL_TYPE=$(lsblk -ndo TYPE "${DISK}" 2>/dev/null || true)
    case "${VL_TYPE}" in
        disk) ;;
        # A loop device is accepted on purpose: `losetup -P` over a card IMAGE
        # presents the same geometry, and probing an image is how a card gets
        # surveyed after it has gone back into the device.  It is also the only
        # way --identify is testable without a Linux handheld in hand.
        loop) ;;
        *) echo "handheld_probe: expected a WHOLE disk, but ${DISK} is a '${VL_TYPE}'" >&2
           echo "  Give the disk, not the partition: /dev/sda, not /dev/sda1." >&2
           exit 2 ;;
    esac

    # HARD refusal.  --identify runs `blockdev --setro` and `mount` on whatever
    # it is handed, and on a workstation the card is one letter away from the
    # root SSD.  A disk carrying a mounted system path is never a handheld card
    # and --force does not override this.
    VL_SYS=$(lsblk -nlo MOUNTPOINT "${DISK}" 2>/dev/null \
             | grep -Ex '/|/boot|/boot/efi|/usr|/var|/home' || true)
    if [ -n "${VL_SYS}" ]; then
        echo "handheld_probe: ${DISK} carries a mounted system filesystem:" >&2
        echo "${VL_SYS}" | sed 's/^/    /' >&2
        echo "  Refusing — this is not a handheld card." >&2
        exit 1
    fi

    # SOFT refusal.  Neither removable nor hotplug means an internal drive.
    # This is the guard that catches a data SSD whose mount points are all
    # under /home/<user>/... and so match nothing in the list above.
    VL_RM=$(lsblk -ndo RM "${DISK}" 2>/dev/null | tr -d '[:space:]')
    VL_HP=$(lsblk -ndo HOTPLUG "${DISK}" 2>/dev/null | tr -d '[:space:]')
    if [ "${VL_TYPE}" != "loop" ] \
       && [ "${VL_RM}" = "0" ] && [ "${VL_HP}" = "0" ] && [ "${DO_FORCE}" -eq 0 ]; then
        echo "handheld_probe: ${DISK} is neither removable nor hotplug — it looks" >&2
        echo "  like an internal drive, not a card.  If you are certain, add --force." >&2
        exit 1
    fi
}

map_linux() {
    lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT "${DISK}" | sed 's/^/  /'
    echo ""

    echo "─── partition table ───"
    ML_LABEL=$(lsblk -ndo PTTYPE "${DISK}" 2>/dev/null | tr -d '[:space:]')
    echo "  scheme: ${ML_LABEL:-none}"
    echo ""

    # FSTYPE, not a string match on localised table output.  The macOS half has
    # to grep `diskutil` prose; here the kernel already knows, and it knows
    # about f2fs and squashfs rootfs images that no "Linux_Ext" match would see.
    lsblk -b -P -o NAME,TYPE,FSTYPE,SIZE,START "${DISK}" 2>/dev/null \
        | while IFS= read -r ML_LINE; do
        echo "${ML_LINE}"
    done > "${GEOM}"

    while IFS= read -r ML_LINE; do
        [ -n "${ML_LINE}" ] || continue
        eval "${ML_LINE}"
        [ "${TYPE}" = "part" ] || continue
        case "${FSTYPE}" in
            ext2|ext3|ext4|f2fs|btrfs|squashfs|xfs) LINUXFS=1 ;;
            vfat|exfat|msdos)                       FAT=1 ;;
        esac
    done < "${GEOM}"
    return 0
}

# How much of a gap is read CONTIGUOUSLY before falling back to sampling, how
# far a located byte is chased, and at what granularity.  In sectors.
GAP_FULL_MAX=65536      # 32 MiB — read whole, in one dd
GAP_CHUNK=64            # 32 KiB — that walk's granularity

# Read a region of the raw device and say what it is made of.  Echoes exactly
# one of: zero | ff | data | unreadable
region_class() {
    # $1 = start sector, $2 = sector count
    if ! dd if="${DISK}" bs=512 skip="$1" count="$2" status=none of="${SAMPLE}" 2>/dev/null; then
        echo unreadable
        return 0
    fi
    if [ ! -s "${SAMPLE}" ]; then
        echo unreadable
    elif [ "$(tr -d '\000' < "${SAMPLE}" | wc -c)" -eq 0 ]; then
        echo zero
    elif [ "$(tr -d '\377' < "${SAMPLE}" | wc -c)" -eq 0 ]; then
        echo ff
    else
        echo data
    fi
}

# Map WHERE the content in a gap actually is, as runs of non-fill chunks, and
# print the biggest ones with their magic.
#
# WHY RUNS AND NOT "the first non-empty byte".  That simpler answer has now
# misled twice, in opposite directions.  It first MISSED a sunxi SPL at sector
# 16 by sampling past it.  Then, reading a TrimUI card, it FOUND sector 2 —
# a duplicate copy of the GPT partition-entry array, byte-identical to the one
# at 73696 — announced "this is the SPL/u-boot case", and hexdumped partition
# metadata as if it were a bootloader.  The actual u-boot was 16 MiB further in
# at sector 32800, and only a by-hand run map found it.  A gap scan exists to
# say what is in the gap; one offset cannot.
map_runs() {
    # $1 = local file holding the region, $2 = its base sector
    MR_F=$1
    MR_BASE=$2
    MR_SZ=$(stat -c%s "${MR_F}" 2>/dev/null || echo 0)
    MR_OFF=0
    MR_RUN=""
    MR_SHOWN=0
    MR_BIG=0
    MR_BIGAT=""
    while [ "${MR_OFF}" -lt "${MR_SZ}" ]; do
        dd if="${MR_F}" bs=512 skip=$(( MR_OFF / 512 )) count="${GAP_CHUNK}" \
           status=none of="${SAMPLE}" 2>/dev/null || true
        MR_SEC=$(( MR_BASE + MR_OFF / 512 ))
        if [ "$(classify_file "${SAMPLE}")" = "data" ]; then
            [ -z "${MR_RUN}" ] && MR_RUN=${MR_SEC}
        elif [ -n "${MR_RUN}" ]; then
            MR_LEN=$(( MR_SEC - MR_RUN ))
            if [ "${MR_LEN}" -gt "${MR_BIG}" ]; then
                MR_BIG=${MR_LEN}; MR_BIGAT=${MR_RUN}
            fi
            if [ "${MR_SHOWN}" -lt 8 ]; then
                printf '      content at sectors %s..%s (%s KiB)\n' \
                    "${MR_RUN}" "$(( MR_SEC - 1 ))" "$(( MR_LEN / 2 ))"
                MR_SHOWN=$(( MR_SHOWN + 1 ))
            fi
            MR_RUN=""
        fi
        MR_OFF=$(( MR_OFF + GAP_CHUNK * 512 ))
    done
    if [ -n "${MR_RUN}" ]; then
        MR_LEN=$(( MR_BASE + MR_SZ / 512 - MR_RUN ))
        if [ "${MR_LEN}" -gt "${MR_BIG}" ]; then MR_BIG=${MR_LEN}; MR_BIGAT=${MR_RUN}; fi
        [ "${MR_SHOWN}" -lt 8 ] && printf '      content at sectors %s..%s (%s KiB)\n' \
            "${MR_RUN}" "$(( MR_BASE + MR_SZ / 512 - 1 ))" "$(( MR_LEN / 2 ))"
    fi
    echo "${MR_BIGAT}" > "${SAMPLE}.big"
}

# Echo zero | ff | data for a local file.
classify_file() {
    if [ ! -s "$1" ]; then
        echo zero
    elif [ "$(tr -d '\000' < "$1" | wc -c)" -eq 0 ]; then
        echo zero
    elif [ "$(tr -d '\377' < "$1" | wc -c)" -eq 0 ]; then
        echo ff
    else
        echo data
    fi
}

# Classify one gap and print the verdict.
#
# READ IT, DO NOT SAMPLE IT.  The first version spread eight 8-sector probes
# across the gap and asserted in a comment that this "will not miss a
# bootloader".  A synthetic sunxi card proved that wrong on the first run: with
# a 2047-sector front gap the probes land on sectors 1, 256, 511 … and sunxi
# u-boot lives at sector 16, so the scanner stepped straight over an SPL and
# printed "no bootloader here".  Rockchip's is at sector 64 and would have
# survived by luck alone.  A gap that misses the thing it exists to find is
# worse than no gap scan, because it reads as evidence.
#
# So: the first 32 MiB of any gap is read contiguously, which covers every
# front gap in practice and the head of any tail.  Only what is left beyond
# that is sampled, and a firmware image out there is large and contiguous
# enough for 1 MiB probes to land inside it.
report_gap() {
    # $1 = label, $2 = start sector, $3 = length in sectors
    RG_START=$2
    RG_LEN=$3

    if [ "${RG_LEN}" -le 0 ]; then
        return 0
    fi

    # Round to NEAREST, and only use MiB once there is a whole one.  Truncating
    # printed the canonical 8191-sector front gap as "3 MiB", and "4 MiB before
    # the first partition" is the exact signature an operator is scanning for —
    # a unit bug that hides the thing the gap scan exists to show.
    if [ "${RG_LEN}" -ge 2048 ]; then
        RG_SIZE="$(( (RG_LEN + 1024) / 2048 )) MiB"
    else
        RG_SIZE="$(( RG_LEN / 2 )) KiB"
    fi
    printf '  %-22s sectors %s..%s  (%s)\n' \
        "$1" "${RG_START}" "$(( RG_START + RG_LEN - 1 ))" "${RG_SIZE}"

    if [ ! -r "${DISK}" ]; then
        echo "      contents: not readable as this user — re-run with sudo"
        return 0
    fi

    RG_HEAD=${RG_LEN}
    if [ "${RG_HEAD}" -gt "${GAP_FULL_MAX}" ]; then
        RG_HEAD=${GAP_FULL_MAX}
    fi
    RG_VERDICT=$(region_class "${RG_START}" "${RG_HEAD}")
    RG_DATA_AT=""

    # Sample only the part beyond the contiguous head, and only if the head
    # itself was empty — once there is data, the offset is what matters.
    if [ "${RG_LEN}" -gt "${RG_HEAD}" ] && [ "${RG_VERDICT}" != "data" ] \
       && [ "${RG_VERDICT}" != "unreadable" ]; then
        RG_REST=$(( RG_LEN - RG_HEAD ))
        RG_STEP=$(( RG_REST / 8 ))
        if [ "${RG_STEP}" -lt 2048 ]; then
            RG_STEP=2048
        fi
        RG_AT=$(( RG_START + RG_HEAD ))
        RG_STOP=$(( RG_START + RG_LEN ))
        while [ "${RG_AT}" -lt "${RG_STOP}" ]; do
            RG_N=2048
            if [ $(( RG_AT + RG_N )) -gt "${RG_STOP}" ]; then
                RG_N=$(( RG_STOP - RG_AT ))
            fi
            RG_SEEN=$(region_class "${RG_AT}" "${RG_N}")
            if [ "${RG_SEEN}" = "data" ]; then
                RG_VERDICT=data
                RG_DATA_AT=${RG_AT}
                break
            fi
            if [ "${RG_SEEN}" != "${RG_VERDICT}" ]; then
                RG_VERDICT=mixed
            fi
            RG_AT=$(( RG_AT + RG_STEP ))
        done
    fi

    case "${RG_VERDICT}" in
        unreadable)
            echo "      contents: unreadable"
            ;;
        zero)
            echo "      contents: all 0x00 — no bootloader here"
            ;;
        ff)
            echo "      contents: all 0xFF — erased flash, never written"
            ;;
        mixed)
            echo "      contents: mixed 0x00 / 0xFF fill, no payload"
            ;;
        data)
            echo "      contents: *** HAS DATA ***"
            dd if="${DISK}" bs=512 skip="${RG_START}" count="${RG_HEAD}" \
               status=none of="${GAPBUF}" 2>/dev/null || true
            map_runs "${GAPBUF}" "${RG_START}"
            RG_DATA_AT=$(cat "${SAMPLE}.big" 2>/dev/null)
            if [ -n "${RG_DATA_AT}" ]; then
                # Narrow to the exact sector: the run start is chunk-aligned,
                # so hexdumping it straight printed 64 sectors' worth of
                # leading zeros and a useless "file(1): data" for a u-boot that
                # actually began 30 sectors in.
                RG_I=0
                while [ "${RG_I}" -lt "${GAP_CHUNK}" ]; do
                    dd if="${DISK}" bs=512 skip=$(( RG_DATA_AT + RG_I )) count=1 \
                       status=none of="${SAMPLE}" 2>/dev/null || true
                    [ "$(classify_file "${SAMPLE}")" = "data" ] && break
                    RG_I=$(( RG_I + 1 ))
                done
                RG_DATA_AT=$(( RG_DATA_AT + RG_I ))
                echo "      largest run, first data at sector ${RG_DATA_AT}" \
                     "(byte offset $(( RG_DATA_AT * 512 )))"
                dd if="${DISK}" bs=512 skip="${RG_DATA_AT}" count=1 status=none \
                   of="${SAMPLE}" 2>/dev/null || true
                if command -v xxd >/dev/null 2>&1; then
                    xxd -l 64 "${SAMPLE}" 2>/dev/null | sed 's/^/        /'
                else
                    od -A x -t x1z -v -N 64 "${SAMPLE}" 2>/dev/null | sed 's/^/        /'
                fi
                echo "        file(1): $(file -b "${SAMPLE}" 2>/dev/null)"
                # Name the vendor if the magic is one of the known ones.  The
                # header of this script says guessing the SoC costs a toolchain;
                # these eight bytes are the cheapest place to stop guessing.
                RG_MAGIC=$(dd if="${SAMPLE}" bs=1 count=8 status=none 2>/dev/null \
                           | tr -dc '[:print:]')
                case "${RG_MAGIC}" in
                    eGON.BT0*|eGON.BT1*)
                        echo "        magic: eGON — Allwinner (sunxi) SPL." \
                             "Toolchain: aarch64 or armhf, check the rootfs." ;;
                    RKNS*|RK3*|RK2*)
                        echo "        magic: RK — Rockchip idbloader." \
                             "RK3326/RK3566 class is a known-good target." ;;
                    BOOT0*|*BOOT*)
                        echo "        magic: BOOT — a vendor bootloader header." ;;
                esac
            fi
            echo "      This is the SPL/u-boot case.  A card with code in its gaps"
            echo "      boots from SD and is a Linux device whatever the table says."
            ;;
    esac
}

gaps_linux() {
    echo "─── gaps between partitions ───"

    GL_BYTES=$(lsblk -bndo SIZE "${DISK}" 2>/dev/null | tr -d '[:space:]')
    if [ -z "${GL_BYTES}" ]; then
        echo "  (could not read disk size)"
        echo ""
        return 0
    fi
    GL_SECTORS=$(( GL_BYTES / 512 ))

    # Partitions in start order, as "start end" pairs.  START is in 512-byte
    # sectors and SIZE in bytes, both straight from the kernel.
    : > "${PARTS}"
    while IFS= read -r GL_LINE; do
        [ -n "${GL_LINE}" ] || continue
        eval "${GL_LINE}"
        [ "${TYPE}" = "part" ] || continue
        [ -n "${START}" ] || continue
        echo "${START} $(( START + SIZE / 512 - 1 )) ${NAME}"
    done < "${GEOM}" | sort -n > "${PARTS}.sorted"
    mv "${PARTS}.sorted" "${PARTS}"

    if [ ! -s "${PARTS}" ]; then
        echo "  No partitions at all — the WHOLE device is unallocated space."
        report_gap "whole device" 1 $(( GL_SECTORS - 1 ))
        echo ""
        return 0
    fi

    # Where the first REAL gap begins.  Sector 0 is the MBR; on a GPT disk the
    # primary header sits at LBA 1 with its entry array behind it, so starting
    # the front gap at 1 makes the scan read the GPT and report "HAS DATA ...
    # this is the SPL/u-boot case" on every GPT card it ever sees.  Caught on a
    # TrimUI card, where the hexdump it proudly printed said `EFI PART`.
    # The header's own first_usable_lba is the authority; 34 is the standard
    # fallback (1 header + 32 sectors of entries).
    GL_FIRST=1
    GL_GPT=0
    dd if="${DISK}" bs=512 skip=1 count=1 status=none of="${GPTHDR}" 2>/dev/null || true
    if [ "$(dd if="${GPTHDR}" bs=8 count=1 status=none 2>/dev/null)" = "EFI PART" ]; then
        GL_GPT=1
        # Skip the GPT METADATA and nothing more.  first_usable_lba is the
        # obvious-looking field and it is the WRONG one: vendors set it far
        # past the table to reserve room for a bootloader, and this very card
        # declares 73728 — so trusting it skipped 36 MiB that a hand scan found
        # holds u-boot and an `allwinner,a133` DTB.  Skipping the GPT to avoid
        # a false SPL hit, and thereby skipping the actual SPL, would have been
        # the same defect wearing the other mask.
        #
        # The metadata really ends after the entry array: partition_entry_lba
        # (u64 @72) + ceil(num_entries (u32 @80) * entry_size (u32 @84) / 512).
        GL_ELBA=$(od -An -tu8 -j 72 -N 8 --endian=little "${GPTHDR}" 2>/dev/null | tr -d '[:space:]')
        GL_ENUM=$(od -An -tu4 -j 80 -N 4 --endian=little "${GPTHDR}" 2>/dev/null | tr -d '[:space:]')
        GL_ESZ=$(od  -An -tu4 -j 84 -N 4 --endian=little "${GPTHDR}" 2>/dev/null | tr -d '[:space:]')
        case "${GL_ELBA}${GL_ENUM}${GL_ESZ}" in
            ''|*[!0-9]*) GL_ELBA=2; GL_ENUM=128; GL_ESZ=128 ;;
        esac
        # The GPT occupies TWO DISJOINT areas: the header at LBA 1, and the
        # entry array at partition_entry_lba.  Those are usually adjacent
        # (2..33) — but this TrimUI card puts the array at 73696, immediately
        # before the first partition, deliberately leaving sectors 2..73695
        # free for u-boot.  So the real gap is BETWEEN the two metadata areas,
        # and any model that treats the GPT as one leading block skips it.
        GL_ENDMETA=$(( GL_ELBA + (GL_ENUM * GL_ESZ + 511) / 512 - 1 ))
        echo "  GPT: header at sector 1, entry array at ${GL_ELBA}..${GL_ENDMETA},"
        echo "  backup at the end — those three skipped as metadata.  Anything"
        echo "  BETWEEN them is scanned: that reserved space is where a vendor"
        echo "  bootloader lives, and on this layout it is 36 MiB of it."
        GL_FIRST=2
    fi
    GL_PREV_END=$(( GL_FIRST - 1 ))
    while read -r GL_S GL_E GL_N; do
        if [ "${GL_S}" -gt $(( GL_PREV_END + 1 )) ]; then
            if [ "${GL_PREV_END}" -eq $(( GL_FIRST - 1 )) ]; then
                if [ "${GL_GPT}" -eq 1 ]; then
                    # Sub-gap A: after the header, before the entry array.
                    GL_A_END=${GL_ELBA}
                    [ "${GL_A_END}" -gt "${GL_S}" ] && GL_A_END=${GL_S}
                    report_gap "before first partition" 2 $(( GL_A_END - 2 ))
                    # Sub-gap B: after the entry array, before the partition.
                    if [ $(( GL_ENDMETA + 1 )) -lt "${GL_S}" ]; then
                        report_gap "before first partition" \
                            $(( GL_ENDMETA + 1 )) $(( GL_S - GL_ENDMETA - 1 ))
                    fi
                else
                    report_gap "before first partition" \
                        "${GL_FIRST}" $(( GL_S - GL_FIRST ))
                fi
            else
                report_gap "between partitions" \
                    $(( GL_PREV_END + 1 )) $(( GL_S - GL_PREV_END - 1 ))
            fi
        fi
        printf '  %-22s sectors %s..%s\n' "${GL_N} (partition)" "${GL_S}" "${GL_E}"
        GL_PREV_END="${GL_E}"
    done < "${PARTS}"

    # Same at the end: the backup GPT is metadata, not an unallocated gap.
    GL_LAST=$(( GL_SECTORS - 1 ))
    if [ "${GL_GPT}" -eq 1 ]; then
        GL_LAST=$(( GL_SECTORS - 34 ))
    fi
    if [ "${GL_PREV_END}" -lt "${GL_LAST}" ]; then
        report_gap "after last partition" \
            $(( GL_PREV_END + 1 )) $(( GL_LAST - GL_PREV_END ))
    fi
    echo ""
}

mounts_linux() {
    echo "─── mounted partitions from this disk ───"
    # Mount points can contain SPACES — see the macOS note above, where an awk
    # $3 split "/Volumes/NO NAME" and made a full card look empty.  `lsblk -l`
    # prints one unescaped mount point per line, so read it line-wise and never
    # word-split it.  (`lsblk -r` would escape the space as \x20 instead, which
    # is a different way to get the same bug.)
    lsblk -nlo MOUNTPOINT "${DISK}" 2>/dev/null \
        | sed 's/[[:space:]]*$//' | while IFS= read -r ML_MP; do
        [ -n "${ML_MP}" ] || continue
        show_layout "${ML_MP}"
    done
    if ! lsblk -nlo MOUNTPOINT "${DISK}" 2>/dev/null | grep -q '[^[:space:]]'; then
        echo "  (none mounted — the map above is still the answer)"
    fi

    # Worth saying out loud: a desktop auto-mount is read-write.  Linux will not
    # write on its own the way Spotlight does, but nothing stops a stray tool.
    IL_RW=$(mount 2>/dev/null | grep "^${DISK}" | grep -c '(rw' || true)
    if [ "${IL_RW}" -gt 0 ]; then
        echo ""
        echo "  NOTE: mounted READ-WRITE by the desktop.  Nothing here writes,"
        echo "  but for a firmware you cannot reinstall, remount read-only:"
        echo "    udisksctl unmount -b ${DISK}1 && mount -o ro,noload ..."
    fi
    echo ""
}

# Report what a binary inside a mounted rootfs IS, resolving symlinks against
# the MOUNT rather than against this machine's root.
#
# `file -bL` cannot be used here and the reason is the nastiest bug this script
# has had.  /sbin/init on an ArkOS card is a symlink to the ABSOLUTE path
# /lib/systemd/systemd; -L follows it, the absolute path resolves against the
# HOST, and the probe reported "x86-64" — this workstation's systemd — as the
# architecture of an aarch64 handheld.  A tool whose entire purpose is
# answering "what arch is this device" cannot answer with the arch of the
# machine asking.  Resolve the link ourselves, inside the mount.
identify_binary() {
    # $1 = mount root, $2 = path within it
    IB_ROOT=$1
    IB_P=$2
    IB_N=0
    while [ -L "${IB_ROOT}${IB_P}" ] && [ "${IB_N}" -lt 10 ]; do
        IB_T=$(readlink "${IB_ROOT}${IB_P}" 2>/dev/null) || break
        case "${IB_T}" in
            /*) IB_P="${IB_T}" ;;
            *)  IB_P="$(dirname "${IB_P}")/${IB_T}" ;;
        esac
        IB_N=$(( IB_N + 1 ))
    done
    if [ ! -e "${IB_ROOT}${IB_P}" ]; then
        echo "dangling symlink -> ${IB_P}"
        return 0
    fi
    if [ "${IB_P}" != "$2" ]; then
        echo "-> ${IB_P}: $(file -b "${IB_ROOT}${IB_P}" 2>/dev/null)"
    else
        file -b "${IB_ROOT}${IB_P}" 2>/dev/null
    fi
}

identify_linux() {
    echo "─── identify: rootfs architecture ───"

    if [ "$(id -u)" -ne 0 ]; then
        echo "  --identify needs root: it sets the device read-only at the block"
        echo "  layer and mounts each Linux filesystem it finds."
        echo "  Re-run:  sudo $0 ${DISK} --identify"
        echo ""
        return 0
    fi

    IL_ANY=0
    while IFS= read -r IL_LINE; do
        [ -n "${IL_LINE}" ] || continue
        eval "${IL_LINE}"
        [ "${TYPE}" = "part" ] || continue
        case "${FSTYPE}" in
            ext2|ext3|ext4|f2fs|btrfs|squashfs|xfs) IL_ANY=1 ;;
        esac
    done < "${GEOM}"

    if [ "${IL_ANY}" -eq 0 ]; then
        echo "  No Linux filesystem on this card — nothing to identify."
        echo "  (That is a finding, not a failure: see the verdict above.)"
        echo ""
        return 0
    fi

    # Belt and braces.  `ro,noload` already stops the ext journal replay that
    # makes a "read-only" mount write; --setro makes the kernel refuse a write
    # to this device from ANY path, including a mount option typo. cleanup()
    # restores it on every exit.
    echo "  Setting ${DISK} read-only at the block layer."
    if blockdev --setro "${DISK}" 2>/dev/null; then
        RO_SET="${DISK}"
    else
        echo "  WARNING: could not set ${DISK} read-only; continuing on mount"
        echo "  options alone."
    fi

    while IFS= read -r IL_LINE; do
        [ -n "${IL_LINE}" ] || continue
        eval "${IL_LINE}"
        [ "${TYPE}" = "part" ] || continue
        case "${FSTYPE}" in
            ext2|ext3|ext4) IL_OPTS="ro,noload" ;;
            f2fs|btrfs|squashfs|xfs) IL_OPTS="ro" ;;
            *) continue ;;
        esac

        IL_MP="${TMPROOT}/${NAME}"
        mkdir -p "${IL_MP}"
        echo ""
        echo "  /dev/${NAME} (${FSTYPE}) — mount -o ${IL_OPTS}"
        # If the desktop already has it mounted, say so and read it THERE.  A
        # second mount of the same superblock with conflicting options is
        # refused by the kernel, and the bare "mount failed" that produced hid
        # a partition that was sitting there readable the whole time.
        IL_EXIST=$(lsblk -nlo MOUNTPOINT "/dev/${NAME}" 2>/dev/null \
                   | sed 's/[[:space:]]*$//' | grep -m1 '[^[:space:]]' || true)
        if [ -n "${IL_EXIST}" ]; then
            echo "      already mounted at ${IL_EXIST} — reading there"
            rmdir "${IL_MP}" 2>/dev/null || true
            IL_MP="${IL_EXIST}"
        elif ! mount -o "${IL_OPTS}" "/dev/${NAME}" "${IL_MP}" 2>&1 | sed 's/^/        /'; then
            echo "      mount failed — skipping"
            rmdir "${IL_MP}" 2>/dev/null || true
            continue
        fi

        # THE answer this whole script is for.  `file` on a binary off the
        # device's OWN rootfs prints arch and ABI outright, which is what turns
        # a MED inventory row into a HIGH one and picks the toolchain.
        for IL_CAND in /bin/sh /bin/busybox /sbin/init /bin/bash /usr/bin/env; do
            if [ -e "${IL_MP}${IL_CAND}" ] || [ -L "${IL_MP}${IL_CAND}" ]; then
                echo "      ${IL_CAND}: $(identify_binary "${IL_MP}" "${IL_CAND}")"
            fi
        done
        if [ -f "${IL_MP}/etc/os-release" ]; then
            echo "      /etc/os-release:"
            sed -n '1,6p' "${IL_MP}/etc/os-release" | sed 's/^/          /'
        fi
        # The kernel release names the vendor outright and is always present,
        # which the marker list below is not.  On the Q90 this reads
        # "4.14.0-miyoo" — the one string that would have said "this is running
        # MiyooCFW" while a hunt for batocera/arkos/jelos paths found nothing
        # and the device got written off on the strength of that silence.
        for IL_KREL in "${IL_MP}"/lib/modules/*; do
            if [ -d "${IL_KREL}" ]; then
                echo "      kernel: $(basename "${IL_KREL}")"
            fi
        done
        # Heuristics, not proof: a hit names the CFW, a miss means nothing.
        for IL_MARK in /usr/share/batocera /etc/arkos /etc/jelos /opt/muos \
                       /usr/miyoo /etc/miyoo /usr/bin/gmenu2x /usr/local/sbin/onion; do
            if [ -e "${IL_MP}${IL_MARK}" ]; then
                echo "      CFW marker: ${IL_MARK}"
            fi
        done

        umount "${IL_MP}" 2>/dev/null || true
        rmdir  "${IL_MP}" 2>/dev/null || true
    done < "${GEOM}"
    echo ""
}

eject_linux() {
    echo ""
    if [ "${DO_EJECT}" -eq 1 ]; then
        echo "─── ejecting ───"
        if command -v udisksctl >/dev/null 2>&1; then
            udisksctl power-off -b "${DISK}" 2>&1 | sed 's/^/  /' || true
        else
            eject "${DISK}" 2>&1 | sed 's/^/  /' || true
        fi
    else
        echo "─── when you are done ───"
        echo "  udisksctl power-off -b ${DISK}"
    fi
}

# ────────────────────────────────── main ────────────────────────────────────

if [ -z "${DISK}" ]; then
    case "${OS}" in
        Darwin) list_darwin ;;
        Linux)  list_linux ;;
        *)      echo "handheld_probe: unsupported platform '${OS}'" >&2; exit 2 ;;
    esac
    exit 0
fi

if [ "${DO_IDENTIFY}" -eq 1 ] && [ "${OS}" != "Linux" ]; then
    echo "handheld_probe: --identify is Linux-only." >&2
    echo "  macOS cannot read an ext4 rootfs at all, which is the reason the" >&2
    echo "  spike puts a Linux box in the loop.  See SPIKE_HANDHELD.md." >&2
    exit 2
fi

echo "═══ ${DISK} ═══"
echo ""

case "${OS}" in
    Darwin)
        validate_darwin
        map_darwin
        classify_verdict
        gaps_darwin
        mounts_darwin
        eject_darwin
        ;;
    Linux)
        validate_linux
        # Scratch for the geometry dump and the raw-sector samples.  One temp
        # dir, removed by cleanup() along with anything --identify mounted.
        TMPROOT=$(mktemp -d /tmp/handheld_probe.XXXXXX)
        GEOM="${TMPROOT}/geom"
        PARTS="${TMPROOT}/parts"
        SAMPLE="${TMPROOT}/sample"
        GPTHDR="${TMPROOT}/gpthdr"
        GAPBUF="${TMPROOT}/gapbuf"
        map_linux
        classify_verdict
        gaps_linux
        mounts_linux
        if [ "${DO_IDENTIFY}" -eq 1 ]; then
            identify_linux
        fi
        eject_linux
        ;;
    *)
        echo "handheld_probe: unsupported platform '${OS}'" >&2
        exit 2
        ;;
esac

footer
